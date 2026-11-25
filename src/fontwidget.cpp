#include "fontwidget.h"

#include <qt_windows.h>

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QClipboard>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLayout>
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

int axisStepsForRange(float min_val, float max_val)
{
    const double span = double(max_val) - double(min_val);
    if (span <= 0.0) {
        return 1;
    }
    if (span <= 100.0) {
        return 200;
    }
    return qBound(200, qRound(span), 1000);
}

float axisValueFromSlider(float min_val, float max_val, int steps, int pos)
{
    if (steps <= 0 || max_val <= min_val) {
        return min_val;
    }
    return min_val + (max_val - min_val) * (float(pos) / float(steps));
}

int axisSliderFromValue(float min_val, float max_val, int steps, float val)
{
    if (steps <= 0 || max_val <= min_val) {
        return 0;
    }
    const float t = qBound(0.0f, (val - min_val) / (max_val - min_val), 1.0f);
    return qRound(t * steps);
}

QString formatAxisValue(float val, float min_val, float max_val)
{
    const float span = max_val - min_val;
    if (span <= 20.0f) {
        return QString::number(val, 'f', 2);
    }
    if (qAbs(val - qRound(val)) < 0.05f) {
        return QString::number(qRound(val));
    }
    return QString::number(val, 'f', 1);
}

bool isLigatureFeatureTag(const QByteArray &tag)
{
    return tag == QByteArrayLiteral("liga")
           || tag == QByteArrayLiteral("clig")
           || tag == QByteArrayLiteral("dlig")
           || tag == QByteArrayLiteral("hlig");
}

QString formatFeaturePillText(const FontFeatureInfo &info, const QFontMetrics &fm)
{
    const QString tag_str = QString::fromLatin1(info.tag);
    if (info.friendly_name.isEmpty() || info.friendly_name == tag_str) {
        return tag_str;
    }
    const QString label = fm.elidedText(info.friendly_name, Qt::ElideRight, 130);
    return QStringLiteral("%1 · %2").arg(tag_str, label);
}

class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget *parent = nullptr, int h_spacing = 6,
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
        return doLayout(QRect(0, 0, width, 0), true);
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
    int m_h_spacing = 6;
    int m_v_spacing = 4;
};

class FeaturePillButton : public QPushButton {
public:
    explicit FeaturePillButton(const FontFeatureInfo &info,
                               QWidget *parent = nullptr)
        : QPushButton(parent)
    {
        setCheckable(true);
        setChecked(info.default_on);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
        // paintEvent() reads underMouse(), but hover enter/leave events (and the
        // repaint they trigger) are only delivered when WA_Hover is set.
        setAttribute(Qt::WA_Hover, true);
        QFont f = font();
        f.setPointSizeF(qMax(8.0, f.pointSizeF() - 1.0));
        setFont(f);
        setText(formatFeaturePillText(info, fontMetrics()));
        setShortcut(QKeySequence());
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    QSize sizeHint() const override
    {
        const QFontMetrics fm = fontMetrics();
        return QSize(fm.horizontalAdvance(text()) + 16, fm.height() + 8);
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
        const QRectF r      = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius  = r.height() / 2.0;
        const bool enabled  = isEnabled();
        const bool checked  = isChecked();

        if (!enabled) {
            painter.setOpacity(0.45);
        }

        QColor bg     = pal.color(QPalette::Button);
        QColor border = pal.color(QPalette::Mid);
        QColor fg     = pal.color(QPalette::ButtonText);

        if (checked) {
            const QColor accent = pal.color(QPalette::Highlight);
            bg                  = accent;
            bg.setAlpha(46);
            border = accent;
            fg     = pal.color(QPalette::WindowText);
        }
        else {
            bg.setAlpha(130);
            border.setAlpha(140);
        }

        if (enabled && underMouse()) {
            border = checked ? pal.color(QPalette::Highlight)
                             : pal.color(QPalette::WindowText);
            if (!checked) {
                bg.setAlpha(190);
            }
        }

        painter.setPen(QPen(border, 1.0));
        painter.setBrush(bg);
        painter.drawRoundedRect(r, radius, radius);

        painter.setPen(fg);
        painter.setFont(font());
        painter.drawText(r, Qt::AlignCenter, text());
    }
};

constexpr int kFeaturePillHSpacing = 6;
constexpr int kFeaturePillVSpacing = 4;

class FeaturePillsScrollArea : public QScrollArea {
public:
    explicit FeaturePillsScrollArea(QWidget *host, QWidget *parent = nullptr)
        : QScrollArea(parent), m_host(host)
    {
        setFrameShape(QFrame::NoFrame);
        setWidgetResizable(true);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setFocusPolicy(Qt::NoFocus);
        if (viewport()) {
            viewport()->setFocusPolicy(Qt::NoFocus);
        }
        if (verticalScrollBar()) {
            verticalScrollBar()->setFocusPolicy(Qt::NoFocus);
        }
        if (horizontalScrollBar()) {
            horizontalScrollBar()->setFocusPolicy(Qt::NoFocus);
        }
        QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Fixed);
        sp.setHeightForWidth(true);
        setSizePolicy(sp);
        setWidget(m_host);
    }

