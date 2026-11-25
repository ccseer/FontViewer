#include "fontwidget.h"

#include <qt_windows.h>

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QClipboard>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegion>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QSvgGenerator>
#include <QSvgRenderer>
#include <QTabBar>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <functional>
#include <iterator>

#include "characterwidget.h"
#include "fontpreviewedit.h"
#include "glyphinspectorwidget.h"
#include "ui_fontwidget.h"
#include "waterfallwidget.h"

#define qprintt qDebug() << "[FontViewer]"

namespace {
constexpr auto g_def_sample
    = "It is the time you have wasted for your rose that makes your "
      "rose so important.";
constexpr auto g_def_pt = 16;

constexpr auto g_preset_pangram = "The quick brown fox jumps over the lazy dog.";
constexpr auto g_preset_paragraph
    = "It is the time you have wasted for your rose that makes your rose so "
      "important.\n"
      "Perfection is achieved, not when there is nothing more to add, but when "
      "there is nothing left to take away.";
constexpr auto g_preset_symbols = "0123456789 1/2 3/4 fi fl -> != ===";
constexpr auto g_preset_cjk
    = "骨 门 关 直 刃 曜 誤 漢\n永 东 过 门 骨 直 具 真";

constexpr int kWaterfallSizes[] = {10, 12, 14, 16, 18, 24, 28, 36, 48, 72};

// inline SVG icons — rendered at runtime, no external files
constexpr auto g_svg_copy_image = R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
     stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <rect x="8" y="8" width="13" height="13" rx="2"/>
  <path d="M16 8V5a2 2 0 0 0-2-2H5a2 2 0 0 0-2 2v9a2 2 0 0 0 2 2h3"/>
</svg>)SVG";

constexpr auto g_svg_save_svg = R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
     stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/>
  <polyline points="7 10 12 15 17 10"/>
  <line x1="12" y1="15" x2="12" y2="3"/>
</svg>)SVG";

constexpr auto g_svg_chevron_up = R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
     stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <polyline points="18 15 12 9 6 15"/>
</svg>)SVG";

constexpr auto g_svg_chevron_down = R"SVG(
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
        qprintt << "svgIcon: invalid SVG";
        return {};
    }

    const int phys = qRound(icon_sz * dpr);
    QPixmap pix(phys, phys);
    pix.setDevicePixelRatio(dpr);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    renderer.render(&p, QRectF(0, 0, icon_sz, icon_sz));
    return QIcon(pix);
}

QPushButton *makeIconButton(const char *tooltip, QWidget *parent)
{
    auto btn = new QPushButton(parent);
    btn->setFlat(true);
    btn->setCursor(Qt::PointingHandCursor);
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

// Keeps the Text tab toolbar on a single row and moves line height and letter
// spacing into a non-modal Aa panel when the available width would clip them.
class TextToolbar : public QWidget {
public:
    TextToolbar(QComboBox *preset,
                QWidget *line_host,
                QWidget *space_host,
                QPushButton *aa_btn,
                QPushButton *invert_btn,
                QCheckBox *waterfall_chk,
                QFrame *aa_panel,
                QWidget *parent = nullptr)
        : QWidget(parent),
          m_preset(preset),
          m_line_host(line_host),
          m_space_host(space_host),
          m_aa_btn(aa_btn),
          m_invert_btn(invert_btn),
          m_waterfall_chk(waterfall_chk),
          m_aa_panel(aa_panel)
    {
        setFocusPolicy(Qt::NoFocus);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

        m_bar_layout = new QHBoxLayout(this);
        m_bar_layout->setContentsMargins(8, 6, 8, 2);
        m_bar_layout->setSpacing(8);
        // Let minimumSizeHint() govern shrinking so the full inline row does
        // not lock the window width above the collapsed threshold.
        m_bar_layout->setSizeConstraint(QLayout::SetNoConstraint);

        m_bar_layout->addWidget(m_preset);
        m_bar_layout->addWidget(m_line_host);
        m_bar_layout->addWidget(m_space_host);
        m_bar_layout->addWidget(m_aa_btn);
        m_bar_layout->addWidget(m_invert_btn);
        m_bar_layout->addWidget(m_waterfall_chk);
        m_bar_layout->addStretch(1);

        m_aa_layout = new QVBoxLayout(m_aa_panel);
        m_aa_layout->setContentsMargins(10, 8, 10, 8);
        m_aa_layout->setSpacing(6);

        m_aa_btn->hide();
    }

    QSize sizeHint() const override
    {
        return QSize(fullInlineWidth(), rowHeight());
    }

    QSize minimumSizeHint() const override
    {
        return QSize(collapsedWidth(), rowHeight());
    }

    void refreshLayout()
    {
        updateMode();
        updateGeometry();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        updateMode();
    }

    void showEvent(QShowEvent *event) override
    {
        QWidget::showEvent(event);
        updateMode();
    }

private:
    int rowHeight() const
    {
        const QMargins m = m_bar_layout->contentsMargins();
        int h            = qMax(m_preset->sizeHint().height(),
                                m_aa_btn->sizeHint().height());
        h = qMax(h, m_line_host->sizeHint().height());
        h = qMax(h, m_space_host->sizeHint().height());
        h = qMax(h, m_invert_btn->sizeHint().height());
        h = qMax(h, m_waterfall_chk->sizeHint().height());
        return h + m.top() + m.bottom();
    }

    int fullInlineWidth() const
    {
        const QMargins m = m_bar_layout->contentsMargins();
        const int sp     = m_bar_layout->spacing();
        return m.left() + m.right() + m_preset->sizeHint().width()
               + m_line_host->sizeHint().width()
               + m_space_host->sizeHint().width()
               + m_invert_btn->sizeHint().width()
               + m_waterfall_chk->sizeHint().width() + sp * 4;
    }

    int collapsedWidth() const
    {
        const QMargins m = m_bar_layout->contentsMargins();
        const int sp     = m_bar_layout->spacing();
        return m.left() + m.right() + m_preset->sizeHint().width()
               + m_aa_btn->sizeHint().width()
               + m_invert_btn->sizeHint().width()
               + m_waterfall_chk->sizeHint().width() + sp * 3;
    }

    void updateMode()
    {
        const bool need_compact = (width() > 0 && width() < fullInlineWidth());
        if (m_initialized && m_compact == need_compact) {
            return;
        }
        m_compact     = need_compact;
        m_initialized = true;

        if (m_compact) {
            m_bar_layout->removeWidget(m_line_host);
            m_bar_layout->removeWidget(m_space_host);
            m_aa_layout->addWidget(m_line_host);
            m_aa_layout->addWidget(m_space_host);
            m_line_host->show();
            m_space_host->show();
            m_aa_btn->show();
        }
        else {
            m_aa_layout->removeWidget(m_line_host);
            m_aa_layout->removeWidget(m_space_host);
            m_bar_layout->insertWidget(1, m_line_host);
            m_bar_layout->insertWidget(2, m_space_host);
            m_line_host->show();
            m_space_host->show();
            {
                QSignalBlocker blocker(m_aa_btn);
                m_aa_btn->setChecked(false);
            }
            m_aa_btn->hide();
            m_aa_panel->hide();
        }
        m_bar_layout->activate();
    }

    QComboBox *m_preset        = nullptr;
    QWidget *m_line_host       = nullptr;
    QWidget *m_space_host      = nullptr;
    QPushButton *m_aa_btn      = nullptr;
    QPushButton *m_invert_btn  = nullptr;
    QCheckBox *m_waterfall_chk = nullptr;
    QFrame *m_aa_panel         = nullptr;
    QHBoxLayout *m_bar_layout  = nullptr;
    QVBoxLayout *m_aa_layout   = nullptr;
    bool m_compact             = false;
    bool m_initialized         = false;
};
}  // namespace

