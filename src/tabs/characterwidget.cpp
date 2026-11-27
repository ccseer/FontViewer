#include "characterwidget.h"

#include <QEvent>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QtMath>

#include <algorithm>

#include "engine/fontinspectorengine.h"
#include "engine/unicodeblocks.h"
#include "fontui.h"
#include "tabs/charactergridmodel.h"

namespace {
bool isBmpScalar(quint32 cp)
{
    return cp > 0 && cp <= 0xFFFFu && !(cp >= 0xD800u && cp <= 0xDFFFu);
}
}  // namespace

CharacterWidget::CharacterWidget(QWidget *parent) : QWidget(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    auto sp = sizePolicy();
    sp.setHorizontalPolicy(QSizePolicy::Expanding);
    sp.setVerticalPolicy(QSizePolicy::Preferred);
    // Qt QScrollArea queries sizePolicy().hasHeightForWidth() on layout-less
    // viewport widgets; overriding virtual hasHeightForWidth() alone is ignored.
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
    syncGlyphFont();
}

int CharacterWidget::cellSizePx() const
{
    return fontui::dp(m_cell_dp, m_dpr);
}

int CharacterWidget::glyphPixelSize() const
{
    return qMax(12, qRound(cellSizePx() * 0.54));
}

int CharacterWidget::columnCount() const
{
    const int pad_x   = fontui::dp(4, m_dpr);
    const int cell_px = cellSizePx();
    const int avail_w = width() > 0 ? width() : 16 * cell_px + pad_x * 2;
    return CharacterGridModel::calcColumns(avail_w, cell_px, pad_x);
}

void CharacterWidget::setDpr(qreal dpr)
{
    const qreal clamped = fontui::clampDpr(dpr);
    if (qFuzzyCompare(m_dpr, clamped)) {
        return;
    }
    m_dpr = clamped;
    syncGlyphFont();
    updateGridGeometry();
    update();
}

void CharacterWidget::setCellSizeDp(int cell_dp)
{
    const int clamped = qBound(kMinCellSizeDp, cell_dp, kMaxCellSizeDp);
    if (m_cell_dp == clamped) {
        return;
    }
    m_cell_dp = clamped;
    syncGlyphFont();
    updateGridGeometry();
    update();
}

void CharacterWidget::init(const QString &path)
{
    FontInspectorEngine engine;
    if (engine.openFile(path)) {
        setCodePoints(engine.faceCodePoints(0));
    }
    else {
        setCodePoints({});
    }
}

void CharacterWidget::setCodePoints(const QVector<quint32> &code_points)
{
    m_code_points = code_points;
    m_hover_index = -1;
    updateGridGeometry();
    update();
}

void CharacterWidget::setSelectedCodePoint(quint32 code_point)
{
    if (m_last_key == code_point) {
        return;
    }
    const quint32 prev = m_last_key;
    m_last_key         = code_point;
    updateCell(indexOfCodePoint(prev));
    updateCell(indexOfCodePoint(code_point));
}

void CharacterWidget::syncGlyphFont()
{
    m_glyph_font = m_base_font;
    m_glyph_font.setPixelSize(glyphPixelSize());
}

void CharacterWidget::updateFont(const QFont &font)
{
    m_base_font = font;
    syncGlyphFont();
    update();
}

void CharacterWidget::updateGridGeometry()
{
    const int w = width() > 0 ? width() : sizeHint().width();
    const int h = heightForWidth(w);
    setMinimumHeight(h);
    updateGeometry();
}

int CharacterWidget::heightForWidth(int width) const
{
    const int total = m_code_points.size();
    if (total == 0) {
        return 0;
    }
    const int cell_px = cellSizePx();
    const int pad_x   = fontui::dp(4, m_dpr);
    const int pad_y   = fontui::dp(4, m_dpr);
    const int cols    = CharacterGridModel::calcColumns(width, cell_px, pad_x);
    const int rows    = CharacterGridModel::calcRows(total, cols);
    return rows * cell_px + pad_y * 2;
}