    bool hasHeightForWidth() const override
    {
        return true;
    }

    int heightForWidth(int width) const override
    {
        const int flow_h = contentHeightForWidth(width);
        return qMin(flow_h, maxAreaHeight());
    }

    QSize sizeHint() const override
    {
        const int w = width() > 0 ? width() : 400;
        return QSize(w, heightForWidth(w));
    }

    QSize minimumSizeHint() const override
    {
        const int w      = width() > 0 ? width() : 200;
        const int flow_h = contentHeightForWidth(w);
        return QSize(0, qMin(flow_h, maxAreaHeight()));
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QScrollArea::resizeEvent(event);
        updateGeometry();
    }

private:
    int maxAreaHeight() const
    {
        int pill_h = 24;
        if (m_host && m_host->layout() && m_host->layout()->count() > 0) {
            if (QLayoutItem *first = m_host->layout()->itemAt(0)) {
                pill_h = qMax(pill_h, first->sizeHint().height());
            }
        }
        return pill_h * 3 + kFeaturePillVSpacing * 2;
    }

    int contentHeightForWidth(int width) const
    {
        if (!m_host || !m_host->layout() || m_host->layout()->count() == 0) {
            return 0;
        }
        const int w = qMax(1, width);
        int h       = m_host->layout()->heightForWidth(w);
        if (h > maxAreaHeight() && verticalScrollBar()) {
            const int sb_w = verticalScrollBar()->sizeHint().width();
            h = m_host->layout()->heightForWidth(qMax(1, w - sb_w));
        }
        return qMax(0, h);
    }

    QWidget *m_host = nullptr;
};

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
    m_inspector.close();
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

    // Move tabWidget below variable-axes and OpenType feature bars.
    ui->gridLayout->addWidget(ui->tabWidget, 3, 0);
    initAxesBar();
    initFeaturesBar();

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
    m_tab_char = wnd_char_sa;
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
                const FontDecorationMetrics deco
                    = m_inspector.decorationMetrics(m_face_index, m_axis_values);
                const GlyphOutlineData outline = ch.isNull()
                    ? GlyphOutlineData{}
                    : m_inspector.glyphOutline(m_face_index, ch, m_axis_values);
                m_wnd_gi->updateGlyph(ch,
                                      buildPreviewFont(pt, PreviewFontRole::Glyph),
                                      m_font_path, deco, outline);
                m_gi_dirty = false;

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

void FontWidget::initAxesBar()
{
    m_axis_timer = new QTimer(this);
    m_axis_timer->setSingleShot(true);
    m_axis_timer->setInterval(16);
    connect(m_axis_timer, &QTimer::timeout, this,
            &FontWidget::refreshActiveTabFromAxes);

    m_axes_bar = new QWidget(this);
    m_axes_bar->setFocusPolicy(Qt::NoFocus);
    m_axes_root = new QVBoxLayout(m_axes_bar);
    m_axes_root->setContentsMargins(0, 2, 0, 2);
    m_axes_root->setSpacing(4);

    m_axes_warning = new QLabel(
        tr("Variable font axes may be inactive under the current font backend."),
        m_axes_bar);
    m_axes_warning->setWordWrap(true);
    m_axes_warning->setFocusPolicy(Qt::NoFocus);
    m_axes_warning->hide();
    m_axes_root->addWidget(m_axes_warning);

    m_axes_rows_host = new QWidget(m_axes_bar);
    m_axes_rows_host->setFocusPolicy(Qt::NoFocus);
    m_axes_grid = new QGridLayout(m_axes_rows_host);
    m_axes_grid->setContentsMargins(0, 0, 0, 0);
    m_axes_grid->setHorizontalSpacing(8);
    m_axes_grid->setVerticalSpacing(4);
    m_axes_grid->setColumnStretch(1, 1);
    m_axes_root->addWidget(m_axes_rows_host);

    m_axes_bar->hide();
    ui->gridLayout->addWidget(m_axes_bar, 1, 0);
}

