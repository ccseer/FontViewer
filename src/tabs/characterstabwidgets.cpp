#include "tabs/characterstabwidgets.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QLineEdit>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>
#include <QShowEvent>

#include "fontui.h"

CharactersToolbar::CharactersToolbar(QComboBox *block_combo,
                                     QLineEdit *filter_edit,
                                     QWidget *cell_host,
                                     QWidget *parent)
    : QWidget(parent),
      m_block_combo(block_combo),
      m_filter_edit(filter_edit),
      m_cell_host(cell_host)
{
    setFocusPolicy(Qt::NoFocus);
    QSizePolicy sp(QSizePolicy::Preferred, QSizePolicy::Preferred);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);

    for (QWidget *w :
         {static_cast<QWidget *>(m_block_combo),
          static_cast<QWidget *>(m_filter_edit), m_cell_host}) {
        if (w) {
            w->setParent(this);
        }
    }
    updateMetrics();
}

void CharactersToolbar::setDpr(qreal dpr)
{
    const qreal clamped = fontui::clampDpr(dpr);
    if (!qFuzzyCompare(m_dpr, clamped)) {
        m_dpr = clamped;
    }
    updateMetrics();
    refreshLayout();
}

bool CharactersToolbar::hasHeightForWidth() const
{
    return true;
}

int CharactersToolbar::heightForWidth(int width) const
{
    return doLayout(qMax(1, width), true);
}

QSize CharactersToolbar::sizeHint() const
{
    const int w = width() > 0 ? width() : 640;
    return QSize(w, heightForWidth(w));
}

QSize CharactersToolbar::minimumSizeHint() const
{
    return QSize(fontui::dp(140, m_dpr), singleRowHeight());
}

void CharactersToolbar::refreshLayout()
{
    updateGeometry();
    if (width() > 0) {
        doLayout(width(), false);
    }
}

void CharactersToolbar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    const int target_h = doLayout(qMax(1, width()), false);
    if (target_h != height()) {
        updateGeometry();
    }
}

void CharactersToolbar::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    const int target_h = doLayout(qMax(1, width()), false);
    if (target_h != height()) {
        updateGeometry();
    }
}

void CharactersToolbar::updateMetrics()
{
    m_margin_lr  = fontui::dp(8, m_dpr);
    m_margin_top = fontui::dp(6, m_dpr);
    m_margin_bot = fontui::dp(4, m_dpr);
    m_h_spacing  = fontui::dp(8, m_dpr);
    m_v_spacing  = fontui::dp(4, m_dpr);
    if (m_filter_edit) {
        m_filter_edit->setMinimumWidth(0);
    }
}

int CharactersToolbar::singleRowHeight() const
{
    int h = 0;
    for (QWidget *w :
         {static_cast<QWidget *>(m_block_combo),
          static_cast<QWidget *>(m_filter_edit), m_cell_host}) {
        if (w) {
            h = qMax(h, w->sizeHint().height());
        }
    }
    return h + m_margin_top + m_margin_bot;
}