QSize CharacterWidget::sizeHint() const
{
    const int cell_px = cellSizePx();
    const int pad_x   = fontui::dp(4, m_dpr);
    const int w = width() > 0 ? width() : (12 * cell_px + pad_x * 2);
    return QSize(w, heightForWidth(w));
}

QSize CharacterWidget::minimumSizeHint() const
{
    const int cell_px = cellSizePx();
    const int pad_x   = fontui::dp(4, m_dpr);
    return QSize(cell_px + pad_x * 2, 0);
}

QRect CharacterWidget::cellRect(int index,
                                int cols,
                                int cell_px,
                                int origin_x,
                                int pad_y) const
{
    const int row = index / cols;
    const int col = index % cols;
    return QRect(origin_x + col * cell_px, pad_y + row * cell_px, cell_px,
                 cell_px);
}

QRect CharacterWidget::cellRectForIndex(int index) const
{
    if (index < 0 || index >= m_code_points.size()) {
        return {};
    }
    const GridMetrics gm = computeGridMetrics();
    return cellRect(index, gm.cols, gm.cell_px, gm.origin_x, gm.pad_y);
}

CharacterWidget::GridMetrics CharacterWidget::computeGridMetrics() const
{
    const int cell_px  = cellSizePx();
    const int cols     = columnCount();
    const int grid_w   = cols * cell_px;
    const int origin_x = qMax(0, (width() - grid_w) / 2);
    const int pad_y    = fontui::dp(4, m_dpr);
    return {cell_px, cols, origin_x, pad_y};
}

int CharacterWidget::indexAtPos(const QPoint &pt) const
{
    const int total = m_code_points.size();
    if (total == 0) {
        return -1;
    }
    const GridMetrics gm = computeGridMetrics();
    const int grid_w     = gm.cols * gm.cell_px;
    if (pt.x() < gm.origin_x || pt.x() >= gm.origin_x + grid_w
        || pt.y() < gm.pad_y) {
        return -1;
    }
    const int col    = (pt.x() - gm.origin_x) / gm.cell_px;
    const int row    = (pt.y() - gm.pad_y) / gm.cell_px;
    const qint64 idx = qint64(row) * gm.cols + col;
    if (idx < 0 || idx >= total) {
        return -1;
    }
    return int(idx);
}

quint32 CharacterWidget::codePointAtIndex(int index) const
{
    if (index < 0 || index >= m_code_points.size()) {
        return 0;
    }
    return m_code_points.at(index);
}

int CharacterWidget::indexOfCodePoint(quint32 cp) const
{
    if (cp == 0 || m_code_points.isEmpty()) {
        return -1;
    }
    auto it = std::lower_bound(m_code_points.cbegin(), m_code_points.cend(), cp);
    if (it != m_code_points.cend() && *it == cp) {
        return int(it - m_code_points.cbegin());
    }
    return m_code_points.indexOf(cp);
}

void CharacterWidget::updateCell(int index)
{
    if (index < 0 || index >= m_code_points.size()) {
        return;
    }
    const GridMetrics gm = computeGridMetrics();
    const int bleed      = qMax(2, fontui::dp(3, m_dpr));
    update(cellRect(index, gm.cols, gm.cell_px, gm.origin_x, gm.pad_y)
               .adjusted(-bleed, -bleed, bleed, bleed));
}

void CharacterWidget::emitSelection(quint32 cp)
{
    emit selectionChangedCodePoint(cp);
    if (cp == 0) {
        emit selectionChanged(QChar());
    }
    else if (isBmpScalar(cp)) {
        emit selectionChanged(QChar(ushort(cp)));
    }
}

void CharacterWidget::mouseMoveEvent(QMouseEvent *event)
{
    const int idx = indexAtPos(event->pos());
    if (idx != m_hover_index) {
        const int prev = m_hover_index;
        m_hover_index  = idx;
        updateCell(prev);
        updateCell(m_hover_index);
    }
}

