#include "textpreviewtab.h"

#include <QAbstractTextDocumentLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QStyleOptionComboBox>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtMath>
#include <QtNumeric>

#include <algorithm>
#include <iterator>

#include "bars/opentypefeaturesbar.h"
#include "fontpreviewedit.h"
#include "fontui.h"
#include "waterfallwidget.h"

namespace {
constexpr auto g_def_sample
    = "It is the time you have wasted for your rose that makes your "
      "rose so important.";

constexpr auto g_preset_pangram = "The quick brown fox jumps over the lazy dog.";
constexpr auto g_preset_paragraph
    = "It is the time you have wasted for your rose that makes your rose so "
      "important.\n"
      "Perfection is achieved, not when there is nothing more to add, but when "
      "there is nothing left to take away.";
constexpr auto g_preset_symbols = "0123456789 1/2 3/4 fi fl -> != ===";

constexpr int kWaterfallSizes[] = {10, 12, 14, 16, 18, 24, 28, 36, 48, 72};
constexpr int kMaxCustomPresetTitleChars = 20;

QString truncatePreservingSurrogates(const QString &text, int max_chars)
{
    if (max_chars <= 0) {
        return {};
    }
    if (text.size() <= max_chars) {
        return text;
    }
    int cut = max_chars;
    if (cut > 0 && text.at(cut - 1).isHighSurrogate()) {
        --cut;
    }
    return text.left(cut);
}

QString formatPresetTitle(const QString &title)
{
    if (title.size() <= kMaxCustomPresetTitleChars) {
        return title;
    }
    return truncatePreservingSurrogates(title, kMaxCustomPresetTitleChars - 1)
           + QChar(0x2026);
}

// Host-aligned floating popup container for compact Aa line height / spacing
// sliders.
class AaPopupPanel : public QFrame {
public:
    explicit AaPopupPanel(QWidget *parent = nullptr) : QFrame(parent)
    {
        setFrameShape(QFrame::StyledPanel);
        setFrameShadow(QFrame::Raised);
        setFocusPolicy(Qt::NoFocus);
    }

    void setDpr(qreal dpr)
    {
        const qreal clamped = fontui::clampDpr(dpr);
        if (qFuzzyCompare(m_dpr, clamped)) {
            return;
        }
        m_dpr = clamped;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QPalette &pal = palette();
        const bool dark     = fontui::isDarkPalette(pal);
        const QColor bg     = dark ? pal.color(QPalette::ToolTipBase)
                                   : pal.color(QPalette::Base);
        const QColor border = fontui::frameBorderColor(pal);

        const qreal pen_w  = fontui::dpf(1.0, m_dpr);
        const qreal radius = fontui::surfaceRadius(m_dpr, true);
        const QRectF r = QRectF(rect()).adjusted(pen_w * 0.5, pen_w * 0.5,
                                                 -pen_w * 0.5, -pen_w * 0.5);
        painter.setPen(QPen(border, pen_w));
        painter.setBrush(bg);
        if (radius > 0.0) {
            painter.drawRoundedRect(r, radius, radius);
        }
        else {
            painter.drawRect(r);
        }
    }

private:
    qreal m_dpr = 1.0;
};

// Keeps the Text tab toolbar on a single row when wide, moves line height and
// letter spacing into a non-modal Aa panel when narrow, and wraps secondary
// controls onto a second row below collapsedWidth() so controls never clip.
class TextToolbar : public QWidget {
public:
    TextToolbar(QComboBox *preset,
                QWidget *line_host,
                QWidget *space_host,
                QPushButton *aa_btn,
                QPushButton *features_btn,
                QPushButton *invert_btn,
                QCheckBox *waterfall_chk,
                QPushButton *reset_btn,
                QFrame *aa_panel,
                QWidget *parent = nullptr)
        : QWidget(parent),
          m_preset(preset),
          m_line_host(line_host),
          m_space_host(space_host),
          m_aa_btn(aa_btn),
          m_features_btn(features_btn),
          m_invert_btn(invert_btn),
          m_waterfall_chk(waterfall_chk),
          m_reset_btn(reset_btn),
          m_aa_panel(aa_panel)
    {
        setFocusPolicy(Qt::NoFocus);
        QSizePolicy sp(QSizePolicy::Preferred, QSizePolicy::Preferred);
        sp.setHeightForWidth(true);
        setSizePolicy(sp);

        for (QWidget *w : {static_cast<QWidget *>(m_preset), m_line_host,
                           m_space_host, static_cast<QWidget *>(m_aa_btn),
                           static_cast<QWidget *>(m_features_btn),
                           static_cast<QWidget *>(m_invert_btn),
                           static_cast<QWidget *>(m_waterfall_chk),
                           static_cast<QWidget *>(m_reset_btn)}) {
            if (w) {
                w->setParent(this);
            }
        }

        m_aa_layout = new QVBoxLayout(m_aa_panel);
        updateMetrics();

        m_aa_btn->hide();
    }

    void setDpr(qreal dpr)
    {
        const qreal clamped = fontui::clampDpr(dpr);
        if (qFuzzyCompare(m_dpr, clamped)) {
            return;
        }
        m_dpr = clamped;
        updateMetrics();
        refreshLayout();
    }

    bool hasHeightForWidth() const override
    {
        return true;
    }

    int heightForWidth(int width) const override
    {
        return doLayout(qMax(1, width), true);
    }

    QSize sizeHint() const override
    {
        const int w = width() > 0 ? width() : fullInlineWidth();
        return QSize(fullInlineWidth(), heightForWidth(w));
    }

    QSize minimumSizeHint() const override
    {
        int max_item_w = 0;
        for (QWidget *w :
             {static_cast<QWidget *>(m_preset), static_cast<QWidget *>(m_aa_btn),
              static_cast<QWidget *>(m_features_btn),
              static_cast<QWidget *>(m_invert_btn),
              static_cast<QWidget *>(m_waterfall_chk),
              static_cast<QWidget *>(m_reset_btn)}) {
            if (w && (w == m_preset || w == m_aa_btn || w == m_invert_btn
                      || w == m_waterfall_chk || isWidgetShown(w))) {
                max_item_w = qMax(max_item_w, w->minimumSizeHint().width());
            }
        }
        return QSize(max_item_w + m_margin_lr * 2, singleRowHeight());
    }

    void setOnLayoutChanged(std::function<void()> cb)
    {
        m_on_layout_changed = std::move(cb);
    }

    void refreshLayout()
    {
        const int w = width() > 0 ? width() : fullInlineWidth();
        updateMode(w);
        doLayout(w, false);
        updateGeometry();
        if (m_on_layout_changed) {
            m_on_layout_changed();
        }
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        syncLayoutAndGeometry();
    }

