#pragma once

#include <QFont>
#include <QRectF>
#include <QVector>
#include <QWidget>

#include "fontinspectorengine.h"

class QCheckBox;
class QLabel;
class QVBoxLayout;
class QGroupBox;

// Forward declaration for preview widget
class GlyphPreviewWidget;

class GlyphInspectorWidget : public QWidget {
    Q_OBJECT

public:
    explicit GlyphInspectorWidget(QWidget *parent = nullptr);

    void updateGlyph(QChar ch,
                     const QFont &font,
                     const QString &font_path,
                     const FontDecorationMetrics &deco = {},
                     const GlyphOutlineData &outline   = {});
    void clearSelection();
    QPixmap getPreviewPixmap() const;
    bool hasSelection() const
    {
        return m_has_selection;
    }
    QChar currentChar() const
    {
        return m_char;
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
    GlyphPreviewWidget *previewWidget() const
    {
        return m_preview_widget;
    }

public slots:
    void updateFont(const QFont &font,
                    const FontDecorationMetrics &deco = {},
                    const GlyphOutlineData &outline   = {});
    void updateTheme(int theme);

private:
    void createUI();
    void updateMetrics();
    void updateLabels();
    QString getUnicodeBlockName(ushort code_point);

    // Data
    QChar m_char;
    QFont m_font;
    QString m_font_path;
    FontDecorationMetrics m_deco_metrics;
    GlyphOutlineData m_outline;
    bool m_has_selection       = false;
    bool m_show_outline_points = false;

    // Metrics cache
    struct GlyphMetrics {
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
    QCheckBox *m_chk_outline;
    GlyphPreviewWidget *m_preview_widget;
    QLabel *m_label_no_selection;
    QGroupBox *m_group_unicode;
    QGroupBox *m_group_glyph;
    QGroupBox *m_group_font;
    QGroupBox *m_group_preview;

    int m_theme = 0;
};

// Custom widget for glyph preview rendering
class GlyphPreviewWidget : public QWidget {
    Q_OBJECT

public:
    explicit GlyphPreviewWidget(QWidget *parent = nullptr);

    void setGlyphData(QChar ch,
                      const QFont &font,
                      const QRectF &bbox,
                      qreal advance_width,
                      const FontDecorationMetrics &deco = {},
                      const GlyphOutlineData &outline   = {});
    void setShowOutlinePoints(bool on);
    bool isShowOutlinePoints() const
    {
        return m_show_outline_points;
    }
    void clear();

    QVector<QRectF> lastLabelRects() const
    {
        return m_last_label_rects;
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
    void paintEvent(QPaintEvent *event) override;

private:
    QChar m_char;
    QFont m_font;
    QRectF m_bbox;
    FontDecorationMetrics m_deco_metrics;
    GlyphOutlineData m_outline;
    qreal m_advance_width       = 0;
    bool m_has_data             = false;
    bool m_show_outline_points  = false;
    mutable QVector<QRectF> m_last_label_rects;
    mutable QRectF m_last_underline_band;
    mutable QRectF m_last_strikeout_band;
    mutable QVector<QPointF> m_last_outline_points;
};