int CharactersToolbar::doLayout(int avail_w, bool test_only) const
{
    const int left_x  = m_margin_lr;
    const int right_x = qMax(left_x + 1, avail_w - m_margin_lr);
    const int inner_w = right_x - left_x;

    int block_w = 0;
    if (m_block_combo) {
        const int min_b = qMax(m_block_combo->minimumSizeHint().width(),
                               fontui::dp(110, m_dpr));
        const int max_b = qMax(min_b, qMin(fontui::dp(210, m_dpr), inner_w / 2));
        block_w         = qBound(min_b, m_block_combo->sizeHint().width(), max_b);
    }
    const int cell_w       = m_cell_host ? m_cell_host->sizeHint().width() : 0;
    const int filter_min_w = m_filter_edit ? fontui::dp(120, m_dpr) : 0;

    const int single_min_w
        = block_w + m_h_spacing + filter_min_w + m_h_spacing + cell_w;
    const int ctrl_h
        = qMax(m_block_combo ? m_block_combo->sizeHint().height() : 0,
               m_filter_edit ? m_filter_edit->sizeHint().height() : 0);
    const int row_h
        = qMax(ctrl_h, m_cell_host ? m_cell_host->sizeHint().height() : 0);

    if (inner_w >= single_min_w) {
        if (!test_only) {
            const int filter_w = inner_w - block_w - cell_w - m_h_spacing * 2;
            int x              = left_x;
            auto place = [&](QWidget *w, int w_px, int h_override) {
                if (!w) {
                    return;
                }
                const int h
                    = h_override > 0 ? h_override : w->sizeHint().height();
                const int y = m_margin_top + qMax(0, (row_h - h) / 2);
                w->setGeometry(x, y, w_px, h);
                x += w_px + m_h_spacing;
            };
            place(m_block_combo, block_w, ctrl_h);
            place(m_filter_edit, filter_w, ctrl_h);
            place(m_cell_host, cell_w, 0);
        }
        return qMax(singleRowHeight(), m_margin_top + row_h + m_margin_bot);
    }

    // Two-row compact wrap: [Block] [Filter] on row 1, [Cell] on row 2,
    // folding to three stacked rows if [Block] + [Filter] would clip.
    const int row1_block_w
        = m_block_combo
              ? qMin(block_w,
                     qMin(inner_w, qMax(fontui::dp(96, m_dpr), inner_w / 2)))
              : 0;
    const int row1_filter_min = m_filter_edit ? fontui::dp(72, m_dpr) : 0;
    const bool can_two_row
        = !m_block_combo || !m_filter_edit
          || inner_w >= row1_block_w + m_h_spacing + row1_filter_min;
    const int r1_h = ctrl_h;
    const int r2_h = m_cell_host ? m_cell_host->sizeHint().height() : 0;
    if (can_two_row) {
        if (!test_only) {
            if (m_block_combo) {
                m_block_combo->setGeometry(
                    left_x, m_margin_top + qMax(0, (r1_h - ctrl_h) / 2),
                    row1_block_w, ctrl_h);
            }
            if (m_filter_edit) {
                const int fx
                    = left_x
                      + (m_block_combo ? (row1_block_w + m_h_spacing) : 0);
                const int fw = qMax(0, right_x - fx);
                m_filter_edit->setGeometry(
                    fx, m_margin_top + qMax(0, (r1_h - ctrl_h) / 2), fw,
                    ctrl_h);
            }
            if (m_cell_host) {
                const int y2 = m_margin_top + r1_h + m_v_spacing;
                m_cell_host->setGeometry(left_x, y2, qMin(inner_w, cell_w),
                                         r2_h);
            }
        }
        return m_margin_top + r1_h + m_v_spacing + r2_h + m_margin_bot;
    }

    if (!test_only) {
        int y = m_margin_top;
        if (m_block_combo) {
            m_block_combo->setGeometry(left_x, y, qMin(inner_w, block_w),
                                       ctrl_h);
            y += ctrl_h + m_v_spacing;
        }
        if (m_filter_edit) {
            m_filter_edit->setGeometry(left_x, y, inner_w, ctrl_h);
            y += ctrl_h + m_v_spacing;
        }
        if (m_cell_host) {
            m_cell_host->setGeometry(left_x, y, qMin(inner_w, cell_w), r2_h);
        }
    }
    const int rows_above = (m_block_combo ? 1 : 0) + (m_filter_edit ? 1 : 0);
    return m_margin_top + rows_above * (ctrl_h + m_v_spacing) + r2_h
           + m_margin_bot;
}

MetaCopyChip::MetaCopyChip(qreal dpr, QWidget *parent)
    : QPushButton(parent), m_dpr(fontui::clampDpr(dpr))
{
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover, true);
    setFlat(true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    syncFontFromParent();
}

void MetaCopyChip::syncFontFromParent()
{
    if (m_syncing_font) {
        return;
    }
    QFont f = parentWidget() ? parentWidget()->font() : font();
    if (const auto tnum = QFont::Tag::fromString("tnum"); tnum.has_value()) {
        f.setFeature(*tnum, 1);
    }
    if (font() != f) {
        m_syncing_font = true;
        setFont(f);
        m_syncing_font = false;
    }
    recomputeDisplay();
}