FontWidget::FontWidget(QWidget *parent)
    : QWidget(parent), m_wnd_char(nullptr), ui(new Ui::FontWidget)
{
    ui->setupUi(this);
    qprintt << this;
}

FontWidget::~FontWidget()
{
    releaseApplicationFont();
    delete ui;
    qprintt << "~" << this;
}

void FontWidget::releaseApplicationFont()
{
    if (m_font_id != -1) {
        QFontDatabase::removeApplicationFont(m_font_id);
        m_font_id = -1;
    }
}

void FontWidget::initUI(const QStringList &names_raw)
{
    /// widget_top
    ui->widget_top->layout()->setContentsMargins(0, 0, 0, 0);
    // names
    QStringList names = names_raw;
    names.removeDuplicates();
    ui->comboBox_family->addItems(names);
    ui->comboBox_family->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    if (names.size() == 1) {
        ui->comboBox_family->setEnabled(false);
    }
    configureComboBox(ui->comboBox_family);
    // style
    ui->comboBox_style->addItems(
        QFontDatabase::styles(ui->comboBox_family->currentText()));
    configureComboBox(ui->comboBox_style);
    // sz
    const auto all_sz = QFontDatabase::standardSizes();
    for (auto i : all_sz) {
        ui->comboBox_sz->addItem(QString::number(i));
    }
    if (all_sz.contains(g_def_pt)) {
        ui->comboBox_sz->setCurrentText(QString::number(g_def_pt));
    }
    configureComboBox(ui->comboBox_sz);
    // The three header combos never take focus, but a hover wheel still changes
    // their value; swallow it like the toolbar controls.
    for (QComboBox *cb : {ui->comboBox_family, ui->comboBox_style,
                          ui->comboBox_sz}) {
        cb->installEventFilter(this);
    }
    // info
    // breaks the layout!!!
    // ui->label_info->setWordWrap(true);
    ui->label_info->setAlignment(Qt::AlignCenter);

    m_label_name_collapsed = new QLabel(this);
    m_label_name_collapsed->setAlignment(Qt::AlignCenter);
    m_label_name_collapsed->hide();
    auto font_bold = m_label_name_collapsed->font();
    font_bold.setBold(true);
    m_label_name_collapsed->setFont(font_bold);

    m_btn_toggle = makeIconButton("Collapse", this);
    connect(m_btn_toggle, &QPushButton::clicked, this, [this]() {
        setCollapsed(!isCollapsed());
        clearViewerFocus();
    });

    // Add to main grid layout at the same cell as widget_top
    // This keeps it at a fixed position relative to the top-right corner
    ui->gridLayout->addWidget(m_btn_toggle, 0, 0,
                              Qt::AlignTop | Qt::AlignRight);

    // Add collapsed label to the same cell, centered
    ui->gridLayout->addWidget(m_label_name_collapsed, 0, 0, Qt::AlignCenter);

    /// tab wnd
    ui->tabWidget->setFocusPolicy(Qt::NoFocus);
    if (auto *bar = ui->tabWidget->tabBar()) {
        bar->setFocusPolicy(Qt::NoFocus);
        for (QWidget *child : bar->findChildren<QWidget *>()) {
            child->setFocusPolicy(Qt::NoFocus);
        }
    }
    ui->tabWidget->setTabText(ui->tabWidget->indexOf(ui->tab_text), "Text");
    ui->tabWidget->setTabText(ui->tabWidget->indexOf(ui->tab_sample), "Sample");
    auto corner     = new QWidget(this);
    auto corner_lay = new QHBoxLayout(corner);
    corner_lay->setContentsMargins(0, 0, 4, 0);
    corner_lay->setSpacing(2);

    m_btn_copy = makeIconButton("Copy as Image", this);
    m_btn_svg  = makeIconButton("Save as SVG", this);
    connect(m_btn_copy, &QPushButton::clicked, this, [this]() {
        copy();
        clearViewerFocus();
    });
    connect(m_btn_svg, &QPushButton::clicked, this, [this]() {
        saveAsSvg();
        clearViewerFocus();
    });

    corner_lay->addWidget(m_btn_copy);
    corner_lay->addWidget(m_btn_svg);
    ui->tabWidget->setCornerWidget(corner);
    connect(ui->tabWidget, &QTabWidget::currentChanged, this,
            &FontWidget::onTabChanged);

    initTextTab();
    initSampleTab();
    initCharTab();
    initGlyphTab();

    connect(ui->comboBox_family, &QComboBox::currentTextChanged, this,
            &FontWidget::onNameChanged);
    connect(ui->comboBox_style, &QComboBox::currentTextChanged, this,
            &FontWidget::onStyleChanged);
    connect(ui->comboBox_sz, &QComboBox::currentTextChanged, this, [this]() {
        updateTabTextPreview();
        emit sigFontSizeChanged(getCurrentFontSize());
    });
    for (QComboBox *cb :
         {ui->comboBox_family, ui->comboBox_style, ui->comboBox_sz}) {
        connect(cb, &QComboBox::activated, this,
                [this](int) { clearViewerFocus(); });
    }
}

