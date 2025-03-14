#include "fontwidget.h"

#include <qt_windows.h>

#include <QClipboard>
#include <QFont>
#include <QFontInfo>
#include <QPushButton>
#include <QWheelEvent>
#include <iostream>

#include "characterwidget.h"
#include "ui_fontwidget.h"

#pragma comment(lib, "user32.lib")
constexpr auto g_def_sample
    = "It is the time you have wasted for your rose that makes your "
      "rose so important.";
constexpr const char *g_samples[]
    = {"The quick brown fox jumps over the lazy dog.",
       "Waltz, bad nymph, for quick jigs vex.",
       "How vexingly quick daft zebras jump!", "123456789 ~!@#$%^&*()-="};
constexpr auto g_def_pt = 16;

FontWidget::FontWidget(QWidget *parent)
    : QWidget(parent), m_wnd_char(nullptr), ui(new Ui::FontWidget)
{
    ui->setupUi(this);
}

FontWidget::~FontWidget()
{
    delete ui;
}

void FontWidget::initUI(const QStringList &names)
{
    /// widget_top
    // names
    ui->comboBox_family->addItems(names);
    if (names.size() == 1) {
        ui->comboBox_family->setEnabled(false);
    }
    connect(ui->comboBox_family, &QComboBox::currentTextChanged, this,
            &FontWidget::onNameChanged);
    // style
    ui->comboBox_style->addItems(
        QFontDatabase::styles(ui->comboBox_family->currentText()));
    connect(ui->comboBox_style, &QComboBox::currentTextChanged, this,
            &FontWidget::onStyleChanged);
    // sz
    const auto all_sz = QFontDatabase::standardSizes();
    for (auto i : all_sz) {
        ui->comboBox_sz->addItem(QString::number(i));
    }
    if (all_sz.contains(g_def_pt)) {
        ui->comboBox_sz->setCurrentText(QString::number(g_def_pt));
    }
    connect(ui->comboBox_sz, &QComboBox::currentTextChanged, this,
            &FontWidget::updateTabTextPreview);
    // info
    ui->label_info->setWordWrap(true);
    ui->label_info->setAlignment(Qt::AlignCenter);

    /// tab wnd
    ui->tabWidget->setTabText(ui->tabWidget->indexOf(ui->tab_text), "Text");
    QPushButton *btn_tab = new QPushButton(this);
    btn_tab->setFlat(true);
    btn_tab->setText("Copy as Image");
    btn_tab->setCursor(Qt::PointingHandCursor);
    connect(btn_tab, &QPushButton::clicked, this, &FontWidget::copy);
    ui->tabWidget->setCornerWidget(btn_tab);
    connect(ui->tabWidget, &QTabWidget::currentChanged, this,
            &FontWidget::onTabChanged);
    // tab text
    ui->comboBox_text->setEditable(true);
    for (auto i : g_samples) {
        ui->comboBox_text->addItem(i);
    }
    ui->comboBox_text->setContextMenuPolicy(Qt::NoContextMenu);
    ui->comboBox_text->setPlaceholderText(g_def_sample);
    connect(ui->comboBox_text, &QComboBox::currentTextChanged, this,
            &FontWidget::updateTabTextPreview);
    ui->scrollArea_text->setFrameShape(QFrame::NoFrame);
    ui->label_preview->setWordWrap(true);
    ui->label_preview->installEventFilter(this);

    // tab sample
    ui->tabWidget->setTabText(ui->tabWidget->indexOf(ui->tab_sample), "Sample");

    // tab char
    auto wnd_char_sa = new QScrollArea;
    m_wnd_char       = new CharacterWidget;
    wnd_char_sa->setFrameShape(QFrame::NoFrame);
    wnd_char_sa->setWidget(m_wnd_char);
    ui->tabWidget->addTab(wnd_char_sa, "Characters");
    m_wnd_char->installEventFilter(this);
}

bool FontWidget::init(const QString &p)
{
    const auto ft_id = QFontDatabase::addApplicationFont(p);
    if (-1 == ft_id) {
        std::cout << "addApplicationFont err" << std::endl;
        return false;
    }
    const QStringList names = QFontDatabase::applicationFontFamilies(ft_id);
    if (names.isEmpty()) {
        std::cout << "applicationFontFamilies err" << std::endl;
        return false;
    }
    initUI(names);
    // init data after initUI
    m_wnd_char->init(p);

    // visible before sending Read msg
    show();

    onTabChanged();
    onNameChanged();
    return true;
}