    void showEvent(QShowEvent *event) override
    {
        QWidget::showEvent(event);
        syncLayoutAndGeometry();
    }

private:
    void syncLayoutAndGeometry()
    {
        const int w = width() > 0 ? width() : fullInlineWidth();
        updateMode(w);
        const int target_h = doLayout(w, false);
        if (target_h != height()) {
            updateGeometry();
        }
        if (m_on_layout_changed) {
            m_on_layout_changed();
        }
    }
    static bool isWidgetShown(const QWidget *w)
    {
        return w && !w->isHidden();
    }

    void updateMetrics()
    {
        m_margin_lr   = fontui::dp(8, m_dpr);
        m_margin_top  = fontui::dp(6, m_dpr);
        m_margin_bot  = fontui::dp(4, m_dpr);
        m_h_spacing   = fontui::dp(8, m_dpr);
        m_v_spacing   = fontui::dp(4, m_dpr);
        if (m_aa_layout) {
            m_aa_layout->setContentsMargins(
                fontui::dp(10, m_dpr), fontui::dp(8, m_dpr),
                fontui::dp(10, m_dpr), fontui::dp(8, m_dpr));
            m_aa_layout->setSpacing(fontui::dp(6, m_dpr));
        }
        if (m_reset_btn) {
            static_cast<fontui::CompactActionButton *>(m_reset_btn)
                ->setDpr(m_dpr);
        }
        fontui::fitComboBoxToContents(m_preset, m_dpr);
    }

    int singleRowHeight() const
    {
        int h = qMax(m_preset->sizeHint().height(),
                     m_aa_btn->sizeHint().height());
        h     = qMax(h, m_line_host->sizeHint().height());
        h     = qMax(h, m_space_host->sizeHint().height());
        if (m_features_btn) {
            h = qMax(h, m_features_btn->sizeHint().height());
        }
        h = qMax(h, m_invert_btn->sizeHint().height());
        h = qMax(h, m_waterfall_chk->sizeHint().height());
        if (m_reset_btn) {
            h = qMax(h, m_reset_btn->sizeHint().height());
        }
        return h + m_margin_top + m_margin_bot;
    }

    int sliderGroupGapExtra() const
    {
        return fontui::dp(6, m_dpr);
    }

    int fullInlineWidth() const
    {
        int w = m_margin_lr * 2 + m_preset->sizeHint().width()
                + m_line_host->sizeHint().width()
                + sliderGroupGapExtra()
                + m_space_host->sizeHint().width()
                + m_invert_btn->sizeHint().width()
                + m_waterfall_chk->sizeHint().width() + m_h_spacing * 4;
        if (isWidgetShown(m_features_btn)) {
            w += m_features_btn->sizeHint().width() + m_h_spacing;
        }
        if (isWidgetShown(m_reset_btn)) {
            w += m_reset_btn->sizeHint().width() + m_h_spacing;
        }
        return w;
    }

    void updateMode(int avail_w)
    {
        const bool need_compact = (avail_w > 0 && avail_w < fullInlineWidth());
        if (m_initialized && m_compact == need_compact) {
            return;
        }
        m_compact     = need_compact;
        m_initialized = true;

        if (m_compact) {
            m_aa_layout->addWidget(m_line_host);
            m_aa_layout->addWidget(m_space_host);
            m_line_host->show();
            m_space_host->show();
            m_aa_btn->show();
        }
        else {
            m_aa_layout->removeWidget(m_line_host);
            m_aa_layout->removeWidget(m_space_host);
            m_line_host->setParent(this);
            m_space_host->setParent(this);
            m_line_host->show();
            m_space_host->show();
            {
                QSignalBlocker blocker(m_aa_btn);
                m_aa_btn->setChecked(false);
            }
            m_aa_btn->hide();
            m_aa_panel->hide();
        }
    }

    int doLayout(int avail_w, bool test_only) const
    {
        const bool compact = (avail_w > 0 && avail_w < fullInlineWidth());
        const int left_x   = m_margin_lr;
        const int right_x  = qMax(left_x, avail_w - m_margin_lr);

        QVector<QWidget *> flow_widgets;
        flow_widgets.append(m_preset);
        if (compact) {
            flow_widgets.append(m_aa_btn);
        }
        else {
            flow_widgets.append(m_line_host);
            flow_widgets.append(m_space_host);
        }
        if (isWidgetShown(m_features_btn)) {
            flow_widgets.append(m_features_btn);
        }
        flow_widgets.append(m_invert_btn);
        flow_widgets.append(m_waterfall_chk);
        if (isWidgetShown(m_reset_btn)) {
            flow_widgets.append(m_reset_btn);
        }

        struct Placed {
            QWidget *w = nullptr;
            int x      = 0;
            QSize sz;
        };
        QVector<Placed> line;
        int x       = left_x;
        int y       = m_margin_top;
        int line_h  = 0;

        auto flush_line = [&](bool is_last_line) {
            if (line.isEmpty()) {
                return;
            }
            if (!test_only) {
                if (is_last_line && isWidgetShown(m_reset_btn)
                    && line.last().w == m_reset_btn) {
                    const int desired_x
                        = right_x - line.last().sz.width();
                    int prev_right = left_x;
                    if (line.size() >= 2) {
                        const Placed &prev = line.at(line.size() - 2);
                        prev_right = prev.x + prev.sz.width() + m_h_spacing;
                    }
                    line.last().x = qMax(prev_right, desired_x);
                }
                for (const Placed &p : line) {
                    const int item_y
                        = y + qMax(0, (line_h - p.sz.height()) / 2);
                    p.w->setGeometry(QRect(QPoint(p.x, item_y), p.sz));
                }
            }
            line.clear();
        };

        for (QWidget *w : flow_widgets) {
            if (!w) {
                continue;
            }
            const QSize sz = w->sizeHint();
            if (!line.isEmpty() && compact && x + sz.width() > right_x) {
                flush_line(false);
                x      = left_x;
                y      = y + line_h + m_v_spacing;
                line_h = 0;
            }
            line.append({w, x, sz});
            x += sz.width() + m_h_spacing;
            if (!compact && w == m_line_host) {
                x += sliderGroupGapExtra();
            }
            line_h = qMax(line_h, sz.height());
        }
        flush_line(true);

        const int total_h = y + line_h + m_margin_bot;
        return qMax(total_h, singleRowHeight());
    }

