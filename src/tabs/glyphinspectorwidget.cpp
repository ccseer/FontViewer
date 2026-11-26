#include "glyphinspectorwidget.h"

#include <QCheckBox>
#include <QEvent>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QRawFont>
#include <QVBoxLayout>
#include <QtMath>

#include <algorithm>
#include <limits>
#include <optional>

#include "fontui.h"

#define qprintt qDebug() << "[GlyphInspector]"

namespace {
qreal rectOverlapArea(const QRectF &a, const QRectF &b)
{
    const QRectF inter = a.intersected(b);
    return inter.isEmpty() ? 0.0 : inter.width() * inter.height();
}

QRectF placeNonOverlappingLabel(const QRectF &preferred,
                                const QRectF &bounds,
                                qreal step_y,
                                const QVector<QRectF> &occupied,
                                bool prefer_horizontal = false)
{
    auto clamp_rect = [&bounds](QRectF r) {
        if (r.left() < bounds.left()) {
            r.moveLeft(bounds.left());
        }
        if (r.right() > bounds.right()) {
            r.moveRight(bounds.right());
        }
        if (r.top() < bounds.top()) {
            r.moveTop(bounds.top());
        }
        if (r.bottom() > bounds.bottom()) {
            r.moveBottom(bounds.bottom());
        }
        return r;
    };

    auto overlaps = [&occupied](const QRectF &r) {
        for (const QRectF &occ : occupied) {
            if (r.intersects(occ)) {
                return true;
            }
        }
        return false;
    };

    const QRectF candidate = clamp_rect(preferred);
    if (!overlaps(candidate)) {
        return candidate;
    }

    QVector<qreal> x_candidates;
    auto push_x = [&](qreal x) {
        const qreal max_x
            = qMax(bounds.left(), bounds.right() - candidate.width());
        const qreal cx = qBound(bounds.left(), x, max_x);
        for (qreal seen : x_candidates) {
            if (qAbs(seen - cx) < 1.0) {
                return;
            }
        }
        x_candidates.append(cx);
    };
    push_x(candidate.left());
    for (const QRectF &occ : occupied) {
        if (prefer_horizontal
            && occ.intersects(QRectF(bounds.left(), candidate.top(),
                                     bounds.width(), candidate.height()))) {
            push_x(occ.left() - candidate.width() - 4.0);
            push_x(occ.right() + 4.0);
        }
    }
    for (const QRectF &occ : occupied) {
        if (occ.intersects(QRectF(candidate.left(), bounds.top(),
                                  candidate.width(), bounds.height()))) {
            push_x(occ.left() - candidate.width() - 4.0);
            push_x(occ.right() + 4.0);
        }
    }
    for (int col = 1; col <= 4; ++col) {
        const qreal dx = qMax(24.0, candidate.width() + 6.0) * col;
        push_x(candidate.left() - dx);
        push_x(candidate.left() + dx);
    }
    push_x(bounds.left());
    push_x(bounds.right() - candidate.width());

    // Horizontal metric labels (ascent, baseline, underline, etc.) should stagger
    // horizontally along their own reference line before moving vertically so
    // labels never cross onto the wrong side of an adjacent line.
    if (prefer_horizontal) {
        for (qreal x : x_candidates) {
            QRectF probe = candidate;
            probe.moveLeft(x);
            if (!overlaps(probe)) {
                return probe;
            }
        }
    }

    const qreal dir     = (step_y >= 0.0) ? 1.0 : -1.0;
    const qreal mag     = qMax(4.0, qAbs(step_y));
    const int max_steps = qMax(16, int(qCeil(bounds.height() / mag)) + 1);
    auto try_vertical   = [&](const QRectF &origin) -> std::optional<QRectF> {
        const QRectF base = clamp_rect(origin);
        if (!overlaps(base)) {
            return base;
        }
        // Start vertical sweeps from the clamped origin so labels whose
        // anchor lines fall far outside the widget still search inside bounds.
        for (qreal s : {dir, -dir}) {
            QRectF prev = base;
            for (int i = 1; i <= max_steps; ++i) {
                const QRectF shifted
                    = clamp_rect(base.translated(0.0, s * mag * i));
                if (!overlaps(shifted)) {
                    return shifted;
                }
                if (shifted == prev) {
                    break;
                }
                prev = shifted;
            }
        }
        return std::nullopt;
    };

    if (const auto r = try_vertical(candidate)) {
        return *r;
    }

    for (qreal x : x_candidates) {
        QRectF probe = candidate;
        probe.moveLeft(x);
        if (const auto r = try_vertical(probe)) {
            return *r;
        }
    }

    QRectF best     = candidate;
    qreal best_area = std::numeric_limits<qreal>::max();
    for (qreal x : x_candidates) {
        for (int i = 0; i <= max_steps; ++i) {
            const qreal max_y
                = qMax(bounds.top(), bounds.bottom() - candidate.height());
            const qreal y = qMin(bounds.top() + mag * i, max_y);
            const QRectF probe(x, y, candidate.width(), candidate.height());
            qreal area = 0.0;
            for (const QRectF &occ : occupied) {
                area += rectOverlapArea(probe, occ);
            }
            if (area < best_area) {
                best_area = area;
                best      = probe;
                if (qFuzzyIsNull(area)) {
                    return best;
                }
            }
        }
    }
    return best;
}

void configureValueLabel(QLabel *label)
{
    label->setWordWrap(true);
    label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}
}  // namespace