void FontWidget::initFeaturesBar()
{
    m_features_bar = new QWidget(this);
    m_features_bar->setFocusPolicy(Qt::NoFocus);
    m_features_root = new QVBoxLayout(m_features_bar);
    m_features_root->setContentsMargins(0, 2, 0, 4);
    m_features_root->setSpacing(4);

    auto header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(8);

    auto title = new QLabel(tr("OpenType Features:"), m_features_bar);
    title->setFocusPolicy(Qt::NoFocus);
    header->addWidget(title);

    m_features_note = new QLabel(tr("Text preview only"), m_features_bar);
    m_features_note->setFocusPolicy(Qt::NoFocus);
    // The spacing hint is long enough to exceed a narrow preview pane, so it
    // wraps instead of widening the whole viewer layout.
    m_features_note->setWordWrap(true);
    applyFeaturesNotePalette();
    header->addWidget(m_features_note, 1);

    m_features_reset = new QPushButton(tr("Reset Features"), m_features_bar);
    m_features_reset->setFocusPolicy(Qt::NoFocus);
    m_features_reset->setFlat(true);
    m_features_reset->setCursor(Qt::PointingHandCursor);
    m_features_reset->hide();
    connect(m_features_reset, &QPushButton::clicked, this, [this]() {
        if (!m_feature_overrides.isEmpty()) {
            m_feature_overrides.clear();
            for (const FeaturePillControl &ctrl : m_feature_controls) {
                if (ctrl.button) {
                    QSignalBlocker blocker(ctrl.button);
                    ctrl.button->setChecked(ctrl.default_on);
                }
            }
            updateFeaturePillsState();
            refreshActiveTabFromFeatures();
        }
        clearViewerFocus();
    });
    header->addWidget(m_features_reset);
    m_features_root->addLayout(header);

    m_features_pills_host = new QWidget(m_features_bar);
    m_features_pills_host->setFocusPolicy(Qt::NoFocus);
    m_features_pills_host->setSizePolicy(QSizePolicy::Expanding,
                                         QSizePolicy::Preferred);
    new FlowLayout(m_features_pills_host, kFeaturePillHSpacing,
                   kFeaturePillVSpacing);

    m_features_scroll
        = new FeaturePillsScrollArea(m_features_pills_host, m_features_bar);
    m_features_root->addWidget(m_features_scroll);

    m_features_bar->hide();
    ui->gridLayout->addWidget(m_features_bar, 2, 0);
}

void FontWidget::applyFeaturesNotePalette()
{
    if (!m_features_note) {
        return;
    }
    QPalette note_pal = palette();
    note_pal.setColor(QPalette::WindowText, note_pal.color(QPalette::Mid));
    m_features_note->setPalette(note_pal);
}

void FontWidget::rebuildFeaturesBar()
{
    if (!m_features_bar || !m_features_pills_host) {
        return;
    }

    m_feature_overrides.clear();
    for (const FeaturePillControl &ctrl : m_feature_controls) {
        if (ctrl.button) {
            ctrl.button->hide();
            ctrl.button->deleteLater();
        }
    }
    m_feature_controls.clear();

    if (auto *flow = m_features_pills_host->layout()) {
        while (QLayoutItem *item = flow->takeAt(0)) {
            delete item;
        }
    }

    // Technical validation gate (Qt 6.8.3 headers): Qt provides no per-font
    // OpenType `locl` language override on Windows DirectWrite, so keep the
    // bar free of non-functional language selectors while honoring GSUB/GPOS
    // features via QFont::setFeature().
    const QVector<FontFeatureInfo> features
        = m_inspector.isValid()
              ? m_inspector.inspectFace(m_face_index).ot_features
              : QVector<FontFeatureInfo>{};

    if (features.isEmpty()) {
        updateFeaturePillsState();
        m_features_bar->hide();
        return;
    }

    auto *flow = m_features_pills_host->layout();
    for (const FontFeatureInfo &info : features) {
        auto *btn = new FeaturePillButton(info, m_features_pills_host);
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
                m_feature_overrides.remove(tag);
            }
            else {
                m_feature_overrides.insert(tag, on);
            }
            updateFeaturePillsState();
            refreshActiveTabFromFeatures();
            clearViewerFocus();
        });

        flow->addWidget(btn);
        m_feature_controls.append(
            {tag, def_on, isLigatureFeatureTag(tag), tip, btn});
    }

    updateFeaturePillsState();
    m_features_bar->show();
    m_features_pills_host->updateGeometry();
    if (m_features_scroll) {
        if (auto *vbar = m_features_scroll->verticalScrollBar()) {
            vbar->setValue(0);
        }
        m_features_scroll->updateGeometry();
    }
}