    QComboBox *m_preset        = nullptr;
    QWidget *m_line_host       = nullptr;
    QWidget *m_space_host      = nullptr;
    QPushButton *m_aa_btn      = nullptr;
    QPushButton *m_features_btn = nullptr;
    QPushButton *m_invert_btn  = nullptr;
    QCheckBox *m_waterfall_chk = nullptr;
    QPushButton *m_reset_btn   = nullptr;
    QFrame *m_aa_panel                  = nullptr;
    QVBoxLayout *m_aa_layout            = nullptr;
    std::function<void()> m_on_layout_changed;
    qreal m_dpr                         = 1.0;
    int m_margin_lr            = 8;
    int m_margin_top           = 6;
    int m_margin_bot           = 4;
    int m_h_spacing            = 8;
    int m_v_spacing            = 4;
    bool m_compact             = false;
    bool m_initialized         = false;
};

// Keeps QComboBox button styling while opening a QMenu popup so Seer Host's
// MaterialStyle draws real separators, checkmarks, and rounded menu chrome.
class PresetComboBox final : public QComboBox {
public:
    explicit PresetComboBox(QWidget *parent = nullptr) : QComboBox(parent)
    {
        setFocusPolicy(Qt::NoFocus);
        setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_menu = new QMenu(this);
        m_menu->setToolTipsVisible(true);
        connect(m_menu, &QMenu::aboutToHide, this, [this]() {
            m_menu_open = false;
            update();
        });
    }

    QSize sizeHint() const override
    {
        const QSize base = QComboBox::sizeHint();
        return QSize(qMax(base.width(), minimumWidth()), base.height());
    }

    QSize minimumSizeHint() const override
    {
        const QSize base = QComboBox::minimumSizeHint();
        return QSize(qMax(base.width(), minimumWidth()), base.height());
    }

    void setDpr(qreal dpr)
    {
        m_dpr = fontui::clampDpr(dpr);
    }

    void showPopup() override
    {
        if (!m_menu || count() == 0) {
            return;
        }
        m_menu->clear();
        for (int i = 0; i < count(); ++i) {
            if (itemData(i, Qt::UserRole + 1).toInt() == 3) {
                QAction *sep = m_menu->addSeparator();
                sep->setData(3);
                continue;
            }
            QString label = itemText(i);
            label.replace(QLatin1Char('&'), QStringLiteral("&&"));
            QAction *act = m_menu->addAction(label);
            act->setCheckable(true);
            act->setChecked(i == currentIndex());
            const QString tip = itemData(i, Qt::ToolTipRole).toString();
            if (!tip.isEmpty()) {
                act->setToolTip(tip);
            }
            connect(act, &QAction::triggered, this, [this, i]() {
                setCurrentIndex(i);
                emit activated(i);
            });
        }
        m_menu->setMinimumWidth(width());
        m_menu_open = true;
        update();
        m_menu->popup(
            mapToGlobal(QPoint(0, height() + fontui::dp(2, m_dpr))));
    }

    void hidePopup() override
    {
        if (m_menu && m_menu->isVisible()) {
            m_menu->close();
        }
        QComboBox::hidePopup();
    }

protected:
    void initStyleOption(QStyleOptionComboBox *option) const override
    {
        QComboBox::initStyleOption(option);
        if (m_menu_open && option) {
            option->state |= QStyle::State_Open;
        }
    }

private:
    QMenu *m_menu    = nullptr;
    qreal m_dpr      = 1.0;
    bool m_menu_open = false;
};
}  // namespace