// GlyphPreviewWidget implementation
GlyphPreviewWidget::GlyphPreviewWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(fontui::dp(260, m_dpr), fontui::dp(280, m_dpr));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void GlyphPreviewWidget::setDpr(qreal dpr)
{
    const qreal clamped = fontui::clampDpr(dpr);
    if (qFuzzyCompare(m_dpr, clamped)) {
        return;
    }
    m_dpr = clamped;
    setMinimumSize(fontui::dp(260, m_dpr), fontui::dp(280, m_dpr));
    update();
}

void GlyphPreviewWidget::setGlyphData(QChar ch,
                                      const QFont &font,
                                      const QRectF &bbox,
                                      qreal advance_width,
                                      const FontDecorationMetrics &deco,
                                      const GlyphOutlineData &outline)
{
    m_char          = ch;
    m_font          = font;
    m_bbox          = bbox;
    m_deco_metrics  = deco;
    m_outline       = outline;
    m_advance_width = advance_width;
    m_has_data      = true;
    update();
}

void GlyphPreviewWidget::setShowOutlinePoints(bool on)
{
    if (m_show_outline_points == on) {
        return;
    }
    m_show_outline_points = on;
    update();
}

void GlyphPreviewWidget::clear()
{
    m_has_data = false;
    m_outline  = {};
    m_last_label_rects.clear();
    m_last_label_texts.clear();
    m_last_underline_band = {};
    m_last_strikeout_band = {};
    m_last_outline_points.clear();
    update();
}

QSize GlyphPreviewWidget::sizeHint() const
{
    return QSize(400, 400);
}

void GlyphPreviewWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    m_last_label_rects.clear();
    m_last_label_texts.clear();
    m_last_underline_band = {};
    m_last_strikeout_band = {};
    m_last_outline_points.clear();

    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    const auto &pal  = palette();
    const QRect rect = this->rect();

    // Fill background
    painter.fillRect(rect, pal.base());

    if (!m_has_data) {
        return;
    }

    // Calculate center
    qreal center_x = rect.width() / 2.0;
    qreal center_y = rect.height() / 2.0;

    // Setup display font
    QFont display_font = m_font;
    if (m_font.pointSizeF() > 0.0) {
        display_font.setPointSizeF(qMax(72.0, m_font.pointSizeF() * 4.0));
    }
    else if (m_font.pixelSize() > 0) {
        display_font.setPixelSize(qMax(96, m_font.pixelSize() * 4));
    }
    else {
        display_font.setPointSizeF(72.0);
    }
    QFontMetricsF display_fm(display_font, this);

    // Get strict metrics from QRawFont at display size to ensure consistency
    QRawFont display_raw = QRawFont::fromFont(display_font);
    QRectF glyph_box;
    qreal display_advance = 0;
    qreal display_ascent  = display_fm.ascent();
    qreal display_descent = display_fm.descent();

    bool use_strict = false;
    if (display_raw.isValid()) {
        QString str(m_char);
        QVector<quint32> indexes = display_raw.glyphIndexesForString(str);
        if (!indexes.isEmpty()) {
            quint32 index = indexes[0];
            const QVector<QPointF> advances
                = display_raw.advancesForGlyphIndexes({index});
            glyph_box       = display_raw.boundingRect(index);
            display_advance = advances.isEmpty() ? 0.0 : advances.at(0).x();
            display_ascent  = display_raw.ascent();
            display_descent = display_raw.descent();
            use_strict      = true;
        }
    }

    // Calculate text position (center the glyph in the widget)
    qreal text_x = center_x - display_advance / 2.0;
    qreal text_y = center_y + (display_ascent - display_descent) / 2.0;

    if (!use_strict) {
        QFontMetricsF base_fm(m_font, this);
        qreal sx = base_fm.horizontalAdvance(m_char) > 0
                       ? qreal(display_fm.horizontalAdvance(m_char))
                             / base_fm.horizontalAdvance(m_char)
                       : 1.0;
        qreal sy = base_fm.height() > 0
                       ? qreal(display_fm.height()) / base_fm.height()
                       : 1.0;

        display_advance = m_advance_width * sx;
        glyph_box       = QRectF(m_bbox.x() * sx, m_bbox.y() * sy,
                                 m_bbox.width() * sx, m_bbox.height() * sy);
        text_x          = center_x - display_fm.horizontalAdvance(m_char) / 2.0;
        text_y = center_y + (display_fm.ascent() - display_fm.descent()) / 2.0;
    }

    // Translate glyph_box to actual drawing position
    glyph_box.translate(text_x, text_y);

    const bool dark = fontui::isDarkPalette(pal);
    const qreal dpr = m_dpr;
    const QColor muted_guide = fontui::mutedTextColor(pal);
    const QColor ascent_clr  = dark ? QColor(100, 150, 255) : QColor(29, 111, 216);
    const QColor descent_clr = dark ? QColor(255, 150, 100) : QColor(201, 81, 27);
    const QColor cap_clr     = dark ? QColor(255, 100, 200) : QColor(179, 50, 138);
    const QColor xh_clr      = dark ? QColor(100, 200, 150) : QColor(25, 135, 107);
    const QColor bbox_clr    = dark ? QColor(100, 255, 100, 110) : QColor(27, 127, 55, 140);
    const QColor adv_clr     = dark ? QColor(200, 100, 255) : QColor(124, 58, 237);
    const QColor ul_clr      = dark ? QColor(70, 160, 255) : QColor(2, 136, 209);
    const QColor st_clr      = dark ? QColor(255, 120, 80) : QColor(211, 47, 47);
    const QColor sb_clr      = dark ? QColor(150, 150, 150) : QColor(95, 99, 104);
    const qreal pad_10       = fontui::dpf(10.0, dpr);
    const qreal pad_20       = fontui::dpf(20.0, dpr);
    const qreal pad_30       = fontui::dpf(30.0, dpr);
    const qreal stroke_1     = fontui::dpf(1.0, dpr);

    // Draw advance width rectangle (subtle fill)
    QColor adv_fill = adv_clr;
    adv_fill.setAlpha(dark ? 12 : 16);
    painter.fillRect(
        QRectF(text_x, pad_20, display_advance, rect.height() - pad_20 * 2.0),
        adv_fill);

    // Derive em pixel size from the same font size source used to draw the glyph.
    qreal em_px = 0.0;
    if (display_font.pointSizeF() > 0.0) {
        em_px = display_font.pointSizeF() * display_fm.fontDpi() / 72.0;
    }
    else if (display_raw.isValid() && display_raw.pixelSize() > 0.0) {
        em_px = display_raw.pixelSize();
    }
    else if (display_font.pixelSize() > 0) {
        em_px = display_font.pixelSize();
    }

    const int upem         = m_deco_metrics.units_per_em;
    const qreal band_width = qMax(0.0, rect.width() - pad_10 * 2.0);
    qreal underline_top_y  = 0.0;
    bool draw_underline    = false;
    qreal strike_top_y     = 0.0;
    bool draw_strikeout    = false;

    if (upem > 0 && em_px > 0.0 && band_width > 0.0) {
        if (m_deco_metrics.has_underline) {
            underline_top_y
                = text_y
                  - (qreal(m_deco_metrics.underline_pos) / upem) * em_px;
            const qreal band_h = qMax(
                1.0, (qreal(m_deco_metrics.underline_thick) / upem) * em_px);
            const QRectF ul_rect(pad_10, underline_top_y, band_width, band_h);
            QColor ul_fill = ul_clr;
            ul_fill.setAlpha(38);
            painter.fillRect(ul_rect, ul_fill);
            QColor ul_line = ul_clr;
            ul_line.setAlpha(190);
            QPen ul_pen(ul_line);
            ul_pen.setStyle(Qt::DashLine);
            ul_pen.setWidthF(stroke_1);
            painter.setPen(ul_pen);
            painter.drawLine(QPointF(pad_10, underline_top_y),
                             QPointF(rect.width() - pad_10, underline_top_y));
            m_last_underline_band = ul_rect;
            draw_underline        = true;
        }
        if (m_deco_metrics.has_strikeout) {
            strike_top_y
                = text_y
                  - (qreal(m_deco_metrics.strikeout_pos) / upem) * em_px;
            const qreal band_h = qMax(
                1.0, (qreal(m_deco_metrics.strikeout_size) / upem) * em_px);
            const QRectF st_rect(pad_10, strike_top_y, band_width, band_h);
            QColor st_fill = st_clr;
            st_fill.setAlpha(38);
            painter.fillRect(st_rect, st_fill);
            QColor st_line = st_clr;
            st_line.setAlpha(190);
            QPen st_pen(st_line);
            st_pen.setStyle(Qt::DashLine);
            st_pen.setWidthF(stroke_1);
            painter.setPen(st_pen);
            painter.drawLine(QPointF(pad_10, strike_top_y),
                             QPointF(rect.width() - pad_10, strike_top_y));
            m_last_strikeout_band = st_rect;
            draw_strikeout        = true;
        }
    }

    // Draw baseline (dashed line) - horizontal through text baseline
    QPen baseline_pen(muted_guide);
    baseline_pen.setStyle(Qt::DashLine);
    baseline_pen.setWidthF(stroke_1);
    painter.setPen(baseline_pen);
    painter.drawLine(QPointF(pad_10, text_y),
                     QPointF(rect.width() - pad_10, text_y));

    // Draw vertical center line (dotted)
    QPen center_pen(muted_guide);
    center_pen.setStyle(Qt::DotLine);
    center_pen.setWidthF(stroke_1);
    painter.setPen(center_pen);
    painter.drawLine(QPointF(center_x, pad_10),
                     QPointF(center_x, rect.height() - pad_10));

    // Draw advance width vertical lines
    QColor adv_line = adv_clr;
    adv_line.setAlpha(dark ? 120 : 140);
    QPen advance_pen(adv_line);
    advance_pen.setStyle(Qt::DashLine);
    advance_pen.setWidthF(stroke_1);
    painter.setPen(advance_pen);
    painter.drawLine(QPointF(text_x, pad_20),
                     QPointF(text_x, rect.height() - pad_20));
    painter.drawLine(QPointF(text_x + display_advance, pad_20),
                     QPointF(text_x + display_advance, rect.height() - pad_20));

    // Draw ascent line (above baseline)
    QColor asc_line = ascent_clr;
    asc_line.setAlpha(dark ? 120 : 140);
    QPen ascent_pen(asc_line);
    ascent_pen.setStyle(Qt::DotLine);
    ascent_pen.setWidthF(stroke_1);
    painter.setPen(ascent_pen);
    qreal ascent_y = text_y - display_ascent;
    painter.drawLine(QPointF(pad_10, ascent_y),
                     QPointF(rect.width() - pad_10, ascent_y));

    // Draw descent line (below baseline)
    QColor desc_line = descent_clr;
    desc_line.setAlpha(dark ? 120 : 140);
    QPen descent_pen(desc_line);
    descent_pen.setStyle(Qt::DotLine);
    descent_pen.setWidthF(stroke_1);
    painter.setPen(descent_pen);
    qreal descent_y = text_y + display_descent;
    painter.drawLine(QPointF(pad_10, descent_y),
                     QPointF(rect.width() - pad_10, descent_y));

    // Draw cap height line
    qreal cap_y = text_y - display_fm.capHeight();
    if (display_fm.capHeight() > 0 && qAbs(cap_y - ascent_y) > 0.1) {
        QColor cap_line = cap_clr;
        cap_line.setAlpha(dark ? 120 : 140);
        QPen cap_pen(cap_line);
        cap_pen.setStyle(Qt::DotLine);
        cap_pen.setWidthF(stroke_1);
        painter.setPen(cap_pen);
        painter.drawLine(QPointF(pad_10, cap_y),
                         QPointF(rect.width() - pad_10, cap_y));
    }

    // Draw x-height line
    qreal x_y = text_y - display_fm.xHeight();
    if (display_fm.xHeight() > 0) {
        QColor xh_line = xh_clr;
        xh_line.setAlpha(dark ? 120 : 140);
        QPen x_pen(xh_line);
        x_pen.setStyle(Qt::DotLine);
        x_pen.setWidthF(stroke_1);
        painter.setPen(x_pen);
        painter.drawLine(QPointF(pad_10, x_y),
                         QPointF(rect.width() - pad_10, x_y));
    }

    // Draw bounding box (green) - actual glyph bounds
    QPen bbox_pen(bbox_clr);
    bbox_pen.setStyle(Qt::DashDotLine);
    bbox_pen.setWidthF(stroke_1);
    painter.setPen(bbox_pen);
    painter.drawRect(glyph_box);

    // Draw bearing markers
    QColor bearing_line = sb_clr;
    bearing_line.setAlpha(dark ? 80 : 110);
    QPen bearing_pen(bearing_line);
    bearing_pen.setStyle(Qt::DotLine);
    bearing_pen.setWidthF(stroke_1);
    painter.setPen(bearing_pen);
    painter.drawLine(QPointF(glyph_box.left(), pad_30),
                     QPointF(glyph_box.left(), rect.height() - pad_30));
    painter.drawLine(QPointF(glyph_box.right(), pad_30),
                     QPointF(glyph_box.right(), rect.height() - pad_30));

    // Draw the glyph
    painter.setFont(display_font);
    painter.setPen(pal.text().color());
    painter.drawText(QPointF(text_x, text_y), QString(m_char));

    // Overlay FreeType Bezier control points and tangent handles on top of the
    // glyph and underneath metric labels when toggled on.
    const int outline_upem = m_deco_metrics.units_per_em > 0
                                 ? m_deco_metrics.units_per_em
                                 : (display_raw.isValid()
                                        ? qRound(display_raw.unitsPerEm())
                                        : 0);
    if (m_show_outline_points && m_outline.valid && outline_upem > 0
        && em_px > 0.0 && !m_outline.points.isEmpty()
        && m_outline.tags.size() == m_outline.points.size()) {
        const qreal scale = em_px / outline_upem;
        QVector<QPointF> screen_pts;
        screen_pts.reserve(m_outline.points.size());
        for (const QPointF &fp : m_outline.points) {
            screen_pts.append(
                QPointF(text_x + fp.x() * scale, text_y - fp.y() * scale));
        }
        m_last_outline_points = screen_pts;

        QPen handle_pen(QColor(230, 125, 35, 165));
        handle_pen.setWidthF(stroke_1);
        painter.setPen(handle_pen);
        int contour_start = 0;
        for (int contour_end : m_outline.contour_ends) {
            if (contour_start < 0 || contour_end < contour_start
                || contour_end >= screen_pts.size()) {
                continue;
            }
            const int count = contour_end - contour_start + 1;
            if (count >= 2) {
                for (int i = contour_start; i <= contour_end; ++i) {
                    const int next
                        = (i == contour_end) ? contour_start : (i + 1);
                    if (m_outline.tags[i]
                            == GlyphOutlineData::PointKind::OffCubic
                        && m_outline.tags[next]
                               == GlyphOutlineData::PointKind::OffCubic) {
                        continue;
                    }
                    if (m_outline.tags[i]
                            != GlyphOutlineData::PointKind::OnCurve
                        || m_outline.tags[next]
                               != GlyphOutlineData::PointKind::OnCurve) {
                        painter.drawLine(screen_pts[i], screen_pts[next]);
                    }
                }
            }
            contour_start = contour_end + 1;
        }

        const QColor base_col = pal.base().color();
        const qreal pt_r      = fontui::dpf(2.5, dpr);
        const qreal pt_side   = fontui::dpf(5.0, dpr);
        for (int i = 0; i < screen_pts.size(); ++i) {
            const QPointF &pt = screen_pts[i];
            if (m_outline.tags[i] == GlyphOutlineData::PointKind::OnCurve) {
                painter.setPen(QPen(
                    dark ? QColor(255, 255, 255, 220) : QColor(20, 40, 70, 220),
                    stroke_1));
                painter.setBrush(QColor(35, 145, 255));
                painter.drawRect(
                    QRectF(pt.x() - pt_r, pt.y() - pt_r, pt_side, pt_side));
            }
            else {
                painter.setPen(
                    QPen(QColor(230, 125, 35), fontui::dpf(1.5, dpr)));
                painter.setBrush(base_col);
                painter.drawEllipse(pt, pt_r, pt_r);
            }
        }
        painter.setBrush(Qt::NoBrush);
    }

    // Draw metric labels measured against the widget font and kept within
    // bounds without overlapping each other.
    painter.save();
    QFont label_font = this->font();
    label_font.setPointSize(8);
    painter.setFont(label_font);
    const QFontMetricsF label_fm(label_font, this);
    const qreal safe_inset   = fontui::dpf(4.0, dpr);
    const qreal step_pad     = fontui::dpf(2.0, dpr);
    const qreal gap_x        = fontui::dpf(2.0, dpr);
    const QRectF safe_bounds = QRectF(rect).adjusted(safe_inset, safe_inset,
                                                     -safe_inset, -safe_inset);
    painter.setClipRect(safe_bounds);
    QVector<QRectF> occupied_labels;

    auto measureLabelWidth = [&](const QString &elided) {
        const qreal text_w = qMax(label_fm.horizontalAdvance(elided),
                                  label_fm.boundingRect(elided).width());
        return qMin(safe_bounds.width(), qCeil(text_w) + safe_inset);
    };

    auto drawMetricLabel = [&](qreal y, const QString &text, QColor color,
                               bool isAbove) {
        const qreal h = qCeil(label_fm.height()) + step_pad;
        if (safe_bounds.width() <= safe_inset || safe_bounds.height() < h) {
            return;
        }
        const QString elided = label_fm.elidedText(
            text, Qt::ElideRight, qMax(1.0, safe_bounds.width() - safe_inset));
        const qreal w = measureLabelWidth(elided);
        const qreal x = qMax(safe_bounds.left(), safe_bounds.right() - w);
        const QRectF preferred(x, isAbove ? y - h - 1.0 : y + 1.0, w, h);
        const QRectF r = placeNonOverlappingLabel(
            preferred, safe_bounds, isAbove ? -(h + step_pad) : (h + step_pad),
            occupied_labels, true);

        occupied_labels.append(r);
        m_last_label_rects.append(r);
        m_last_label_texts.append(text);
        painter.setPen(color);
        painter.drawText(r, Qt::AlignRight | Qt::AlignVCenter, elided);
    };

    // Priority order places core baseline/extents first, then secondary metrics.
    drawMetricLabel(text_y, "baseline", muted_guide, true);
    drawMetricLabel(ascent_y, "ascent", ascent_clr, true);
    drawMetricLabel(descent_y, "descent", descent_clr, false);
    if (display_fm.capHeight() > 0 && qAbs(cap_y - ascent_y) > 0.1) {
        drawMetricLabel(cap_y, "cap height", cap_clr, true);
    }
    if (display_fm.xHeight() > 0) {
        drawMetricLabel(x_y, "x-height", xh_clr, true);
    }
    if (draw_strikeout) {
        drawMetricLabel(strike_top_y,
                        QString("strikeout (%1/%2)")
                            .arg(m_deco_metrics.strikeout_pos)
                            .arg(m_deco_metrics.strikeout_size),
                        st_clr, true);
    }
    if (draw_underline) {
        drawMetricLabel(underline_top_y,
                        QString("underline (%1/%2)")
                            .arg(m_deco_metrics.underline_pos)
                            .arg(m_deco_metrics.underline_thick),
                        ul_clr, false);
    }

    // Vertical metrics (Origin, Advance, LSB, RSB)
    auto drawVerticalLabel = [&](qreal x, const QString &text, QColor color,
                                 bool isBottom, bool isLeftOfLine) {
        const qreal h = qCeil(label_fm.height()) + step_pad;
        if (safe_bounds.width() <= safe_inset || safe_bounds.height() < h) {
            return;
        }
        const QString elided = label_fm.elidedText(
            text, Qt::ElideRight, qMax(1.0, safe_bounds.width() - safe_inset));
        const qreal w = measureLabelWidth(elided);
        qreal label_x = isLeftOfLine ? x - w - gap_x : x + gap_x;

        // Boundary check: if outside is clipped by widget, move to inside
        if (label_x < safe_bounds.left()) {
            label_x = x + gap_x;
        }
        if (label_x + w > safe_bounds.right()) {
            label_x = x - w - gap_x;
        }
        label_x = qBound(safe_bounds.left(), label_x, safe_bounds.right() - w);

        const qreal label_y
            = isBottom ? (safe_bounds.bottom() - h) : safe_bounds.top();
        const QRectF r = placeNonOverlappingLabel(
            QRectF(label_x, label_y, w, h), safe_bounds,
            isBottom ? -(h + step_pad) : (h + step_pad), occupied_labels);

        occupied_labels.append(r);
        m_last_label_rects.append(r);
        m_last_label_texts.append(text);
        painter.setPen(color);
        painter.drawText(
            r,
            (r.center().x() < x ? Qt::AlignRight : Qt::AlignLeft)
                | Qt::AlignVCenter,
            elided);
    };

    drawVerticalLabel(text_x, "origin", adv_clr, true, true);
    drawVerticalLabel(text_x + display_advance, "advance", adv_clr, true, false);

    if (qAbs(glyph_box.left() - text_x) > 0.1) {
        drawVerticalLabel(glyph_box.left(), "LSB", sb_clr, false, true);
    }
    if (qAbs(glyph_box.right() - (text_x + display_advance)) > 0.1) {
        drawVerticalLabel(glyph_box.right(), "RSB", sb_clr, false, false);
    }
    painter.restore();
}

