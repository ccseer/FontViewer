#include "opentypefeaturesbar.h"

#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QtMath>

#include "fontui.h"

namespace {
constexpr int kFeaturePillHSpacing = 5;
constexpr int kFeaturePillVSpacing = 4;

QString formatFeaturePillText(const FontFeatureInfo &info,
                              const QFontMetrics &fm,
                              qreal dpr)
{
    const QString tag_str = QString::fromLatin1(info.tag);
    if (info.friendly_name.isEmpty() || info.friendly_name == tag_str) {
        return tag_str;
    }
    const QString label = fm.elidedText(info.friendly_name, Qt::ElideRight,
                                        fontui::dp(120, dpr));
    return QStringLiteral("%1 · %2").arg(tag_str, label);
}

class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget *parent = nullptr, int h_spacing = 5,
                        int v_spacing = 4)
        : QLayout(parent), m_h_spacing(h_spacing), m_v_spacing(v_spacing)
    {
        setContentsMargins(0, 0, 0, 0);
    }

    ~FlowLayout() override
    {
        QLayoutItem *item = nullptr;
        while ((item = takeAt(0)) != nullptr) {
            delete item;
        }
    }

    void setSpacingPair(int h_spacing, int v_spacing)
    {
        if (m_h_spacing == h_spacing && m_v_spacing == v_spacing) {
            return;
        }
        m_h_spacing = h_spacing;
        m_v_spacing = v_spacing;
        invalidate();
    }

    void addItem(QLayoutItem *item) override
    {
        m_items.append(item);
    }

    int count() const override
    {
        return m_items.size();
    }

    QLayoutItem *itemAt(int index) const override
    {
        return m_items.value(index);
    }

    QLayoutItem *takeAt(int index) override
    {
        if (index >= 0 && index < m_items.size()) {
            return m_items.takeAt(index);
        }
        return nullptr;
    }

    Qt::Orientations expandingDirections() const override
    {
        return {};
    }

    bool hasHeightForWidth() const override
    {
        return true;
    }

    int heightForWidth(int width) const override
    {
        return doLayout(QRect(0, 0, qMax(1, width), 0), true);
    }

    void setGeometry(const QRect &rect) override
    {
        QLayout::setGeometry(rect);
        doLayout(rect, false);
    }

    QSize sizeHint() const override
    {
        return minimumSize();
    }

    QSize minimumSize() const override
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

private:
    int doLayout(const QRect &rect, bool test_only) const
    {
        int left   = 0;
        int top    = 0;
        int right  = 0;
        int bottom = 0;
        getContentsMargins(&left, &top, &right, &bottom);
        const QRect effective = rect.adjusted(left, top, -right, -bottom);
        int x                 = effective.x();
        int y                 = effective.y();
        int line_height       = 0;

        for (QLayoutItem *item : m_items) {
            if (item->isEmpty()) {
                continue;
            }
            const QSize item_sz = item->sizeHint();
            int next_x          = x + item_sz.width() + m_h_spacing;
            if (next_x - m_h_spacing - 1 > effective.right()
                && line_height > 0) {
                x           = effective.x();
                y           = y + line_height + m_v_spacing;
                next_x      = x + item_sz.width() + m_h_spacing;
                line_height = 0;
            }
            if (!test_only) {
                item->setGeometry(QRect(QPoint(x, y), item_sz));
            }
            x           = next_x;
            line_height = qMax(line_height, item_sz.height());
        }
        return y + line_height - rect.y() + bottom;
    }

    QVector<QLayoutItem *> m_items;
    int m_h_spacing = 5;
    int m_v_spacing = 4;
};

