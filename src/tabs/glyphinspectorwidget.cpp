#include "glyphinspectorwidget.h"

#include <QEasingCurve>
#include <QEvent>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QPushButton>
#include <QRawFont>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>

#include <algorithm>
#include <array>
#include <limits>
#include <optional>

#include "engine/unicodeblocks.h"
#include "fontui.h"

#define qprintt qDebug() << "[GlyphInspector]"

namespace {
struct GlyphGuideSpec {
    GlyphGuideId id;
    const char *key;
    const char *label;
    const char *tooltip;
    Qt::PenStyle pen_style;
    bool has_band;
};

constexpr std::array<GlyphGuideSpec, 11> kGuideSpecs = {{
    {GlyphGuideId::Baseline, "baseline", "Baseline",
     "Baseline (y = 0) — Horizontal reference line glyphs sit on",
     Qt::DashLine, false},
    {GlyphGuideId::Ascent, "ascent", "Ascent",
     "Ascent — Maximum height above baseline for font glyphs", Qt::DotLine,
     false},
    {GlyphGuideId::Descent, "descent", "Descent",
     "Descent — Maximum depth below baseline for descenders", Qt::DotLine,
     false},
    {GlyphGuideId::CapHeight, "cap_height", "Cap",
     "Cap Height — Height of flat uppercase letters above baseline",
     Qt::DotLine, false},
    {GlyphGuideId::XHeight, "x_height", "x-Height",
     "x-Height — Height of lowercase letters (such as 'x')", Qt::DotLine,
     false},
    {GlyphGuideId::Underline, "underline", "Underline",
     "Underline — Text underline position and thickness", Qt::DashLine, true},
    {GlyphGuideId::Strikeout, "strikeout", "Strike",
     "Strikeout — Strikethrough position and thickness", Qt::DashLine, true},
    {GlyphGuideId::Advance, "advance", "Advance",
     "Advance Width — Origin and horizontal advance to the next glyph",
     Qt::DashLine, true},
    {GlyphGuideId::Bearings, "bearings", "Bearings",
     "Side Bearings (LSB / RSB) — Space between glyph edges and advance box",
     Qt::DotLine, false},
    {GlyphGuideId::BBox, "bbox", "BBox",
     "Bounding Box — Tight rectangle enclosing the drawn glyph outline",
     Qt::DashDotLine, false},
    {GlyphGuideId::Center, "center", "Center",
     "Center Axis — Vertical midpoint of the preview canvas", Qt::DotLine,
     false},
}};

const GlyphGuideSpec &guideSpec(GlyphGuideId id)
{
    const int idx = static_cast<int>(id);
    if (idx >= 0 && idx < int(kGuideSpecs.size())) {
        return kGuideSpecs[size_t(idx)];
    }
    return kGuideSpecs[0];
}

QFont makeDisplayFontForMetrics(const QFont &base_font)
{
    QFont display_font = base_font;
    if (base_font.pointSizeF() > 0.0) {
        display_font.setPointSizeF(qMax(72.0, base_font.pointSizeF() * 4.0));
    }
    else if (base_font.pixelSize() > 0) {
        display_font.setPixelSize(qMax(96, base_font.pixelSize() * 4));
    }
    else {
        display_font.setPointSizeF(72.0);
    }
    return display_font;
}

bool isGuideAvailableWithMetrics(GlyphGuideId id,
                                 const QFontMetricsF &display_fm,
                                 qreal display_ascent,
                                 const FontDecorationMetrics &deco)
{
    switch (id) {
    case GlyphGuideId::CapHeight:
        return display_fm.capHeight() > 0.0
               && qAbs(display_ascent - display_fm.capHeight()) > 0.1;
    case GlyphGuideId::XHeight:
        return display_fm.xHeight() > 0.0;
    case GlyphGuideId::Underline:
        return deco.units_per_em > 0 && deco.has_underline;
    case GlyphGuideId::Strikeout:
        return deco.units_per_em > 0 && deco.has_strikeout;
    default:
        return true;
    }
}

class GuidePillButton : public QPushButton {
public:
    GuidePillButton(const QString &label,
                    qreal dpr,
                    bool is_outline,
                    GlyphGuideId guide_id,
                    QWidget *parent = nullptr)
        : QPushButton(label, parent),
          m_dpr(fontui::clampDpr(dpr)),
          m_is_outline(is_outline),
          m_guide_id(guide_id)
    {
        setCheckable(true);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::ArrowCursor);
        setAttribute(Qt::WA_Hover, true);
        setShortcut(QKeySequence());
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        syncFont();
    }

    void setDpr(qreal dpr)
    {
        const qreal clamped = fontui::clampDpr(dpr);
        if (qFuzzyCompare(m_dpr, clamped)) {
            return;
        }
        m_dpr = clamped;
        syncFont();
    }

    void syncFont()
    {
        if (m_syncing_font) {
            return;
        }
        const QFont f = fontui::scaledCaptionFont(this, m_dpr, -1.0, 8.0, 0.88);
        if (font() != f) {
            m_syncing_font = true;
            setFont(f);
            m_syncing_font = false;
        }
        updateGeometry();
        update();
    }

    QSize sizeHint() const override
    {
        const QFontMetrics fm = fontMetrics();
        const int pad_h       = fontui::dp(8, m_dpr);
        const int pad_v       = fontui::dp(3, m_dpr);
        const int border_w    = qCeil(fontui::dpf(1.0, m_dpr));
        const int swatch_w    = fontui::dp(14, m_dpr);
        const int gap         = fontui::dp(5, m_dpr);
        return QSize(pad_h * 2 + border_w + swatch_w + gap
                         + fm.horizontalAdvance(text()),
                     fm.height() + pad_v * 2);
    }