// GlyphInspectorWidget implementation
GlyphInspectorWidget::GlyphInspectorWidget(QWidget *parent) : QWidget(parent)
{
    createUI();
}

void GlyphInspectorWidget::setDpr(qreal dpr)
{
    const qreal clamped = fontui::clampDpr(dpr);
    if (qFuzzyCompare(m_dpr, clamped)) {
        return;
    }
    m_dpr = clamped;
    if (m_main_layout) {
        const int pad = fontui::dp(10, m_dpr);
        m_main_layout->setContentsMargins(pad, pad, pad, pad);
        m_main_layout->setSpacing(pad);
    }
    if (m_preview_widget) {
        m_preview_widget->setDpr(m_dpr);
    }
}

void GlyphInspectorWidget::createUI()
{
    m_main_layout = new QVBoxLayout(this);
    const int pad = fontui::dp(10, m_dpr);
    m_main_layout->setContentsMargins(pad, pad, pad, pad);
    m_main_layout->setSpacing(pad);

    // No selection label
    m_label_no_selection
        = new QLabel("Select a character from the Characters tab", this);
    m_label_no_selection->setAlignment(Qt::AlignCenter);
    m_label_no_selection->setWordWrap(true);
    QFont font = m_label_no_selection->font();
    font.setPointSize(14);
    font.setItalic(true);
    m_label_no_selection->setFont(font);
    applyEmptyLabelPalette();
    m_main_layout->addWidget(m_label_no_selection, 1);

    // Unicode Information group
    m_group_unicode = new QGroupBox("Unicode Information", this);
    auto unicode_layout = new QFormLayout(m_group_unicode);
    unicode_layout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_label_unicode_value   = new QLabel(this);
    m_label_unicode_decimal = new QLabel(this);
    m_label_unicode_block   = new QLabel(this);
    configureValueLabel(m_label_unicode_value);
    configureValueLabel(m_label_unicode_decimal);
    configureValueLabel(m_label_unicode_block);
    unicode_layout->addRow("Code Point:", m_label_unicode_value);
    unicode_layout->addRow("Decimal:", m_label_unicode_decimal);
    unicode_layout->addRow("Block:", m_label_unicode_block);
    m_main_layout->addWidget(m_group_unicode);

    // Glyph Metrics group
    m_group_glyph = new QGroupBox("Glyph Metrics", this);
    auto glyph_layout = new QFormLayout(m_group_glyph);
    glyph_layout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_label_advance_width = new QLabel(this);
    m_label_bbox          = new QLabel(this);
    m_label_bearings      = new QLabel(this);
    m_label_contours      = new QLabel(this);
    m_label_points        = new QLabel(this);
    configureValueLabel(m_label_advance_width);
    configureValueLabel(m_label_bbox);
    configureValueLabel(m_label_bearings);
    configureValueLabel(m_label_contours);
    configureValueLabel(m_label_points);
    glyph_layout->addRow("Advance Width:", m_label_advance_width);
    glyph_layout->addRow("Bounding Box:", m_label_bbox);
    glyph_layout->addRow("Bearings:", m_label_bearings);
    glyph_layout->addRow("Contours:", m_label_contours);
    glyph_layout->addRow("Points:", m_label_points);
    m_main_layout->addWidget(m_group_glyph);

    // Font Metrics group
    m_group_font = new QGroupBox("Font Metrics", this);
    auto font_layout = new QFormLayout(m_group_font);
    font_layout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    m_label_ascent       = new QLabel(this);
    m_label_descent      = new QLabel(this);
    m_label_line_gap     = new QLabel(this);
    m_label_units_per_em = new QLabel(this);
    m_label_underline    = new QLabel(this);
    m_label_strikeout    = new QLabel(this);
    configureValueLabel(m_label_ascent);
    configureValueLabel(m_label_descent);
    configureValueLabel(m_label_line_gap);
    configureValueLabel(m_label_units_per_em);
    configureValueLabel(m_label_underline);
    configureValueLabel(m_label_strikeout);
    font_layout->addRow("Ascent:", m_label_ascent);
    font_layout->addRow("Descent:", m_label_descent);
    font_layout->addRow("Line Gap:", m_label_line_gap);
    font_layout->addRow("Units per Em:", m_label_units_per_em);
    font_layout->addRow("Underline:", m_label_underline);
    font_layout->addRow("Strikeout:", m_label_strikeout);
    m_main_layout->addWidget(m_group_font);

    // Visual Preview group
    m_group_preview     = new QGroupBox("Visual Preview", this);
    auto preview_layout = new QVBoxLayout(m_group_preview);
    auto preview_bar    = new QHBoxLayout();
    preview_bar->setContentsMargins(0, 0, 0, 0);
    m_chk_outline = new QCheckBox("Show Outline Points", this);
    m_chk_outline->setFocusPolicy(Qt::NoFocus);
    m_chk_outline->setChecked(m_show_outline_points);
    preview_bar->addStretch(1);
    preview_bar->addWidget(m_chk_outline);
    preview_layout->addLayout(preview_bar);
    m_preview_widget = new GlyphPreviewWidget(this);
    m_preview_widget->setDpr(m_dpr);
    m_preview_widget->setShowOutlinePoints(m_show_outline_points);
    preview_layout->addWidget(m_preview_widget, 1);
    m_main_layout->addWidget(m_group_preview, 1);

    connect(m_chk_outline, &QCheckBox::toggled, this, [this](bool on) {
        if (m_show_outline_points == on) {
            return;
        }
        m_show_outline_points = on;
        if (m_preview_widget) {
            m_preview_widget->setShowOutlinePoints(on);
        }
    });

    // Initially hide all groups
    m_group_unicode->hide();
    m_group_glyph->hide();
    m_group_font->hide();
    m_group_preview->hide();
}

