#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QFont>
#include <QPalette>
#include <QRectF>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <array>
#include <optional>

#include "engine/fontinspectorengine.h"

class QLabel;
class QPushButton;
class QTimer;
class QVBoxLayout;
class QGroupBox;
class QHideEvent;

namespace fontui {
class FlowSeparator;
}

// Forward declaration for preview widget
class GlyphPreviewWidget;

enum class GlyphGuideId {
    Baseline = 0,
    Ascent,
    Descent,
    CapHeight,
    XHeight,
    Underline,
    Strikeout,
    Advance,
    Bearings,
    BBox,
    Center,
};

QString glyphGuideKey(GlyphGuideId id);
std::optional<GlyphGuideId> glyphGuideFromKey(const QString &key);
QColor glyphGuideColor(GlyphGuideId id, const QPalette &pal,
                       bool opaque = false);

class GlyphInspectorWidget : public QWidget {
    Q_OBJECT

public:
    explicit GlyphInspectorWidget(QWidget *parent = nullptr);

    void updateGlyph(quint32 code_point,
                     const QFont &font,
                     const QString &font_path,
                     const FontDecorationMetrics &deco = {},
                     const GlyphOutlineData &outline   = {},
                     const QString &glyph_name         = {});
    void updateGlyph(QChar ch,
                     const QFont &font,
                     const QString &font_path,
                     const FontDecorationMetrics &deco = {},
                     const GlyphOutlineData &outline   = {},
                     const QString &glyph_name         = {})
    {
        updateGlyph(ch.isNull() ? 0u : quint32(ch.unicode()), font, font_path,
                    deco, outline, glyph_name);
    }
    void setDpr(qreal dpr);
    void clearSelection();
    QPixmap getPreviewPixmap() const;
    bool hasSelection() const
    {
        return m_has_selection;
    }
    quint32 currentCodePoint() const
    {
        return m_code_point;
    }
    QChar currentChar() const
    {
        return (m_code_point > 0 && m_code_point <= 0xFFFFu
                && !(m_code_point >= 0xD800u && m_code_point <= 0xDFFFu))
                   ? QChar(ushort(m_code_point))
                   : QChar();
    }
    const QFont &currentFont() const
    {
        return m_font;
    }
    const FontDecorationMetrics &decorationMetrics() const
    {
        return m_deco_metrics;
    }
    const GlyphOutlineData &outlineData() const
    {
        return m_outline;
    }
    void setShowOutlinePoints(bool on);
    bool isShowOutlinePoints() const
    {
        return m_show_outline_points;
    }
    void setOutlineShortcutHint(const QString &hint);
    QStringList hiddenGuideIds() const;
    void setHiddenGuideIds(const QStringList &ids);
    QVector<QPushButton *> guidePillButtons() const;
    GlyphPreviewWidget *previewWidget() const
    {
        return m_preview_widget;
    }

public slots:
    void updateFont(const QFont &font,
                    const FontDecorationMetrics &deco = {},
                    const GlyphOutlineData &outline   = {},
                    const QString &glyph_name         = {});
    void updateTheme(int theme);

protected:
    void changeEvent(QEvent *event) override;

private:
    static constexpr int kGuideCount = 11;

    void createUI();
    void applyEmptyLabelPalette();
    void syncEmptyLabelFont();
    void syncMetricsAndFonts();
    void updateMetrics();
    void updateLabels();
    void syncGuideControls(bool animate = false);
    bool hasHiddenAvailableGuide(
        const std::array<bool, kGuideCount> &availability) const;
    void updateToggleAllButton(
        const std::array<bool, kGuideCount> &availability);
    void refreshThemeVisuals();
    std::array<bool, kGuideCount> computeGuideAvailability() const;

    // Data
    quint32 m_code_point = 0;
    QFont m_font;
    QString m_font_path;
    QString m_glyph_name;
    FontDecorationMetrics m_deco_metrics;
    GlyphOutlineData m_outline;
    bool m_has_selection       = false;
    bool m_show_outline_points = false;

    // Metrics cache
    struct GlyphMetrics {
        quint32 glyph_id    = 0;
        qreal advance_width = 0;
        QRectF bounding_box;
        qreal left_bearing  = 0;
        qreal right_bearing = 0;
        qreal ascent        = 0;
        qreal descent       = 0;
        qreal line_gap      = 0;
        int units_per_em    = 0;
    } m_metrics;

