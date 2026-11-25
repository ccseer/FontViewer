#pragma once

#include <QByteArray>
#include <QHash>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

struct FT_FaceRec_;
struct FT_LibraryRec_;

struct FontNameRecord {
    QString label;
    QString value;
};

struct FontVariableAxisInfo {
    QByteArray tag;  // 4-byte ASCII tag, e.g. "wght", "wdth", "slnt"
    QString name;    // resolved from name table, fallback to tag
    float min_value     = 0.0f;
    float default_value = 0.0f;
    float max_value     = 0.0f;
};

struct FontNamedInstance {
    QString name;
    QVector<float> coords;  // 1:1 with variable_axes
};

struct FontDecorationMetrics {
    bool has_underline  = false;
    int underline_pos   = 0;  // post.underlinePosition (FUnits, y-up, top edge)
    int underline_thick = 0;  // post.underlineThickness (FUnits)
    bool has_strikeout  = false;
    int strikeout_pos   = 0;  // OS/2.yStrikeoutPosition (FUnits, y-up, top edge)
    int strikeout_size  = 0;  // OS/2.yStrikeoutSize (FUnits)
    int units_per_em    = 0;
};

struct FontFeatureInfo {
    QByteArray tag;
    QString friendly_name;
    bool default_on = false;
};

struct GlyphOutlineData {
    enum PointKind : quint8 {
        OffConic = 0,
        OnCurve  = 1,
        OffCubic = 2,
    };

    bool valid        = false;
    int contour_count = 0;
    int point_count   = 0;
    QVector<QPointF> points;    // FUnits, y-up
    QVector<quint8> tags;       // PointKind
    QVector<int> contour_ends;  // inclusive end index per contour
};

struct FontInspectionData {
    QVector<FontNameRecord> name_records;
    FontDecorationMetrics decoration_metrics;
    QVector<FontVariableAxisInfo> variable_axes;
    QVector<FontNamedInstance> named_instances;
    QVector<FontFeatureInfo> ot_features;
    QStringList locl_languages;
};

class FontInspectorEngine {
public:
    FontInspectorEngine() = default;
    ~FontInspectorEngine();

    FontInspectorEngine(const FontInspectorEngine &)            = delete;
    FontInspectorEngine &operator=(const FontInspectorEngine &) = delete;

    bool openFile(const QString &font_path);
    void close();

    bool isValid() const
    {
        return !m_faces.isEmpty();
    }
    QString fontPath() const
    {
        return m_font_path;
    }
    int faceCount() const
    {
        return m_faces.size();
    }

    int resolveFaceIndex(const QString &family, const QString &style) const;
    const FontInspectionData &inspectFace(int face_index) const;
    std::optional<FontNamedInstance> matchNamedInstance(
        int face_index, const QString &style) const;
    FontDecorationMetrics decorationMetrics(
        int face_index, const QHash<QByteArray, float> &axis_coords = {});
    GlyphOutlineData glyphOutline(
        int face_index,
        QChar ch,
        const QHash<QByteArray, float> &axis_coords = {});

    static QVector<FontNameRecord> readNameRecords(const QString &font_path);
    static QString normalizeStyleName(const QString &name);
    QString probeSampleForFace(int face_index) const;

private:
    struct FaceEntry {
        FT_FaceRec_ *face = nullptr;
        QString family_name;
        QString style_name;
        QString typographic_family;
        QString typographic_subfamily;
        QStringList family_aliases;
        QStringList style_aliases;
        QVector<QStringList> instance_aliases;
        QVector<long> applied_coords;
        bool has_applied_coords = false;
        FontInspectionData data;
    };

    bool ensureLibrary();
    void applyAxisCoords(FaceEntry &entry,
                         const QHash<QByteArray, float> &axis_coords);
    void populateFace(FaceEntry &entry);

    FT_LibraryRec_ *m_library = nullptr;
    QString m_font_path;
    QByteArray m_font_bytes;
    QVector<FaceEntry> m_faces;
};