void GlyphInspectorWidget::setShowOutlinePoints(bool on)
{
    m_show_outline_points = on;
    if (m_chk_outline && m_chk_outline->isChecked() != on) {
        QSignalBlocker blocker(m_chk_outline);
        m_chk_outline->setChecked(on);
    }
    if (m_preview_widget) {
        m_preview_widget->setShowOutlinePoints(on);
    }
}

void GlyphInspectorWidget::setOutlineShortcutHint(const QString &hint)
{
    if (!m_chk_outline) {
        return;
    }
    const QString base = tr("Show outline control points");
    m_chk_outline->setToolTip(
        hint.isEmpty() ? base : QStringLiteral("%1 (%2)").arg(base, hint));
}

void GlyphInspectorWidget::updateGlyph(QChar ch,
                                       const QFont &font,
                                       const QString &font_path,
                                       const FontDecorationMetrics &deco,
                                       const GlyphOutlineData &outline)
{
    m_char          = ch;
    m_font          = font;
    m_font_path     = font_path;
    m_deco_metrics  = deco;
    m_outline       = outline;
    m_has_selection = !ch.isNull();

    if (m_has_selection) {
        m_label_no_selection->hide();
        m_group_unicode->show();
        m_group_glyph->show();
        m_group_font->show();
        m_group_preview->show();

        updateMetrics();
        updateLabels();
        m_preview_widget->setGlyphData(m_char, m_font, m_metrics.bounding_box,
                                       m_metrics.advance_width, m_deco_metrics,
                                       m_outline);
    }
    else {
        clearSelection();
    }
}