void FontWidget::initTextTab()
{
    auto *tab    = ui->tab_text;
    auto *layout = qobject_cast<QGridLayout *>(tab->layout());
    if (!layout) {
        layout = new QGridLayout(tab);
    }
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    tab->installEventFilter(this);

    m_aa_panel = new QFrame(tab);
    m_aa_panel->setFrameShape(QFrame::StyledPanel);
    m_aa_panel->setFrameShadow(QFrame::Raised);
    m_aa_panel->setAutoFillBackground(true);
    m_aa_panel->setFocusPolicy(Qt::NoFocus);
    m_aa_panel->hide();

    m_combo_preset = new QComboBox(tab);
    m_combo_preset->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    configureComboBox(m_combo_preset);

    auto add_preset = [this](const QString &label, const QString &content) {
        const int index = m_combo_preset->count();
        m_combo_preset->addItem(label, content);
        m_combo_preset->setItemData(index, 0, Qt::UserRole + 1);
    };
    add_preset(tr("Pangram"), QString::fromUtf8(g_preset_pangram));
    add_preset(tr("Paragraph"), QString::fromUtf8(g_preset_paragraph));
    add_preset(tr("Digits & Symbols"), QString::fromUtf8(g_preset_symbols));
    add_preset(tr("CJK Mixed"), QString::fromUtf8(g_preset_cjk));

    auto make_slider = [&](const QString &name, const QString &tip, int lo,
                           int hi, int step, int page, int value,
                           QSlider **slider_out,
                           QLabel **value_out) -> QWidget * {
        auto *host = new QWidget(tab);
        host->setFocusPolicy(Qt::NoFocus);
        auto *lay = new QHBoxLayout(host);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(4);
        auto *caption = new QLabel(name, host);
        caption->setToolTip(tip);
        auto *slider = new QSlider(Qt::Horizontal, host);
        slider->setRange(lo, hi);
        slider->setSingleStep(step);
        slider->setPageStep(page);
        slider->setValue(value);
        const int track_w
            = qMax(64, QFontMetrics(caption->font())
                           .horizontalAdvance(QStringLiteral("MMMMMMMM")));
        slider->setMinimumWidth(track_w);
        slider->setMaximumWidth(track_w + 32);
        slider->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        slider->setFocusPolicy(Qt::NoFocus);
        slider->setToolTip(tip);
        auto *value_label = new QLabel(host);
        value_label->setMinimumWidth(
            QFontMetrics(value_label->font())
                .horizontalAdvance(QStringLiteral("-20.0"))
            + 4);
        value_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lay->addWidget(caption);
        lay->addWidget(slider);
        lay->addWidget(value_label);
        *slider_out = slider;
        *value_out  = value_label;
        return host;
    };

    // Resolution is 0.1 for the height ratio and 0.5 px for the spacing, so the
    // sliders work on tenths of a point and halves of a pixel respectively.
    QWidget *line_host = make_slider(tr("Height"), tr("Line height"), 10, 25, 1,
                                     2, qRound(m_line_height * 10.0),
                                     &m_slider_line, &m_label_line);
    QWidget *space_host
        = make_slider(tr("Spacing"), tr("Letter spacing (px)"), -10, 40, 1, 2,
                      qRound(m_letter_spacing * 2.0), &m_slider_space,
                      &m_label_space);

    m_btn_aa = new QPushButton(QStringLiteral("Aa"), tab);
    m_btn_aa->setCheckable(true);
    m_btn_aa->setFlat(true);
    m_btn_aa->setCursor(Qt::PointingHandCursor);
    m_btn_aa->setFocusPolicy(Qt::NoFocus);
    m_btn_aa->setToolTip(tr("Line height and letter spacing"));

    m_btn_invert = new QPushButton(tr("Invert"), tab);
    m_btn_invert->setCheckable(true);
    m_btn_invert->setFlat(true);
    m_btn_invert->setCursor(Qt::PointingHandCursor);
    m_btn_invert->setFocusPolicy(Qt::NoFocus);
    m_btn_invert->setToolTip(tr("Swap preview foreground and background"));

    m_chk_waterfall = new QCheckBox(tr("Waterfall"), tab);
    m_chk_waterfall->setFocusPolicy(Qt::NoFocus);
    m_chk_waterfall->setToolTip(tr("Show the type-scale waterfall"));

    m_toolbar = new TextToolbar(m_combo_preset, line_host, space_host, m_btn_aa,
                                m_btn_invert, m_chk_waterfall, m_aa_panel, tab);

    m_stack = new QStackedWidget(tab);
    m_stack->setFocusPolicy(Qt::NoFocus);

    m_edit = new FontPreviewEdit(m_stack);
    m_stack->addWidget(m_edit);

    m_scroll_waterfall = new QScrollArea(m_stack);
    m_scroll_waterfall->setFrameShape(QFrame::NoFrame);
    m_scroll_waterfall->setWidgetResizable(true);
    m_scroll_waterfall->setFocusPolicy(Qt::NoFocus);
    m_scroll_waterfall->installEventFilter(this);
    m_waterfall = new WaterfallWidget(m_scroll_waterfall);
    m_waterfall->setFontProvider([this](int pt) {
        return buildPreviewFont(pt, PreviewFontRole::Text);
    });
    m_scroll_waterfall->setWidget(m_waterfall);
    m_stack->addWidget(m_scroll_waterfall);

    layout->addWidget(m_toolbar, 0, 0);
    layout->addWidget(m_stack, 1, 0);
    layout->setRowStretch(1, 1);

    connect(m_combo_preset, &QComboBox::currentIndexChanged, this,
            &FontWidget::onPresetChanged);
    connect(m_combo_preset, &QComboBox::activated, this,
            [this](int) { clearViewerFocus(); });
    connect(m_edit, &FontPreviewEdit::sigUserEdited, this,
            &FontWidget::onUserEdited);
    connect(m_edit, &FontPreviewEdit::sigToast, this, &FontWidget::sigToast);
    connect(m_edit, &FontPreviewEdit::sigZoomRequested, this,
            [this](int steps) { zoomSize(steps); });
    connect(m_waterfall, &WaterfallWidget::sigSizeClicked, this,
            [this](int pt) {
                setCurrentFontSize(pt);
                clearViewerFocus();
            });
    connect(m_waterfall, &WaterfallWidget::sigZoomRequested, this,
            [this](int steps) {
                zoomSize(steps);
                clearViewerFocus();
            });
    connect(m_slider_line, &QSlider::valueChanged, this, [this](int value) {
        setLineHeight(value / 10.0);
        clearViewerFocus();
    });
    connect(m_slider_line, &QSlider::sliderPressed, this,
            [this]() { clearViewerFocus(); });
    connect(m_slider_space, &QSlider::valueChanged, this, [this](int value) {
        setLetterSpacing(value * 0.5);
        clearViewerFocus();
    });
    connect(m_slider_space, &QSlider::sliderPressed, this,
            [this]() { clearViewerFocus(); });
    connect(m_btn_aa, &QPushButton::toggled, this, [this](bool on) {
        clearViewerFocus();
        if (on && m_btn_aa->isVisible()) {
            repositionAaPanel();
            m_aa_panel->show();
            m_aa_panel->raise();
        }
        else {
            m_aa_panel->hide();
        }
    });
    connect(m_btn_invert, &QPushButton::toggled, this, [this](bool on) {
        m_invert_preview = on;
        applyInvertPreview();
        clearViewerFocus();
    });
    connect(m_chk_waterfall, &QCheckBox::toggled, this, [this](bool on) {
        setWaterfallEnabled(on);
        clearViewerFocus();
    });

    m_slider_line->installEventFilter(this);
    m_slider_space->installEventFilter(this);
    m_combo_preset->installEventFilter(this);

    if (m_label_line) {
        m_label_line->setText(QString::number(m_line_height, 'f', 1));
    }
    if (m_label_space) {
        m_label_space->setText(QString::number(m_letter_spacing, 'f', 1));
    }

    // Seed the editor with the first preset so an untouched viewer is not blank.
    // The first added item is already current, so setCurrentIndex(0) would not
    // emit anything; drive the handler explicitly.
    if (m_combo_preset->count() > 0) {
        onPresetChanged(0);
    }
}