TextPreviewTab::TextPreviewTab(QWidget *parent) : QWidget(parent)
{
    setFocusPolicy(Qt::NoFocus);

    auto *layout = new fontui::MinHfwGridLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_aa_panel = new AaPopupPanel(this);
    m_aa_panel->hide();

    m_combo_preset = new PresetComboBox(this);

    auto add_preset = [this](const QString &label, const QString &content) {
        const int index = m_combo_preset->count();
        m_combo_preset->addItem(label, content);
        m_combo_preset->setItemData(index, 0, Qt::UserRole + 1);
    };
    add_preset(tr("Pangram"), QString::fromUtf8(g_preset_pangram));
    add_preset(tr("Paragraph"), QString::fromUtf8(g_preset_paragraph));
    add_preset(tr("Digits & Symbols"), QString::fromUtf8(g_preset_symbols));
    fontui::fitComboBoxToContents(m_combo_preset, m_dpr);

    auto make_slider = [&](const QString &name, const QString &tip,
                           const QString &badge_obj_name, int lo, int hi,
                           int step, int page, int value, QSlider **slider_out,
                           QLabel **value_out) -> QWidget * {
        auto *host = new QWidget(this);
        host->setFocusPolicy(Qt::NoFocus);
        auto *lay = new QHBoxLayout(host);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(fontui::dp(4, m_dpr));
        auto *caption = new QLabel(name, host);
        caption->setToolTip(tip);
        auto *slider = new QSlider(Qt::Horizontal, host);
        slider->setRange(lo, hi);
        slider->setSingleStep(step);
        slider->setPageStep(page);
        slider->setValue(value);
        const int track_w
            = qMax(fontui::dp(64, m_dpr),
                   QFontMetrics(caption->font())
                       .horizontalAdvance(QStringLiteral("MMMMMMMM")));
        slider->setMinimumWidth(track_w);
        slider->setMaximumWidth(track_w + fontui::dp(32, m_dpr));
        slider->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        slider->setFocusPolicy(Qt::NoFocus);
        slider->setToolTip(tip);
        auto *value_label = new fontui::ValueBadge(m_dpr, QStringLiteral("-20.0"), host);
        value_label->setObjectName(badge_obj_name);
        lay->addWidget(caption);
        lay->addWidget(slider);
        lay->addWidget(value_label);
        *slider_out = slider;
        *value_out  = value_label;
        return host;
    };

    // Resolution is 0.1 for the height ratio and 0.5 px for the spacing, so the
    // sliders work on tenths of a point and halves of a pixel respectively.
    QWidget *line_host = make_slider(
        tr("Height"), tr("Line height"),
        QStringLiteral("text_line_value_label"), 10, 25, 1, 2,
        qRound(m_line_height * 10.0), &m_slider_line, &m_label_line);
    m_line_host = line_host;
    QWidget *space_host = make_slider(
        tr("Spacing"), tr("Letter spacing (px)"),
        QStringLiteral("text_space_value_label"), -10, 40, 1, 2,
        qRound(m_letter_spacing * 2.0), &m_slider_space, &m_label_space);

    m_btn_aa = new QPushButton(QStringLiteral("Aa"), this);
    m_btn_aa->setCheckable(true);
    m_btn_aa->setFlat(true);
    m_btn_aa->setFocusPolicy(Qt::NoFocus);
    m_btn_aa->setToolTip(tr("Line height and letter spacing"));

    m_btn_features = new QPushButton(tr("Features"), this);
    m_btn_features->setObjectName(QStringLiteral("text_features_btn"));
    m_btn_features->setCheckable(true);
    m_btn_features->setFlat(true);
    m_btn_features->setFocusPolicy(Qt::NoFocus);
    m_btn_features->setCursor(Qt::ArrowCursor);
    m_btn_features->setToolTip(tr("OpenType features"));
    m_btn_features->hide();

    m_btn_invert = new QPushButton(tr("Invert"), this);
    m_btn_invert->setCheckable(true);
    m_btn_invert->setFlat(true);
    m_btn_invert->setFocusPolicy(Qt::NoFocus);
    m_btn_invert->setToolTip(tr("Swap preview foreground and background"));

    m_chk_waterfall = new QCheckBox(tr("Waterfall"), this);
    m_chk_waterfall->setFocusPolicy(Qt::NoFocus);
    m_chk_waterfall->setToolTip(tr("Show the type-scale waterfall"));

    m_btn_reset = new fontui::CompactActionButton(tr("Reset"), m_dpr,
                                                  this, false);
    m_btn_reset->setObjectName(QStringLiteral("text_toolbar_reset_btn"));
    m_btn_reset->setToolTip(tr("Reset text preview parameters"));
    m_btn_reset->hide();

    m_toolbar = new TextToolbar(m_combo_preset, line_host, space_host, m_btn_aa,
                                m_btn_features, m_btn_invert, m_chk_waterfall,
                                m_btn_reset, m_aa_panel, this);
    static_cast<TextToolbar *>(m_toolbar)->setOnLayoutChanged([this]() {
        if (m_aa_panel && m_aa_panel->isVisible()) {
            repositionAaPanel();
        }
    });

    m_features_bar = new OpenTypeFeaturesBar(this);
    m_features_bar->setDpr(m_dpr);
    m_features_bar->hide();

    m_surface_host = new QWidget(this);
    m_surface_host->setFocusPolicy(Qt::NoFocus);
    auto *surface_lay = new QVBoxLayout(m_surface_host);
    surface_lay->setSpacing(0);

    auto *frame     = new fontui::SurfaceFrame(m_surface_host);
    m_surface_frame = frame;

    m_stack = new QStackedWidget(frame);
    m_stack->setFocusPolicy(Qt::NoFocus);

    m_edit = new FontPreviewEdit(m_stack);
    m_stack->addWidget(m_edit);

    m_scroll_waterfall = new QScrollArea(m_stack);
    m_scroll_waterfall->setObjectName(QStringLiteral("text_waterfall_scroll"));
    m_scroll_waterfall->setFrameShape(QFrame::NoFrame);
    m_scroll_waterfall->setWidgetResizable(true);
    m_scroll_waterfall->verticalScrollBar()->setProperty("is_readonly", true);
    m_scroll_waterfall->horizontalScrollBar()->setProperty("is_readonly",
                                                           true);
    m_scroll_waterfall->setFocusPolicy(Qt::NoFocus);
    m_scroll_waterfall->installEventFilter(this);
    m_waterfall = new WaterfallWidget(m_scroll_waterfall);
    m_waterfall->setFontProvider([this](int pt) {
        return m_font_provider ? m_font_provider(pt) : QFont();
    });
    m_scroll_waterfall->setWidget(m_waterfall);
    m_stack->addWidget(m_scroll_waterfall);

    frame->addContent(m_stack);
    surface_lay->addWidget(frame);

    layout->addWidget(m_toolbar, 0, 0);
    layout->addWidget(m_features_bar, 1, 0);
    layout->addWidget(m_surface_host, 2, 0);
    layout->setRowStretch(2, 1);

    updateLayoutMetrics();

    connect(m_combo_preset, &QComboBox::currentIndexChanged, this,
            &TextPreviewTab::onPresetChanged);
    connect(m_combo_preset, &QComboBox::activated, this,
            [this](int) { emit sigInteractionClearFocus(); });
    connect(m_edit, &FontPreviewEdit::sigUserEdited, this,
            &TextPreviewTab::onUserEdited);
    connect(m_edit, &FontPreviewEdit::sigToast, this, &TextPreviewTab::sigToast);
    connect(m_edit, &FontPreviewEdit::sigZoomRequested, this,
            &TextPreviewTab::sigZoomRequested);
    connect(m_edit, &FontPreviewEdit::sigFocusChanged, this,
            [this](bool focused) {
                if (m_surface_frame) {
                    static_cast<fontui::SurfaceFrame *>(m_surface_frame)
                        ->setFocused(focused);
                }
            });
    connect(m_waterfall, &WaterfallWidget::sigSizeClicked, this,
            [this](int pt) {
                emit sigSizeClicked(pt);
                emit sigInteractionClearFocus();
            });
    connect(m_waterfall, &WaterfallWidget::sigZoomRequested, this,
            [this](int steps) {
                emit sigZoomRequested(steps);
                emit sigInteractionClearFocus();
            });
    connect(m_slider_line, &QSlider::valueChanged, this, [this](int value) {
        setLineHeight(value / 10.0);
        emit sigInteractionClearFocus();
    });
    connect(m_slider_line, &QSlider::sliderPressed, this,
            &TextPreviewTab::sigInteractionClearFocus);
    connect(m_slider_space, &QSlider::valueChanged, this, [this](int value) {
        setLetterSpacing(value * 0.5);
        emit sigInteractionClearFocus();
    });
    connect(m_slider_space, &QSlider::sliderPressed, this,
            &TextPreviewTab::sigInteractionClearFocus);
    connect(m_btn_aa, &QPushButton::toggled, this, [this](bool on) {
        emit sigInteractionClearFocus();
        if (on && m_btn_aa->isVisible()) {
            repositionAaPanel();
            m_aa_panel->show();
            m_aa_panel->raise();
        }
        else {
            m_aa_panel->hide();
        }
    });
    connect(m_btn_features, &QPushButton::toggled, this, [this](bool on) {
        emit sigInteractionClearFocus();
        if (on) {
            closeAaPanel();
        }
        m_features_expanded = on;
        syncFeaturesDrawerState();
    });
    connect(m_features_bar, &OpenTypeFeaturesBar::sigFeaturesChanged, this,
            [this]() {
                syncFeaturesDrawerState();
                emit sigFeaturesChanged();
            });
    connect(m_features_bar, &OpenTypeFeaturesBar::sigInteractionClearFocus,
            this, &TextPreviewTab::sigInteractionClearFocus);
    connect(m_btn_invert, &QPushButton::toggled, this, [this](bool on) {
        m_invert_preview = on;
        applyInvertPreview();
        updateResetButtonState();
        emit sigInteractionClearFocus();
    });
    connect(m_chk_waterfall, &QCheckBox::toggled, this, [this](bool on) {
        setWaterfallEnabled(on);
        emit sigInteractionClearFocus();
    });
    connect(m_btn_reset, &QPushButton::clicked, this, [this]() {
        resetPreviewSettings();
        emit sigInteractionClearFocus();
    });

    m_slider_line->installEventFilter(this);
    m_slider_space->installEventFilter(this);
    m_combo_preset->installEventFilter(this);

    if (m_label_line) {
        m_label_line->setText(QString::number(m_line_height, 'f', 1));
        static_cast<fontui::ValueBadge *>(m_label_line)
            ->setModified(!qFuzzyCompare(m_line_height, 1.4));
    }
    if (m_label_space) {
        m_label_space->setText(QString::number(m_letter_spacing, 'f', 1));
        static_cast<fontui::ValueBadge *>(m_label_space)
            ->setModified(!qFuzzyIsNull(m_letter_spacing));
    }

    // Seed the editor with the first preset so an untouched viewer is not blank.
    // The first added item is already current, so setCurrentIndex(0) would not
    // emit anything; drive the handler explicitly.
    if (m_combo_preset->count() > 0) {
        onPresetChanged(0);
    }
    updateResetButtonState();
}