    QSize minimumSizeHint() const override
    {
        return sizeHint();
    }

protected:
    void changeEvent(QEvent *event) override
    {
        QPushButton::changeEvent(event);
        if (!m_syncing_font
            && (event->type() == QEvent::FontChange
                || event->type() == QEvent::StyleChange)) {
            syncFont();
        }
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QPalette &pal = palette();
        const qreal pen_w   = fontui::dpf(1.0, m_dpr);
        const QRectF r = QRectF(rect()).adjusted(pen_w * 0.5, pen_w * 0.5,
                                                 -pen_w * 0.5, -pen_w * 0.5);
        const qreal radius = r.height() / 2.0;
        const bool enabled = isEnabled();
        const bool checked = isChecked();
        const bool dark    = fontui::isDarkPalette(pal);
        const bool hovered = enabled && underMouse();

        if (!enabled) {
            painter.setOpacity(0.45);
        }

        const QColor fg_base = pal.color(QPalette::WindowText);
        const QColor guide_clr
            = m_is_outline ? QColor(230, 125, 35)
                           : glyphGuideColor(m_guide_id, pal, true);

        QColor bg     = fg_base;
        QColor border = fg_base;
        QColor fg     = fg_base;

        if (checked) {
            bg.setAlpha(hovered ? (dark ? 28 : 20) : (dark ? 16 : 12));
            border = guide_clr;
            border.setAlpha(hovered ? 235 : 168);
        }
        else {
            bg.setAlpha(hovered ? (dark ? 18 : 14) : (dark ? 10 : 6));
            border.setAlpha(hovered ? (dark ? 68 : 54) : (dark ? 32 : 24));
            fg.setAlpha(hovered ? 230 : 195);
        }

        painter.setPen(QPen(border, pen_w));
        painter.setBrush(bg);
        painter.drawRoundedRect(r, radius, radius);

        const int pad_h      = fontui::dp(8, m_dpr);
        const qreal swatch_w = fontui::dpf(14.0, m_dpr);
        const qreal gap      = fontui::dpf(5.0, m_dpr);
        const qreal sx       = r.left() + pad_h;
        const qreal cy       = r.center().y();

        QColor swatch_clr = guide_clr;
        if (!checked) {
            swatch_clr = fg_base;
            swatch_clr.setAlphaF(0.35);
        }

        if (m_is_outline) {
            const qreal p_r = fontui::dpf(2.0, m_dpr);
            const QPointF left_pt(sx + p_r, cy + fontui::dpf(2.0, m_dpr));
            const QPointF right_pt(sx + swatch_w - p_r,
                                   cy - fontui::dpf(2.0, m_dpr));
            painter.setPen(
                QPen(swatch_clr, fontui::dpf(1.2, m_dpr), Qt::SolidLine));
            painter.drawLine(left_pt, right_pt);

            painter.setBrush(pal.base());
            painter.setPen(QPen(swatch_clr, fontui::dpf(1.2, m_dpr)));
            painter.drawEllipse(left_pt, p_r, p_r);

            QColor sq_fill = checked ? QColor(35, 145, 255) : swatch_clr;
            painter.setBrush(sq_fill);
            painter.setPen(Qt::NoPen);
            const qreal sq = fontui::dpf(4.0, m_dpr);
            painter.drawRect(QRectF(right_pt.x() - sq * 0.5,
                                    right_pt.y() - sq * 0.5, sq, sq));
        }
        else {
            const GlyphGuideSpec &spec = guideSpec(m_guide_id);
            if (spec.has_band) {
                QColor band_clr = swatch_clr;
                band_clr.setAlpha(checked ? 55 : 22);
                const qreal band_h = fontui::dpf(3.0, m_dpr);
                painter.fillRect(QRectF(sx, cy, swatch_w, band_h), band_clr);
            }
            QPen line_pen(swatch_clr, fontui::dpf(1.5, m_dpr), spec.pen_style);
            painter.setPen(line_pen);
            painter.drawLine(QPointF(sx, cy), QPointF(sx + swatch_w, cy));
        }

        const QRectF text_rect(sx + swatch_w + gap, r.top(),
                               qMax(0.0, r.right() - pad_h - (sx + swatch_w + gap)),
                               r.height());
        painter.setPen(fg);
        painter.setFont(font());
        painter.drawText(text_rect, Qt::AlignLeft | Qt::AlignVCenter, text());
    }

private:
    qreal m_dpr             = 1.0;
    bool m_is_outline       = false;
    bool m_syncing_font     = false;
    GlyphGuideId m_guide_id = GlyphGuideId::Baseline;
};

class ToggleAllGuidesButton : public fontui::CompactActionButton {
public:
    ToggleAllGuidesButton(qreal dpr, QWidget *parent = nullptr)
        : CompactActionButton(tr("Hide All"), dpr, parent, false)
    {
        setCursor(Qt::ArrowCursor);
    }

    QSize sizeHint() const override
    {
        const QFontMetrics fm = fontMetrics();
        const int w = qMax(fm.horizontalAdvance(tr("Hide All")),
                           fm.horizontalAdvance(tr("Show All")))
                      + fontui::dp(12, dpr());
        return QSize(w, fm.height() + fontui::dp(4, dpr()));
    }

    QSize minimumSizeHint() const override
    {
        return sizeHint();
    }
};

class GuidePillsHost : public QWidget {
public:
    explicit GuidePillsHost(qreal dpr, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFocusPolicy(Qt::NoFocus);
        QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Preferred);
        sp.setHeightForWidth(true);
        setSizePolicy(sp);
        new fontui::FlowLayout(this, fontui::dp(5, dpr), fontui::dp(4, dpr));
    }

    bool hasHeightForWidth() const override
    {
        return layout() != nullptr && layout()->hasHeightForWidth();
    }

    int heightForWidth(int width) const override
    {
        return layout() ? layout()->heightForWidth(qMax(1, width)) : 0;
    }

    QSize sizeHint() const override
    {
        const int w = width() > 0 ? width() : 280;
        return QSize(w, heightForWidth(w));
    }

    QSize minimumSizeHint() const override
    {
        return layout() ? layout()->minimumSize() : QSize(0, 0);
    }
};

class GuideControlsBar : public QWidget {
public:
    GuideControlsBar(GuidePillsHost *pills_host,
                     QPushButton *toggle_btn,
                     qreal dpr,
                     QWidget *parent = nullptr)
        : QWidget(parent),
          m_pills_host(pills_host),
          m_toggle_btn(toggle_btn),
          m_dpr(fontui::clampDpr(dpr))
    {
        setFocusPolicy(Qt::NoFocus);
        QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Preferred);
        sp.setHeightForWidth(true);
        setSizePolicy(sp);
        if (m_pills_host) {
            m_pills_host->setParent(this);
        }
        if (m_toggle_btn) {
            m_toggle_btn->setParent(this);
        }
    }

    GuidePillsHost *pillsHost() const
    {
        return m_pills_host;
    }

    void setDpr(qreal dpr)
    {
        const qreal clamped = fontui::clampDpr(dpr);
        if (qFuzzyCompare(m_dpr, clamped)) {
            return;
        }
        m_dpr = clamped;
        updateGeometry();
    }

    bool hasHeightForWidth() const override
    {
        return true;
    }

    int heightForWidth(int width) const override
    {
        const int gap   = fontui::dp(6, m_dpr);
        const int btn_w = m_toggle_btn ? m_toggle_btn->sizeHint().width() : 0;
        const int btn_h = m_toggle_btn ? m_toggle_btn->sizeHint().height() : 0;
        const int pills_w = qMax(1, width - btn_w - gap);
        const int pills_h
            = m_pills_host ? m_pills_host->heightForWidth(pills_w) : 0;
        return qMax(pills_h, btn_h);
    }

    QSize sizeHint() const override
    {
        const int w = width() > 0 ? width() : 360;
        return QSize(w, heightForWidth(w));
    }

    QSize minimumSizeHint() const override
    {
        const int gap = fontui::dp(6, m_dpr);
        const QSize btn_sz
            = m_toggle_btn ? m_toggle_btn->sizeHint() : QSize(0, 0);
        const QSize pills_min
            = m_pills_host ? m_pills_host->minimumSizeHint() : QSize(0, 0);
        return QSize(pills_min.width() + gap + btn_sz.width(),
                     qMax(pills_min.height(), btn_sz.height()));
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        relayout();
    }

    void showEvent(QShowEvent *event) override
    {
        QWidget::showEvent(event);
        relayout();
    }

public:
    void relayout()
    {
        const int gap   = fontui::dp(6, m_dpr);
        const QSize bsz = m_toggle_btn ? m_toggle_btn->sizeHint() : QSize();
        const int w     = qMax(1, width());
        const int pills_w = qMax(1, w - bsz.width() - gap);
        const int pills_h
            = m_pills_host ? m_pills_host->heightForWidth(pills_w) : 0;
        if (m_pills_host) {
            m_pills_host->setGeometry(0, 0, pills_w, pills_h);
            if (QLayout *l = m_pills_host->layout()) {
                l->activate();
            }
        }
        if (m_toggle_btn) {
            m_toggle_btn->setGeometry(
                qMax(0, w - bsz.width()), 0, bsz.width(), bsz.height());
        }
        const int target_h = qMax(pills_h, bsz.height());
        if (target_h != height()) {
            updateGeometry();
        }
    }

private:
    GuidePillsHost *m_pills_host = nullptr;
    QPushButton *m_toggle_btn    = nullptr;
    qreal m_dpr                  = 1.0;
};
}  // namespace

QString glyphGuideKey(GlyphGuideId id)
{
    return QString::fromLatin1(guideSpec(id).key);
}