void CharacterWidget::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    if (m_hover_index != -1) {
        const int prev = m_hover_index;
        m_hover_index  = -1;
        updateCell(prev);
    }
}

void CharacterWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    const int idx      = indexAtPos(event->pos());
    const quint32 cp   = codePointAtIndex(idx);
    const quint32 prev = m_last_key;
    m_last_key         = cp;
    emitSelection(cp);
    emit interactionClearFocus();
    if (prev != cp) {
        updateCell(indexOfCodePoint(prev));
    }
    updateCell(idx);
}

void CharacterWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }

    const quint32 cp = codePointAtIndex(indexAtPos(event->pos()));
    emit interactionClearFocus();
    if (cp != 0) {
        emit characterDoubleClickedCodePoint(cp);
        if (isBmpScalar(cp)) {
            emit characterDoubleClicked(QChar(ushort(cp)));
        }
    }
}

void CharacterWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateGridGeometry();
}

void CharacterWidget::paintEvent(QPaintEvent *event)
{
    const QPalette &pal = palette();
    QPainter painter(this);
    painter.fillRect(event->rect(), pal.base());

    const int total = m_code_points.size();
    if (total == 0) {
        return;
    }

    const GridMetrics gm = computeGridMetrics();
    const int cell_px    = gm.cell_px;
    const int cols       = gm.cols;
    const int origin_x   = gm.origin_x;
    const int pad_y      = gm.pad_y;
    const QRect rt       = event->rect();

    const int row_begin = qMax(0, (rt.top() - pad_y) / cell_px);
    const int row_end   = qMax(0, (rt.bottom() - pad_y) / cell_px);
    const int col_begin = qMax(0, (rt.left() - origin_x) / cell_px);
    const int col_end   = qMin(cols - 1, (rt.right() - origin_x) / cell_px);
    if (col_begin > col_end) {
        return;
    }

    const bool dark      = fontui::isDarkPalette(pal);
    const QColor fg_base = pal.color(QPalette::WindowText);
    const QColor accent  = fontui::accentColor(pal);
    QColor grid_line     = fg_base;
    grid_line.setAlpha(dark ? 16 : 12);

    const qreal pen_w   = fontui::dpf(1.0, m_dpr);
    const qreal radius  = fontui::surfaceRadius(m_dpr, false);
    const qreal inset   = fontui::dpf(1.5, m_dpr);
    const QFontMetricsF fm(m_glyph_font, this);
    painter.setFont(m_glyph_font);

    for (int row = row_begin; row <= row_end; ++row) {
        const int row_first = row * cols;
        if (row_first >= total) {
            break;
        }
        const int y = pad_y + row * cell_px;
        for (int col = col_begin; col <= col_end; ++col) {
            const int idx = row_first + col;
            if (idx >= total) {
                break;
            }
            const int x      = origin_x + col * cell_px;
            const quint32 cp = m_code_points.at(idx);
            const QRect cell(x, y, cell_px, cell_px);

            // Closed crisp hairline grid boundaries (left on col 0, top on row 0,
            // plus shared right and bottom edges).
            painter.save();
            painter.setPen(QPen(grid_line, 0));
            const int rx = x + cell_px - 1;
            const int by = y + cell_px - 1;
            if (row == 0) {
                painter.drawLine(x, y, rx, y);
            }
            if (col == 0) {
                painter.drawLine(x, y + (row == 0 ? 1 : 0), x, by);
            }
            painter.drawLine(x + (col == 0 ? 1 : 0), by, rx, by);
            painter.drawLine(rx, y + (row == 0 ? 1 : 0), rx, by - 1);
            painter.restore();

            const bool selected = (m_last_key != 0 && cp == m_last_key);
            const bool hovered  = (idx == m_hover_index);
            if (selected || hovered) {
                const QRectF r = QRectF(cell).adjusted(inset, inset, -inset,
                                                       -inset);
                QColor fill;
                QColor border;
                if (selected) {
                    fill = accent;
                    if (hovered) {
                        fill = fill.lighter(108);
                    }
                }
                else {
                    fill = fg_base;
                    fill.setAlpha(dark ? 24 : 14);
                    border = fg_base;
                    border.setAlpha(dark ? 52 : 40);
                }

                painter.save();
                painter.setRenderHint(QPainter::Antialiasing, true);
                painter.setPen(selected ? Qt::NoPen : QPen(border, pen_w));
                painter.setBrush(fill);
                if (radius > 0.0) {
                    painter.drawRoundedRect(r, radius, radius);
                }
                else {
                    painter.drawRect(r);
                }
                painter.restore();
            }

            const QString glyph_str = QString::fromUcs4(&cp, 1);
            const QRectF br = QChar::isSpace(cp) ? QRectF()
                                                 : fm.tightBoundingRect(glyph_str);
            const bool whitespace_like
                = QChar::isSpace(cp)
                  || (br.isEmpty() && fm.horizontalAdvance(glyph_str) > 0.0);
            if (whitespace_like) {
                const qreal box_side = qMin(qreal(cell_px) * 0.42,
                                            fontui::dpf(18.0, m_dpr));
                const QRectF blank_r(
                    cell.center().x() - box_side * 0.5,
                    cell.center().y() - box_side * 0.5, box_side, box_side);
                QColor blank_clr = selected ? pal.highlightedText().color()
                                            : fg_base;
                blank_clr.setAlpha(selected ? 165 : 55);
                painter.save();
                QPen dash_pen(blank_clr, pen_w, Qt::DashLine);
                painter.setPen(dash_pen);
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(blank_r);
                painter.restore();
                continue;
            }

            painter.save();
            // Allow modest overhang while guarding against extreme glyph bleeds
            // across distant columns.
            const int clip_pad = fontui::dp(2, m_dpr);
            painter.setClipRect(cell.adjusted(-clip_pad, -clip_pad, clip_pad,
                                              clip_pad));
            painter.setRenderHint(QPainter::TextAntialiasing, true);
            painter.setPen(selected ? pal.highlightedText().color()
                                    : pal.text().color());

            const qreal draw_x
                = !br.isEmpty()
                      ? (cell.center().x() - br.center().x())
                      : (cell.x() + (cell_px - fm.horizontalAdvance(glyph_str)) * 0.5);
            const qreal draw_y
                = !br.isEmpty()
                      ? (cell.center().y() - br.center().y())
                      : (cell.y() + (cell_px - fm.height()) * 0.5 + fm.ascent());
            painter.drawText(QPointF(draw_x, draw_y), glyph_str);
            painter.restore();
        }
    }
}

QPixmap CharacterWidget::getSelectedCharPix()
{
    if (m_last_key == 0) {
        return {};
    }
    const auto &pal   = palette();
    const qreal dpr   = qMax(m_dpr, devicePixelRatioF());
    const int side    = qMax(fontui::dp(96, m_dpr), cellSizePx() * 2);
    const QRect rt(0, 0, side, side);
    QPixmap pix(qRound(side * dpr), qRound(side * dpr));
    pix.setDevicePixelRatio(dpr);
    pix.fill(pal.base().color());
    QFont draw_ft = m_glyph_font;
    draw_ft.setPixelSize(qMax(16, qRound(side * 0.58)));
    QPainter p(&pix);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    p.setFont(draw_ft);
    p.setPen(QPen(pal.text().color()));
    p.drawText(rt, Qt::AlignCenter, QString::fromUcs4(&m_last_key, 1));
    return pix;
}

CharacterWidget::SelectedCharInfo CharacterWidget::selectedCharInfo() const
{
    if (m_last_key == 0) {
        return {};
    }
    const QChar ch = isBmpScalar(m_last_key) ? QChar(ushort(m_last_key))
                                             : QChar();
    return {m_last_key, ch, m_glyph_font, cellSizePx()};
}