void FontWidget::onNameChanged()
{
    const auto name   = ui->comboBox_family->currentText();
    const auto styles = QFontDatabase::styles(name);
    ui->comboBox_style->clear();
    ui->comboBox_style->addItems(styles);
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
    auto cur_wnd = ui->tabWidget->currentWidget();
    if (cur_wnd == ui->tab_text) {
        cur_wnd->layout()->addWidget(ui->scrollArea_text);
        ui->label_preview->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }
    else if (cur_wnd == ui->tab_sample) {
        cur_wnd->layout()->addWidget(ui->scrollArea_text);
        ui->label_preview->setAlignment(Qt::AlignCenter);
    }
    updateTabTextPreview();
}

void FontWidget::copy()
{
    qApp->setOverrideCursor(Qt::BusyCursor);

    QPixmap pix;
    if (ui->tabWidget->currentWidget() == ui->tab_text
        || ui->tabWidget->currentWidget() == ui->tab_sample) {
        pix = ui->label_preview->grab();
    }
    else {
        // takes too long
        // pix = m_wnd_char->grab();
        pix = m_wnd_char->getSelectedCharPix();
        if (pix.isNull()) {
            // sendMsg2Seer(SEER_OIT_SUB_WAGGLE, {});
        }
    }
    if (!pix.isNull()) {
        qApp->clipboard()->setPixmap(pix);
    }
    qApp->restoreOverrideCursor();
}

void FontWidget::updateTabTextPreview()
{
    const auto name = ui->comboBox_family->currentText();
    QFont ft(name);
    ft.setPointSize(ui->comboBox_sz->currentText().toInt());
    const auto style = ui->comboBox_style->currentText();
    // doesn't work: https://bugreports.qt.io/browse/QTBUG-69499
    // ft.setStyleName(ui->comboBox_style->currentText());
    ft.setWeight((QFont::Weight)QFontDatabase::weight(name, style));
    // there are things like Demi-Italic exists, but we can't support these
    ft.setItalic(QFontDatabase::italic(name, style));

    ui->label_preview->setFont(ft);
    m_wnd_char->updateFont(ft);

    QString text;
    auto cur_wnd = ui->tabWidget->currentWidget();
    if (cur_wnd == ui->tab_text) {
        text = ui->comboBox_text->currentText().trimmed();
        if (text.isEmpty()) {
            text = g_def_sample;
        }
    }
    else if (cur_wnd == ui->tab_sample) {
        const auto ws = QFontDatabase::writingSystems(name);
        for (auto i : ws) {
            text.append(QFontDatabase::writingSystemSample(i));
            text.append("\n");
        }
        text = text.trimmed();
    }

    ui->label_preview->setText(text);
}

bool FontWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (!(watched == ui->label_preview || watched == m_wnd_char)) {
        return false;
    }
    if (event->type() != QEvent::Wheel) {
        return false;
    }
    QWheelEvent *e = static_cast<QWheelEvent *>(event);
    if (e->modifiers() != Qt::ControlModifier) {
        return false;
    }
    if (e->angleDelta().y() == 0) {
        return true;
    }

    auto index_cur = ui->comboBox_sz->currentIndex();
    // zoom_in
    if (e->angleDelta().y() > 0) {
        if (index_cur >= ui->comboBox_sz->count() - 1) {
            return true;
        }
        index_cur = index_cur + 1;
    }
    // zoom out
    else {
        if (index_cur == 0) {
            return true;
        }
        index_cur = index_cur - 1;
    }
    ui->comboBox_sz->setCurrentIndex(index_cur);

    return true;
}

void FontWidget::updateDPR(qreal r)
{
    auto ft = qApp->font();
    ft.setPixelSize(r * 12);
    // ft.setPointSize(12);
    this->setFont(ft);

    std::cout << "dpi changed " << r << std::endl;
}

// void Viewer::onThemeChanged(int theme)
// {
//     auto pal = qApp->palette(this);
//     /// light
//     if (theme == 0) {
//         ui->scrollAreaWidgetContents->setStyleSheet("");
//         pal.setColor(QPalette::Window, "white");
//         pal.setColor(QPalette::WindowText, "#333333");
//         this->setPalette(pal);
//         return;
//     }
//     /// dark
//     // need to use qss to set tab color
//     // maybe next time
//     ui->scrollAreaWidgetContents->setStyleSheet(
//         "background:white; color: #333;");
// }