std::optional<GlyphGuideId> glyphGuideFromKey(const QString &key)
{
    const QString norm = key.trimmed().toLower();
    for (const GlyphGuideSpec &spec : kGuideSpecs) {
        if (norm == QLatin1String(spec.key)) {
            return spec.id;
        }
    }
    return std::nullopt;
}

QColor glyphGuideColor(GlyphGuideId id, const QPalette &pal, bool opaque)
{
    const bool dark = fontui::isDarkPalette(pal);
    switch (id) {
    case GlyphGuideId::Baseline:
    case GlyphGuideId::Center:
        return fontui::mutedTextColor(pal);
    case GlyphGuideId::Ascent:
        return dark ? QColor(100, 150, 255) : QColor(29, 111, 216);
    case GlyphGuideId::Descent:
        return dark ? QColor(255, 150, 100) : QColor(201, 81, 27);
    case GlyphGuideId::CapHeight:
        return dark ? QColor(255, 100, 200) : QColor(179, 50, 138);
    case GlyphGuideId::XHeight:
        return dark ? QColor(100, 200, 150) : QColor(25, 135, 107);
    case GlyphGuideId::Underline:
        return dark ? QColor(70, 160, 255) : QColor(2, 136, 209);
    case GlyphGuideId::Strikeout:
        return dark ? QColor(255, 120, 80) : QColor(211, 47, 47);
    case GlyphGuideId::Advance:
        return dark ? QColor(200, 100, 255) : QColor(124, 58, 237);
    case GlyphGuideId::Bearings:
        return dark ? QColor(150, 150, 150) : QColor(95, 99, 104);
    case GlyphGuideId::BBox:
        if (opaque) {
            return dark ? QColor(100, 255, 100) : QColor(27, 127, 55);
        }
        return dark ? QColor(100, 255, 100, 110) : QColor(27, 127, 55, 140);
    }
    return fontui::mutedTextColor(pal);
}

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

class ElidingValueLabel : public QLabel {
public:
    explicit ElidingValueLabel(QWidget *parent = nullptr) : QLabel(parent)
    {
        setWordWrap(false);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }

    void setFullText(const QString &full_text)
    {
        m_full_text = full_text;
        updateElide();
        updateGeometry();
    }

    QSize minimumSizeHint() const override
    {
        const QFontMetrics fm = fontMetrics();
        const int cap_w       = qMax(1, fm.averageCharWidth() * 6);
        const int w           = m_full_text.isEmpty()
                                    ? 0
                                    : qMin(fm.horizontalAdvance(m_full_text) + 2, cap_w);
        return QSize(w, fm.height());
    }

    QSize sizeHint() const override
    {
        const QFontMetrics fm = fontMetrics();
        const int cap_w       = qMax(1, fm.averageCharWidth() * 24);
        const int w           = m_full_text.isEmpty()
                                    ? 0
                                    : qMin(fm.horizontalAdvance(m_full_text) + 2, cap_w);
        return QSize(w, fm.height());
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        updateElide();
    }

    void changeEvent(QEvent *event) override
    {
        QLabel::changeEvent(event);
        if (event->type() == QEvent::FontChange
            || event->type() == QEvent::StyleChange) {
            updateElide();
            updateGeometry();
        }
    }

private:
    void updateElide()
    {
        if (m_full_text.isEmpty()) {
            clear();
            setToolTip(QString());
            return;
        }
        const int w = width();
        const QString shown
            = (w > 0) ? fontMetrics().elidedText(m_full_text, Qt::ElideRight, w)
                      : m_full_text;
        setText(shown);
        setToolTip(shown != m_full_text ? m_full_text : QString());
    }

    QString m_full_text;
};
}  // namespace