void FontWidget::initSampleTab()
{
    ui->scrollArea_sample->setFocusPolicy(Qt::NoFocus);
    ui->label_sample->setFocusPolicy(Qt::NoFocus);
    ui->label_sample->setTextFormat(Qt::PlainText);
    ui->label_sample->setAlignment(Qt::AlignCenter);
    ui->label_sample->setWordWrap(true);
    ui->label_sample->installEventFilter(this);
}

void FontWidget::initCharTab()
{
    auto *wnd_char_sa = new QScrollArea(this);
    m_wnd_char        = new CharacterWidget(this);
    wnd_char_sa->setFrameShape(QFrame::NoFrame);
    wnd_char_sa->setFocusPolicy(Qt::NoFocus);
    wnd_char_sa->setWidget(m_wnd_char);
    ui->tabWidget->addTab(wnd_char_sa, "Characters");
    m_wnd_char->installEventFilter(this);
}

void FontWidget::initGlyphTab()
{
    auto *wnd_glyph_sa = new QScrollArea(this);
    m_wnd_gi           = new GlyphInspectorWidget(this);
    wnd_glyph_sa->setFrameShape(QFrame::NoFrame);
    wnd_glyph_sa->setFocusPolicy(Qt::NoFocus);
    wnd_glyph_sa->setWidgetResizable(true);
    wnd_glyph_sa->setWidget(m_wnd_gi);
    ui->tabWidget->addTab(wnd_glyph_sa, "Glyph Inspector");
    m_tab_gi = wnd_glyph_sa;

    connect(m_wnd_char, &CharacterWidget::selectionChanged, this,
            [this](QChar ch) {
                const int pt = getCurrentFontSize();
                m_wnd_gi->updateGlyph(ch,
                                      buildPreviewFont(pt, PreviewFontRole::Glyph),
                                      m_font_path);

                // Update tab title with character code
                int tab_index = ui->tabWidget->indexOf(m_tab_gi);
                if (tab_index != -1) {
                    if (ch.isNull()) {
                        ui->tabWidget->setTabText(tab_index, "Glyph Inspector");
                    }
                    else {
                        QString hex = QString("%1")
                                          .arg(ch.unicode(), 4, 16, QChar('0'))
                                          .toUpper();
                        QString title
                            = QString("Glyph Inspector - U+%1").arg(hex);
                        ui->tabWidget->setTabText(tab_index, title);
                    }
                }
            });

    connect(m_wnd_char, &CharacterWidget::characterDoubleClicked, this,
            [this](QChar ch) {
                Q_UNUSED(ch);
                int tab_index = ui->tabWidget->indexOf(m_tab_gi);
                if (tab_index != -1) {
                    ui->tabWidget->setCurrentIndex(tab_index);
                }
            });
}

bool FontWidget::init(const QString &p)
{
    // An instance binds one font, but a repeated init() must not leak the
    // previous registration.
    releaseApplicationFont();
    m_font_path = p;
    m_font_id   = QFontDatabase::addApplicationFont(p);
    if (-1 == m_font_id) {
        qprintt << "addApplicationFont err";
        return false;
    }
    const QStringList names = QFontDatabase::applicationFontFamilies(m_font_id);
    if (names.isEmpty()) {
        qprintt << "applicationFontFamilies err";
        releaseApplicationFont();
        return false;
    }
    initUI(names);
    // init data after initUI
    m_wnd_char->init(p);

    onTabChanged();
    onNameChanged();
    updateTheme(m_theme);
    clearViewerFocus();
    return true;
}