void TextPreviewTab::setDpr(qreal dpr)
{
    const qreal clamped = fontui::clampDpr(dpr);
    if (qFuzzyCompare(m_dpr, clamped)) {
        return;
    }
    m_dpr = clamped;
    updateLayoutMetrics();
}

void TextPreviewTab::updateLayoutMetrics()
{
    if (m_aa_panel) {
        static_cast<AaPopupPanel *>(m_aa_panel)->setDpr(m_dpr);
    }
    if (m_surface_host && m_surface_host->layout()) {
        m_surface_host->layout()->setContentsMargins(
            fontui::dp(8, m_dpr), fontui::dp(2, m_dpr), fontui::dp(8, m_dpr),
            fontui::dp(8, m_dpr));
    }
    if (m_surface_frame) {
        static_cast<fontui::SurfaceFrame *>(m_surface_frame)->setDpr(m_dpr);
    }
    if (m_edit) {
        m_edit->setDpr(m_dpr);
    }
    if (m_combo_preset) {
        static_cast<PresetComboBox *>(m_combo_preset)->setDpr(m_dpr);
        fontui::fitComboBoxToContents(m_combo_preset, m_dpr);
    }
    if (m_waterfall) {
        m_waterfall->setDpr(m_dpr);
    }
    auto rescale_slider_host = [this](QSlider *slider, QLabel *val_lbl) {
        if (slider && slider->parentWidget()) {
            if (auto *lay = slider->parentWidget()->layout()) {
                lay->setSpacing(fontui::dp(4, m_dpr));
            }
            const int track_w
                = qMax(fontui::dp(64, m_dpr),
                       QFontMetrics(slider->parentWidget()->font())
                           .horizontalAdvance(QStringLiteral("MMMMMMMM")));
            slider->setMinimumWidth(track_w);
            slider->setMaximumWidth(track_w + fontui::dp(32, m_dpr));
        }
        if (val_lbl) {
            auto *badge = static_cast<fontui::ValueBadge *>(val_lbl);
            badge->setDpr(m_dpr);
            badge->refreshFont();
        }
    };
    rescale_slider_host(m_slider_line, m_label_line);
    rescale_slider_host(m_slider_space, m_label_space);
    if (m_toolbar) {
        static_cast<TextToolbar *>(m_toolbar)->setDpr(m_dpr);
    }
    if (m_features_bar) {
        m_features_bar->setDpr(m_dpr);
    }
    if (m_aa_panel && m_aa_panel->isVisible()) {
        repositionAaPanel();
    }
}

void TextPreviewTab::refreshToolbarAndAaPanel()
{
    if (m_toolbar) {
        static_cast<TextToolbar *>(m_toolbar)->refreshLayout();
        if (m_aa_panel && m_aa_panel->isVisible()) {
            repositionAaPanel();
        }
    }
}

void TextPreviewTab::syncFeaturesDrawerState()
{
    const bool has_feat = m_features_bar && m_features_bar->hasFeatures();
    const int active_cnt
        = m_features_bar ? m_features_bar->overrideCount() : 0;
    if (!has_feat) {
        m_features_expanded = false;
    }

    if (m_btn_features) {
        const QString new_text = active_cnt > 0
                                     ? tr("Features · %1").arg(active_cnt)
                                     : tr("Features");
        const bool text_changed = (m_btn_features->text() != new_text);
        if (text_changed) {
            m_btn_features->setText(new_text);
        }
        if (m_btn_features->isChecked() != (has_feat && m_features_expanded)) {
            QSignalBlocker blocker(m_btn_features);
            m_btn_features->setChecked(has_feat && m_features_expanded);
        }
        const QString base = active_cnt > 0
                                 ? tr("OpenType features (%1 active)").arg(active_cnt)
                                 : tr("OpenType features");
        m_btn_features->setToolTip(
            m_reset_ot_hint.isEmpty()
                ? base
                : QStringLiteral("%1 (%2: %3)")
                      .arg(base, tr("Clear"), m_reset_ot_hint));

        const bool vis_changed = (!m_btn_features->isHidden() != has_feat);
        m_btn_features->setVisible(has_feat);
        if (text_changed || vis_changed) {
            refreshToolbarAndAaPanel();
        }
    }

    if (m_features_bar) {
        m_features_bar->setVisible(has_feat && m_features_expanded);
    }
}

void TextPreviewTab::setFontProvider(FontProvider provider)
{
    m_font_provider = std::move(provider);
}

void TextPreviewTab::loadPresets(QSettings &ini)
{
    ini.beginGroup(QStringLiteral("Presets"));
    QStringList keys = ini.childKeys();
    if (keys.isEmpty()) {
        // Avoid spaces in seeded keys (%20) and ASCII '"' (\" escapes) in
        // QSettings::IniFormat so fontviewer.ini stays clean and readable.
        ini.setValue(
            QStringLiteral("BoJack"),
            QStringLiteral(
                "\u201CIn this terrifying world all we have are the "
                "connections that we make.\u201D\n- BoJack"));
        ini.setValue(
            QStringLiteral("Albus_Dumbledore"),
            QStringLiteral(
                "\u201CHappiness can be found, even in the darkest of times, "
                "if one only remembers to turn on the light.\u201D\n- Albus "
                "Dumbledore"));
        ini.setValue(
            QStringLiteral("Best_App"),
            QStringLiteral("Seer is the best app in this universe. \U0001F680"));
        ini.sync();
        keys = ini.childKeys();
    }

    QVector<CustomPreset> loaded;
    loaded.reserve(keys.size());
    for (const QString &key : keys) {
        const QVariant raw_var = ini.value(key);
        QString text
            = (raw_var.metaType().id() == QMetaType::QStringList)
                  ? raw_var.toStringList().join(QStringLiteral(", "))
                  : raw_var.toString();
        text.replace(QStringLiteral("\\r\\n"), QStringLiteral("\n"));
        text.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
        if (text.trimmed().isEmpty()) {
            continue;
        }
        text = truncatePreservingSurrogates(text,
                                            FontPreviewEdit::kMaxPasteChars);
        loaded.append({key, text});
    }
    ini.endGroup();
    setCustomPresets(loaded);
}

