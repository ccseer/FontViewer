#include "fontinfo.h"

#include <qt_windows.h>

#include <QFile>
#include <QFileInfo>
#include <QHash>

namespace {

// name IDs we care about (OpenType spec)
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

// Big-endian read helpers (name table is big-endian)
quint16 readU16(const uchar *p)
{
    return (quint16(p[0]) << 8) | p[1];
}
quint32 readU32(const uchar *p)
{
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8)
           | p[3];
}

QString decodeNameRecord(const uchar *string_storage,
                         quint16 platform_id,
                         quint16 encoding_id,
                         quint16 length,
                         quint16 offset)
{
    const uchar *src = string_storage + offset;
    // Platform 3 (Windows) encoding 1 = UTF-16 BE
    if (platform_id == 3 && encoding_id == 1) {
        return QString::fromUtf16(reinterpret_cast<const char16_t *>(src),
                                  length / 2)
            .trimmed();
    }
    // Platform 1 (Mac) encoding 0 = Mac Roman
    if (platform_id == 1 && encoding_id == 0) {
        return QString::fromLatin1(reinterpret_cast<const char *>(src), length)
            .trimmed();
    }
    // Platform 0 (Unicode) — UTF-16 BE
    if (platform_id == 0) {
        return QString::fromUtf16(reinterpret_cast<const char16_t *>(src),
                                  length / 2)
            .trimmed();
    }
    return {};
}

// Parse the raw name table bytes into a label->value map.
// Prefer platform 3 (Windows UTF-16) records; fall back to platform 1.
QVector<FontNameRecord> parseNameTable(const QByteArray &data)
{
    if (data.size() < 6) {
        return {};
    }
    const uchar *p = reinterpret_cast<const uchar *>(data.constData());

    // name table header
    // quint16 format   = readU16(p);      // unused
    quint16 count      = readU16(p + 2);
    quint16 string_off = readU16(p + 4);

    if (data.size() < 6 + count * 12) {
        return {};
    }
    const uchar *records        = p + 6;
    const uchar *string_storage = p + string_off;

    // collect best value per name ID: prefer platform 3
    // key = name_id, value = (platform_id, decoded string)
    struct BestRecord { quint16 platform_id; QString value; };
    QHash<quint16, BestRecord> best;

    for (quint16 i = 0; i < count; ++i) {
        const uchar *r  = records + i * 12;
        quint16 plat_id = readU16(r + 0);
        quint16 enc_id  = readU16(r + 2);
        // quint16 lang_id = readU16(r + 4); // unused
        quint16 name_id = readU16(r + 6);
        quint16 length  = readU16(r + 8);
        quint16 offset  = readU16(r + 10);

        if (string_off + offset + length > (quint32)data.size()) {
            continue;
        }

        QString val
            = decodeNameRecord(string_storage, plat_id, enc_id, length, offset);
        if (val.isEmpty()) {
            continue;
        }

        auto it = best.find(name_id);
        if (it == best.end()) {
            best.insert(name_id, {plat_id, val});
        }
        else if (plat_id == 3 && it->platform_id != 3) {
            *it = {plat_id, val};
        }
    }

    // build output in declaration order
    QVector<FontNameRecord> result;
    result.reserve(std::size(g_name_entries));
    for (const auto &entry : g_name_entries) {
        auto it = best.find(entry.id);
        if (it != best.end() && !it->value.isEmpty()) {
            result.append({QString::fromLatin1(entry.label), it->value});
        }
    }
    return result;
}

// Read the 'name' table from a font file using GDI GetFontData.
// Falls back to direct file parsing if GDI fails (e.g. TTC collections).
QByteArray fetchNameTable(const QString &path)
{
    // 'name' table tag: big-endian 0x6E616D65
    constexpr DWORD kNameTag = 0x656D616E;  // GDI uses little-endian tag

    const std::wstring wpath = path.toStdWString();
    HDC hdc                  = CreateCompatibleDC(nullptr);
    if (!hdc) {
        return {};
    }

    HFONT hfont = nullptr;
    QByteArray result;

    // AddFontResourceEx so we don't permanently install it
    if (AddFontResourceExW(wpath.c_str(), FR_PRIVATE, nullptr) > 0) {
        LOGFONTW lf  = {};
        lf.lfCharSet = DEFAULT_CHARSET;
        // use the filename stem as the face name — good enough for GDI lookup
        const QString stem       = QFileInfo(path).baseName();
        const std::wstring wstem = stem.toStdWString();
        wcsncpy_s(lf.lfFaceName, wstem.c_str(), LF_FACESIZE - 1);

        hfont = CreateFontIndirectW(&lf);
        if (hfont) {
            HGDIOBJ old = SelectObject(hdc, hfont);
            DWORD size  = GetFontData(hdc, kNameTag, 0, nullptr, 0);
            if (size != GDI_ERROR && size > 0) {
                result.resize((int)size);
                GetFontData(hdc, kNameTag, 0,
                            reinterpret_cast<LPVOID>(result.data()), size);
            }
            SelectObject(hdc, old);
            DeleteObject(hfont);
        }
        RemoveFontResourceExW(wpath.c_str(), FR_PRIVATE, nullptr);
    }
    DeleteDC(hdc);

    // GDI path failed — fall back to reading the raw file directly.
    // For a plain TTF/OTF the name table offset is in the sfnt offset table.
    if (result.isEmpty()) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            return {};
        }
        QByteArray raw = f.readAll();
        if (raw.size() < 12) {
            return {};
        }
        const uchar *rp    = reinterpret_cast<const uchar *>(raw.constData());
        quint16 num_tables = readU16(rp + 4);
        if (raw.size() < 12 + num_tables * 16) {
            return {};
        }
        constexpr quint32 kName = 0x6E616D65;  // 'name'
        for (quint16 t = 0; t < num_tables; ++t) {
            const uchar *rec = rp + 12 + t * 16;
            quint32 tag      = readU32(rec + 0);
            quint32 offset   = readU32(rec + 8);
            quint32 length   = readU32(rec + 12);
            if (tag == kName && offset + length <= (quint32)raw.size()) {
                result = raw.mid((int)offset, (int)length);
                break;
            }
        }
    }

    return result;
}

}  // namespace

QVector<FontNameRecord> readFontNameTable(const QString &path)
{
    const QByteArray data = fetchNameTable(path);
    if (data.isEmpty()) {
        return {};
    }
    return parseNameTable(data);
}