class FeaturePillButton : public QPushButton {
public:
    explicit FeaturePillButton(const FontFeatureInfo &info,
                               qreal dpr,
                               QWidget *parent = nullptr)
        : QPushButton(parent), m_info(info), m_dpr(fontui::clampDpr(dpr))
    {
        setCheckable(true);
        setChecked(info.default_on);
        setFocusPolicy(Qt::NoFocus);
        // paintEvent() reads underMouse(), but hover enter/leave events (and the
        // repaint they trigger) are only delivered when WA_Hover is set.
        setAttribute(Qt::WA_Hover, true);
        QFont f = font();
        f.setPointSizeF(qMax(8.0, f.pointSizeF() - 1.0));
        setFont(f);
        setText(formatFeaturePillText(m_info, fontMetrics(), m_dpr));
        setShortcut(QKeySequence());
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    void setDpr(qreal dpr)
    {
        const qreal clamped = fontui::clampDpr(dpr);
        if (qFuzzyCompare(m_dpr, clamped)) {
            return;
        }
        m_dpr = clamped;
        setText(formatFeaturePillText(m_info, fontMetrics(), m_dpr));
        updateGeometry();
        update();
    }

    QSize sizeHint() const override
    {
        const QFontMetrics fm = fontMetrics();
        const int pad_h       = fontui::dp(10, m_dpr);
        const int pad_v       = fontui::dp(3, m_dpr);
        const int border_w    = qCeil(fontui::dpf(1.0, m_dpr));
        const int mark_sz     = checkMarkSize(fm);
        const int gap         = fontui::dp(4, m_dpr);
        // Reserve checkmark width and border stroke for all pills so toggling
        // never reflows or crowds the checked label.
        return QSize(pad_h * 2 + border_w + mark_sz + gap
                         + fm.horizontalAdvance(text()),
                     fm.height() + pad_v * 2);
    }

    QSize minimumSizeHint() const override
    {
        return sizeHint();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QPalette &pal = palette();
        const qreal pen_w   = fontui::dpf(1.0, m_dpr);
        const QRectF r = QRectF(rect()).adjusted(pen_w * 0.5, pen_w * 0.5,
                                                 -pen_w * 0.5, -pen_w * 0.5);
        const qreal radius  = r.height() / 2.0;
        const bool enabled  = isEnabled();
        const bool checked  = isChecked();
        const bool dark     = fontui::isDarkPalette(pal);
        const bool hovered  = enabled && underMouse();

        if (!enabled) {
            painter.setOpacity(0.45);
        }

        const QColor fg_base = pal.color(QPalette::WindowText);
        const QColor accent  = fontui::accentColor(pal);

        QColor bg;
        QColor border;
        QColor fg = fg_base;

        if (checked) {
            bg     = accent;
            bg.setAlpha(hovered ? (dark ? 68 : 44) : (dark ? 48 : 28));
            border = accent;
            if (!hovered) {
                border.setAlpha(dark ? 220 : 200);
            }
        }
        else {
            // Derive subtle surfaces from WindowText rather than QPalette::Button,
            // which Host sets to transparent (0, 0, 0, 0) in MaterialStyle.
            bg = fg_base;
            bg.setAlpha(hovered ? (dark ? 30 : 20) : (dark ? 15 : 10));
            border = fg_base;
            border.setAlpha(hovered ? (dark ? 96 : 82) : (dark ? 44 : 36));
            fg.setAlpha(hovered ? 255 : 215);
        }

        painter.setPen(QPen(border, pen_w));
        painter.setBrush(bg);
        painter.drawRoundedRect(r, radius, radius);

        const QFontMetrics fm = fontMetrics();
        if (checked) {
            const int pad_h     = fontui::dp(10, m_dpr);
            const qreal mark_sz = checkMarkSize(fm);
            const qreal gap     = fontui::dpf(4.0, m_dpr);
            const QRectF mark(r.left() + pad_h,
                              r.center().y() - mark_sz * 0.5, mark_sz, mark_sz);

            QPainterPath check;
            check.moveTo(mark.left() + mark_sz * 0.14,
                         mark.top() + mark_sz * 0.54);
            check.lineTo(mark.left() + mark_sz * 0.40,
                         mark.top() + mark_sz * 0.78);
            check.lineTo(mark.left() + mark_sz * 0.88,
                         mark.top() + mark_sz * 0.24);
            QPen check_pen(accent, fontui::dpf(1.6, m_dpr), Qt::SolidLine,
                           Qt::RoundCap, Qt::RoundJoin);
            painter.setPen(check_pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(check);

            const QRectF text_rect(
                mark.right() + gap, r.top(),
                qMax(0.0, r.right() - pad_h - (mark.right() + gap)),
                r.height());
            painter.setPen(fg);
            painter.setFont(font());
            painter.drawText(text_rect, Qt::AlignLeft | Qt::AlignVCenter,
                             text());
        }
        else {
            painter.setPen(fg);
            painter.setFont(font());
            painter.drawText(r, Qt::AlignCenter, text());
        }
    }

private:
    int checkMarkSize(const QFontMetrics &fm) const
    {
        return qMax(fontui::dp(10, m_dpr), qRound(fm.height() * 0.65));
    }

    FontFeatureInfo m_info;
    qreal m_dpr = 1.0;
};

class FeaturePillsContainer : public QWidget {
public:
    explicit FeaturePillsContainer(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFocusPolicy(Qt::NoFocus);
        QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Preferred);
        sp.setHeightForWidth(true);
        setSizePolicy(sp);
    }

    bool hasHeightForWidth() const override
    {
        return layout() != nullptr && layout()->hasHeightForWidth();
    }

    int heightForWidth(int width) const override
    {
        if (!layout() || layout()->count() == 0) {
            return 0;
        }
        return layout()->heightForWidth(qMax(1, width));
    }

    QSize sizeHint() const override
    {
        const int w = width() > 0 ? width() : 400;
        return QSize(w, heightForWidth(w));
    }

    QSize minimumSizeHint() const override
    {
        const int w = width() > 0 ? width() : 200;
        return QSize(0, heightForWidth(w));
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        updateGeometry();
        if (QWidget *p = parentWidget()) {
            p->updateGeometry();
        }
    }
};
}  // namespace