    // UI components
    QLabel *m_label_unicode_value;
    QLabel *m_label_unicode_decimal;
    QLabel *m_label_unicode_block;
    QLabel *m_label_glyph_id = nullptr;
    QLabel *m_label_advance_width;
    QLabel *m_label_bbox;
    QLabel *m_label_bearings;
    QLabel *m_label_contours;
    QLabel *m_label_points;
    QLabel *m_label_ascent;
    QLabel *m_label_descent;
    QLabel *m_label_line_gap;
    QLabel *m_label_units_per_em;
    QLabel *m_label_underline;
    QLabel *m_label_strikeout;
    QSet<GlyphGuideId> m_hidden_guides;
    QVector<QPushButton *> m_guide_pills;
    QPushButton *m_btn_outline    = nullptr;
    QPushButton *m_btn_toggle_all = nullptr;
    fontui::FlowSeparator *m_guide_sep = nullptr;
    QWidget *m_guide_bar               = nullptr;
    QString m_outline_hint;
    GlyphPreviewWidget *m_preview_widget;
    QLabel *m_label_no_selection;
    QGroupBox *m_group_unicode;
    QGroupBox *m_group_glyph;
    QGroupBox *m_group_font;
    QGroupBox *m_group_preview;
    QVBoxLayout *m_main_layout = nullptr;

    int m_theme  = 0;
    qreal m_dpr  = 1.0;
};

// Custom widget for glyph preview rendering
class GlyphPreviewWidget : public QWidget {
    Q_OBJECT

public:
    explicit GlyphPreviewWidget(QWidget *parent = nullptr);

    void setDpr(qreal dpr);
    void setGlyphData(quint32 code_point,
                      const QFont &font,
                      const QRectF &bbox,
                      qreal advance_width,
                      const FontDecorationMetrics &deco = {},
                      const GlyphOutlineData &outline   = {});
    void setGlyphData(QChar ch,
                      const QFont &font,
                      const QRectF &bbox,
                      qreal advance_width,
                      const FontDecorationMetrics &deco = {},
                      const GlyphOutlineData &outline   = {})
    {
        setGlyphData(ch.isNull() ? 0u : quint32(ch.unicode()), font, bbox,
                     advance_width, deco, outline);
    }
    void setShowOutlinePoints(bool on, bool animate = false);
    bool isShowOutlinePoints() const
    {
        return m_show_outline_points;
    }
    void setGuideVisible(GlyphGuideId id, bool visible, bool animate = true);
    bool isGuideVisible(GlyphGuideId id) const
    {
        return !m_hidden_guides.contains(id);
    }
    void setHiddenGuides(const QSet<GlyphGuideId> &hidden, bool animate = false);
    const QSet<GlyphGuideId> &hiddenGuides() const
    {
        return m_hidden_guides;
    }
    void clear();

    qreal guideOpacity(GlyphGuideId id) const;
    qreal outlineOpacity() const
    {
        return m_outline_alpha;
    }
    bool isGuideAnimating() const;

    QVector<QRectF> lastLabelRects() const
    {
        return m_last_label_rects;
    }
    QStringList lastLabelTexts() const
    {
        return m_last_label_texts;
    }
    QRectF lastUnderlineBand() const
    {
        return m_last_underline_band;
    }
    QRectF lastStrikeoutBand() const
    {
        return m_last_strikeout_band;
    }
    QVector<QPointF> lastOutlineScreenPoints() const
    {
        return m_last_outline_points;
    }

    QSize sizeHint() const override;

protected:
    void hideEvent(QHideEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    static constexpr int kGuideCount     = 11;
    static constexpr int kAnimDurationMs = 150;

    void syncAnimationState(bool animate);
    void advanceAnimations();

    quint32 m_code_point = 0;
    QFont m_font;
    QRectF m_bbox;
    FontDecorationMetrics m_deco_metrics;
    GlyphOutlineData m_outline;
    qreal m_dpr                 = 1.0;
    qreal m_advance_width       = 0;
    bool m_has_data             = false;
    bool m_show_outline_points  = false;
    QSet<GlyphGuideId> m_hidden_guides;
    std::array<qreal, kGuideCount> m_guide_alphas{};
    std::array<qreal, kGuideCount> m_guide_from{};
    std::array<qreal, kGuideCount> m_guide_targets{};
    qreal m_outline_alpha  = 0.0;
    qreal m_outline_from   = 0.0;
    qreal m_outline_target = 0.0;
    QTimer *m_anim_timer   = nullptr;
    QElapsedTimer m_anim_clock;
    mutable QVector<QRectF> m_last_label_rects;
    mutable QStringList m_last_label_texts;
    mutable QRectF m_last_underline_band;
    mutable QRectF m_last_strikeout_band;
    mutable QVector<QPointF> m_last_outline_points;
};