void FontWidget::clearViewerFocus()
{
    QWidget *focus = QApplication::focusWidget();
    if (focus && (focus == this || isAncestorOf(focus))) {
        focus->clearFocus();
    }
}

QFont FontWidget::buildPreviewFont(int pt_size, PreviewFontRole role) const
{
    const QString name  = ui->comboBox_family->currentText();
    const QString style = ui->comboBox_style->currentText();

    QFont ft(name);
    ft.setPointSize(qMax(1, pt_size));
    // doesn't work: https://bugreports.qt.io/browse/QTBUG-69499
    // ft.setStyleName(ui->comboBox_style->currentText());
    if (auto weight = QFontDatabase::weight(name, style); weight != -1) {
        ft.setWeight(QFont::Weight(weight));
    }
    // there are things like Demi-Italic exists, but we can't support these
    ft.setItalic(QFontDatabase::italic(name, style));

    if (role == PreviewFontRole::Text && !qFuzzyIsNull(m_letter_spacing)) {
        ft.setLetterSpacing(QFont::AbsoluteSpacing, m_letter_spacing);
    }
    return ft;
}

QString FontWidget::waterfallText() const
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

QVector<int> FontWidget::waterfallSizes() const
{
    QVector<int> sizes(std::begin(kWaterfallSizes), std::end(kWaterfallSizes));
    const int current = getCurrentFontSize();
    if (current > 0 && !sizes.contains(current)) {
        sizes.append(current);
    }
    std::sort(sizes.begin(), sizes.end());
    return sizes;
}

void FontWidget::syncWaterfall()
{
    if (!m_waterfall) {
        return;
    }
    m_waterfall->setSizes(waterfallSizes());
    m_waterfall->setCurrentSize(getCurrentFontSize());
    m_waterfall->setPreviewText(waterfallText());
}

void FontWidget::ensureWaterfallRowVisible(int pt)
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
    const int top    = int(row.top());
    const int bottom = int(row.bottom());
    if (top < bar->value()) {
        bar->setValue(qMax(0, top - 8));
    }
    else if (bottom > bar->value() + page_h) {
        bar->setValue(bottom - page_h + 8);
    }
}

void FontWidget::ensureWaterfallRowVisibleLater()
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
        ensureWaterfallRowVisible(getCurrentFontSize());
    });
}

void FontWidget::repositionAaPanel()
{
    if (!m_aa_panel || !m_btn_aa || !ui->tab_text) {
        return;
    }
    m_aa_panel->adjustSize();
    const QSize panel_sz
        = m_aa_panel->sizeHint().expandedTo(m_aa_panel->minimumSizeHint());
    const QPoint btn_bl
        = m_btn_aa->mapTo(ui->tab_text, QPoint(0, m_btn_aa->height()));
    const int margin = 6;
    const int max_x
        = qMax(margin, ui->tab_text->width() - panel_sz.width() - margin);
    const int max_y
        = qMax(margin, ui->tab_text->height() - panel_sz.height() - margin);
    const int x = qBound(margin, btn_bl.x(), max_x);
    const int y = qBound(margin, btn_bl.y() + 4, max_y);
    m_aa_panel->setGeometry(QRect(QPoint(x, y), panel_sz));
}

void FontWidget::setCollapsed(bool c)
{
    if (m_is_collapsed == c) {
        return;
    }
    m_is_collapsed = c;

    ui->widget_top_left->setVisible(!c);
    ui->label_info->setVisible(!c);
    m_label_name_collapsed->setVisible(c);
    if (c && m_btn_aa && m_btn_aa->isChecked()) {
        m_btn_aa->setChecked(false);
    }

    updateTheme(m_theme);
    emit sigCollapsedChanged(c);
}

void FontWidget::setLineHeight(qreal h)
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
    }
    if (m_edit) {
        m_edit->setLineHeight(m_line_height);
    }
}

void FontWidget::setLetterSpacing(qreal s)
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
    }
    updateTabTextPreview();
}

void FontWidget::setWaterfallEnabled(bool on)
{
    m_waterfall_enabled = on;
    if (m_chk_waterfall && m_chk_waterfall->isChecked() != on) {
        QSignalBlocker blocker(m_chk_waterfall);
        m_chk_waterfall->setChecked(on);
    }
    if (m_stack) {
        m_stack->setCurrentIndex(on ? 1 : 0);
    }
    if (on) {
        clearViewerFocus();
        syncWaterfall();
        m_waterfall_needs_scroll = true;
        ensureWaterfallRowVisible(getCurrentFontSize());
        if (m_waterfall_needs_scroll) {
            ensureWaterfallRowVisibleLater();
        }
    }
    else {
        m_waterfall_needs_scroll = false;
    }
}

void FontWidget::setInvertPreview(bool on)
{
    m_invert_preview = on;
    if (m_btn_invert && m_btn_invert->isChecked() != on) {
        QSignalBlocker blocker(m_btn_invert);
        m_btn_invert->setChecked(on);
    }
    applyInvertPreview();
}

void FontWidget::applyInvertPreview()
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

    if (m_waterfall) {
        m_waterfall->setInverted(m_invert_preview);
    }
}

void FontWidget::clearPreviewUndoHistory()
{
    if (m_edit && m_edit->document()) {
        m_edit->document()->clearUndoRedoStacks();
    }
}

void FontWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange
        || event->type() == QEvent::ApplicationPaletteChange) {
        applyInvertPreview();
        if (m_waterfall) {
            m_waterfall->update();
        }
    }
}

void FontWidget::onNameChanged()
{
    const auto name = ui->comboBox_family->currentText();
    m_label_name_collapsed->setText(name);
    const auto styles = QFontDatabase::styles(name);
    {
        QSignalBlocker blocker(ui->comboBox_style);
        ui->comboBox_style->clear();
        ui->comboBox_style->addItems(styles);
    }
    ui->comboBox_style->setEnabled(styles.size() > 1);

    onStyleChanged();
}