// GlyphPreviewWidget implementation
GlyphPreviewWidget::GlyphPreviewWidget(QWidget *parent) : QWidget(parent)
{
    m_guide_alphas.fill(1.0);
    m_guide_from.fill(1.0);
    m_guide_targets.fill(1.0);
    m_anim_timer = new QTimer(this);
    m_anim_timer->setInterval(16);
    connect(m_anim_timer, &QTimer::timeout, this,
            &GlyphPreviewWidget::advanceAnimations);
    setMinimumSize(fontui::dp(260, m_dpr), fontui::dp(380, m_dpr));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void GlyphPreviewWidget::setDpr(qreal dpr)
{
    const qreal clamped = fontui::clampDpr(dpr);
    if (qFuzzyCompare(m_dpr, clamped)) {
        return;
    }
    m_dpr = clamped;
    setMinimumSize(fontui::dp(260, m_dpr), fontui::dp(380, m_dpr));
    updateGeometry();
    update();
}

void GlyphPreviewWidget::setGlyphData(quint32 code_point,
                                      const QFont &font,
                                      const QRectF &bbox,
                                      qreal advance_width,
                                      const FontDecorationMetrics &deco,
                                      const GlyphOutlineData &outline)
{
    m_code_point    = code_point;
    m_font          = font;
    m_bbox          = bbox;
    m_deco_metrics  = deco;
    m_outline       = outline;
    m_advance_width = advance_width;
    m_has_data      = (code_point != 0);
    update();
}

void GlyphPreviewWidget::setShowOutlinePoints(bool on, bool animate)
{
    if (m_show_outline_points == on) {
        return;
    }
    m_show_outline_points = on;
    syncAnimationState(animate);
}

void GlyphPreviewWidget::setGuideVisible(GlyphGuideId id,
                                         bool visible,
                                         bool animate)
{
    if (isGuideVisible(id) == visible) {
        return;
    }
    if (visible) {
        m_hidden_guides.remove(id);
    }
    else {
        m_hidden_guides.insert(id);
    }
    syncAnimationState(animate);
}

void GlyphPreviewWidget::setHiddenGuides(const QSet<GlyphGuideId> &hidden,
                                         bool animate)
{
    if (m_hidden_guides == hidden) {
        return;
    }
    m_hidden_guides = hidden;
    syncAnimationState(animate);
}

qreal GlyphPreviewWidget::guideOpacity(GlyphGuideId id) const
{
    const int idx = static_cast<int>(id);
    return (idx >= 0 && idx < kGuideCount) ? m_guide_alphas[size_t(idx)] : 1.0;
}

bool GlyphPreviewWidget::isGuideAnimating() const
{
    return m_anim_timer && m_anim_timer->isActive();
}

void GlyphPreviewWidget::syncAnimationState(bool animate)
{
    std::array<qreal, kGuideCount> next_targets{};
    for (int i = 0; i < kGuideCount; ++i) {
        const auto id    = static_cast<GlyphGuideId>(i);
        next_targets[size_t(i)] = isGuideVisible(id) ? 1.0 : 0.0;
    }
    const qreal next_outline = m_show_outline_points ? 1.0 : 0.0;

    if (!animate || !isVisible() || !m_has_data) {
        if (m_anim_timer && m_anim_timer->isActive()) {
            m_anim_timer->stop();
        }
        m_guide_from     = next_targets;
        m_guide_targets  = next_targets;
        m_guide_alphas   = next_targets;
        m_outline_from   = next_outline;
        m_outline_target = next_outline;
        m_outline_alpha  = next_outline;
        update();
        return;
    }

    if (next_targets == m_guide_targets
        && qFuzzyCompare(next_outline + 1.0, m_outline_target + 1.0)) {
        return;
    }

    m_guide_from     = m_guide_alphas;
    m_guide_targets  = next_targets;
    m_outline_from   = m_outline_alpha;
    m_outline_target = next_outline;

    bool needs_anim = qAbs(m_outline_from - m_outline_target) > 0.001;
    for (int i = 0; !needs_anim && i < kGuideCount; ++i) {
        if (qAbs(m_guide_from[size_t(i)] - m_guide_targets[size_t(i)])
            > 0.001) {
            needs_anim = true;
        }
    }
    if (needs_anim) {
        m_anim_clock.restart();
        if (m_anim_timer && !m_anim_timer->isActive()) {
            m_anim_timer->start();
        }
    }
    else {
        if (m_anim_timer && m_anim_timer->isActive()) {
            m_anim_timer->stop();
        }
        m_guide_alphas  = m_guide_targets;
        m_outline_alpha = m_outline_target;
    }
    update();
}

void GlyphPreviewWidget::advanceAnimations()
{
    const qreal t
        = qBound(0.0, qreal(m_anim_clock.elapsed()) / kAnimDurationMs, 1.0);
    const qreal eased
        = QEasingCurve(QEasingCurve::OutCubic).valueForProgress(t);
    for (int i = 0; i < kGuideCount; ++i) {
        const size_t idx   = size_t(i);
        m_guide_alphas[idx]
            = m_guide_from[idx]
              + (m_guide_targets[idx] - m_guide_from[idx]) * eased;
    }
    m_outline_alpha
        = m_outline_from + (m_outline_target - m_outline_from) * eased;

    if (t >= 1.0) {
        m_guide_alphas  = m_guide_targets;
        m_outline_alpha = m_outline_target;
        if (m_anim_timer) {
            m_anim_timer->stop();
        }
    }
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
    syncAnimationState(false);
}

QSize GlyphPreviewWidget::sizeHint() const
{
    return QSize(fontui::dp(420, m_dpr), fontui::dp(440, m_dpr));
}

void GlyphPreviewWidget::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    if (m_anim_timer && m_anim_timer->isActive()) {
        m_anim_timer->stop();
        m_guide_alphas  = m_guide_targets;
        m_guide_from    = m_guide_targets;
        m_outline_alpha = m_outline_target;
        m_outline_from  = m_outline_target;
        update();
    }
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
    const QFont display_font = makeDisplayFontForMetrics(m_font);
    QFontMetricsF display_fm(display_font, this);

    // Get strict metrics from QRawFont at display size to ensure consistency
    QRawFont display_raw = QRawFont::fromFont(display_font);
    QRectF glyph_box;
    qreal display_advance = 0;
    qreal display_ascent  = display_fm.ascent();
    qreal display_descent = display_fm.descent();

    bool use_strict = false;
    const QString glyph_str
        = (m_code_point > 0 && m_code_point <= 0x10FFFFu)
              ? QString::fromUcs4(&m_code_point, 1)
              : QString();
    if (display_raw.isValid() && !glyph_str.isEmpty()) {
        QVector<quint32> indexes = display_raw.glyphIndexesForString(glyph_str);
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
        const qreal base_adv = base_fm.horizontalAdvance(glyph_str);
        const qreal disp_adv = display_fm.horizontalAdvance(glyph_str);
        qreal sx             = base_adv > 0 ? disp_adv / base_adv : 1.0;
        qreal sy = base_fm.height() > 0
                       ? qreal(display_fm.height()) / base_fm.height()
                       : 1.0;

        display_advance = m_advance_width * sx;
        glyph_box       = QRectF(m_bbox.x() * sx, m_bbox.y() * sy,
                                 m_bbox.width() * sx, m_bbox.height() * sy);
        text_x          = center_x - disp_adv / 2.0;
        text_y = center_y + (display_fm.ascent() - display_fm.descent()) / 2.0;
    }

    // Translate glyph_box to actual drawing position
    glyph_box.translate(text_x, text_y);

    const bool dark          = fontui::isDarkPalette(pal);
    const qreal dpr          = m_dpr;
    const QColor muted_guide = glyphGuideColor(GlyphGuideId::Baseline, pal);
    const QColor ascent_clr  = glyphGuideColor(GlyphGuideId::Ascent, pal);
    const QColor descent_clr = glyphGuideColor(GlyphGuideId::Descent, pal);
    const QColor cap_clr     = glyphGuideColor(GlyphGuideId::CapHeight, pal);
    const QColor xh_clr      = glyphGuideColor(GlyphGuideId::XHeight, pal);
    const QColor bbox_clr    = glyphGuideColor(GlyphGuideId::BBox, pal);
    const QColor adv_clr     = glyphGuideColor(GlyphGuideId::Advance, pal);
    const QColor ul_clr      = glyphGuideColor(GlyphGuideId::Underline, pal);
    const QColor st_clr      = glyphGuideColor(GlyphGuideId::Strikeout, pal);
    const QColor sb_clr      = glyphGuideColor(GlyphGuideId::Bearings, pal);
    const qreal pad_10       = fontui::dpf(10.0, dpr);
    const qreal pad_20       = fontui::dpf(20.0, dpr);
    const qreal pad_30       = fontui::dpf(30.0, dpr);
    const qreal stroke_1     = fontui::dpf(1.0, dpr);
    auto guideAlpha          = [&](GlyphGuideId id) -> qreal {
        const int idx = static_cast<int>(id);
        return (idx >= 0 && idx < kGuideCount) ? m_guide_alphas[size_t(idx)]
                                                        : 1.0;
    };
    auto scaleAlpha = [](QColor c, qreal factor) -> QColor {
        if (factor < 0.999) {
            c.setAlpha(qBound(0, qRound(c.alpha() * factor), 255));
        }
        return c;
    };

    const qreal adv_alpha    = guideAlpha(GlyphGuideId::Advance);
    const qreal ul_alpha     = guideAlpha(GlyphGuideId::Underline);
    const qreal st_alpha     = guideAlpha(GlyphGuideId::Strikeout);
    const qreal base_alpha   = guideAlpha(GlyphGuideId::Baseline);
    const qreal center_alpha = guideAlpha(GlyphGuideId::Center);
    const qreal asc_alpha    = guideAlpha(GlyphGuideId::Ascent);
    const qreal desc_alpha   = guideAlpha(GlyphGuideId::Descent);
    const qreal cap_alpha    = guideAlpha(GlyphGuideId::CapHeight);
    const qreal xh_alpha     = guideAlpha(GlyphGuideId::XHeight);
    const qreal bbox_alpha   = guideAlpha(GlyphGuideId::BBox);
    const qreal sb_alpha     = guideAlpha(GlyphGuideId::Bearings);

    // Draw advance width rectangle (subtle fill)
    if (adv_alpha > 0.005) {
        QColor adv_fill = adv_clr;
        adv_fill.setAlpha(dark ? 12 : 16);
        painter.fillRect(QRectF(text_x, pad_20, display_advance,
                                rect.height() - pad_20 * 2.0),
                         scaleAlpha(adv_fill, adv_alpha));
    }

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
        const bool ul_logical = isGuideVisible(GlyphGuideId::Underline);
        if (m_deco_metrics.has_underline && (ul_logical || ul_alpha > 0.005)) {
            underline_top_y
                = text_y
                  - (qreal(m_deco_metrics.underline_pos) / upem) * em_px;
            const qreal band_h = qMax(
                1.0, (qreal(m_deco_metrics.underline_thick) / upem) * em_px);
            const QRectF ul_rect(pad_10, underline_top_y, band_width, band_h);
            if (ul_alpha > 0.005) {
                QColor ul_fill = ul_clr;
                ul_fill.setAlpha(38);
                painter.fillRect(ul_rect, scaleAlpha(ul_fill, ul_alpha));
                QColor ul_line = ul_clr;
                ul_line.setAlpha(190);
                QPen ul_pen(scaleAlpha(ul_line, ul_alpha));
                ul_pen.setStyle(Qt::DashLine);
                ul_pen.setWidthF(stroke_1);
                painter.setPen(ul_pen);
                painter.drawLine(QPointF(pad_10, underline_top_y),
                                 QPointF(rect.width() - pad_10, underline_top_y));
            }
            if (ul_logical) {
                m_last_underline_band = ul_rect;
            }
            draw_underline = true;
        }
        const bool st_logical = isGuideVisible(GlyphGuideId::Strikeout);
        if (m_deco_metrics.has_strikeout && (st_logical || st_alpha > 0.005)) {
            strike_top_y
                = text_y
                  - (qreal(m_deco_metrics.strikeout_pos) / upem) * em_px;
            const qreal band_h = qMax(
                1.0, (qreal(m_deco_metrics.strikeout_size) / upem) * em_px);
            const QRectF st_rect(pad_10, strike_top_y, band_width, band_h);
            if (st_alpha > 0.005) {
                QColor st_fill = st_clr;
                st_fill.setAlpha(38);
                painter.fillRect(st_rect, scaleAlpha(st_fill, st_alpha));
                QColor st_line = st_clr;
                st_line.setAlpha(190);
                QPen st_pen(scaleAlpha(st_line, st_alpha));
                st_pen.setStyle(Qt::DashLine);
                st_pen.setWidthF(stroke_1);
                painter.setPen(st_pen);
                painter.drawLine(QPointF(pad_10, strike_top_y),
                                 QPointF(rect.width() - pad_10, strike_top_y));
            }
            if (st_logical) {
                m_last_strikeout_band = st_rect;
            }
            draw_strikeout = true;
        }
    }

    // Draw baseline (dashed line) - horizontal through text baseline
    if (base_alpha > 0.005) {
        QPen baseline_pen(scaleAlpha(muted_guide, base_alpha));
        baseline_pen.setStyle(Qt::DashLine);
        baseline_pen.setWidthF(stroke_1);
        painter.setPen(baseline_pen);
        painter.drawLine(QPointF(pad_10, text_y),
                         QPointF(rect.width() - pad_10, text_y));
    }

    // Draw vertical center line (dotted)
    if (center_alpha > 0.005) {
        QPen center_pen(scaleAlpha(muted_guide, center_alpha));
        center_pen.setStyle(Qt::DotLine);
        center_pen.setWidthF(stroke_1);
        painter.setPen(center_pen);
        painter.drawLine(QPointF(center_x, pad_10),
                         QPointF(center_x, rect.height() - pad_10));
    }

    // Draw advance width vertical lines
    if (adv_alpha > 0.005) {
        QColor adv_line = adv_clr;
        adv_line.setAlpha(dark ? 120 : 140);
        QPen advance_pen(scaleAlpha(adv_line, adv_alpha));
        advance_pen.setStyle(Qt::DashLine);
        advance_pen.setWidthF(stroke_1);
        painter.setPen(advance_pen);
        painter.drawLine(QPointF(text_x, pad_20),
                         QPointF(text_x, rect.height() - pad_20));
        painter.drawLine(
            QPointF(text_x + display_advance, pad_20),
            QPointF(text_x + display_advance, rect.height() - pad_20));
    }

    // Draw ascent line (above baseline)
    qreal ascent_y = text_y - display_ascent;
    if (asc_alpha > 0.005) {
        QColor asc_line = ascent_clr;
        asc_line.setAlpha(dark ? 120 : 140);
        QPen ascent_pen(scaleAlpha(asc_line, asc_alpha));
        ascent_pen.setStyle(Qt::DotLine);
        ascent_pen.setWidthF(stroke_1);
        painter.setPen(ascent_pen);
        painter.drawLine(QPointF(pad_10, ascent_y),
                         QPointF(rect.width() - pad_10, ascent_y));
    }

    // Draw descent line (below baseline)
    qreal descent_y = text_y + display_descent;
    if (desc_alpha > 0.005) {
        QColor desc_line = descent_clr;
        desc_line.setAlpha(dark ? 120 : 140);
        QPen descent_pen(scaleAlpha(desc_line, desc_alpha));
        descent_pen.setStyle(Qt::DotLine);
        descent_pen.setWidthF(stroke_1);
        painter.setPen(descent_pen);
        painter.drawLine(QPointF(pad_10, descent_y),
                         QPointF(rect.width() - pad_10, descent_y));
    }

    // Draw cap height line
    const bool has_cap
        = isGuideAvailableWithMetrics(GlyphGuideId::CapHeight, display_fm,
                                      display_ascent, m_deco_metrics);
    qreal cap_y = text_y - display_fm.capHeight();
    if (has_cap && cap_alpha > 0.005) {
        QColor cap_line = cap_clr;
        cap_line.setAlpha(dark ? 120 : 140);
        QPen cap_pen(scaleAlpha(cap_line, cap_alpha));
        cap_pen.setStyle(Qt::DotLine);
        cap_pen.setWidthF(stroke_1);
        painter.setPen(cap_pen);
        painter.drawLine(QPointF(pad_10, cap_y),
                         QPointF(rect.width() - pad_10, cap_y));
    }

    // Draw x-height line
    const bool has_xheight
        = isGuideAvailableWithMetrics(GlyphGuideId::XHeight, display_fm,
                                      display_ascent, m_deco_metrics);
    qreal x_y = text_y - display_fm.xHeight();
    if (has_xheight && xh_alpha > 0.005) {
        QColor xh_line = xh_clr;
        xh_line.setAlpha(dark ? 120 : 140);
        QPen x_pen(scaleAlpha(xh_line, xh_alpha));
        x_pen.setStyle(Qt::DotLine);
        x_pen.setWidthF(stroke_1);
        painter.setPen(x_pen);
        painter.drawLine(QPointF(pad_10, x_y),
                         QPointF(rect.width() - pad_10, x_y));
    }

    // Draw bounding box (green) - actual glyph bounds
    if (bbox_alpha > 0.005) {
        QPen bbox_pen(scaleAlpha(bbox_clr, bbox_alpha));
        bbox_pen.setStyle(Qt::DashDotLine);
        bbox_pen.setWidthF(stroke_1);
        painter.setPen(bbox_pen);
        painter.drawRect(glyph_box);
    }

    // Draw bearing markers
    if (sb_alpha > 0.005) {
        QColor bearing_line = sb_clr;
        bearing_line.setAlpha(dark ? 80 : 110);
        QPen bearing_pen(scaleAlpha(bearing_line, sb_alpha));
        bearing_pen.setStyle(Qt::DotLine);
        bearing_pen.setWidthF(stroke_1);
        painter.setPen(bearing_pen);
        painter.drawLine(QPointF(glyph_box.left(), pad_30),
                         QPointF(glyph_box.left(), rect.height() - pad_30));
        painter.drawLine(QPointF(glyph_box.right(), pad_30),
                         QPointF(glyph_box.right(), rect.height() - pad_30));
    }

    // Draw the glyph
    painter.setFont(display_font);
    painter.setPen(pal.text().color());
    painter.drawText(QPointF(text_x, text_y), glyph_str);

    // Overlay FreeType Bezier control points and tangent handles on top of the
    // glyph and underneath metric labels when toggled on.
    const int outline_upem = m_deco_metrics.units_per_em > 0
                                 ? m_deco_metrics.units_per_em
                                 : (display_raw.isValid()
                                        ? qRound(display_raw.unitsPerEm())
                                        : 0);
    if ((m_show_outline_points || m_outline_alpha > 0.005) && m_outline.valid
        && outline_upem > 0 && em_px > 0.0 && !m_outline.points.isEmpty()
        && m_outline.tags.size() == m_outline.points.size()) {
        const qreal scale = em_px / outline_upem;
        QVector<QPointF> screen_pts;
        screen_pts.reserve(m_outline.points.size());
        for (const QPointF &fp : m_outline.points) {
            screen_pts.append(
                QPointF(text_x + fp.x() * scale, text_y - fp.y() * scale));
        }
        if (m_show_outline_points) {
            m_last_outline_points = screen_pts;
        }

        if (m_outline_alpha > 0.005) {
            QPen handle_pen(
                scaleAlpha(QColor(230, 125, 35, 165), m_outline_alpha));
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

            const QColor base_col
                = scaleAlpha(pal.base().color(), m_outline_alpha);
            const QColor on_stroke
                = scaleAlpha(dark ? QColor(255, 255, 255, 220)
                                  : QColor(20, 40, 70, 220),
                             m_outline_alpha);
            const QColor on_fill
                = scaleAlpha(QColor(35, 145, 255), m_outline_alpha);
            const QColor off_stroke
                = scaleAlpha(QColor(230, 125, 35), m_outline_alpha);
            const qreal pt_r    = fontui::dpf(2.5, dpr);
            const qreal pt_side = fontui::dpf(5.0, dpr);
            for (int i = 0; i < screen_pts.size(); ++i) {
                const QPointF &pt = screen_pts[i];
                if (m_outline.tags[i] == GlyphOutlineData::PointKind::OnCurve) {
                    painter.setPen(QPen(on_stroke, stroke_1));
                    painter.setBrush(on_fill);
                    painter.drawRect(
                        QRectF(pt.x() - pt_r, pt.y() - pt_r, pt_side, pt_side));
                }
                else {
                    painter.setPen(QPen(off_stroke, fontui::dpf(1.5, dpr)));
                    painter.setBrush(base_col);
                    painter.drawEllipse(pt, pt_r, pt_r);
                }
            }
            painter.setBrush(Qt::NoBrush);
        }
    }

    // Draw metric labels measured against the widget font and kept within
    // bounds without overlapping each other.
    painter.save();
    const QFont label_font
        = fontui::scaledCaptionFont(this, dpr, -1.0, 8.0, 0.80);
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
                               bool isAbove, bool record_logical,
                               qreal alpha) {
        if (!record_logical && alpha <= 0.005) {
            return;
        }
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

        if (record_logical) {
            occupied_labels.append(r);
            m_last_label_rects.append(r);
            m_last_label_texts.append(text);
        }
        if (alpha > 0.005) {
            painter.setPen(scaleAlpha(color, alpha));
            painter.drawText(r, Qt::AlignRight | Qt::AlignVCenter, elided);
        }
    };

    // Priority order places core baseline/extents first, then secondary metrics.
    if (isGuideVisible(GlyphGuideId::Baseline) || base_alpha > 0.005) {
        drawMetricLabel(text_y, "baseline", muted_guide, true,
                        isGuideVisible(GlyphGuideId::Baseline), base_alpha);
    }
    if (isGuideVisible(GlyphGuideId::Ascent) || asc_alpha > 0.005) {
        drawMetricLabel(ascent_y, "ascent", ascent_clr, true,
                        isGuideVisible(GlyphGuideId::Ascent), asc_alpha);
    }
    if (isGuideVisible(GlyphGuideId::Descent) || desc_alpha > 0.005) {
        drawMetricLabel(descent_y, "descent", descent_clr, false,
                        isGuideVisible(GlyphGuideId::Descent), desc_alpha);
    }
    if (has_cap
        && (isGuideVisible(GlyphGuideId::CapHeight) || cap_alpha > 0.005)) {
        drawMetricLabel(cap_y, "cap height", cap_clr, true,
                        isGuideVisible(GlyphGuideId::CapHeight), cap_alpha);
    }
    if (has_xheight
        && (isGuideVisible(GlyphGuideId::XHeight) || xh_alpha > 0.005)) {
        drawMetricLabel(x_y, "x-height", xh_clr, true,
                        isGuideVisible(GlyphGuideId::XHeight), xh_alpha);
    }
    if (draw_strikeout) {
        drawMetricLabel(strike_top_y,
                        QString("strikeout (%1/%2)")
                            .arg(m_deco_metrics.strikeout_pos)
                            .arg(m_deco_metrics.strikeout_size),
                        st_clr, true, isGuideVisible(GlyphGuideId::Strikeout),
                        st_alpha);
    }
    if (draw_underline) {
        drawMetricLabel(underline_top_y,
                        QString("underline (%1/%2)")
                            .arg(m_deco_metrics.underline_pos)
                            .arg(m_deco_metrics.underline_thick),
                        ul_clr, false, isGuideVisible(GlyphGuideId::Underline),
                        ul_alpha);
    }

    // Vertical metrics (Origin, Advance, LSB, RSB)
    auto drawVerticalLabel = [&](qreal x, const QString &text, QColor color,
                                 bool isBottom, bool isLeftOfLine,
                                 bool record_logical, qreal alpha) {
        if (!record_logical && alpha <= 0.005) {
            return;
        }
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

        if (record_logical) {
            occupied_labels.append(r);
            m_last_label_rects.append(r);
            m_last_label_texts.append(text);
        }
        if (alpha > 0.005) {
            painter.setPen(scaleAlpha(color, alpha));
            painter.drawText(
                r,
                (r.center().x() < x ? Qt::AlignRight : Qt::AlignLeft)
                    | Qt::AlignVCenter,
                elided);
        }
    };

    if (isGuideVisible(GlyphGuideId::Advance) || adv_alpha > 0.005) {
        const bool adv_vis = isGuideVisible(GlyphGuideId::Advance);
        drawVerticalLabel(text_x, "origin", adv_clr, true, true, adv_vis,
                          adv_alpha);
        drawVerticalLabel(text_x + display_advance, "advance", adv_clr, true,
                          false, adv_vis, adv_alpha);
    }

    if (isGuideVisible(GlyphGuideId::Bearings) || sb_alpha > 0.005) {
        const bool sb_vis = isGuideVisible(GlyphGuideId::Bearings);
        if (qAbs(glyph_box.left() - text_x) > 0.1) {
            drawVerticalLabel(glyph_box.left(), "LSB", sb_clr, false, true,
                              sb_vis, sb_alpha);
        }
        if (qAbs(glyph_box.right() - (text_x + display_advance)) > 0.1) {
            drawVerticalLabel(glyph_box.right(), "RSB", sb_clr, false, false,
                              sb_vis, sb_alpha);
        }
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
    syncMetricsAndFonts();
}

