#include "fontinspectorengine.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_TABLES_H

#include <QFile>
#include <QHash>
#include <QtMath>
#include <QtNumeric>

#include <algorithm>
#include <iterator>
#include <limits>

namespace {
struct NameEntry {
    quint16 id;
    const char *label;
};

constexpr NameEntry g_name_entries[] = {
    {0, "Copyright"},
    {1, "Family"},
    {2, "Subfamily"},
    {4, "Full Name"},
    {5, "Version"},
    {6, "PostScript Name"},
    {8, "Manufacturer"},
    {9, "Designer"},
    {11, "Vendor URL"},
    {13, "License"},
    {16, "Typographic Family"},
    {17, "Typographic Subfamily"},
};

constexpr quint16 kWinEnglishUsLangId = 0x0409;

float fixedToFloat(FT_Fixed value)
{
    return float(double(value) / 65536.0);
}

FT_Fixed floatToFixed(float value)
{
    return FT_Fixed(qRound64(double(value) * 65536.0));
}

QByteArray tagToBytes(FT_ULong tag)
{
    char buf[4] = {
        char((tag >> 24) & 0xFF),
        char((tag >> 16) & 0xFF),
        char((tag >> 8) & 0xFF),
        char(tag & 0xFF),
    };
    return QByteArray(buf, 4);
}

QString decodeSfntName(const FT_SfntName &sfnt)
{
    if (!sfnt.string || sfnt.string_len == 0) {
        return {};
    }
    const auto *src = reinterpret_cast<const uchar *>(sfnt.string);
    const int len   = int(sfnt.string_len);

    // Platform 3 (Windows) encoding 1/10 and Platform 0 (Unicode) store UTF-16 BE.
    if ((sfnt.platform_id == 3
         && (sfnt.encoding_id == 1 || sfnt.encoding_id == 10))
        || sfnt.platform_id == 0) {
        QByteArray buf(reinterpret_cast<const char *>(src), len);
        for (int i = 0; i + 1 < buf.size(); i += 2) {
            std::swap(buf[i], buf[i + 1]);
        }
        return QString::fromUtf16(
                   reinterpret_cast<const char16_t *>(buf.constData()),
                   buf.size() / 2)
            .trimmed();
    }
    // Platform 1 (Mac) encoding 0 = Mac Roman.
    if (sfnt.platform_id == 1 && sfnt.encoding_id == 0) {
        return QString::fromLatin1(reinterpret_cast<const char *>(src), len)
            .trimmed();
    }
    return {};
}

int scoreSfntRecord(const FT_SfntName &sfnt)
{
    if (sfnt.platform_id == 3) {
        return (sfnt.language_id == kWinEnglishUsLangId) ? 4 : 3;
    }
    if (sfnt.platform_id == 0) {
        return 2;
    }
    if (sfnt.platform_id == 1) {
        return 1;
    }
    return 0;
}

QHash<quint16, QString> collectSfntNames(
    FT_Face face, QHash<quint16, QStringList> *aliases_out = nullptr)
{
    struct Best {
        int score = 0;
        QString value;
    };
    QHash<quint16, Best> best;
    const FT_UInt count = FT_Get_Sfnt_Name_Count(face);
    for (FT_UInt i = 0; i < count; ++i) {
        FT_SfntName sfnt{};
        if (FT_Get_Sfnt_Name(face, i, &sfnt) != 0) {
            continue;
        }
        const int score = scoreSfntRecord(sfnt);
        if (score == 0) {
            continue;
        }
        const QString val = decodeSfntName(sfnt);
        if (val.isEmpty()) {
            continue;
        }
        if (aliases_out) {
            QStringList &list = (*aliases_out)[sfnt.name_id];
            if (!list.contains(val, Qt::CaseInsensitive)) {
                list.append(val);
            }
        }
        auto it = best.find(sfnt.name_id);
        if (it == best.end() || score > it->score) {
            best.insert(sfnt.name_id, {score, val});
        }
    }

    QHash<quint16, QString> result;
    result.reserve(best.size());
    for (auto it = best.constBegin(); it != best.constEnd(); ++it) {
        result.insert(it.key(), it.value().value);
    }
    return result;
}

FontDecorationMetrics readDecorationMetricsFromFace(FT_Face face)
{
    FontDecorationMetrics metrics;
    if (!face) {
        return metrics;
    }
    metrics.units_per_em = int(face->units_per_EM);

    // Read raw SFNT post / OS/2 fields directly: FreeType shifts
    // FT_FaceRec::underline_position by half thickness during load, whereas
    // post.underlinePosition is the top edge expected by the inspector.
    // FT_Get_Sfnt_Table(FT_SFNT_POST) returns a pointer to the zero-initialized
    // struct even when the post table is missing, so verify table presence and
    // supported FormatType (1.0, 2.0, 2.5, 3.0) before exposing metrics.
    FT_ULong post_len = 0;
    if (FT_Load_Sfnt_Table(face, FT_MAKE_TAG('p', 'o', 's', 't'), 0, nullptr,
                           &post_len)
            == 0
        && post_len >= 32) {
        if (const auto *post = static_cast<const TT_Postscript *>(
                FT_Get_Sfnt_Table(face, FT_SFNT_POST))) {
            const FT_Fixed fmt = post->FormatType;
            if (fmt == 0x00010000L || fmt == 0x00020000L || fmt == 0x00025000L
                || fmt == 0x00030000L) {
                metrics.has_underline   = true;
                metrics.underline_pos   = int(post->underlinePosition);
                metrics.underline_thick = int(post->underlineThickness);
            }
        }
    }
    if (const auto *os2
        = static_cast<const TT_OS2 *>(FT_Get_Sfnt_Table(face, FT_SFNT_OS2))) {
        if (os2->version != 0xFFFF) {
            metrics.has_strikeout  = true;
            metrics.strikeout_pos  = int(os2->yStrikeoutPosition);
            metrics.strikeout_size = int(os2->yStrikeoutSize);
        }
    }
    return metrics;
}

const FontInspectionData &emptyInspectionData()
{
    static const FontInspectionData kEmpty;
    return kEmpty;
}
}  // namespace