void FontWidget::onStyleChanged()
{
    // if the size is smooth, make the item bold.
    // unless they are the same, no need to bold all items.
    QFont ft           = this->font();
    const auto cb_sz_t = ui->comboBox_sz->count();
    for (int i = 0; i < cb_sz_t; ++i) {
        ui->comboBox_sz->setItemData(i, QVariant(ft), Qt::FontRole);
    }

    const auto pt_sz = QFontDatabase::smoothSizes(
        ui->comboBox_family->currentText(), ui->comboBox_style->currentText());

    if (pt_sz.size() != cb_sz_t) {
        ft.setBold(true);
        for (int i = 0; i < cb_sz_t; ++i) {
            if (pt_sz.contains(ui->comboBox_sz->itemText(i).toInt())) {
                ui->comboBox_sz->setItemData(i, QVariant(ft), Qt::FontRole);
            }
        }
    }
    updateTabTextPreview();
    updateSamplePreview();
    updateInfo();
}

void FontWidget::updateInfo()
{
    const QString name  = ui->comboBox_family->currentText();
    const QString style = ui->comboBox_style->currentText();

    QString text = QString::number(m_wnd_char->getCharCount());
    text += " Characters\n";

    const auto weight = QFontDatabase::weight(name, style);
    if (weight != -1) {
        text += "Weight: " + QString::number(weight) + "\n";
    }

    if (QFontDatabase::isFixedPitch(name, style)) {
        text += "Fixed Pitch\n";
    }

    QString line;
    if (QFontDatabase::isScalable(name, style)) {
        line += "Scalable, ";
    }
    if (QFontDatabase::isSmoothlyScalable(name, style)) {
        line += "Smoothly Scalable, ";
    }
    if (QFontDatabase::isBitmapScalable(name, style)) {
        line += "Bitmap Scalable, ";
    }
    if (!line.isEmpty()) {
        line = line.trimmed();
        line = line.left(line.size() - 1);
        text.append(line + "\n");
    }

    if (QFontDatabase::isPrivateFamily(name)) {
        text += "Private Family\n";
    }

    const auto ws = QFontDatabase::writingSystems(name);
    line.clear();
    for (auto i : ws) {
        line.append(QFontDatabase::writingSystemName(i));
        line.append(", ");
    }
    if (!line.isEmpty()) {
        line = line.trimmed();
        if (line.endsWith(",")) {
            line = line.left(line.size() - 1);
        }
        text.append(line + "\n");
    }

    ui->label_info->setText(text.trimmed());
}

void FontWidget::onTabChanged()
{
    // Switching tabs is a chrome interaction: leaving the caret in a hidden
    // editor would keep the host's editing guard armed.
    clearViewerFocus();

    auto cur_wnd = ui->tabWidget->currentWidget();
    if (cur_wnd != ui->tab_text && m_btn_aa && m_btn_aa->isChecked()) {
        m_btn_aa->setChecked(false);
    }
    if (cur_wnd == ui->tab_text) {
        updateTabTextPreview();
        if (m_stack) {
            m_stack->setCurrentIndex(m_waterfall_enabled ? 1 : 0);
        }
    }
    else if (cur_wnd == ui->tab_sample) {
        updateSamplePreview();
    }
}

void FontWidget::updateTabTextPreview()
{
    if (!m_edit || !m_wnd_char) {
        return;
    }
    const int pt = getCurrentFontSize();
    const QFont text_font  = buildPreviewFont(pt, PreviewFontRole::Text);
    const QFont glyph_font = buildPreviewFont(pt, PreviewFontRole::Glyph);

    m_edit->setFont(text_font);
    m_wnd_char->updateFont(glyph_font);
    if (m_wnd_gi) {
        m_wnd_gi->updateFont(glyph_font);
    }
    // The Sample tab shares the family/style/size state; only its text differs,
    // so refresh the font here instead of waiting for the next family change.
    if (ui->label_sample) {
        ui->label_sample->setFont(text_font);
    }

    syncWaterfall();
    if (m_waterfall) {
        m_waterfall->refreshFont();
    }
    if (m_waterfall_enabled) {
        m_waterfall_needs_scroll = true;
        ensureWaterfallRowVisible(pt);
        if (m_waterfall_needs_scroll) {
            ensureWaterfallRowVisibleLater();
        }
    }
}

void FontWidget::updateSamplePreview()
{
    if (!ui->label_sample) {
        return;
    }
    const auto name = ui->comboBox_family->currentText();
    QString text;
    const auto ws = QFontDatabase::writingSystems(name);
    for (auto i : ws) {
        text.append(QFontDatabase::writingSystemSample(i));
        text.append("\n");
    }
    ui->label_sample->setText(text.trimmed());
    ui->label_sample->setFont(
        buildPreviewFont(getCurrentFontSize(), PreviewFontRole::Text));
}

void FontWidget::onPresetChanged(int index)
{
    if (!m_edit || !m_combo_preset || index < 0) {
        return;
    }

    const bool is_recent
        = (m_combo_preset->itemData(index, Qt::UserRole + 1).toInt() == 1);
    const QString text = is_recent
                             ? m_last_edited
                             : m_combo_preset->itemData(index, Qt::UserRole).toString();
    if (text.isEmpty() && !is_recent) {
        return;
    }

    m_edit->setPreviewText(text);
    syncWaterfall();
    if (m_waterfall) {
        m_waterfall->update();
    }
    clearViewerFocus();
}

void FontWidget::ensureRecentItem()
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
    if (inserted && m_toolbar) {
        static_cast<TextToolbar *>(m_toolbar)->refreshLayout();
        if (m_aa_panel && m_aa_panel->isVisible()) {
            repositionAaPanel();
        }
    }
}

void FontWidget::onUserEdited()
{
    if (!m_edit) {
        return;
    }
    m_last_edited = m_edit->toPlainText();
    ensureRecentItem();

    if (m_waterfall_enabled) {
        syncWaterfall();
        if (m_waterfall) {
            m_waterfall->update();
        }
    }
}