void FontWidget::updateFeaturePillsState()
{
    const bool has_spacing = !qFuzzyIsNull(m_letter_spacing);
    bool has_ligature      = false;
    for (const FeaturePillControl &ctrl : m_feature_controls) {
        if (!ctrl.button) {
            continue;
        }
        const bool disabled_by_spacing = has_spacing && ctrl.is_ligature;
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
    if (m_features_note) {
        if (has_spacing && has_ligature) {
            m_features_note->setText(
                tr("Text preview only · Ligatures disabled while letter spacing ≠ 0"));
        }
        else {
            m_features_note->setText(tr("Text preview only"));
        }
    }
    if (m_features_reset) {
        m_features_reset->setVisible(!m_feature_overrides.isEmpty());
    }
}

void FontWidget::rebuildAxesBar(bool reset_to_defaults)
{
    if (!m_axes_bar || !m_axes_grid) {
        return;
    }

    if (m_axis_timer) {
        m_axis_timer->stop();
    }

    for (const AxisControl &ctrl : m_axis_controls) {
        if (ctrl.slider) {
            ctrl.slider->removeEventFilter(this);
        }
    }
    m_axis_controls.clear();

    while (QLayoutItem *item = m_axes_grid->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }

    const auto &axes = m_inspector.inspectFace(m_face_index).variable_axes;
    if (axes.isEmpty()) {
        m_axis_values.clear();
        m_axes_warning->hide();
        m_axes_bar->hide();
        return;
    }

    QHash<QByteArray, float> next_values;
    for (int row = 0; row < axes.size(); ++row) {
        const FontVariableAxisInfo &ax = axes[row];
        float cur_val                  = ax.default_value;
        if (!reset_to_defaults && m_axis_values.contains(ax.tag)) {
            cur_val = qBound(ax.min_value, m_axis_values.value(ax.tag),
                             ax.max_value);
        }
        next_values.insert(ax.tag, cur_val);

        const QString title
            = (ax.name == QString::fromLatin1(ax.tag))
                  ? ax.name
                  : QStringLiteral("%1 (%2)").arg(
                        ax.name, QString::fromLatin1(ax.tag));
        auto *name_lbl = new QLabel(title, m_axes_rows_host);
        name_lbl->setFocusPolicy(Qt::NoFocus);

        const int steps = axisStepsForRange(ax.min_value, ax.max_value);
        auto *slider    = new QSlider(Qt::Horizontal, m_axes_rows_host);
        slider->setRange(0, steps);
        slider->setValue(
            axisSliderFromValue(ax.min_value, ax.max_value, steps, cur_val));
        slider->setFocusPolicy(Qt::NoFocus);
        slider->setEnabled(ax.max_value > ax.min_value);
        slider->installEventFilter(this);

        auto *val_lbl = new QLabel(
            formatAxisValue(cur_val, ax.min_value, ax.max_value),
            m_axes_rows_host);
        val_lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        val_lbl->setMinimumWidth(
            QFontMetrics(val_lbl->font())
                .horizontalAdvance(QStringLiteral("-1000.00"))
            + 4);
        val_lbl->setFocusPolicy(Qt::NoFocus);

        auto *reset_btn = new QPushButton(tr("Reset"), m_axes_rows_host);
        reset_btn->setFlat(true);
        reset_btn->setCursor(Qt::PointingHandCursor);
        reset_btn->setFocusPolicy(Qt::NoFocus);
        reset_btn->setToolTip(
            tr("Reset %1 to default (%2)")
                .arg(title, formatAxisValue(ax.default_value, ax.min_value,
                                            ax.max_value)));

        m_axes_grid->addWidget(name_lbl, row, 0);
        m_axes_grid->addWidget(slider, row, 1);
        m_axes_grid->addWidget(val_lbl, row, 2);
        m_axes_grid->addWidget(reset_btn, row, 3);

        AxisControl ctrl;
        ctrl.tag         = ax.tag;
        ctrl.min_val     = ax.min_value;
        ctrl.default_val = ax.default_value;
        ctrl.max_val     = ax.max_value;
        ctrl.steps       = steps;
        ctrl.slider      = slider;
        ctrl.value       = val_lbl;
        m_axis_controls.append(ctrl);

        const QByteArray tag = ax.tag;
        connect(slider, &QSlider::sliderPressed, this,
                [this]() { clearViewerFocus(); });
        connect(slider, &QSlider::valueChanged, this, [this, tag](int pos) {
            for (const AxisControl &c : m_axis_controls) {
                if (c.tag == tag) {
                    const float v = axisValueFromSlider(c.min_val, c.max_val,
                                                        c.steps, pos);
                    m_axis_values.insert(tag, v);
                    if (c.value) {
                        c.value->setText(
                            formatAxisValue(v, c.min_val, c.max_val));
                    }
                    break;
                }
            }
            clearViewerFocus();
            if (m_axis_timer && !m_axis_timer->isActive()) {
                m_axis_timer->start();
            }
        });
        connect(reset_btn, &QPushButton::clicked, this, [this, tag]() {
            for (const AxisControl &c : m_axis_controls) {
                if (c.tag == tag) {
                    setVariableAxisValue(tag, c.default_val);
                    break;
                }
            }
            clearViewerFocus();
        });
    }

    m_axis_values = std::move(next_values);
    m_axes_bar->show();
    probeVariableAxisBackend();
}

void FontWidget::setVariableAxisValue(const QByteArray &tag, float value)
{
    for (const AxisControl &c : m_axis_controls) {
        if (c.tag != tag) {
            continue;
        }
        const float finite  = qIsFinite(value) ? value : c.default_val;
        const float clamped = qBound(c.min_val, finite, c.max_val);
        m_axis_values.insert(tag, clamped);
        if (c.slider) {
            QSignalBlocker blocker(c.slider);
            c.slider->setValue(
                axisSliderFromValue(c.min_val, c.max_val, c.steps, clamped));
        }
        if (c.value) {
            c.value->setText(formatAxisValue(clamped, c.min_val, c.max_val));
        }
        if (m_axis_timer) {
            m_axis_timer->stop();
        }
        refreshActiveTabFromAxes();
        return;
    }
}

void FontWidget::syncAxesForStyle(const QString &style)
{
    std::optional<FontNamedInstance> inst;
    if (ui && ui->comboBox_family) {
        // DirectWrite also registers named instances as standalone family names
        // (e.g. family "Roboto Condensed" with style "Regular"). Match the
        // family's subfamily suffix before falling back to bare style so
        // "Regular" under "Roboto Condensed" resolves to "Condensed Regular".
        const QString fam = ui->comboBox_family->currentText();
        QString suffix;
        for (const auto &rec : m_inspector.inspectFace(m_face_index).name_records) {
            if (rec.label == QLatin1String("Family")
                || rec.label == QLatin1String("Typographic Family")) {
                if (fam.startsWith(rec.value + QLatin1Char(' '),
                                   Qt::CaseInsensitive)) {
                    suffix = fam.mid(rec.value.size()).trimmed();
                    break;
                }
            }
        }
        if (!suffix.isEmpty()) {
            if (FontInspectorEngine::normalizeStyleName(style)
                == QLatin1String("regular")) {
                inst = m_inspector.matchNamedInstance(m_face_index, suffix);
            }
            else {
                inst = m_inspector.matchNamedInstance(
                    m_face_index, suffix + QLatin1Char(' ') + style);
                if (!inst.has_value()) {
                    inst = m_inspector.matchNamedInstance(m_face_index, suffix);
                }
            }
        }
    }
    if (!inst.has_value()) {
        inst = m_inspector.matchNamedInstance(m_face_index, style);
    }
    if (!inst.has_value()) {
        return;
    }
    const auto &axes = m_inspector.inspectFace(m_face_index).variable_axes;
    const int n      = qMin(axes.size(), inst->coords.size());
    for (int i = 0; i < n; ++i) {
        const QByteArray &tag = axes[i].tag;
        const float val
            = qBound(axes[i].min_value, inst->coords[i], axes[i].max_value);
        m_axis_values.insert(tag, val);
        for (const AxisControl &c : m_axis_controls) {
            if (c.tag == tag) {
                if (c.slider) {
                    QSignalBlocker blocker(c.slider);
                    c.slider->setValue(axisSliderFromValue(c.min_val, c.max_val,
                                                           c.steps, val));
                }
                if (c.value) {
                    c.value->setText(
                        formatAxisValue(val, c.min_val, c.max_val));
                }
                break;
            }
        }
    }
}

void FontWidget::probeVariableAxisBackend()
{
    if (!m_axes_warning) {
        return;
    }
    if (m_gdi_probed) {
        m_axes_warning->setVisible(m_gdi_inactive);
        return;
    }

    const auto &axes = m_inspector.inspectFace(m_face_index).variable_axes;
    const FontVariableAxisInfo *probe_axis = nullptr;
    for (const FontVariableAxisInfo &ax : axes) {
        if (ax.min_value < ax.max_value) {
            probe_axis = &ax;
            break;
        }
    }
    if (!probe_axis) {
        m_axes_warning->hide();
        return;
    }

    const auto tag = QFont::Tag::fromString(probe_axis->tag);
    if (!tag.has_value()) {
        m_axes_warning->hide();
        return;
    }

    m_gdi_probed      = true;
    const QString fam = ui->comboBox_family->currentText();
    QFont f_min(fam);
    QFont f_max(fam);
    f_min.setPointSize(48);
    f_max.setPointSize(48);
    f_min.setVariableAxis(*tag, probe_axis->min_value);
    f_max.setVariableAxis(*tag, probe_axis->max_value);

    QString sample = m_inspector.probeSampleForFace(m_face_index);
    if (sample.isEmpty()) {
        sample = QStringLiteral("Hamburgefonstiv");
    }
    const qreal w_min    = QFontMetricsF(f_min, this).horizontalAdvance(sample);
    const qreal w_max    = QFontMetricsF(f_max, this).horizontalAdvance(sample);

    bool same = (qAbs(w_min - w_max) < 0.05);
    if (same) {
        auto render_probe = [&sample](const QFont &f) {
            QImage img(160, 64, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::white);
            QPainter p(&img);
            p.setRenderHint(QPainter::TextAntialiasing, true);
            p.setFont(f);
            p.setPen(Qt::black);
            p.drawText(QPointF(8.0, 46.0), sample);
            p.end();
            return img;
        };
        same = (render_probe(f_min) == render_probe(f_max));
    }

    m_gdi_inactive = same;
    m_axes_warning->setVisible(m_gdi_inactive);
}

QVector<FontNameRecord> FontWidget::readNameRecords()
{
    if (!m_inspector.isValid() && !m_font_path.isEmpty()) {
        m_inspector.openFile(m_font_path);
    }
    if (!m_inspector.isValid()) {
        return {};
    }
    const int face = qBound(0, m_face_index, m_inspector.faceCount() - 1);
    return m_inspector.inspectFace(face).name_records;
}

bool FontWidget::init(const QString &p)
{
    // An instance binds one font, but a repeated init() must not leak the
    // previous registration.
    releaseApplicationFont();
    m_font_path    = p;
    m_face_index   = 0;
    m_gdi_probed   = false;
    m_gdi_inactive = false;
    m_inspector.openFile(p);
    m_font_id = QFontDatabase::addApplicationFont(p);
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

    onNameChanged();
    onTabChanged();
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

    const auto &axes = m_inspector.inspectFace(m_face_index).variable_axes;
    if (axes.isEmpty()) {
        // doesn't work: https://bugreports.qt.io/browse/QTBUG-69499
        // ft.setStyleName(ui->comboBox_style->currentText());
        if (auto weight = QFontDatabase::weight(name, style); weight != -1) {
            ft.setWeight(QFont::Weight(weight));
        }
        // there are things like Demi-Italic exists, but we can't support these
        ft.setItalic(QFontDatabase::italic(name, style));
    }
    else {
        bool has_wght         = false;
        bool has_ital_or_slnt = false;
        for (const FontVariableAxisInfo &ax : axes) {
            if (ax.tag == QByteArrayLiteral("wght")) {
                has_wght = true;
            }
            if (ax.tag == QByteArrayLiteral("ital")
                || ax.tag == QByteArrayLiteral("slnt")) {
                has_ital_or_slnt = true;
            }
            const float val = m_axis_values.value(ax.tag, ax.default_value);
            if (const auto tag = QFont::Tag::fromString(ax.tag);
                tag.has_value()) {
                ft.setVariableAxis(*tag, val);
            }
        }
        if (!has_wght) {
            if (auto weight = QFontDatabase::weight(name, style);
                weight != -1) {
                ft.setWeight(QFont::Weight(weight));
            }
        }
        if (!has_ital_or_slnt) {
            ft.setItalic(QFontDatabase::italic(name, style));
        }
    }

    if (role == PreviewFontRole::Text) {
        const bool has_spacing = !qFuzzyIsNull(m_letter_spacing);
        for (auto it = m_feature_overrides.cbegin();
             it != m_feature_overrides.cend(); ++it) {
            if (has_spacing && isLigatureFeatureTag(it.key())) {
                continue;
            }
            if (it.key() == QByteArrayLiteral("kern")) {
                ft.setKerning(it.value());
            }
            if (const auto tag = QFont::Tag::fromString(it.key());
                tag.has_value()) {
                ft.setFeature(*tag, it.value() ? 1u : 0u);
            }
        }
        if (has_spacing) {
            ft.setLetterSpacing(QFont::AbsoluteSpacing, m_letter_spacing);
        }
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
    updateFeaturePillsState();
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
        applyFeaturesNotePalette();
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

    m_face_index = m_inspector.resolveFaceIndex(
        name, ui->comboBox_style->currentText());
    rebuildAxesBar(true);
    rebuildFeaturesBar();

    onStyleChanged();
}

void FontWidget::onStyleChanged()
{
    const QString name  = ui->comboBox_family->currentText();
    const QString style = ui->comboBox_style->currentText();
    const int new_face  = m_inspector.resolveFaceIndex(name, style);
    if (new_face != m_face_index) {
        m_face_index = new_face;
        rebuildAxesBar(false);
        rebuildFeaturesBar();
    }
    syncAxesForStyle(style);

    // if the size is smooth, make the item bold.
    // unless they are the same, no need to bold all items.
    QFont ft           = this->font();
    const auto cb_sz_t = ui->comboBox_sz->count();
    for (int i = 0; i < cb_sz_t; ++i) {
        ui->comboBox_sz->setItemData(i, QVariant(ft), Qt::FontRole);
    }

    const auto pt_sz = QFontDatabase::smoothSizes(name, style);

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
        if (m_text_dirty) {
            refreshTextWidgetsFromFont();
            m_text_dirty = false;
        }
        else {
            updateTabTextPreview();
        }
        if (m_stack) {
            m_stack->setCurrentIndex(m_waterfall_enabled ? 1 : 0);
        }
    }
    else if (cur_wnd == ui->tab_sample) {
        if (m_sample_dirty && ui->label_sample) {
            ui->label_sample->setFont(
                buildPreviewFont(getCurrentFontSize(), PreviewFontRole::Text));
            m_sample_dirty = false;
        }
        else {
            updateSamplePreview();
        }
    }
    else if (cur_wnd == m_tab_char && m_char_dirty) {
        if (m_wnd_char) {
            m_wnd_char->updateFont(
                buildPreviewFont(getCurrentFontSize(), PreviewFontRole::Glyph));
        }
        m_char_dirty = false;
    }
    else if (cur_wnd == m_tab_gi && m_gi_dirty) {
        refreshGlyphInspectorFromFont();
        m_gi_dirty = false;
    }
}

void FontWidget::refreshTextWidgetsFromFont()
{
    if (!m_edit) {
        return;
    }
    const int pt          = getCurrentFontSize();
    const QFont text_font = buildPreviewFont(pt, PreviewFontRole::Text);
    m_edit->setFont(text_font);
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

void FontWidget::updateGlyphInspectorFont(const QFont &glyph_font)
{
    if (!m_wnd_gi) {
        return;
    }
    const FontDecorationMetrics deco
        = m_inspector.decorationMetrics(m_face_index, m_axis_values);
    const QChar ch                 = m_wnd_gi->hasSelection()
                                         ? m_wnd_gi->currentChar()
                                         : QChar();
    const GlyphOutlineData outline = ch.isNull()
        ? GlyphOutlineData{}
        : m_inspector.glyphOutline(m_face_index, ch, m_axis_values);
    m_wnd_gi->updateFont(glyph_font, deco, outline);
}

void FontWidget::refreshGlyphInspectorFromFont()
{
    if (!m_wnd_gi) {
        return;
    }
    updateGlyphInspectorFont(
        buildPreviewFont(getCurrentFontSize(), PreviewFontRole::Glyph));
}

void FontWidget::refreshActiveTabFromFeatures()
{
    m_text_dirty   = true;
    m_sample_dirty = true;

    auto cur_wnd = ui->tabWidget->currentWidget();
    if (cur_wnd == ui->tab_text) {
        refreshTextWidgetsFromFont();
        m_text_dirty = false;
    }
    else if (cur_wnd == ui->tab_sample) {
        if (ui->label_sample) {
            ui->label_sample->setFont(
                buildPreviewFont(getCurrentFontSize(), PreviewFontRole::Text));
        }
        m_sample_dirty = false;
    }
}

void FontWidget::setShowOutlinePoints(bool on)
{
    if (m_wnd_gi) {
        m_wnd_gi->setShowOutlinePoints(on);
    }
}

bool FontWidget::isShowOutlinePoints() const
{
    return m_wnd_gi ? m_wnd_gi->isShowOutlinePoints() : false;
}

void FontWidget::setFeatureEnabled(const QByteArray &tag, bool enabled)
{
    bool found     = false;
    bool def_on    = false;
    for (const FeaturePillControl &ctrl : m_feature_controls) {
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
        m_feature_overrides.remove(tag);
    }
    else {
        m_feature_overrides.insert(tag, enabled);
    }
    updateFeaturePillsState();
    refreshActiveTabFromFeatures();
}

bool FontWidget::isFeatureEnabled(const QByteArray &tag) const
{
    for (const FeaturePillControl &ctrl : m_feature_controls) {
        if (ctrl.tag == tag) {
            return m_feature_overrides.value(tag, ctrl.default_on);
        }
    }
    return false;
}

QVector<QPushButton *> FontWidget::featurePillButtons() const
{
    QVector<QPushButton *> result;
    result.reserve(m_feature_controls.size());
    for (const FeaturePillControl &ctrl : m_feature_controls) {
        if (ctrl.button) {
            result.append(ctrl.button);
        }
    }
    return result;
}

void FontWidget::refreshActiveTabFromAxes()
{
    m_text_dirty   = true;
    m_sample_dirty = true;
    m_char_dirty   = true;
    m_gi_dirty     = true;

    auto cur_wnd = ui->tabWidget->currentWidget();
    if (cur_wnd == ui->tab_text) {
        refreshTextWidgetsFromFont();
        m_text_dirty = false;
    }
    else if (cur_wnd == ui->tab_sample) {
        if (ui->label_sample) {
            ui->label_sample->setFont(
                buildPreviewFont(getCurrentFontSize(), PreviewFontRole::Text));
        }
        m_sample_dirty = false;
    }
    else if (cur_wnd == m_tab_char) {
        if (m_wnd_char) {
            m_wnd_char->updateFont(
                buildPreviewFont(getCurrentFontSize(), PreviewFontRole::Glyph));
        }
        m_char_dirty = false;
    }
    else if (cur_wnd == m_tab_gi) {
        refreshGlyphInspectorFromFont();
        m_gi_dirty = false;
    }
}

void FontWidget::updateTabTextPreview()
{
    if (!m_edit || !m_wnd_char) {
        return;
    }
    const int pt           = getCurrentFontSize();
    const QFont text_font  = buildPreviewFont(pt, PreviewFontRole::Text);
    const QFont glyph_font = buildPreviewFont(pt, PreviewFontRole::Glyph);

    m_edit->setFont(text_font);
    m_text_dirty = false;

    if (ui->tabWidget->currentWidget() == m_tab_char) {
        m_wnd_char->updateFont(glyph_font);
        m_char_dirty = false;
    }
    else {
        m_char_dirty = true;
    }

    if (m_wnd_gi) {
        updateGlyphInspectorFont(glyph_font);
        m_gi_dirty = false;
    }
    // The Sample tab shares the family/style/size state; only its text differs,
    // so refresh the font here instead of waiting for the next family change.
    if (ui->label_sample) {
        ui->label_sample->setFont(text_font);
        m_sample_dirty = false;
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

        bool is_axis_slider = false;
        for (const AxisControl &c : m_axis_controls) {
            if (watched == c.slider) {
                is_axis_slider = true;
                break;
            }
        }
        if (is_axis_slider || watched == m_slider_line
            || watched == m_slider_space || watched == m_combo_preset
            || watched == ui->comboBox_family || watched == ui->comboBox_style
            || watched == ui->comboBox_sz) {
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
    applyFeaturesNotePalette();
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