FontInspectorEngine::~FontInspectorEngine()
{
    close();
    if (m_library) {
        FT_Done_FreeType(m_library);
        m_library = nullptr;
    }
}

bool FontInspectorEngine::ensureLibrary()
{
    if (m_library) {
        return true;
    }
    return FT_Init_FreeType(&m_library) == 0 && m_library != nullptr;
}

void FontInspectorEngine::close()
{
    for (FaceEntry &entry : m_faces) {
        if (entry.face) {
            FT_Done_Face(entry.face);
            entry.face = nullptr;
        }
    }
    m_faces.clear();
    m_font_bytes.clear();
    m_font_path.clear();
}

bool FontInspectorEngine::openFile(const QString &font_path)
{
    if (font_path.isEmpty()) {
        close();
        return false;
    }
    if (m_font_path == font_path && !m_faces.isEmpty()) {
        return true;
    }

    close();
    if (!ensureLibrary()) {
        return false;
    }

    QFile file(font_path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const qint64 file_size = file.size();
    if (file_size <= 0 || file_size > std::numeric_limits<FT_Long>::max()) {
        return false;
    }
    m_font_bytes = file.readAll();
    if (m_font_bytes.isEmpty()
        || m_font_bytes.size() > std::numeric_limits<FT_Long>::max()) {
        m_font_bytes.clear();
        return false;
    }

    FT_Face face0 = nullptr;
    if (FT_New_Memory_Face(m_library,
                           reinterpret_cast<const FT_Byte *>(
                               m_font_bytes.constData()),
                           FT_Long(m_font_bytes.size()), 0, &face0)
            != 0
        || !face0) {
        m_font_bytes.clear();
        return false;
    }

    m_font_path            = font_path;
    const FT_Long num_face = qMax<FT_Long>(1, face0->num_faces);
    m_faces.reserve(int(qMin<FT_Long>(num_face, 1024)));

    FaceEntry entry0;
    entry0.face = face0;
    populateFace(entry0);
    m_faces.append(std::move(entry0));

    for (FT_Long i = 1; i < num_face; ++i) {
        FT_Face face_i = nullptr;
        if (FT_New_Memory_Face(m_library,
                               reinterpret_cast<const FT_Byte *>(
                                   m_font_bytes.constData()),
                               FT_Long(m_font_bytes.size()), i, &face_i)
                != 0
            || !face_i) {
            continue;
        }
        FaceEntry entry;
        entry.face = face_i;
        populateFace(entry);
        m_faces.append(std::move(entry));
    }

    return !m_faces.isEmpty();
}

void FontInspectorEngine::populateFace(FaceEntry &entry)
{
    FT_Face face = entry.face;
    if (!face) {
        return;
    }

    QHash<quint16, QStringList> name_aliases;
    const QHash<quint16, QString> names = collectSfntNames(face, &name_aliases);

    entry.family_name = face->family_name
                            ? QString::fromUtf8(face->family_name).trimmed()
                            : names.value(1);
    entry.style_name = face->style_name
                           ? QString::fromUtf8(face->style_name).trimmed()
                           : names.value(2);
    entry.typographic_family    = names.value(16);
    entry.typographic_subfamily = names.value(17);

    auto append_alias = [](QStringList &dst, const QString &val) {
        if (!val.isEmpty() && !dst.contains(val, Qt::CaseInsensitive)) {
            dst.append(val);
        }
    };
    append_alias(entry.family_aliases, entry.family_name);
    append_alias(entry.family_aliases, entry.typographic_family);
    for (quint16 id : {quint16(1), quint16(16), quint16(21)}) {
        for (const QString &alias : name_aliases.value(id)) {
            append_alias(entry.family_aliases, alias);
        }
    }
    append_alias(entry.style_aliases, entry.style_name);
    append_alias(entry.style_aliases, entry.typographic_subfamily);
    for (quint16 id : {quint16(2), quint16(17), quint16(22)}) {
        for (const QString &alias : name_aliases.value(id)) {
            append_alias(entry.style_aliases, alias);
        }
    }

    entry.data.name_records.reserve(std::size(g_name_entries));
    for (const auto &spec : g_name_entries) {
        const QString val = names.value(spec.id);
        if (!val.isEmpty()) {
            entry.data.name_records.append(
                {QString::fromLatin1(spec.label), val});
        }
    }

    entry.data.decoration_metrics = readDecorationMetricsFromFace(face);

    if (FT_HAS_MULTIPLE_MASTERS(face)) {
        FT_MM_Var *mm_var = nullptr;
        if (FT_Get_MM_Var(face, &mm_var) == 0 && mm_var) {
            entry.data.variable_axes.reserve(int(mm_var->num_axis));
            for (FT_UInt i = 0; i < mm_var->num_axis; ++i) {
                const FT_Var_Axis &ax = mm_var->axis[i];
                FontVariableAxisInfo info;
                info.tag = tagToBytes(ax.tag);
                if (ax.strid > 0 && ax.strid != 0xFFFF) {
                    info.name = names.value(quint16(ax.strid));
                }
                if (info.name.isEmpty()) {
                    info.name = QString::fromLatin1(info.tag);
                }
                info.min_value     = fixedToFloat(ax.minimum);
                info.default_value = fixedToFloat(ax.def);
                info.max_value     = fixedToFloat(ax.maximum);
                if (info.max_value < info.min_value) {
                    std::swap(info.min_value, info.max_value);
                }
                info.default_value = qBound(info.min_value, info.default_value,
                                            info.max_value);
                entry.data.variable_axes.append(std::move(info));
            }

            entry.data.named_instances.reserve(int(mm_var->num_namedstyles));
            entry.instance_aliases.reserve(int(mm_var->num_namedstyles));
            for (FT_UInt j = 0; j < mm_var->num_namedstyles; ++j) {
                const FT_Var_Named_Style &ns = mm_var->namedstyle[j];
                if (!ns.coords || ns.strid == 0 || ns.strid == 0xFFFF) {
                    continue;
                }
                const quint16 sid      = quint16(ns.strid);
                const QString inst_name = names.value(sid);
                if (inst_name.isEmpty()) {
                    continue;
                }
                FontNamedInstance inst;
                inst.name = inst_name;
                inst.coords.reserve(int(mm_var->num_axis));
                for (FT_UInt k = 0; k < mm_var->num_axis; ++k) {
                    inst.coords.append(fixedToFloat(ns.coords[k]));
                }
                entry.data.named_instances.append(std::move(inst));
                QStringList inst_aliases = name_aliases.value(sid);
                if (!inst_aliases.contains(inst_name, Qt::CaseInsensitive)) {
                    inst_aliases.prepend(inst_name);
                }
                entry.instance_aliases.append(std::move(inst_aliases));
            }
            FT_Done_MM_Var(m_library, mm_var);
        }
    }
}

QString FontInspectorEngine::normalizeStyleName(const QString &name)
{
    QString out;
    out.reserve(name.size());
    for (QChar ch : name) {
        if (!ch.isSpace() && ch != QLatin1Char('-') && ch != QLatin1Char('_')) {
            out.append(ch.toLower());
        }
    }
    return out;
}

int FontInspectorEngine::resolveFaceIndex(const QString &family,
                                          const QString &style) const
{
    if (m_faces.isEmpty()) {
        return 0;
    }

    auto matches_family = [&family](const FaceEntry &entry) {
        if (family.isEmpty()) {
            return true;
        }
        for (const QString &alias : entry.family_aliases) {
            if (alias.compare(family, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
        return false;
    };

    const QString norm_style = normalizeStyleName(style);

    // 1. Exact family + style match.
    for (int i = 0; i < m_faces.size(); ++i) {
        const FaceEntry &entry = m_faces[i];
        if (!matches_family(entry)) {
            continue;
        }
        for (const QString &alias : entry.style_aliases) {
            if (alias.compare(style, Qt::CaseInsensitive) == 0) {
                return i;
            }
        }
    }

    // 2. Normalized style or named-instance match under the target family.
    if (!norm_style.isEmpty()) {
        for (int i = 0; i < m_faces.size(); ++i) {
            const FaceEntry &entry = m_faces[i];
            if (!matches_family(entry)) {
                continue;
            }
            for (const QString &alias : entry.style_aliases) {
                if (normalizeStyleName(alias) == norm_style) {
                    return i;
                }
            }
            for (const QStringList &inst_aliases : entry.instance_aliases) {
                for (const QString &alias : inst_aliases) {
                    if (normalizeStyleName(alias) == norm_style) {
                        return i;
                    }
                }
            }
        }
    }

    // 3. First face matching the family (common for variable fonts whose Qt
    //    style list includes synthesized named instances).
    for (int i = 0; i < m_faces.size(); ++i) {
        if (matches_family(m_faces[i])) {
            return i;
        }
    }

    return 0;
}

const FontInspectionData &FontInspectorEngine::inspectFace(int face_index) const
{
    if (face_index < 0 || face_index >= m_faces.size()) {
        return emptyInspectionData();
    }
    return m_faces[face_index].data;
}

std::optional<FontNamedInstance> FontInspectorEngine::matchNamedInstance(
    int face_index, const QString &style) const
{
    if (face_index < 0 || face_index >= m_faces.size()) {
        return std::nullopt;
    }
    const QString norm = normalizeStyleName(style);
    if (norm.isEmpty()) {
        return std::nullopt;
    }
    const FaceEntry &entry = m_faces[face_index];
    auto find_by_norm    = [&entry](const QString &target) -> std::optional<FontNamedInstance> {
        for (int i = 0; i < entry.data.named_instances.size(); ++i) {
            if (i < entry.instance_aliases.size()) {
                for (const QString &alias : entry.instance_aliases[i]) {
                    if (normalizeStyleName(alias) == target) {
                        return entry.data.named_instances[i];
                    }
                }
            }
            else if (normalizeStyleName(entry.data.named_instances[i].name)
                     == target) {
                return entry.data.named_instances[i];
            }
        }
        return std::nullopt;
    };

    if (auto match = find_by_norm(norm)) {
        return match;
    }
    // DirectWrite drops the "Regular" suffix on non-default width/slope
    // instances (e.g. "Condensed Regular" -> "Condensed").
    if (!norm.endsWith(QStringLiteral("regular"))) {
        return find_by_norm(norm + QStringLiteral("regular"));
    }
    return std::nullopt;
}

FontDecorationMetrics FontInspectorEngine::decorationMetrics(
    int face_index, const QHash<QByteArray, float> &axis_coords)
{
    if (face_index < 0 || face_index >= m_faces.size()) {
        return {};
    }
    FaceEntry &entry = m_faces[face_index];
    FT_Face face     = entry.face;
    if (!face) {
        return {};
    }

    const auto &axes = entry.data.variable_axes;
    if (!axes.isEmpty() && FT_HAS_MULTIPLE_MASTERS(face)) {
        QVector<FT_Fixed> coords(axes.size());
        for (int i = 0; i < axes.size(); ++i) {
            auto it   = axis_coords.constFind(axes[i].tag);
            float val = (it != axis_coords.constEnd() && qIsFinite(it.value()))
                            ? it.value()
                            : axes[i].default_value;
            val       = qBound(axes[i].min_value, val, axes[i].max_value);
            coords[i] = floatToFixed(val);
        }

        // Skip re-applying identical coordinates: FreeType returns -1
        // ("no change") from FT_Set_Var_Design_Coordinates and skips
        // tt_apply_mvar(), so overwriting post/OS/2 first would clobber active
        // MVAR deltas back to defaults.
        if (!entry.has_applied_coords || entry.applied_coords != coords) {
            // FreeType's tt_apply_mvar() skips writing fields whose delta is 0,
            // so returning to default coords leaves prior non-zero deltas
            // behind unless the unmodified baseline fields are restored first.
            const FontDecorationMetrics &def = entry.data.decoration_metrics;
            if (def.has_underline) {
                if (auto *post = static_cast<TT_Postscript *>(
                        FT_Get_Sfnt_Table(face, FT_SFNT_POST))) {
                    post->underlinePosition  = FT_Short(def.underline_pos);
                    post->underlineThickness = FT_Short(def.underline_thick);
                }
            }
            if (def.has_strikeout) {
                if (auto *os2 = static_cast<TT_OS2 *>(
                        FT_Get_Sfnt_Table(face, FT_SFNT_OS2))) {
                    os2->yStrikeoutPosition = FT_Short(def.strikeout_pos);
                    os2->yStrikeoutSize     = FT_Short(def.strikeout_size);
                }
            }

            // Triggers tt_apply_mvar so post / OS/2 reflect active coords.
            if (FT_Set_Var_Design_Coordinates(face, FT_UInt(coords.size()),
                                              coords.data())
                == 0) {
                entry.applied_coords     = std::move(coords);
                entry.has_applied_coords = true;
            }
            else {
                entry.has_applied_coords = false;
            }
        }
    }

    return readDecorationMetricsFromFace(face);
}

QString FontInspectorEngine::probeSampleForFace(int face_index) const
{
    if (face_index < 0 || face_index >= m_faces.size()
        || !m_faces[face_index].face) {
        return {};
    }
    FT_Face face = m_faces[face_index].face;
    if (FT_Get_Char_Index(face, 'H') != 0 && FT_Get_Char_Index(face, 'a') != 0) {
        return QStringLiteral("Hamburgefonstiv");
    }

    QString sample;
    FT_UInt gindex    = 0;
    FT_ULong charcode = FT_Get_First_Char(face, &gindex);
    while (gindex != 0 && sample.size() < 4) {
        if (charcode >= 0x21 && charcode <= 0xFFFF
            && !(charcode >= 0xD800 && charcode <= 0xDFFF)) {
            const QChar ch{char16_t(charcode)};
            if (!ch.isSpace() && ch.isPrint()) {
                sample.append(ch);
            }
        }
        charcode = FT_Get_Next_Char(face, charcode, &gindex);
    }
    return sample;
}

QVector<FontNameRecord> FontInspectorEngine::readNameRecords(
    const QString &font_path)
{
    FontInspectorEngine engine;
    if (!engine.openFile(font_path)) {
        return {};
    }
    return engine.inspectFace(0).name_records;
}