void MetaCopyChip::setDpr(qreal dpr)
{
    const qreal clamped = fontui::clampDpr(dpr);
    if (!qFuzzyCompare(m_dpr, clamped)) {
        m_dpr = clamped;
        recomputeDisplay();
    }
}

void MetaCopyChip::setFullText(const QString &value)
{
    if (m_full_text == value && m_max_width < 0) {
        return;
    }
    m_full_text = value;
    m_max_width = -1;
    recomputeDisplay();
}

void MetaCopyChip::setMaxWidth(int max_w)
{
    if (m_max_width == max_w) {
        return;
    }
    m_max_width = max_w;
    recomputeDisplay();
}

void MetaCopyChip::refreshMetrics()
{
    recomputeDisplay();
}

int MetaCopyChip::fullContentWidth() const
{
    return widthForText(m_full_text);
}

QSize MetaCopyChip::sizeHint() const
{
    const int w = widthForText(text());
    return QSize(m_max_width >= 0 ? qMin(w, m_max_width) : w,
                 heightForMetrics());
}

QSize MetaCopyChip::minimumSizeHint() const
{
    return sizeHint();
}

void MetaCopyChip::changeEvent(QEvent *event)
{
    QPushButton::changeEvent(event);
    if (!m_syncing_font
        && (event->type() == QEvent::FontChange
            || event->type() == QEvent::StyleChange)) {
        syncFontFromParent();
    }
}

void MetaCopyChip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QPalette &pal = palette();
    const bool hovered  = underMouse() || isDown();
    const QRectF rf     = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius  = fontui::surfaceRadius(m_dpr, false);

    if (isDown()) {
        QColor fill = fontui::accentColor(pal);
        fill.setAlpha(40);
        p.setBrush(fill);
        QColor border = fontui::accentColor(pal);
        border.setAlpha(150);
        p.setPen(QPen(border, fontui::dpf(1.0, m_dpr)));
        p.drawRoundedRect(rf, radius, radius);
    }
    else if (hovered) {
        QColor fill = pal.color(QPalette::WindowText);
        fill.setAlpha(fontui::isDarkPalette(pal) ? 24 : 16);
        p.setBrush(fill);
        QColor border = fontui::frameBorderColor(pal);
        border.setAlpha(140);
        p.setPen(QPen(border, fontui::dpf(1.0, m_dpr)));
        p.drawRoundedRect(rf, radius, radius);
    }

    QColor fg = isDown() ? fontui::accentColor(pal)
                         : pal.color(QPalette::WindowText);
    if (!isEnabled()) {
        fg.setAlpha(120);
    }
    p.setPen(fg);
    p.setFont(font());
    const int pad_x     = qMin(fontui::dp(4, m_dpr), qMax(0, width() / 4));
    const QRect text_rc = rect().adjusted(pad_x, 0, -pad_x, 0);
    p.drawText(text_rc, Qt::AlignCenter | Qt::TextSingleLine, text());
}

int MetaCopyChip::heightForMetrics() const
{
    const QFontMetrics fm(font());
    return qMax(fontui::dp(20, m_dpr), fm.height() + fontui::dp(4, m_dpr));
}

int MetaCopyChip::widthForText(const QString &t) const
{
    if (t.isEmpty()) {
        return 0;
    }
    const QFontMetrics fm(font());
    return fm.horizontalAdvance(t) + fontui::dp(8, m_dpr);
}

void MetaCopyChip::recomputeDisplay()
{
    if (m_full_text.isEmpty()) {
        setText(QString());
        setToolTip(QString());
        updateGeometry();
        update();
        return;
    }
    QString shown = m_full_text;
    if (m_max_width >= 0) {
        const int avail_text = qMax(1, m_max_width - fontui::dp(8, m_dpr));
        shown = fontMetrics().elidedText(m_full_text, Qt::ElideRight, avail_text);
    }
    setText(shown);
    setToolTip(shown == m_full_text
                   ? QCoreApplication::translate("CharactersTab",
                                                 "Click to copy \"%1\"")
                         .arg(m_full_text)
                   : QCoreApplication::translate("CharactersTab",
                                                 "\"%1\" — click to copy")
                         .arg(m_full_text));
    updateGeometry();
    update();
}
