#include "opentypefeaturesbar.h"

#include <QEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPushButton>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QtMath>

#include "fontui.h"

namespace {
constexpr int kFeaturePillHSpacing = 5;
constexpr int kFeaturePillVSpacing = 4;
constexpr int kDrawerMaxHeightPx   = 156;

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

QVector<QByteArray> mutexPeerTags(const QByteArray &tag)
{
    static constexpr const char *kMutexGroups[][3] = {
        {"lnum", "onum", nullptr},
        {"pnum", "tnum", nullptr},
        {"frac", "afrc", nullptr},
        {"sups", "subs", "sinf"},
    };
    for (const auto &group : kMutexGroups) {
        bool in_group = false;
        for (const char *member : group) {
            if (member && tag == member) {
                in_group = true;
                break;
            }
        }
        if (in_group) {
            QVector<QByteArray> peers;
            for (const char *member : group) {
                if (member && tag != member) {
                    peers.append(QByteArray(member));
                }
            }
            return peers;
        }
    }
    return {};
}

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
            bg = accent;
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
    qreal m_dpr         = 1.0;
    bool m_syncing_font = false;
};

class FeaturePillsContainer : public QWidget {
public:
    explicit FeaturePillsContainer(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFocusPolicy(Qt::NoFocus);
        setAutoFillBackground(false);
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

    m_note = new QLabel(this);
    m_note->setObjectName(QStringLiteral("opentype_note"));
    m_note->setFocusPolicy(Qt::NoFocus);
    // The spacing hint is long enough to exceed a narrow preview pane, so it
    // wraps instead of widening the whole viewer layout.
    m_note->setWordWrap(true);
    m_note->hide();
    m_root_layout->addWidget(m_note);

    m_body_layout = new QHBoxLayout();
    m_body_layout->setContentsMargins(0, 0, 0, 0);

    m_scroll_area = new QScrollArea(this);
    m_scroll_area->setObjectName(QStringLiteral("opentype_features_scroll"));
    m_scroll_area->setFrameShape(QFrame::NoFrame);
    m_scroll_area->setWidgetResizable(true);
    m_scroll_area->setFocusPolicy(Qt::NoFocus);
    m_scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll_area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll_area->viewport()->setAutoFillBackground(false);
    m_scroll_area->verticalScrollBar()->setProperty("is_readonly", true);
    m_scroll_area->verticalScrollBar()->setFocusPolicy(Qt::NoFocus);
    m_scroll_area->horizontalScrollBar()->setProperty("is_readonly", true);
    m_scroll_area->horizontalScrollBar()->setFocusPolicy(Qt::NoFocus);

    m_pills_host = new FeaturePillsContainer(m_scroll_area);
    new fontui::FlowLayout(m_pills_host,
                           fontui::dp(kFeaturePillHSpacing, m_dpr),
                           fontui::dp(kFeaturePillVSpacing, m_dpr));
    m_scroll_area->setWidget(m_pills_host);
    m_body_layout->addWidget(m_scroll_area, 1);

    m_reset = new fontui::CompactActionButton(tr("Clear"), m_dpr, this);
    m_reset->setObjectName(QStringLiteral("opentype_reset_btn"));
    m_reset->setToolTip(tr("Clear OpenType features"));
    QSizePolicy reset_sp = m_reset->sizePolicy();
    // Reserve Clear's slot in the top-right corner so toggling a pill never
    // reflows the pill rows or shifts vertical layout height.
    reset_sp.setRetainSizeWhenHidden(true);
    m_reset->setSizePolicy(reset_sp);
    m_reset->hide();
    connect(m_reset, &QPushButton::clicked, this, [this]() {
        resetOverrides();
        emit sigInteractionClearFocus();
    });
    m_body_layout->addWidget(m_reset, 0, Qt::AlignTop | Qt::AlignRight);
    m_root_layout->addLayout(m_body_layout);

    applyPalette();
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
    updateScrollAreaHeight();
}

void OpenTypeFeaturesBar::updateLayoutMetrics()
{
    if (m_root_layout) {
        const int lr = fontui::dp(8, m_dpr);
        m_root_layout->setContentsMargins(lr, fontui::dp(2, m_dpr), lr,
                                          fontui::dp(4, m_dpr));
        m_root_layout->setSpacing(fontui::dp(4, m_dpr));
    }
    if (m_body_layout) {
        m_body_layout->setSpacing(fontui::dp(8, m_dpr));
    }
    if (m_reset) {
        static_cast<fontui::CompactActionButton *>(m_reset)->setDpr(m_dpr);
    }
    for (fontui::FlowSeparator *sep : m_separators) {
        if (sep) {
            sep->setDpr(m_dpr);
        }
    }
    if (m_pills_host) {
        if (auto *flow
            = static_cast<fontui::FlowLayout *>(m_pills_host->layout())) {
            flow->setSpacingPair(fontui::dp(kFeaturePillHSpacing, m_dpr),
                                 fontui::dp(kFeaturePillVSpacing, m_dpr));
        }
        for (const FeaturePillControl &ctrl : m_controls) {
            if (auto *btn = static_cast<FeaturePillButton *>(ctrl.button)) {
                btn->setDpr(m_dpr);
                btn->syncFont();
            }
        }
        m_pills_host->updateGeometry();
    }
    if (m_body_layout) {
        m_body_layout->invalidate();
    }
    if (m_root_layout) {
        m_root_layout->invalidate();
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
    if (m_note) {
        QPalette note_pal = palette();
        note_pal.setColor(QPalette::WindowText,
                          fontui::mutedTextColor(palette()));
        m_note->setPalette(note_pal);
    }
    if (m_reset) {
        m_reset->update();
    }
    for (const FeaturePillControl &ctrl : m_controls) {
        if (ctrl.button) {
            ctrl.button->update();
        }
    }
    for (fontui::FlowSeparator *sep : m_separators) {
        if (sep) {
            sep->update();
        }
    }
}

void OpenTypeFeaturesBar::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::StyleChange
        || event->type() == QEvent::FontChange) {
        updateLayoutMetrics();
        updateScrollAreaHeight();
        applyPalette();
    }
    else if (event->type() == QEvent::PaletteChange
             || event->type() == QEvent::ApplicationPaletteChange) {
        applyPalette();
    }
}

void OpenTypeFeaturesBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateScrollAreaHeight();
}

void OpenTypeFeaturesBar::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateScrollAreaHeight();
    if (m_body_layout) {
        m_body_layout->invalidate();
    }
    if (m_root_layout) {
        m_root_layout->invalidate();
        m_root_layout->activate();
    }
    updateGeometry();
    if (QWidget *p = parentWidget()) {
        if (QLayout *pl = p->layout()) {
            pl->invalidate();
            pl->activate();
        }
    }
}

void OpenTypeFeaturesBar::setResetShortcutHint(const QString &hint)
{
    m_reset_hint = hint;
    if (m_reset) {
        const QString base = tr("Clear OpenType features");
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

    for (fontui::FlowSeparator *sep : m_separators) {
        if (sep) {
            sep->hide();
            sep->deleteLater();
        }
    }
    m_separators.clear();

    if (auto *flow = m_pills_host->layout()) {
        while (QLayoutItem *item = flow->takeAt(0)) {
            delete item;
        }
    }

    if (features.isEmpty()) {
        updatePillsState();
        updateScrollAreaHeight();
        return;
    }

    auto *flow     = m_pills_host->layout();
    int prev_group = -1;
    for (const FontFeatureInfo &info : features) {
        const int group = FontInspectorEngine::featureGroupIndex(info.tag);
        if (prev_group != -1 && group != prev_group) {
            auto *sep = new fontui::FlowSeparator(m_dpr, m_pills_host);
            flow->addWidget(sep);
            m_separators.append(sep);
        }
        prev_group = group;

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
        connect(btn, &QPushButton::toggled, this, [this, tag](bool on) {
            applyToggleWithMutex(tag, on);
            emit sigInteractionClearFocus();
        });

        flow->addWidget(btn);
        m_controls.append(
            {tag, def_on, isLigatureFeatureTag(tag), tip, btn});
    }

    updatePillsState();
    m_pills_host->updateGeometry();
    updateScrollAreaHeight();
    if (m_scroll_area && m_scroll_area->verticalScrollBar()) {
        m_scroll_area->verticalScrollBar()->setValue(0);
    }
    updateGeometry();
}

void OpenTypeFeaturesBar::applyToggleWithMutex(const QByteArray &tag,
                                               bool enabled)
{
    bool found      = false;
    bool def_on     = false;
    bool btn_dirty  = false;
    for (const FeaturePillControl &ctrl : m_controls) {
        if (ctrl.tag == tag) {
            found  = true;
            def_on = ctrl.default_on;
            if (ctrl.button && ctrl.button->isChecked() != enabled) {
                btn_dirty = true;
                QSignalBlocker blocker(ctrl.button);
                ctrl.button->setChecked(enabled);
            }
            break;
        }
    }
    if (!found) {
        return;
    }

    const bool current_on = m_overrides.value(tag, def_on);
    QVector<int> dirty_peers;
    if (enabled) {
        const QVector<QByteArray> peers = mutexPeerTags(tag);
        for (int i = 0; i < m_controls.size(); ++i) {
            const FeaturePillControl &peer = m_controls.at(i);
            if (!peers.contains(peer.tag)) {
                continue;
            }
            const bool peer_on = m_overrides.value(peer.tag, peer.default_on);
            if (peer_on || (peer.button && peer.button->isChecked())) {
                dirty_peers.append(i);
            }
        }
    }
    if (current_on == enabled && !btn_dirty && dirty_peers.isEmpty()) {
        return;
    }

    if (enabled == def_on) {
        m_overrides.remove(tag);
    }
    else {
        m_overrides.insert(tag, enabled);
    }

    for (int idx : dirty_peers) {
        const FeaturePillControl &peer = m_controls.at(idx);
        if (peer.button) {
            QSignalBlocker blocker(peer.button);
            peer.button->setChecked(false);
        }
        if (!peer.default_on) {
            m_overrides.remove(peer.tag);
        }
        else {
            m_overrides.insert(peer.tag, false);
        }
    }

    updatePillsState();
    emit sigFeaturesChanged();
}

void OpenTypeFeaturesBar::setLetterSpacingActive(bool active)
{
    m_spacing_active = active;
    updatePillsState();
}

void OpenTypeFeaturesBar::setFeatureEnabled(const QByteArray &tag, bool enabled)
{
    applyToggleWithMutex(tag, enabled);
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
    const bool show_note = m_spacing_active && has_ligature;
    if (m_note) {
        if (show_note) {
            m_note->setText(
                tr("Ligatures disabled while letter spacing ≠ 0"));
        }
        else {
            m_note->clear();
        }
        if (m_note->isVisibleTo(this) != show_note) {
            m_note->setVisible(show_note);
            updateGeometry();
        }
    }
    if (m_reset) {
        m_reset->setVisible(!m_overrides.isEmpty());
    }
}

void OpenTypeFeaturesBar::updateScrollAreaHeight()
{
    if (m_updating_scroll_height || !m_scroll_area || !m_pills_host) {
        return;
    }
    QScopedValueRollback<bool> guard(m_updating_scroll_height, true);

    if (m_controls.isEmpty()) {
        if (m_scroll_area->minimumHeight() != 0
            || m_scroll_area->maximumHeight() != 0) {
            m_scroll_area->setFixedHeight(0);
        }
        return;
    }

    const int margin_lr
        = m_root_layout
              ? (m_root_layout->contentsMargins().left()
                 + m_root_layout->contentsMargins().right())
              : 0;
    int ref_w = width();
    if (QWidget *p = parentWidget();
        p && p->isVisible() && p->width() > 0) {
        int parent_w = p->width();
        if (QLayout *pl = p->layout()) {
            const QMargins pm = pl->contentsMargins();
            parent_w          = qMax(1, parent_w - pm.left() - pm.right());
        }
        if (!isVisible() || ref_w <= 0 || ref_w != parent_w) {
            ref_w = parent_w;
        }
    }
    const int body_gap = m_body_layout ? m_body_layout->spacing() : 0;
    const int reset_w
        = m_reset ? (m_reset->sizeHint().width() + body_gap) : 0;
    int avail_w = ref_w > 0 ? qMax(1, ref_w - margin_lr - reset_w) : 0;
    if (avail_w <= 0) {
        avail_w = m_scroll_area->width() > 0 ? m_scroll_area->width() : 400;
    }

    const int max_h     = fontui::dp(kDrawerMaxHeightPx, m_dpr);
    const int natural_h = m_pills_host->heightForWidth(avail_w);
    const bool need_sb  = (natural_h > max_h);
    const int target_h  = need_sb ? max_h : natural_h;

    const bool h_changed = (m_scroll_area->minimumHeight() != target_h
                            || m_scroll_area->maximumHeight() != target_h);
    if (h_changed) {
        m_scroll_area->setFixedHeight(target_h);
    }

    const int sb_w = (need_sb && m_scroll_area->verticalScrollBar())
                         ? m_scroll_area->verticalScrollBar()->sizeHint().width()
                         : 0;
    const int vp_w   = qMax(1, avail_w - sb_w);
    const int host_h = qMax(target_h, m_pills_host->heightForWidth(vp_w));
    if (m_pills_host->width() != vp_w || m_pills_host->height() != host_h) {
        m_pills_host->resize(vp_w, host_h);
    }
    m_pills_host->updateGeometry();
    m_scroll_area->updateGeometry();
    if (h_changed) {
        if (m_body_layout) {
            m_body_layout->invalidate();
        }
        if (m_root_layout) {
            m_root_layout->invalidate();
            if (isVisible()) {
                m_root_layout->activate();
            }
        }
        updateGeometry();
    }
}