void GlyphInspectorWidget::clearSelection()
{
    m_has_selection = false;
    m_char          = QChar();
    m_outline       = {};
    m_label_no_selection->show();
    m_group_unicode->hide();
    m_group_glyph->hide();
    m_group_font->hide();
    m_group_preview->hide();
    m_preview_widget->clear();
}

QPixmap GlyphInspectorWidget::getPreviewPixmap() const
{
    if (!m_has_selection || !m_preview_widget) {
        return {};
    }
    return m_preview_widget->grab();
}

void GlyphInspectorWidget::updateFont(const QFont &font,
                                      const FontDecorationMetrics &deco,
                                      const GlyphOutlineData &outline)
{
    m_deco_metrics = deco;
    m_outline      = outline;
    if (m_has_selection) {
        m_font = font;
        updateMetrics();
        updateLabels();
        m_preview_widget->setGlyphData(m_char, m_font, m_metrics.bounding_box,
                                       m_metrics.advance_width, m_deco_metrics,
                                       m_outline);
    }
}

void GlyphInspectorWidget::applyEmptyLabelPalette()
{
    if (!m_label_no_selection) {
        return;
    }
    QPalette pal = palette();
    pal.setColor(QPalette::WindowText, fontui::mutedTextColor(palette()));
    m_label_no_selection->setPalette(pal);
}

void GlyphInspectorWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange
        || event->type() == QEvent::ApplicationPaletteChange) {
        applyEmptyLabelPalette();
        if (m_preview_widget) {
            m_preview_widget->update();
        }
    }
}

void GlyphInspectorWidget::updateTheme(int theme)
{
    m_theme = theme;
    applyEmptyLabelPalette();
    if (m_preview_widget) {
        m_preview_widget->update();
    }
}

void GlyphInspectorWidget::updateMetrics()
{
    // Try QRawFont first for accurate metrics
    QRawFont raw_font = QRawFont::fromFont(m_font);
    if (!raw_font.isValid()) {
        qprintt << "QRawFont invalid, falling back to QFontMetrics";
        QFontMetricsF fm(m_font);
        m_metrics.advance_width = fm.horizontalAdvance(m_char);
        m_metrics.bounding_box  = fm.boundingRect(m_char);
        m_metrics.ascent        = fm.ascent();
        m_metrics.descent       = fm.descent();
        m_metrics.line_gap      = fm.lineSpacing() - fm.height();
        m_metrics.units_per_em  = 0;
        m_metrics.left_bearing  = 0;
        m_metrics.right_bearing = 0;
        return;
    }

    QString str(m_char);
    QVector<quint32> glyph_indexes = raw_font.glyphIndexesForString(str);
    if (glyph_indexes.isEmpty()) {
        qprintt << "No glyph index for character" << m_char;
        m_metrics = {};
        return;
    }

    quint32 glyph_index       = glyph_indexes[0];
    QVector<QPointF> advances = raw_font.advancesForGlyphIndexes({glyph_index});

    m_metrics.advance_width = advances.isEmpty() ? 0 : advances[0].x();
    m_metrics.bounding_box  = raw_font.boundingRect(glyph_index);
    m_metrics.ascent        = raw_font.ascent();
    m_metrics.descent       = raw_font.descent();
    m_metrics.line_gap      = raw_font.leading();
    m_metrics.units_per_em  = raw_font.unitsPerEm();

    m_metrics.left_bearing = m_metrics.bounding_box.left();
    m_metrics.right_bearing
        = m_metrics.advance_width - m_metrics.bounding_box.right();
}