void FontWidget::copy()
{
    qApp->setOverrideCursor(Qt::BusyCursor);

    QPixmap pix;
    auto cur_tab = ui->tabWidget->currentWidget();
    const qreal dpr = devicePixelRatioF();

    if (cur_tab == ui->tab_text) {
        if (m_waterfall_enabled && m_waterfall) {
            const QSize logical = m_waterfall->contentSize();
            pix = QPixmap(QSize(qCeil(logical.width() * dpr),
                                qCeil(logical.height() * dpr)));
            if (!pix.isNull()) {
                pix.setDevicePixelRatio(dpr);
                pix.fill(m_waterfall->baseColor());
                QPainter painter(&pix);
                m_waterfall->renderContent(&painter);
                painter.end();
            }
        }
        else if (m_edit) {
            pix = renderEditPixmap();
        }
    }
    else if (cur_tab == ui->tab_sample) {
        pix = renderSamplePixmap();
    }
    else if (cur_tab == m_tab_gi) {
        if (!m_wnd_gi || !m_wnd_gi->hasSelection()) {
            qApp->restoreOverrideCursor();
            emit sigToast("Please select a character first");
            return;
        }
        pix = m_wnd_gi->getPreviewPixmap();
        if (pix.isNull()) {
            qprintt << __FUNCTION__ << "glyph inspector pix.isNull";
            qApp->restoreOverrideCursor();
            emit sigToast("Failed to copy preview");
            return;
        }
    }
    else if (m_wnd_char) {
        // Characters tab
        pix = m_wnd_char->getSelectedCharPix();
        if (pix.isNull()) {
            qprintt << __FUNCTION__ << "pix.isNull";
            qApp->restoreOverrideCursor();
            emit sigToast("Please select a character first");
            return;
        }
    }

    if (pix.isNull()) {
        qApp->restoreOverrideCursor();
        emit sigToast("Failed to copy preview");
        return;
    }

    qApp->clipboard()->setPixmap(pix);
    qApp->restoreOverrideCursor();
}

QSizeF FontWidget::prepareEditDocumentSize() const
{
    if (!m_edit) {
        return {};
    }
    QTextDocument *doc = m_edit->document();
    doc->setTextWidth(qMax(1, m_edit->viewport()->width()));
    return doc->documentLayout()->documentSize();
}

void FontWidget::drawEditDocument(QPainter &painter,
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

QPixmap FontWidget::renderEditPixmap()
{
    const QSizeF doc_size = prepareEditDocumentSize();
    if (doc_size.isEmpty()) {
        return {};
    }
    const qreal dpr = devicePixelRatioF();

    QPixmap pix(QSize(qCeil(doc_size.width() * dpr),
                      qCeil(doc_size.height() * dpr)));
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

QPixmap FontWidget::renderSamplePixmap()
{
    QLabel *label = ui->label_sample;
    const qreal dpr = devicePixelRatioF();

    QPixmap pix(QSize(qCeil(label->width() * dpr), qCeil(label->height() * dpr)));
    if (pix.isNull()) {
        return {};
    }
    pix.setDevicePixelRatio(dpr);
    pix.fill(label->palette().color(label->backgroundRole()));
    QPainter painter(&pix);
    label->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    painter.end();
    return pix;
}

bool FontWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->tab_text && event->type() == QEvent::Resize) {
        if (m_aa_panel && m_aa_panel->isVisible()) {
            repositionAaPanel();
        }
        return false;
    }

    if (watched == m_scroll_waterfall && event->type() == QEvent::Show) {
        if (m_waterfall_enabled && m_waterfall_needs_scroll) {
            ensureWaterfallRowVisibleLater();
        }
        return false;
    }

    if (event->type() == QEvent::Wheel) {
        auto *wheel = static_cast<QWheelEvent *>(event);

        if (watched == m_slider_line || watched == m_slider_space
            || watched == m_combo_preset || watched == ui->comboBox_family
            || watched == ui->comboBox_style || watched == ui->comboBox_sz) {
            // Steppers, sliders and combos change their value on a hover wheel
            // even without focus; only swallowing the event keeps them stable.
            wheel->accept();
            return true;
        }

        if (watched == ui->label_sample || watched == m_wnd_char) {
            if (wheel->modifiers() != Qt::ControlModifier) {
                return false;
            }
            if (wheel->angleDelta().y() == 0) {
                wheel->accept();
                return true;
            }
            zoomSize(wheel->angleDelta().y() > 0 ? 1 : -1);
            clearViewerFocus();
            wheel->accept();
            return true;
        }
        return false;
    }
    return QWidget::eventFilter(watched, event);
}

void FontWidget::zoomSize(int steps)
{
    if (steps == 0) {
        return;
    }
    const int count = ui->comboBox_sz->count();
    if (count == 0) {
        return;
    }
    const int current = ui->comboBox_sz->currentIndex();
    const int index   = qBound(0, current + steps, count - 1);
    if (index != current) {
        ui->comboBox_sz->setCurrentIndex(index);
    }
}

void FontWidget::updateDPR(qreal)
{
    updateTheme(m_theme);
}

void FontWidget::setCurrentText(const QString &t)
{
    if (t.isEmpty() || !m_edit) {
        return;
    }
    m_last_edited = t;
    ensureRecentItem();
    m_edit->setPreviewText(t);
    syncWaterfall();
}

QString FontWidget::getCurrentText() const
{
    return m_edit ? m_edit->toPlainText() : QString();
}

void FontWidget::setCurrentFontSize(int s)
{
    int size = s;
    if (size < 1 || size > 999) {
        size = g_def_pt;
    }

    int index = ui->comboBox_sz->findText(QString::number(size));
    if (index == -1) {
        index = ui->comboBox_sz->count();
        for (int i = 0; i < ui->comboBox_sz->count(); ++i) {
            if (ui->comboBox_sz->itemText(i).toInt() > size) {
                index = i;
                break;
            }
        }
        ui->comboBox_sz->insertItem(index, QString::number(size));
    }
    ui->comboBox_sz->setCurrentIndex(index);
}

int FontWidget::getCurrentFontSize() const
{
    return ui->comboBox_sz->currentText().toInt();
}