void TextPreviewTab::setCustomPresets(const QVector<CustomPreset> &presets)
{
    if (!m_combo_preset) {
        return;
    }
    {
        QSignalBlocker blocker(m_combo_preset);
        for (int i = m_combo_preset->count() - 1; i >= 0; --i) {
            const int kind = m_combo_preset->itemData(i, Qt::UserRole + 1).toInt();
            if (kind == 2 || kind == 3) {
                m_combo_preset->removeItem(i);
            }
        }
        bool added_sep = false;
        for (const CustomPreset &p : presets) {
            if (p.title.trimmed().isEmpty() || p.content.trimmed().isEmpty()) {
                continue;
            }
            if (!added_sep) {
                const int sep_idx = m_combo_preset->count();
                m_combo_preset->insertSeparator(sep_idx);
                m_combo_preset->setItemData(sep_idx, 3, Qt::UserRole + 1);
                added_sep = true;
            }
            const int idx = m_combo_preset->count();
            m_combo_preset->addItem(formatPresetTitle(p.title), p.content);
            m_combo_preset->setItemData(idx, 2, Qt::UserRole + 1);
            m_combo_preset->setItemData(idx, p.title, Qt::ToolTipRole);
        }
    }
    fontui::fitComboBoxToContents(m_combo_preset, m_dpr);
    refreshToolbarAndAaPanel();
}

void TextPreviewTab::applyFont(const QFont &text_font, int current_pt)
{
    m_current_pt = current_pt;
    if (m_edit) {
        m_edit->setFont(text_font);
    }
    syncWaterfall();
    if (m_waterfall) {
        m_waterfall->refreshFont();
    }
    if (m_waterfall_enabled) {
        m_waterfall_needs_scroll = true;
        ensureWaterfallRowVisible(m_current_pt);
        if (m_waterfall_needs_scroll) {
            ensureWaterfallRowVisibleLater();
        }
    }
}

void TextPreviewTab::clearUndoHistory()
{
    if (m_edit && m_edit->document()) {
        m_edit->document()->clearUndoRedoStacks();
    }
}

void TextPreviewTab::closeAaPanel()
{
    if (m_btn_aa && m_btn_aa->isChecked()) {
        m_btn_aa->setChecked(false);
    }
}

void TextPreviewTab::setCurrentText(const QString &t)
{
    if (t.isEmpty() || !m_edit) {
        return;
    }
    m_last_edited = t;
    ensureRecentItem();
    m_edit->setPreviewText(t);
    syncWaterfall();
    updateResetButtonState();
}

QString TextPreviewTab::currentText() const
{
    return m_edit ? m_edit->toPlainText() : QString();
}

void TextPreviewTab::setLineHeight(qreal h)
{
    if (!qIsFinite(h)) {
        h = 1.4;
    }
    m_line_height = qBound(1.0, h, 2.5);
    if (m_slider_line) {
        QSignalBlocker blocker(m_slider_line);
        m_slider_line->setValue(qRound(m_line_height * 10.0));
    }
    if (m_label_line) {
        m_label_line->setText(QString::number(m_line_height, 'f', 1));
        static_cast<fontui::ValueBadge *>(m_label_line)
            ->setModified(!qFuzzyCompare(m_line_height, 1.4));
    }
    if (m_edit) {
        m_edit->setLineHeight(m_line_height);
    }
    updateResetButtonState();
}

void TextPreviewTab::setLetterSpacing(qreal s)
{
    if (!qIsFinite(s)) {
        s = 0.0;
    }
    m_letter_spacing = qBound(-5.0, s, 20.0);
    if (m_slider_space) {
        QSignalBlocker blocker(m_slider_space);
        m_slider_space->setValue(qRound(m_letter_spacing * 2.0));
    }
    if (m_label_space) {
        m_label_space->setText(QString::number(m_letter_spacing, 'f', 1));
        static_cast<fontui::ValueBadge *>(m_label_space)
            ->setModified(!qFuzzyIsNull(m_letter_spacing));
    }
    if (m_features_bar) {
        m_features_bar->setLetterSpacingActive(!qFuzzyIsNull(m_letter_spacing));
    }
    emit sigLetterSpacingChanged(m_letter_spacing);
    updateResetButtonState();
}

void TextPreviewTab::setWaterfallEnabled(bool on)
{
    m_waterfall_enabled = on;
    if (m_chk_waterfall && m_chk_waterfall->isChecked() != on) {
        QSignalBlocker blocker(m_chk_waterfall);
        m_chk_waterfall->setChecked(on);
    }
    if (m_stack) {
        m_stack->setCurrentIndex(on ? 1 : 0);
    }
    if (m_surface_frame) {
        static_cast<fontui::SurfaceFrame *>(m_surface_frame)
            ->setWaterfallActive(on);
    }
    if (m_line_host) {
        m_line_host->setEnabled(!on);
        const QString tip = on ? tr("Line height is unavailable in Waterfall mode")
                               : tr("Line height");
        m_line_host->setToolTip(tip);
        if (m_slider_line) {
            m_slider_line->setToolTip(tip);
        }
    }
    if (on) {
        emit sigInteractionClearFocus();
        syncWaterfall();
        m_waterfall_needs_scroll = true;
        ensureWaterfallRowVisible(m_current_pt);
        if (m_waterfall_needs_scroll) {
            ensureWaterfallRowVisibleLater();
        }
    }
    else {
        m_waterfall_needs_scroll = false;
    }
    updateResetButtonState();
}

void TextPreviewTab::setInvertPreview(bool on)
{
    m_invert_preview = on;
    if (m_btn_invert && m_btn_invert->isChecked() != on) {
        QSignalBlocker blocker(m_btn_invert);
        m_btn_invert->setChecked(on);
    }
    applyInvertPreview();
    updateResetButtonState();
}

void TextPreviewTab::applyInvertPreview()
{
    if (!m_edit) {
        return;
    }

    if (m_invert_preview) {
        const QPalette base = palette();
        QPalette pal        = base;
        const QColor base_color
            = base.color(QPalette::Active, QPalette::Base);
        const QColor text_color
            = base.color(QPalette::Active, QPalette::Text);
        pal.setColor(QPalette::Base, text_color);
        pal.setColor(QPalette::Text, base_color);
        pal.setColor(QPalette::WindowText, base_color);
        m_edit->setPalette(pal);
        m_edit->viewport()->setPalette(pal);
    }
    else {
        m_edit->setPalette(QPalette());
        m_edit->viewport()->setPalette(QPalette());
    }

    if (m_surface_frame) {
        static_cast<fontui::SurfaceFrame *>(m_surface_frame)
            ->setInverted(m_invert_preview);
    }
    if (m_waterfall) {
        m_waterfall->setInverted(m_invert_preview);
    }
}