void GlyphInspectorWidget::syncEmptyLabelFont()
{
    if (!m_label_no_selection) {
        return;
    }
    QFont font = fontui::scaledCaptionFont(m_label_no_selection, m_dpr, 5.0,
                                           14.0, 1.45);
    font.setItalic(true);
    if (m_label_no_selection->font() != font) {
        m_label_no_selection->setFont(font);
    }
}

void GlyphInspectorWidget::syncMetricsAndFonts()
{
    if (m_main_layout) {
        const int pad = fontui::dp(10, m_dpr);
        m_main_layout->setContentsMargins(pad, pad, pad, pad);
        m_main_layout->setSpacing(pad);
    }
    syncEmptyLabelFont();
    if (m_preview_widget) {
        m_preview_widget->setDpr(m_dpr);
    }
    if (m_guide_sep) {
        m_guide_sep->setDpr(m_dpr);
    }
    for (QPushButton *pill : m_guide_pills) {
        if (pill) {
            auto *btn = static_cast<GuidePillButton *>(pill);
            btn->setDpr(m_dpr);
            btn->syncFont();
        }
    }
    if (m_btn_outline) {
        auto *btn = static_cast<GuidePillButton *>(m_btn_outline);
        btn->setDpr(m_dpr);
        btn->syncFont();
    }
    if (m_btn_toggle_all) {
        static_cast<fontui::CompactActionButton *>(m_btn_toggle_all)
            ->setDpr(m_dpr);
    }
    if (m_guide_bar) {
        auto *bar = static_cast<GuideControlsBar *>(m_guide_bar);
        bar->setDpr(m_dpr);
        if (auto *host = bar->pillsHost()) {
            if (auto *flow = static_cast<fontui::FlowLayout *>(host->layout())) {
                flow->setSpacingPair(fontui::dp(5, m_dpr), fontui::dp(4, m_dpr));
            }
            host->updateGeometry();
        }
        bar->relayout();
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
    syncEmptyLabelFont();
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
    m_label_glyph_id      = new ElidingValueLabel(this);
    m_label_advance_width = new QLabel(this);
    m_label_bbox          = new QLabel(this);
    m_label_bearings      = new QLabel(this);
    m_label_contours      = new QLabel(this);
    m_label_points        = new QLabel(this);
    m_label_glyph_id->setObjectName(QStringLiteral("gi_glyph_id_label"));
    configureValueLabel(m_label_advance_width);
    configureValueLabel(m_label_bbox);
    configureValueLabel(m_label_bearings);
    configureValueLabel(m_label_contours);
    configureValueLabel(m_label_points);
    glyph_layout->addRow("Glyph ID:", m_label_glyph_id);
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

    auto *pills_host = new GuidePillsHost(m_dpr, this);
    auto *flow       = static_cast<fontui::FlowLayout *>(pills_host->layout());
    m_guide_pills.reserve(int(kGuideSpecs.size()));
    for (const GlyphGuideSpec &spec : kGuideSpecs) {
        auto *pill = new GuidePillButton(QString::fromLatin1(spec.label), m_dpr,
                                         false, spec.id, pills_host);
        pill->setObjectName(
            QStringLiteral("guide_pill_%1").arg(QLatin1String(spec.key)));
        pill->setChecked(!m_hidden_guides.contains(spec.id));
        pill->setToolTip(QString::fromUtf8(spec.tooltip));
        const GlyphGuideId id = spec.id;
        connect(pill, &QPushButton::toggled, this, [this, id](bool on) {
            if (on) {
                m_hidden_guides.remove(id);
            }
            else {
                m_hidden_guides.insert(id);
            }
            if (m_preview_widget) {
                m_preview_widget->setGuideVisible(id, on, true);
            }
            updateToggleAllButton(computeGuideAvailability());
        });
        flow->addWidget(pill);
        m_guide_pills.append(pill);
    }

    m_guide_sep = new fontui::FlowSeparator(m_dpr, pills_host);
    flow->addWidget(m_guide_sep);

    m_btn_outline = new GuidePillButton(tr("Outline"), m_dpr, true,
                                        GlyphGuideId::Baseline, pills_host);
    m_btn_outline->setObjectName(QStringLiteral("guide_pill_outline"));
    m_btn_outline->setChecked(m_show_outline_points);
    setOutlineShortcutHint(m_outline_hint);
    connect(m_btn_outline, &QPushButton::toggled, this, [this](bool on) {
        if (m_show_outline_points == on) {
            return;
        }
        m_show_outline_points = on;
        if (m_preview_widget) {
            m_preview_widget->setShowOutlinePoints(on, true);
        }
    });
    flow->addWidget(m_btn_outline);

    m_btn_toggle_all = new ToggleAllGuidesButton(m_dpr, this);
    m_btn_toggle_all->setObjectName(QStringLiteral("guide_toggle_all_btn"));
    connect(m_btn_toggle_all, &QPushButton::clicked, this, [this]() {
        const auto availability = computeGuideAvailability();
        const bool any_hidden   = hasHiddenAvailableGuide(availability);
        const int count = qMin(m_guide_pills.size(), int(kGuideSpecs.size()));
        for (int i = 0; i < count; ++i) {
            if (!availability[size_t(i)]) {
                continue;
            }
            const GlyphGuideId id = kGuideSpecs[size_t(i)].id;
            if (any_hidden) {
                m_hidden_guides.remove(id);
            }
            else {
                m_hidden_guides.insert(id);
            }
        }
        syncGuideControls(true);
    });

    m_guide_bar
        = new GuideControlsBar(pills_host, m_btn_toggle_all, m_dpr, this);
    preview_layout->addWidget(m_guide_bar);

    m_preview_widget = new GlyphPreviewWidget(this);
    m_preview_widget->setDpr(m_dpr);
    m_preview_widget->setShowOutlinePoints(m_show_outline_points, false);
    m_preview_widget->setHiddenGuides(m_hidden_guides, false);
    preview_layout->addWidget(m_preview_widget, 1);
    m_main_layout->addWidget(m_group_preview, 1);

    syncGuideControls(false);

    // Initially hide all groups
    m_group_unicode->hide();
    m_group_glyph->hide();
    m_group_font->hide();
    m_group_preview->hide();
}

void GlyphInspectorWidget::setShowOutlinePoints(bool on)
{
    m_show_outline_points = on;
    if (m_btn_outline && m_btn_outline->isChecked() != on) {
        QSignalBlocker blocker(m_btn_outline);
        m_btn_outline->setChecked(on);
    }
    if (m_preview_widget) {
        m_preview_widget->setShowOutlinePoints(on, isVisible());
    }
}

void GlyphInspectorWidget::setOutlineShortcutHint(const QString &hint)
{
    m_outline_hint = hint;
    if (!m_btn_outline) {
        return;
    }
    const QString base
        = tr("Outline Control Points — On-curve nodes, Bezier handles, and "
             "tangents");
    m_btn_outline->setToolTip(
        hint.isEmpty() ? base : QStringLiteral("%1 (%2)").arg(base, hint));
}

QStringList GlyphInspectorWidget::hiddenGuideIds() const
{
    QStringList ids;
    for (const GlyphGuideSpec &spec : kGuideSpecs) {
        if (m_hidden_guides.contains(spec.id)) {
            ids.append(QString::fromLatin1(spec.key));
        }
    }
    return ids;
}

void GlyphInspectorWidget::setHiddenGuideIds(const QStringList &ids)
{
    QSet<GlyphGuideId> parsed;
    for (const QString &entry : ids) {
        for (const QString &part :
             entry.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            if (const auto id = glyphGuideFromKey(part)) {
                parsed.insert(*id);
            }
        }
    }
    m_hidden_guides = parsed;
    syncGuideControls();
}

QVector<QPushButton *> GlyphInspectorWidget::guidePillButtons() const
{
    return m_guide_pills;
}

std::array<bool, GlyphInspectorWidget::kGuideCount>
GlyphInspectorWidget::computeGuideAvailability() const
{
    static_assert(int(kGuideSpecs.size()) == kGuideCount);
    std::array<bool, kGuideCount> result{};
    const QFont display_font = makeDisplayFontForMetrics(m_font);
    const QFontMetricsF display_fm(display_font, this);
    qreal display_ascent = display_fm.ascent();
    if (m_has_selection && m_code_point != 0) {
        const QRawFont display_raw = QRawFont::fromFont(display_font);
        if (display_raw.isValid()) {
            const QVector<quint32> indexes
                = display_raw.glyphIndexesForString(
                    QString::fromUcs4(&m_code_point, 1));
            if (!indexes.isEmpty()) {
                display_ascent = display_raw.ascent();
            }
        }
    }
    for (size_t i = 0; i < kGuideSpecs.size(); ++i) {
        result[i] = isGuideAvailableWithMetrics(
            kGuideSpecs[i].id, display_fm, display_ascent, m_deco_metrics);
    }
    return result;
}

bool GlyphInspectorWidget::hasHiddenAvailableGuide(
    const std::array<bool, kGuideCount> &availability) const
{
    const int count = qMin(m_guide_pills.size(), int(kGuideSpecs.size()));
    for (int i = 0; i < count; ++i) {
        if (availability[size_t(i)]
            && m_hidden_guides.contains(kGuideSpecs[size_t(i)].id)) {
            return true;
        }
    }
    return false;
}

void GlyphInspectorWidget::updateToggleAllButton(
    const std::array<bool, kGuideCount> &availability)
{
    if (!m_btn_toggle_all) {
        return;
    }
    if (hasHiddenAvailableGuide(availability)) {
        m_btn_toggle_all->setText(tr("Show All"));
        m_btn_toggle_all->setToolTip(tr("Show all guide lines"));
    }
    else {
        m_btn_toggle_all->setText(tr("Hide All"));
        m_btn_toggle_all->setToolTip(tr("Hide all guide lines"));
    }
}

void GlyphInspectorWidget::syncGuideControls(bool animate)
{
    const auto availability = computeGuideAvailability();
    const int count = qMin(m_guide_pills.size(), int(kGuideSpecs.size()));
    for (int i = 0; i < count; ++i) {
        QPushButton *pill = m_guide_pills.at(i);
        if (!pill) {
            continue;
        }
        const GlyphGuideId id = kGuideSpecs[size_t(i)].id;
        const bool avail      = availability[size_t(i)];
        pill->setVisible(avail);
        {
            QSignalBlocker blocker(pill);
            pill->setChecked(!m_hidden_guides.contains(id));
        }
        if (id == GlyphGuideId::Underline && avail) {
            pill->setToolTip(
                QStringLiteral(
                    "Underline (pos %1 / thick %2 FUnits) — Text underline "
                    "position and thickness")
                    .arg(m_deco_metrics.underline_pos)
                    .arg(m_deco_metrics.underline_thick));
        }
        else if (id == GlyphGuideId::Strikeout && avail) {
            pill->setToolTip(
                QStringLiteral(
                    "Strikeout (pos %1 / size %2 FUnits) — Strikethrough "
                    "position and thickness")
                    .arg(m_deco_metrics.strikeout_pos)
                    .arg(m_deco_metrics.strikeout_size));
        }
        else {
            pill->setToolTip(QString::fromUtf8(guideSpec(id).tooltip));
        }
    }
    updateToggleAllButton(availability);
    if (m_preview_widget) {
        m_preview_widget->setHiddenGuides(m_hidden_guides, animate);
    }
    if (m_guide_bar) {
        auto *bar = static_cast<GuideControlsBar *>(m_guide_bar);
        if (auto *host = bar->pillsHost()) {
            host->updateGeometry();
        }
        bar->relayout();
    }
}

void GlyphInspectorWidget::updateGlyph(quint32 code_point,
                                       const QFont &font,
                                       const QString &font_path,
                                       const FontDecorationMetrics &deco,
                                       const GlyphOutlineData &outline,
                                       const QString &glyph_name)
{
    m_code_point    = code_point;
    m_font          = font;
    m_font_path     = font_path;
    m_glyph_name    = glyph_name;
    m_deco_metrics  = deco;
    m_outline       = outline;
    m_has_selection = (code_point != 0);

    if (m_has_selection) {
        m_label_no_selection->hide();
        m_group_unicode->show();
        m_group_glyph->show();
        m_group_font->show();
        m_group_preview->show();

        updateMetrics();
        updateLabels();
        syncGuideControls(false);
        m_preview_widget->setGlyphData(m_code_point, m_font,
                                       m_metrics.bounding_box,
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
    m_code_point    = 0;
    m_glyph_name.clear();
    m_outline       = {};
    m_label_no_selection->show();
    m_group_unicode->hide();
    m_group_glyph->hide();
    m_group_font->hide();
    m_group_preview->hide();
    if (m_label_glyph_id) {
        static_cast<ElidingValueLabel *>(m_label_glyph_id)
            ->setFullText(QString());
    }
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
                                      const GlyphOutlineData &outline,
                                      const QString &glyph_name)
{
    m_deco_metrics = deco;
    m_outline      = outline;
    if (!glyph_name.isNull()) {
        m_glyph_name = glyph_name;
    }
    if (m_has_selection) {
        m_font = font;
        updateMetrics();
        updateLabels();
        syncGuideControls(false);
        m_preview_widget->setGlyphData(m_code_point, m_font,
                                       m_metrics.bounding_box,
                                       m_metrics.advance_width, m_deco_metrics,
                                       m_outline);
    }
    else {
        m_font = font;
        syncGuideControls(false);
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

void GlyphInspectorWidget::refreshThemeVisuals()
{
    applyEmptyLabelPalette();
    for (QPushButton *pill : m_guide_pills) {
        if (pill) {
            pill->update();
        }
    }
    if (m_btn_outline) {
        m_btn_outline->update();
    }
    if (m_guide_sep) {
        m_guide_sep->update();
    }
    if (m_btn_toggle_all) {
        m_btn_toggle_all->update();
    }
    if (m_preview_widget) {
        m_preview_widget->update();
    }
}

void GlyphInspectorWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::StyleChange
        || event->type() == QEvent::FontChange) {
        syncMetricsAndFonts();
        updateToggleAllButton(computeGuideAvailability());
        refreshThemeVisuals();
    }
    else if (event->type() == QEvent::PaletteChange
             || event->type() == QEvent::ApplicationPaletteChange) {
        refreshThemeVisuals();
    }
}

void GlyphInspectorWidget::updateTheme(int theme)
{
    m_theme = theme;
    refreshThemeVisuals();
}

void GlyphInspectorWidget::updateMetrics()
{
    const QString str = QString::fromUcs4(&m_code_point, 1);
    // Try QRawFont first for accurate metrics
    QRawFont raw_font = QRawFont::fromFont(m_font);
    if (!raw_font.isValid()) {
        qprintt << "QRawFont invalid, falling back to QFontMetrics";
        QFontMetricsF fm(m_font);
        m_metrics               = {};
        m_metrics.advance_width = fm.horizontalAdvance(str);
        m_metrics.bounding_box  = fm.boundingRect(str);
        m_metrics.ascent        = fm.ascent();
        m_metrics.descent       = fm.descent();
        m_metrics.line_gap      = fm.lineSpacing() - fm.height();
        return;
    }

    QVector<quint32> glyph_indexes = raw_font.glyphIndexesForString(str);
    if (glyph_indexes.isEmpty()) {
        qprintt << "No glyph index for character"
                << unicode_blocks::formatCodePoint(m_code_point);
        m_metrics = {};
        return;
    }

    quint32 glyph_index       = glyph_indexes[0];
    QVector<QPointF> advances = raw_font.advancesForGlyphIndexes({glyph_index});

    m_metrics.glyph_id      = glyph_index;
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
    m_label_unicode_value->setText(
        unicode_blocks::formatCodePoint(m_code_point));
    m_label_unicode_decimal->setText(QString::number(m_code_point));
    const QString block_name = unicode_blocks::blockName(m_code_point);
    m_label_unicode_block->setText(block_name);
    m_label_unicode_block->setToolTip(block_name);

    // Glyph metrics
    if (m_label_glyph_id) {
        QString full_id;
        if (m_metrics.glyph_id > 0) {
            full_id = m_glyph_name.isEmpty()
                ? QStringLiteral("#%1").arg(m_metrics.glyph_id)
                : QStringLiteral("#%1 (%2)")
                      .arg(m_metrics.glyph_id)
                      .arg(m_glyph_name);
        }
        else {
            full_id = m_glyph_name.isEmpty() ? QStringLiteral("N/A")
                                             : m_glyph_name;
        }
        static_cast<ElidingValueLabel *>(m_label_glyph_id)
            ->setFullText(full_id);
    }
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