void FontWidget::updateTheme(int theme)
{
    m_theme = theme;

    if (m_btn_copy && m_btn_svg) {
        const qreal dpr = devicePixelRatioF();

        // Get tab bar height
        int sz = 0;
        if (auto bar = ui->tabWidget->findChild<QTabBar *>()) {
            sz = bar->height();
            if (sz <= 0) {
                sz = bar->sizeHint().height();
            }
        }
        if (sz <= 0) {
            sz = qRound(32 * dpr);
        }

        // width == height, and height matches tab height
        const int btn_sz  = sz;
        const int margin  = qRound(6 * dpr);
        const int icon_sz = btn_sz - 2 * margin;

        const QColor color = palette().color(QPalette::WindowText);
        auto icon_copy     = svgIcon(g_svg_copy_image, color, icon_sz, dpr);
        auto icon_svg      = svgIcon(g_svg_save_svg, color, icon_sz, dpr);

        m_btn_copy->setFixedSize(btn_sz, btn_sz);
        m_btn_copy->setIconSize(QSize(icon_sz, icon_sz));
        m_btn_copy->setIcon(icon_copy);

        m_btn_svg->setFixedSize(btn_sz, btn_sz);
        m_btn_svg->setIconSize(QSize(icon_sz, icon_sz));
        m_btn_svg->setIcon(icon_svg);

        if (m_btn_toggle) {
            m_btn_toggle->setFixedSize(btn_sz, btn_sz);
            m_btn_toggle->setIconSize(QSize(icon_sz, icon_sz));
            auto icon_toggle
                = svgIcon(m_is_collapsed ? g_svg_chevron_down : g_svg_chevron_up,
                          color, icon_sz, dpr);
            m_btn_toggle->setIcon(icon_toggle);
            m_btn_toggle->setToolTip(m_is_collapsed ? "Expand" : "Collapse");
        }
    }

    if (m_wnd_gi) {
        m_wnd_gi->updateTheme(theme);
    }
    applyInvertPreview();
    if (m_waterfall) {
        m_waterfall->update();
    }
}

bool FontWidget::saveAsSvgTo(const QString &path, QString *error_out)
{
    auto fail = [error_out](const QString &msg) {
        if (error_out) {
            *error_out = msg;
        }
        return false;
    };

    auto cur_tab         = ui->tabWidget->currentWidget();
    const bool is_text   = (cur_tab == ui->tab_text);
    const bool is_sample = (cur_tab == ui->tab_sample);
    const bool is_gi     = (cur_tab == m_tab_gi);

    QSize sz;
    std::function<void(QPainter &)> draw_fn;
    QColor background;

    if (is_text && m_waterfall_enabled && m_waterfall) {
        sz         = m_waterfall->contentSize();
        background = m_waterfall->baseColor();
        draw_fn    = [this](QPainter &p) { m_waterfall->renderContent(&p); };
    }
    else if (is_text && m_edit) {
        const QSizeF doc_size = prepareEditDocumentSize();
        sz    = QSize(qCeil(doc_size.width()), qCeil(doc_size.height()));
        background = m_edit->palette().color(QPalette::Base);
        draw_fn    = [this, doc_size](QPainter &p) {
            drawEditDocument(p, doc_size);
        };
    }
    else if (is_sample) {
        sz         = ui->label_sample->size();
        background = ui->label_sample->palette().color(
            ui->label_sample->backgroundRole());
        draw_fn    = [this](QPainter &p) {
            ui->label_sample->render(&p, QPoint(), QRegion(),
                                     QWidget::DrawChildren);
        };
    }
    else if (is_gi) {
        if (!m_wnd_gi || !m_wnd_gi->hasSelection()) {
            return fail(QStringLiteral("Please select a character first"));
        }
        QPixmap preview = m_wnd_gi->getPreviewPixmap();
        if (preview.isNull()) {
            return fail(QStringLiteral("Failed to generate preview"));
        }
        sz      = preview.deviceIndependentSize().toSize();
        draw_fn = [preview](QPainter &p) { p.drawPixmap(0, 0, preview); };
    }
    else if (m_wnd_char) {
        const auto info = m_wnd_char->selectedCharInfo();
        if (info.ch.isNull()) {
            return fail(QStringLiteral("Please select a character first"));
        }
        const int side = qMax(64, info.square_size * 2);
        sz             = QSize(side, side);
        QFont ft       = info.font;
        ft.setPixelSize(side / 2);
        draw_fn = [ch = info.ch, ft, side](QPainter &p) {
            p.setRenderHints(QPainter::Antialiasing
                             | QPainter::TextAntialiasing);
            p.setFont(ft);
            p.drawText(QRect(0, 0, side, side), Qt::AlignCenter, QString(ch));
        };
    }

    if (!draw_fn || !sz.isValid() || sz.isEmpty()) {
        return fail(QStringLiteral("Nothing to save"));
    }

    QSvgGenerator gen;
    gen.setFileName(path);
    gen.setSize(sz);
    gen.setViewBox(QRect(QPoint(0, 0), sz));
    // Layouts are measured at the widget's logical DPI; match it so font sizes
    // in the SVG stay proportional to the viewBox instead of 72-DPI scaled.
    gen.setResolution(qMax(72, logicalDpiX()));
    gen.setTitle(ui->comboBox_family->currentText());

    QPainter p(&gen);
    if (!p.isActive()) {
        return fail(QStringLiteral("Failed to save: ") + path);
    }
    if (background.isValid()) {
        p.fillRect(QRect(QPoint(0, 0), sz), background);
    }
    draw_fn(p);
    p.end();
    return true;
}

void FontWidget::saveAsSvg()
{
    auto cur_tab     = ui->tabWidget->currentWidget();
    const bool is_gi = (cur_tab == m_tab_gi);
    const bool is_char
        = (cur_tab != ui->tab_text && cur_tab != ui->tab_sample && !is_gi);

    QChar selected_char;
    if ((is_gi || is_char) && m_wnd_char) {
        selected_char = m_wnd_char->selectedCharInfo().ch;
    }

    const QString dir
        = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString family = ui->comboBox_family->currentText();
    QString stem         = family;
    if (!selected_char.isNull()) {
        stem
            += QString("_U+%1").arg(selected_char.unicode(), 4, 16, QChar('0'));
    }
    stem.replace(QRegularExpression(R"([\/:*?"<>|])"), "_");
    const QString path = dir + "/" + stem + ".svg";

    QString err;
    if (!saveAsSvgTo(path, &err)) {
        emit sigToast(err);
        return;
    }

    QFontMetrics fm(this->font());
    emit sigToast("Saved: "
                  + fm.elidedText(path, Qt::ElideMiddle, this->width() * 0.7));
}