void TextPreviewTab::updateWaterfall()
{
    if (m_waterfall) {
        m_waterfall->update();
    }
    if (m_surface_frame) {
        m_surface_frame->update();
    }
}

void TextPreviewTab::syncStackIndex()
{
    if (m_stack) {
        m_stack->setCurrentIndex(m_waterfall_enabled ? 1 : 0);
    }
    if (m_surface_frame) {
        static_cast<fontui::SurfaceFrame *>(m_surface_frame)
            ->setWaterfallActive(m_waterfall_enabled);
    }
}

void TextPreviewTab::updateShortcutTooltips(const QString &invert_hint,
                                            const QString &waterfall_hint,
                                            const QString &reset_ot_hint)
{
    m_invert_hint    = invert_hint;
    m_waterfall_hint = waterfall_hint;
    m_reset_ot_hint  = reset_ot_hint;
    if (m_btn_invert) {
        const QString base = tr("Swap preview foreground and background");
        m_btn_invert->setToolTip(
            invert_hint.isEmpty()
                ? base
                : QStringLiteral("%1 (%2)").arg(base, invert_hint));
    }
    if (m_chk_waterfall) {
        const QString base = tr("Show the type-scale waterfall");
        m_chk_waterfall->setToolTip(
            waterfall_hint.isEmpty()
                ? base
                : QStringLiteral("%1 (%2)").arg(base, waterfall_hint));
    }
    if (m_features_bar) {
        m_features_bar->setResetShortcutHint(reset_ot_hint);
    }
    syncFeaturesDrawerState();
}

void TextPreviewTab::setOpenTypeFeatures(
    const QVector<FontFeatureInfo> &features)
{
    if (!m_features_bar) {
        return;
    }
    m_features_bar->rebuild(features);
    m_features_bar->setLetterSpacingActive(!qFuzzyIsNull(m_letter_spacing));
    syncFeaturesDrawerState();
}

void TextPreviewTab::setFeatureEnabled(const QByteArray &tag, bool enabled)
{
    if (m_features_bar) {
        m_features_bar->setFeatureEnabled(tag, enabled);
    }
}

bool TextPreviewTab::isFeatureEnabled(const QByteArray &tag) const
{
    return m_features_bar ? m_features_bar->isFeatureEnabled(tag) : false;
}

void TextPreviewTab::resetOpenTypeFeatures()
{
    if (m_features_bar) {
        m_features_bar->resetOverrides();
    }
}

bool TextPreviewTab::hasOpenTypeOverrides() const
{
    return m_features_bar && m_features_bar->hasOverrides();
}

const QHash<QByteArray, bool> &TextPreviewTab::openTypeOverrides() const
{
    static const QHash<QByteArray, bool> kEmpty;
    return m_features_bar ? m_features_bar->overrides() : kEmpty;
}

QVector<QPushButton *> TextPreviewTab::featurePillButtons() const
{
    return m_features_bar ? m_features_bar->pillButtons()
                          : QVector<QPushButton *>{};
}

QPixmap TextPreviewTab::renderPixmap(qreal dpr) const
{
    if (m_waterfall_enabled && m_waterfall) {
        const QSize logical = m_waterfall->contentSize();
        QPixmap pix(
            QSize(qCeil(logical.width() * dpr), qCeil(logical.height() * dpr)));
        if (!pix.isNull()) {
            pix.setDevicePixelRatio(dpr);
            pix.fill(m_waterfall->baseColor());
            QPainter painter(&pix);
            m_waterfall->renderContent(&painter);
            painter.end();
        }
        return pix;
    }

    const QSizeF doc_size = prepareEditDocumentSize();
    if (doc_size.isEmpty() || !m_edit) {
        return {};
    }
    QPixmap pix(
        QSize(qCeil(doc_size.width() * dpr), qCeil(doc_size.height() * dpr)));
    if (pix.isNull()) {
        return {};
    }
    pix.setDevicePixelRatio(dpr);
    pix.fill(m_edit->palette().color(QPalette::Base));

    QPainter painter(&pix);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    drawEditDocument(painter, doc_size);
    painter.end();
    return pix;
}

bool TextPreviewTab::prepareSvgExport(
    QSize &size_out,
    QColor &background_out,
    std::function<void(QPainter &)> &draw_out) const
{
    if (m_waterfall_enabled && m_waterfall) {
        size_out       = m_waterfall->contentSize();
        background_out = m_waterfall->baseColor();
        QPointer<WaterfallWidget> safe_wf(m_waterfall);
        draw_out = [safe_wf](QPainter &p) {
            if (safe_wf) {
                safe_wf->renderContent(&p);
            }
        };
        return true;
    }
    if (m_edit) {
        const QSizeF doc_size = prepareEditDocumentSize();
        size_out = QSize(qCeil(doc_size.width()), qCeil(doc_size.height()));
        background_out = m_edit->palette().color(QPalette::Base);
        QPointer<const TextPreviewTab> safe_self(this);
        draw_out = [safe_self, doc_size](QPainter &p) {
            if (safe_self) {
                safe_self->drawEditDocument(p, doc_size);
            }
        };
        return true;
    }
    return false;
}

void TextPreviewTab::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::StyleChange
        || event->type() == QEvent::FontChange) {
        updateLayoutMetrics();
        refreshToolbarAndAaPanel();
    }
    else if (event->type() == QEvent::PaletteChange
             || event->type() == QEvent::ApplicationPaletteChange) {
        applyInvertPreview();
        updateWaterfall();
        if (m_features_bar) {
            m_features_bar->applyPalette();
        }
        if (m_aa_panel) {
            m_aa_panel->update();
        }
        if (m_label_line) {
            m_label_line->update();
        }
        if (m_label_space) {
            m_label_space->update();
        }
        if (m_btn_reset) {
            m_btn_reset->update();
        }
    }
}

void TextPreviewTab::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_aa_panel && m_aa_panel->isVisible()) {
        repositionAaPanel();
    }
}