void GlyphInspectorWidget::updateLabels()
{
    // Unicode info
    ushort code_point = m_char.unicode();
    m_label_unicode_value->setText(
        QString("U+%1").arg(code_point, 4, 16, QChar('0')).toUpper());
    m_label_unicode_decimal->setText(QString::number(code_point));
    const QString block_name = getUnicodeBlockName(code_point);
    m_label_unicode_block->setText(block_name);
    m_label_unicode_block->setToolTip(block_name);

    // Glyph metrics
    m_label_advance_width->setText(
        QString("%1 px").arg(m_metrics.advance_width, 0, 'f', 2));
    m_label_bbox->setText(QString("(%1, %2, %3, %4)")
                              .arg(m_metrics.bounding_box.x(), 0, 'f', 1)
                              .arg(m_metrics.bounding_box.y(), 0, 'f', 1)
                              .arg(m_metrics.bounding_box.width(), 0, 'f', 1)
                              .arg(m_metrics.bounding_box.height(), 0, 'f', 1));
    m_label_bearings->setText(QString("Left %1, Right %2")
                                  .arg(m_metrics.left_bearing, 0, 'f', 2)
                                  .arg(m_metrics.right_bearing, 0, 'f', 2));
    m_label_contours->setText(
        m_outline.valid ? QString::number(m_outline.contour_count)
                        : QStringLiteral("N/A"));
    m_label_points->setText(
        m_outline.valid ? QString::number(m_outline.point_count)
                        : QStringLiteral("N/A"));

    // Font metrics
    m_label_ascent->setText(QString("%1").arg(m_metrics.ascent, 0, 'f', 2));
    m_label_descent->setText(QString("%1").arg(m_metrics.descent, 0, 'f', 2));
    m_label_line_gap->setText(QString("%1").arg(m_metrics.line_gap, 0, 'f', 2));
    const int upem = m_deco_metrics.units_per_em > 0
                         ? m_deco_metrics.units_per_em
                         : m_metrics.units_per_em;
    m_label_units_per_em->setText(upem > 0 ? QString::number(upem) : "N/A");
    m_label_underline->setText(
        m_deco_metrics.has_underline
            ? QString("pos %1 / thick %2 (FUnits)")
                  .arg(m_deco_metrics.underline_pos)
                  .arg(m_deco_metrics.underline_thick)
            : QStringLiteral("N/A"));
    m_label_strikeout->setText(
        m_deco_metrics.has_strikeout
            ? QString("pos %1 / size %2 (FUnits)")
                  .arg(m_deco_metrics.strikeout_pos)
                  .arg(m_deco_metrics.strikeout_size)
            : QStringLiteral("N/A"));
}

QString GlyphInspectorWidget::getUnicodeBlockName(ushort code_point)
{
    // Basic Unicode block detection
    if (code_point <= 0x007F)
        return "Basic Latin";
    if (code_point <= 0x00FF)
        return "Latin-1 Supplement";
    if (code_point <= 0x017F)
        return "Latin Extended-A";
    if (code_point <= 0x024F)
        return "Latin Extended-B";
    if (code_point <= 0x02AF)
        return "IPA Extensions";
    if (code_point <= 0x036F)
        return "Combining Diacritical Marks";
    if (code_point <= 0x03FF)
        return "Greek and Coptic";
    if (code_point <= 0x04FF)
        return "Cyrillic";
    if (code_point <= 0x052F)
        return "Cyrillic Supplement";
    if (code_point <= 0x058F)
        return "Armenian";
    if (code_point <= 0x05FF)
        return "Hebrew";
    if (code_point <= 0x06FF)
        return "Arabic";
    if (code_point <= 0x09FF)
        return "Devanagari / Bengali / Gurmukhi / Gujarati / Oriya / Tamil / "
               "Telugu / Kannada / Malayalam";
    if (code_point <= 0x0FFF)
        return "Thai / Lao / Tibetan / Myanmar";
    if (code_point <= 0x109F)
        return "Georgian / Hangul Jamo";
    if (code_point <= 0x11FF)
        return "Ethiopic / Cherokee";
    if (code_point <= 0x1FFF)
        return "Unified Canadian Aboriginal Syllabics / Ogham / Runic / "
               "Tagalog / Hanunoo / Buhid / Tagbanwa / Khmer / Mongolian";
    if (code_point <= 0x20CF)
        return "General Punctuation / Superscripts and Subscripts / Currency "
               "Symbols";
    if (code_point <= 0x214F)
        return "Letterlike Symbols / Number Forms";
    if (code_point <= 0x218F)
        return "Arrows";
    if (code_point <= 0x24FF)
        return "Mathematical Operators / Miscellaneous Technical / Control "
               "Pictures / OCR / Enclosed Alphanumerics / Box Drawing / Block "
               "Elements / Geometric Shapes / Miscellaneous Symbols";
    if (code_point <= 0x27BF)
        return "Dingbats / Miscellaneous Mathematical Symbols-A / Supplemental "
               "Arrows-A";
    if (code_point <= 0x2FFF)
        return "Braille Patterns / Supplemental Arrows-B / Miscellaneous "
               "Mathematical Symbols-B / Supplemental Mathematical Operators / "
               "Miscellaneous Symbols and Arrows";
    if (code_point <= 0x4DBF)
        return "CJK Radicals Supplement / Kangxi Radicals / Ideographic "
               "Description Characters / CJK Symbols and Punctuation / "
               "Hiragana / Katakana / Bopomofo / Hangul Compatibility Jamo / "
               "Kanbun / Bopomofo Extended / CJK Strokes / Katakana Phonetic "
               "Extensions / Enclosed CJK Letters and Months / CJK "
               "Compatibility / CJK Unified Ideographs Extension A";
    if (code_point <= 0x9FFF)
        return "CJK Unified Ideographs";
    if (code_point <= 0xD7FF)
        return "Hangul Syllables";
    if (code_point <= 0xF8FF)
        return "Private Use Area";
    if (code_point <= 0xFAFF)
        return "CJK Compatibility Ideographs";
    if (code_point <= 0xFB4F)
        return "Alphabetic Presentation Forms";
    if (code_point <= 0xFDFF)
        return "Arabic Presentation Forms-A";
    if (code_point <= 0xFE6F)
        return "Combining Half Marks / CJK Compatibility Forms / Small Form "
               "Variants";
    if (code_point <= 0xFEFF)
        return "Arabic Presentation Forms-B / Halfwidth and Fullwidth Forms / "
               "Specials";
    return "Unknown Block";
}
