#include "waterfallwidget.h"

#include <QEvent>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>

#include "fontui.h"

namespace {
constexpr qreal kOuterPadding = 8.0;
constexpr qreal kRowPadding   = 6.0;
constexpr qreal kTextGap      = 12.0;
constexpr qreal kMinRowHeight = 28.0;
}  // namespace

WaterfallWidget::WaterfallWidget(QWidget *parent) : QWidget(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

void WaterfallWidget::setDpr(qreal dpr)
{
    const qreal clamped = fontui::clampDpr(dpr);
    if (qFuzzyCompare(m_dpr, clamped)) {
        return;
    }
    m_dpr = clamped;
    relayout();
    update();
}

void WaterfallWidget::setFontProvider(FontProvider provider)
{
    m_font_provider = std::move(provider);
    relayout();
    update();
}

void WaterfallWidget::setSizes(const QVector<int> &sizes_pt)
{
    QVector<int> sorted;
    sorted.reserve(sizes_pt.size());
    for (int pt : sizes_pt) {
        if (pt > 0) {
            sorted.append(pt);
        }
    }
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

    if (sorted == m_sizes) {
        return;
    }
    m_sizes = sorted;

    if (!m_sizes.contains(m_current) && !m_sizes.isEmpty()) {
        m_current = m_sizes.first();
    }

    relayout();
    update();
}

void WaterfallWidget::setCurrentSize(int pt)
{
    if (m_current == pt) {
        return;
    }
    m_current = pt;
    update();
}

void WaterfallWidget::setPreviewText(const QString &text)
{
    if (m_text == text) {
        return;
    }
    m_text = text;
    relayout();
    update();
}

void WaterfallWidget::setInverted(bool inverted)
{
    if (m_inverted == inverted) {
        return;
    }
    m_inverted = inverted;
    update();
}

void WaterfallWidget::refreshFont()
{
    relayout();
    update();
}

QFont WaterfallWidget::rowFont(int pt) const
{
    const int clamped_pt = qMax(1, pt);
    if (m_font_provider) {
        return m_font_provider(clamped_pt);
    }
    QFont font = this->font();
    font.setPointSize(clamped_pt);
    return font;
}

QString WaterfallWidget::sizeLabel(int pt) const
{
    return QString::number(pt) + QStringLiteral(" pt");
}

QColor WaterfallWidget::baseColor() const
{
    const QPalette &pal = palette();
    return m_inverted ? pal.color(QPalette::Text) : pal.color(QPalette::Base);
}

QColor WaterfallWidget::textColor() const
{
    const QPalette &pal = palette();
    return m_inverted ? pal.color(QPalette::Base) : pal.color(QPalette::Text);
}

QColor WaterfallWidget::mutedTextColor() const
{
    if (m_inverted) {
        QColor color = textColor();
        color.setAlpha(170);
        return color;
    }
    QColor color = palette().color(QPalette::PlaceholderText);
    if (!color.isValid()) {
        return fontui::mutedTextColor(palette());
    }
    return color;
}

void WaterfallWidget::relayout()
{
    m_rows.clear();
    m_rows.reserve(m_sizes.size());

    if (m_sizes.isEmpty()) {
        m_content_height = 0.0;
        setMinimumHeight(0);
        updateGeometry();
        return;
    }

    const QFont label_font = font();
    const QFontMetrics label_fm(label_font, this);
    m_label_width = 0.0;
    for (int pt : m_sizes) {
        m_label_width = qMax(m_label_width,
                             qreal(label_fm.horizontalAdvance(sizeLabel(pt))));
    }
    m_label_width = qCeil(m_label_width) + fontui::dpf(18.0, m_dpr);

    const qreal outer_pad = fontui::dpf(kOuterPadding, m_dpr);
    const qreal row_pad   = fontui::dpf(kRowPadding, m_dpr);
    const qreal min_row_h = fontui::dpf(kMinRowHeight, m_dpr);
    const qreal min_pill_row
        = qreal(label_fm.height()) + fontui::dpf(12.0, m_dpr);
    qreal y = outer_pad;
    for (int pt : m_sizes) {
        QFontMetricsF fm(rowFont(pt), this);
        const qreal row_height
            = qMax(qMax(min_row_h, min_pill_row), fm.height() + row_pad * 2.0);
        Row row;
        row.pt   = pt;
        row.rect = QRectF(0.0, y, qreal(width()), row_height);
        m_rows.append(row);
        y += row_height;
    }

    m_content_height = y + outer_pad;
    setMinimumHeight(int(qCeil(m_content_height)));
    updateGeometry();
}

void WaterfallWidget::drawRows(QPainter &painter, qreal height)
{
    painter.save();
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing,
                           true);
    painter.fillRect(QRectF(0.0, 0.0, qreal(width()), height), baseColor());

    const QFont label_font = font();
    const QFontMetrics label_fm(label_font, painter.device());
    const QColor accent    = fontui::accentColor(palette());
    const qreal bar_w      = fontui::dpf(3.0, m_dpr);
    const qreal inset_8    = fontui::dpf(8.0, m_dpr);
    const qreal inset_10   = fontui::dpf(10.0, m_dpr);
    const qreal text_gap   = fontui::dpf(kTextGap, m_dpr);

    for (const Row &row : m_rows) {
        const bool current = (row.pt == m_current);
        const qreal pill_height
            = qMin(row.rect.height() - fontui::dpf(4.0, m_dpr),
                   qreal(label_fm.height()) + inset_8);

        if (current) {
            QColor tint = accent;
            tint.setAlphaF(0.09);
            painter.fillRect(row.rect, tint);
            painter.fillRect(QRectF(row.rect.left(), row.rect.top() + bar_w,
                                    bar_w, row.rect.height() - bar_w * 2.0),
                             accent);
        }

        const QRectF pill(row.rect.left() + inset_8,
                          row.rect.center().y() - pill_height / 2.0,
                          m_label_width - inset_8, pill_height);
        painter.setFont(label_font);
        if (current) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(accent);
            painter.drawRoundedRect(pill, pill_height / 2.0, pill_height / 2.0);
            painter.setPen(palette().color(QPalette::HighlightedText));
        }
        else {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(mutedTextColor());
        }
        painter.drawText(pill, Qt::AlignCenter, sizeLabel(row.pt));

        const QFont text_font = rowFont(row.pt);
        const QFontMetrics text_fm(text_font, painter.device());
        const qreal text_x = row.rect.left() + m_label_width + text_gap;
        const qreal text_w = qMax(inset_10, row.rect.right() - text_x - inset_10);
        const QString elided
            = text_fm.elidedText(m_text, Qt::ElideRight, int(text_w));

        painter.setFont(text_font);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(textColor());
        painter.drawText(
            QRectF(text_x, row.rect.top(), text_w, row.rect.height()),
            Qt::AlignLeft | Qt::AlignVCenter, elided);
    }

    painter.restore();
}

void WaterfallWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange) {
        relayout();
        update();
    }
}

void WaterfallWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    drawRows(painter, qreal(height()));
}

void WaterfallWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        for (const Row &row : m_rows) {
            if (row.rect.contains(event->position())) {
                event->accept();
                emit sigSizeClicked(row.pt);
                return;
            }
        }
    }
    QWidget::mousePressEvent(event);
}

void WaterfallWidget::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const int delta = event->angleDelta().y();
        if (delta != 0) {
            emit sigZoomRequested(delta > 0 ? 1 : -1);
        }
        event->accept();
        return;
    }
    event->ignore();
}

void WaterfallWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    relayout();
}

QRectF WaterfallWidget::rowRect(int pt) const
{
    for (const Row &row : m_rows) {
        if (row.pt == pt) {
            return row.rect;
        }
    }
    return {};
}

QSize WaterfallWidget::contentSize() const
{
    return QSize(qMax(1, width()), int(qCeil(m_content_height)));
}

void WaterfallWidget::renderContent(QPainter *painter)
{
    if (!painter) {
        return;
    }
    drawRows(*painter, m_content_height);
}

QSize WaterfallWidget::sizeHint() const
{
    return QSize(420, int(qCeil(m_content_height)));
}