bool TextPreviewTab::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_scroll_waterfall && event->type() == QEvent::Show) {
        if (m_waterfall_enabled && m_waterfall_needs_scroll) {
            ensureWaterfallRowVisibleLater();
        }
        return false;
    }

    if (event->type() == QEvent::Wheel) {
        if (watched == m_slider_line || watched == m_slider_space
            || watched == m_combo_preset) {
            static_cast<QWheelEvent *>(event)->accept();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void TextPreviewTab::onPresetChanged(int index)
{
    if (!m_edit || !m_combo_preset || index < 0) {
        return;
    }

    const bool is_recent
        = (m_combo_preset->itemData(index, Qt::UserRole + 1).toInt() == 1);
    const QString text
        = is_recent
              ? m_last_edited
              : m_combo_preset->itemData(index, Qt::UserRole).toString();
    if (text.isEmpty() && !is_recent) {
        return;
    }

    m_edit->setPreviewText(text);
    syncWaterfall();
    updateWaterfall();
    updateResetButtonState();
    emit sigInteractionClearFocus();
}

void TextPreviewTab::ensureRecentItem()
{
    if (!m_combo_preset) {
        return;
    }

    bool inserted = false;
    {
        QSignalBlocker blocker(m_combo_preset);
        if (!m_recent_present) {
            m_combo_preset->insertItem(0, tr("Last used"));
            m_combo_preset->setItemData(0, QString(), Qt::UserRole);
            m_combo_preset->setItemData(0, 1, Qt::UserRole + 1);
            m_recent_present = true;
            inserted         = true;
        }
        // Editing leaves the preset list pointing at "Last used": the text is no
        // longer any of the built-in presets, so showing one of them would lie.
        m_combo_preset->setCurrentIndex(0);
    }
    if (inserted) {
        fontui::fitComboBoxToContents(m_combo_preset, m_dpr);
        refreshToolbarAndAaPanel();
    }
}

void TextPreviewTab::onUserEdited()
{
    if (!m_edit) {
        return;
    }
    m_last_edited = m_edit->toPlainText();
    ensureRecentItem();

    if (m_waterfall_enabled) {
        syncWaterfall();
        updateWaterfall();
    }
    updateResetButtonState();
}

bool TextPreviewTab::hasCustomState() const
{
    return !qFuzzyCompare(m_line_height, 1.4)
           || !qFuzzyIsNull(m_letter_spacing)
           || m_invert_preview
           || m_waterfall_enabled;
}

void TextPreviewTab::updateResetButtonState()
{
    if (!m_btn_reset) {
        return;
    }
    const bool show = hasCustomState();
    if (!m_btn_reset->isHidden() == show) {
        return;
    }
    m_btn_reset->setVisible(show);
    refreshToolbarAndAaPanel();
}

void TextPreviewTab::resetPreviewSettings()
{
    setLineHeight(1.4);
    setLetterSpacing(0.0);
    setInvertPreview(false);
    setWaterfallEnabled(false);
    updateResetButtonState();
}

QString TextPreviewTab::waterfallText() const
{
    QString text = m_edit ? m_edit->toPlainText().trimmed() : QString();
    const int newline = text.indexOf(QLatin1Char('\n'));
    if (newline >= 0) {
        text = text.left(newline).trimmed();
    }
    if (text.isEmpty()) {
        text = QString::fromUtf8(g_def_sample);
    }
    return text;
}

QVector<int> TextPreviewTab::waterfallSizes() const
{
    QVector<int> sizes(std::begin(kWaterfallSizes), std::end(kWaterfallSizes));
    if (m_current_pt > 0 && !sizes.contains(m_current_pt)) {
        sizes.append(m_current_pt);
    }
    std::sort(sizes.begin(), sizes.end());
    return sizes;
}

void TextPreviewTab::syncWaterfall()
{
    if (!m_waterfall) {
        return;
    }
    m_waterfall->setSizes(waterfallSizes());
    m_waterfall->setCurrentSize(m_current_pt);
    m_waterfall->setPreviewText(waterfallText());
}

void TextPreviewTab::ensureWaterfallRowVisible(int pt)
{
    if (!m_scroll_waterfall || !m_waterfall || !m_waterfall_enabled) {
        return;
    }
    const QRectF row = m_waterfall->rowRect(pt);
    if (!row.isValid()) {
        return;
    }
    QScrollBar *bar  = m_scroll_waterfall->verticalScrollBar();
    const int page_h = m_scroll_waterfall->viewport()->height();
    if (page_h <= 0 || !m_scroll_waterfall->isVisible()) {
        return;
    }
    m_waterfall_needs_scroll = false;
    const int top            = int(row.top());
    const int bottom         = int(row.bottom());
    const int pad            = fontui::dp(8, m_dpr);
    if (top < bar->value()) {
        bar->setValue(qMax(0, top - pad));
    }
    else if (bottom > bar->value() + page_h) {
        bar->setValue(bottom - page_h + pad);
    }
}

void TextPreviewTab::ensureWaterfallRowVisibleLater()
{
    if (m_ensure_pending) {
        return;
    }
    m_ensure_pending = true;
    // The row list can grow while a size is being applied (a custom size is
    // inserted on the fly). QScrollArea only refreshes its scroll bar range when
    // the child's posted LayoutRequest is processed, so a synchronous call here
    // would clamp against the previous range.
    QTimer::singleShot(0, this, [this]() {
        m_ensure_pending = false;
        ensureWaterfallRowVisible(m_current_pt);
    });
}

void TextPreviewTab::repositionAaPanel()
{
    if (!m_aa_panel || !m_btn_aa) {
        return;
    }
    m_aa_panel->adjustSize();
    const QSize panel_sz
        = m_aa_panel->sizeHint().expandedTo(m_aa_panel->minimumSizeHint());
    const QPoint btn_bl
        = m_btn_aa->mapTo(this, QPoint(0, m_btn_aa->height()));
    const int margin = fontui::dp(6, m_dpr);
    const int gap    = fontui::dp(4, m_dpr);
    const int max_x  = qMax(margin, width() - panel_sz.width() - margin);
    const int max_y  = qMax(margin, height() - panel_sz.height() - margin);
    const int x      = qBound(margin, btn_bl.x(), max_x);
    const int y      = qBound(margin, btn_bl.y() + gap, max_y);
    m_aa_panel->setGeometry(QRect(QPoint(x, y), panel_sz));
}

QSizeF TextPreviewTab::prepareEditDocumentSize() const
{
    if (!m_edit) {
        return {};
    }
    QTextDocument *doc = m_edit->document();
    doc->setTextWidth(qMax(1, m_edit->viewport()->width()));
    return doc->documentLayout()->documentSize();
}

void TextPreviewTab::drawEditDocument(QPainter &painter,
                                      const QSizeF &doc_size) const
{
    if (!m_edit) {
        return;
    }
    QAbstractTextDocumentLayout::PaintContext ctx;
    ctx.palette        = m_edit->palette();
    ctx.clip           = QRectF(QPointF(0, 0), doc_size);
    ctx.cursorPosition = -1;
    m_edit->document()->documentLayout()->draw(&painter, ctx);
}
