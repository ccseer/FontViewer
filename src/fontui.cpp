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

FlowSeparator::FlowSeparator(qreal dpr, QWidget *parent)
    : QWidget(parent), m_dpr(clampDpr(dpr))
{
    setFocusPolicy(Qt::NoFocus);
    setProperty("flow_separator", true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void FlowSeparator::setDpr(qreal dpr)
{
    const qreal clamped = clampDpr(dpr);
    if (qFuzzyCompare(m_dpr, clamped)) {
        return;
    }
    m_dpr = clamped;
    updateGeometry();
    update();
}

QSize FlowSeparator::sizeHint() const
{
    return QSize(qMax(1, dp(1, m_dpr)), dp(14, m_dpr));
}

QSize FlowSeparator::minimumSizeHint() const
{
    return sizeHint();
}

void FlowSeparator::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), frameBorderColor(palette()));
}

FlowLayout::FlowLayout(QWidget *parent, int h_spacing, int v_spacing)
    : QLayout(parent), m_h_spacing(h_spacing), m_v_spacing(v_spacing)
{
    setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout()
{
    QLayoutItem *item = nullptr;
    while ((item = takeAt(0)) != nullptr) {
        delete item;
    }
}

void FlowLayout::setSpacingPair(int h_spacing, int v_spacing)
{
    if (m_h_spacing == h_spacing && m_v_spacing == v_spacing) {
        return;
    }
    m_h_spacing = h_spacing;
    m_v_spacing = v_spacing;
    invalidate();
}

void FlowLayout::addItem(QLayoutItem *item)
{
    m_items.append(item);
}

int FlowLayout::count() const
{
    return m_items.size();
}

QLayoutItem *FlowLayout::itemAt(int index) const
{
    return m_items.value(index);
}

QLayoutItem *FlowLayout::takeAt(int index)
{
    if (index >= 0 && index < m_items.size()) {
        return m_items.takeAt(index);
    }
    return nullptr;
}

Qt::Orientations FlowLayout::expandingDirections() const
{
    return {};
}

bool FlowLayout::hasHeightForWidth() const
{
    return true;
}

int FlowLayout::heightForWidth(int width) const
{
    return doLayout(QRect(0, 0, qMax(1, width), 0), true);
}

void FlowLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

QSize FlowLayout::sizeHint() const
{
    return minimumSize();
}

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for (const QLayoutItem *item : m_items) {
        if (!item->isEmpty()) {
            size = size.expandedTo(item->minimumSize());
        }
    }
    const QMargins m = contentsMargins();
    size += QSize(m.left() + m.right(), m.top() + m.bottom());
    return size;
}

int FlowLayout::doLayout(const QRect &rect, bool test_only) const
{
    auto is_separator = [](const QLayoutItem *item) {
        if (!item) {
            return false;
        }
        const QWidget *w = item->widget();
        return w && w->property("flow_separator").toBool();
    };

    int left   = 0;
    int top    = 0;
    int right  = 0;
    int bottom = 0;
    getContentsMargins(&left, &top, &right, &bottom);
    const QRect effective = rect.adjusted(left, top, -right, -bottom);
    int x                 = effective.x();
    int y                 = effective.y();
    int line_height       = 0;

    struct PlacedItem {
        QLayoutItem *item = nullptr;
        int x             = 0;
        QSize size;
    };
    QVector<PlacedItem> line_items;
    bool line_has_non_sep = false;
    bool last_was_sep     = false;

    auto flush_line = [&]() {
        if (!test_only) {
            for (const PlacedItem &placed : line_items) {
                const int item_y
                    = y + qMax(0, (line_height - placed.size.height()) / 2);
                placed.item->setGeometry(
                    QRect(QPoint(placed.x, item_y), placed.size));
            }
        }
        line_items.clear();
        line_has_non_sep = false;
        last_was_sep     = false;
    };

    for (int i = 0; i < m_items.size(); ++i) {
        QLayoutItem *item = m_items.at(i);
        if (item->isEmpty()) {
            continue;
        }
        const QSize item_sz = item->sizeHint();
        if (is_separator(item)) {
            if (!line_has_non_sep || last_was_sep) {
                if (!test_only) {
                    item->setGeometry(QRect());
                }
                continue;
            }
            QLayoutItem *next_non_sep = nullptr;
            for (int j = i + 1; j < m_items.size(); ++j) {
                QLayoutItem *cand = m_items.at(j);
                if (cand->isEmpty() || is_separator(cand)) {
                    continue;
                }
                next_non_sep = cand;
                break;
            }
            bool fits_on_line = (next_non_sep != nullptr)
                                && (x + item_sz.width() - 1 <= effective.right());
            if (fits_on_line) {
                const QSize next_sz = next_non_sep->sizeHint();
                const int after_sep_x = x + item_sz.width() + m_h_spacing;
                if (after_sep_x + next_sz.width() - 1 > effective.right()) {
                    fits_on_line = false;
                }
            }
            if (!fits_on_line) {
                if (!test_only) {
                    item->setGeometry(QRect());
                }
                // Wrap the following group onto the next row so two groups
                // never share a line without a separator between them.
                if (next_non_sep) {
                    flush_line();
                    x           = effective.x();
                    y           = y + line_height + m_v_spacing;
                    line_height = 0;
                }
                continue;
            }
            line_items.append({item, x, item_sz});
            x += item_sz.width() + m_h_spacing;
            line_height  = qMax(line_height, item_sz.height());
            last_was_sep = true;
            continue;
        }

        if (line_has_non_sep && x + item_sz.width() - 1 > effective.right()) {
            flush_line();
            x           = effective.x();
            y           = y + line_height + m_v_spacing;
            line_height = 0;
        }
        line_items.append({item, x, item_sz});
        x += item_sz.width() + m_h_spacing;
        line_height      = qMax(line_height, item_sz.height());
        line_has_non_sep = true;
        last_was_sep     = false;
    }

    if (!line_items.isEmpty()) {
        flush_line();
    }
    return y + line_height - rect.y() + bottom;
}

}  // namespace fontui