OpenTypeFeaturesBar::OpenTypeFeaturesBar(QWidget *parent) : QWidget(parent)
{
    setFocusPolicy(Qt::NoFocus);

    m_root_layout = new QVBoxLayout(this);

    m_header_layout = new QHBoxLayout();
    m_header_layout->setContentsMargins(0, 0, 0, 0);

    auto *title = new QLabel(tr("OpenType Features:"), this);
    title->setFocusPolicy(Qt::NoFocus);
    m_header_layout->addWidget(title);

    m_note = new QLabel(tr("Text preview only"), this);
    m_note->setFocusPolicy(Qt::NoFocus);
    // The spacing hint is long enough to exceed a narrow preview pane, so it
    // wraps instead of widening the whole viewer layout.
    m_note->setWordWrap(true);
    applyPalette();
    m_header_layout->addWidget(m_note, 1);

    m_reset = new fontui::CompactActionButton(tr("Reset"), m_dpr, this);
    m_reset->setToolTip(tr("Reset OpenType features"));
    m_reset->hide();
    connect(m_reset, &QPushButton::clicked, this, [this]() {
        resetOverrides();
        emit sigInteractionClearFocus();
    });
    m_header_layout->addWidget(m_reset);
    m_root_layout->addLayout(m_header_layout);

    m_pills_host = new FeaturePillsContainer(this);
    new FlowLayout(m_pills_host, fontui::dp(kFeaturePillHSpacing, m_dpr),
                   fontui::dp(kFeaturePillVSpacing, m_dpr));
    m_root_layout->addWidget(m_pills_host);

    updateLayoutMetrics();
    hide();
}

void OpenTypeFeaturesBar::setDpr(qreal dpr)
{
    const qreal clamped = fontui::clampDpr(dpr);
    if (qFuzzyCompare(m_dpr, clamped)) {
        return;
    }
    m_dpr = clamped;
    updateLayoutMetrics();
}

void OpenTypeFeaturesBar::updateLayoutMetrics()
{
    if (m_root_layout) {
        m_root_layout->setContentsMargins(0, fontui::dp(2, m_dpr), 0,
                                          fontui::dp(2, m_dpr));
        m_root_layout->setSpacing(fontui::dp(4, m_dpr));
    }
    if (m_header_layout) {
        m_header_layout->setSpacing(fontui::dp(8, m_dpr));
    }
    if (m_reset) {
        static_cast<fontui::CompactActionButton *>(m_reset)->setDpr(m_dpr);
    }
    if (m_pills_host) {
        if (auto *flow = static_cast<FlowLayout *>(m_pills_host->layout())) {
            flow->setSpacingPair(fontui::dp(kFeaturePillHSpacing, m_dpr),
                                 fontui::dp(kFeaturePillVSpacing, m_dpr));
        }
        for (const FeaturePillControl &ctrl : m_controls) {
            if (auto *btn = static_cast<FeaturePillButton *>(ctrl.button)) {
                btn->setDpr(m_dpr);
            }
        }
        m_pills_host->updateGeometry();
    }
    updateGeometry();
}

bool OpenTypeFeaturesBar::isLigatureFeatureTag(const QByteArray &tag)
{
    return tag == QByteArrayLiteral("liga")
           || tag == QByteArrayLiteral("clig")
           || tag == QByteArrayLiteral("dlig")
           || tag == QByteArrayLiteral("hlig");
}

void OpenTypeFeaturesBar::applyPalette()
{
    if (!m_note) {
        return;
    }
    QPalette note_pal = palette();
    note_pal.setColor(QPalette::WindowText,
                      fontui::mutedTextColor(palette()));
    m_note->setPalette(note_pal);
}

void OpenTypeFeaturesBar::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange
        || event->type() == QEvent::ApplicationPaletteChange) {
        applyPalette();
    }
}

void OpenTypeFeaturesBar::setResetShortcutHint(const QString &hint)
{
    m_reset_hint = hint;
    if (m_reset) {
        const QString base = tr("Reset OpenType features");
        m_reset->setToolTip(
            hint.isEmpty() ? base : QStringLiteral("%1 (%2)").arg(base, hint));
    }
}

