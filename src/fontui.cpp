#include "fontui.h"

#include <QByteArray>
#include <QComboBox>
#include <QDebug>
#include <QFontMetrics>
#include <QListView>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSvgRenderer>
#include <QtMath>

namespace fontui {

const char kSvgCopyImage[] = R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
     stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <rect x="8" y="8" width="13" height="13" rx="2"/>
  <path d="M16 8V5a2 2 0 0 0-2-2H5a2 2 0 0 0-2 2v9a2 2 0 0 0 2 2h3"/>
</svg>)SVG";

const char kSvgSaveSvg[] = R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
     stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/>
  <polyline points="7 10 12 15 17 10"/>
  <line x1="12" y1="15" x2="12" y2="3"/>
</svg>)SVG";

const char kSvgChevronUp[] = R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
     stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <polyline points="18 15 12 9 6 15"/>
</svg>)SVG";

const char kSvgChevronDown[] = R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
     stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <polyline points="6 9 12 15 18 9"/>
</svg>)SVG";

QIcon svgIcon(const char *svg_data, const QColor &color, int icon_sz, qreal dpr)
{
    QByteArray data(svg_data);
    data.replace("currentColor", color.name(QColor::HexRgb).toUtf8());

    QSvgRenderer renderer(data);
    if (!renderer.isValid()) {
        qDebug() << "[FontViewer] svgIcon: invalid SVG";
        return {};
    }

    const qreal scale = clampDpr(dpr);
    const int phys    = qMax(1, qRound(icon_sz * scale));
    QPixmap pix(phys, phys);
    pix.setDevicePixelRatio(scale);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    renderer.render(&p, QRectF(0, 0, icon_sz, icon_sz));
    return QIcon(pix);
}

QPushButton *makeIconButton(const char *tooltip, QWidget *parent)
{
    auto *btn = new QPushButton(parent);
    btn->setFlat(true);
    btn->setToolTip(tooltip);
    btn->setFocusPolicy(Qt::NoFocus);
    return btn;
}

void configureComboBox(QComboBox *cb)
{
    auto *list = new QListView(cb);
    list->setResizeMode(QListView::Adjust);
    list->verticalScrollBar()->setProperty("is_readonly", true);
    cb->setView(list);
    cb->setFocusPolicy(Qt::NoFocus);
}

void fitComboBoxToContents(QComboBox *cb, qreal dpr)
{
    if (!cb) {
        return;
    }
    const QFontMetrics fm = cb->fontMetrics();
    int max_text_w        = 0;
    for (int i = 0; i < cb->count(); ++i) {
        // Custom [Presets] entries (kind == 2) are excluded so long user keys
        // do not expand the toolbar past its compact threshold.
        if (cb->itemData(i, Qt::UserRole + 1).toInt() == 2) {
            continue;
        }
        max_text_w = qMax(max_text_w, fm.horizontalAdvance(cb->itemText(i)));
    }
    // Seer Host MaterialStyle reserves 36*dpr for left/right padding and the
    // dropdown arrow, and ignores QComboBox's max-item contentsSize hint;
    // setting minimumWidth keeps long items from truncating.
    const int min_w = max_text_w + dp(44, dpr);
    if (cb->minimumWidth() != min_w) {
        cb->setMinimumWidth(min_w);
        cb->updateGeometry();
    }
}

CompactActionButton::CompactActionButton(const QString &text,
                                         qreal dpr,
                                         QWidget *parent,
                                         bool retain_hidden)
    : QPushButton(text, parent), m_dpr(clampDpr(dpr))
{
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover, true);
    QSizePolicy sp(QSizePolicy::Fixed, QSizePolicy::Fixed);
    sp.setRetainSizeWhenHidden(retain_hidden);
    setSizePolicy(sp);
}

void CompactActionButton::setDpr(qreal dpr)
{
    const qreal clamped = clampDpr(dpr);
    if (qFuzzyCompare(m_dpr, clamped)) {
        return;
    }
    m_dpr = clamped;
    updateGeometry();
    update();
}

QSize CompactActionButton::sizeHint() const
{
    const QFontMetrics fm = fontMetrics();
    return QSize(fm.horizontalAdvance(text()) + dp(12, m_dpr),
                 fm.height() + dp(4, m_dpr));
}

QSize CompactActionButton::minimumSizeHint() const
{
    return sizeHint();
}

void CompactActionButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QPalette &pal = palette();
    const bool dark     = isDarkPalette(pal);
    const bool hovered  = isEnabled() && underMouse();
    const bool pressed  = isEnabled() && isDown();

    if (pressed || hovered) {
        QColor bg = pal.color(QPalette::WindowText);
        bg.setAlpha(pressed ? (dark ? 42 : 28) : (dark ? 26 : 16));
        painter.setPen(Qt::NoPen);
        painter.setBrush(bg);
        const qreal r = surfaceRadius(m_dpr, false);
        if (r > 0.0) {
            painter.drawRoundedRect(QRectF(rect()), r, r);
        }
        else {
            painter.drawRect(QRectF(rect()));
        }
    }

    QColor fg = accentColor(pal);
    if (!isEnabled()) {
        fg.setAlpha(110);
    }
    else if (!hovered && !pressed) {
        fg.setAlpha(220);
    }
    painter.setPen(fg);
    painter.setFont(font());
    painter.drawText(rect(), Qt::AlignCenter, text());
}

}  // namespace fontui