void OpenTypeFeaturesBar::rebuild(const QVector<FontFeatureInfo> &features)
{
    if (!m_pills_host) {
        return;
    }

    m_overrides.clear();
    for (const FeaturePillControl &ctrl : m_controls) {
        if (ctrl.button) {
            ctrl.button->hide();
            ctrl.button->deleteLater();
        }
    }
    m_controls.clear();

    if (auto *flow = m_pills_host->layout()) {
        while (QLayoutItem *item = flow->takeAt(0)) {
            delete item;
        }
    }

    if (features.isEmpty()) {
        updatePillsState();
        hide();
        return;
    }

    auto *flow = m_pills_host->layout();
    for (const FontFeatureInfo &info : features) {
        auto *btn             = new FeaturePillButton(info, m_dpr, m_pills_host);
        const QString tag_str = QString::fromLatin1(info.tag);
        const QString tip
            = QStringLiteral("%1 — %2 (%3)")
                  .arg(tag_str,
                       info.friendly_name.isEmpty() ? tag_str
                                                    : info.friendly_name,
                       info.default_on ? tr("default on") : tr("default off"));
        btn->setToolTip(tip);

        const QByteArray tag = info.tag;
        const bool def_on    = info.default_on;
        connect(btn, &QPushButton::toggled, this, [this, tag, def_on](bool on) {
            if (on == def_on) {
                m_overrides.remove(tag);
            }
            else {
                m_overrides.insert(tag, on);
            }
            updatePillsState();
            emit sigFeaturesChanged();
            emit sigInteractionClearFocus();
        });

        flow->addWidget(btn);
        m_controls.append(
            {tag, def_on, isLigatureFeatureTag(tag), tip, btn});
    }

    updatePillsState();
    show();
    m_pills_host->updateGeometry();
    updateGeometry();
}

void OpenTypeFeaturesBar::setLetterSpacingActive(bool active)
{
    m_spacing_active = active;
    updatePillsState();
}

void OpenTypeFeaturesBar::setFeatureEnabled(const QByteArray &tag, bool enabled)
{
    bool found  = false;
    bool def_on = false;
    for (const FeaturePillControl &ctrl : m_controls) {
        if (ctrl.tag == tag) {
            found  = true;
            def_on = ctrl.default_on;
            if (ctrl.button) {
                QSignalBlocker blocker(ctrl.button);
                ctrl.button->setChecked(enabled);
            }
            break;
        }
    }
    if (!found) {
        return;
    }
    if (enabled == def_on) {
        m_overrides.remove(tag);
    }
    else {
        m_overrides.insert(tag, enabled);
    }
    updatePillsState();
    emit sigFeaturesChanged();
}

bool OpenTypeFeaturesBar::isFeatureEnabled(const QByteArray &tag) const
{
    for (const FeaturePillControl &ctrl : m_controls) {
        if (ctrl.tag == tag) {
            return m_overrides.value(tag, ctrl.default_on);
        }
    }
    return false;
}

void OpenTypeFeaturesBar::resetOverrides()
{
    if (m_overrides.isEmpty()) {
        return;
    }
    m_overrides.clear();
    for (const FeaturePillControl &ctrl : m_controls) {
        if (ctrl.button) {
            QSignalBlocker blocker(ctrl.button);
            ctrl.button->setChecked(ctrl.default_on);
        }
    }
    updatePillsState();
    emit sigFeaturesChanged();
}

QVector<QPushButton *> OpenTypeFeaturesBar::pillButtons() const
{
    QVector<QPushButton *> result;
    result.reserve(m_controls.size());
    for (const FeaturePillControl &ctrl : m_controls) {
        if (ctrl.button) {
            result.append(ctrl.button);
        }
    }
    return result;
}

void OpenTypeFeaturesBar::updatePillsState()
{
    bool has_ligature = false;
    for (const FeaturePillControl &ctrl : m_controls) {
        if (!ctrl.button) {
            continue;
        }
        const bool disabled_by_spacing = m_spacing_active && ctrl.is_ligature;
        ctrl.button->setEnabled(!disabled_by_spacing);
        if (ctrl.is_ligature) {
            has_ligature = true;
            ctrl.button->setToolTip(
                disabled_by_spacing
                    ? ctrl.tooltip + QLatin1Char('\n')
                          + tr("Disabled by Qt while letter spacing is non-zero")
                    : ctrl.tooltip);
        }
    }
    if (m_note) {
        if (m_spacing_active && has_ligature) {
            m_note->setText(
                tr("Text preview only · Ligatures disabled while letter spacing ≠ 0"));
        }
        else {
            m_note->setText(tr("Text preview only"));
        }
    }
    if (m_reset) {
        m_reset->setVisible(!m_overrides.isEmpty());
    }
}
