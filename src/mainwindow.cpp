#include "mainwindow.h"

#include <qt_windows.h>

#include <QClipboard>
#include <QFont>
#include <QFontInfo>
#include <QPushButton>
#include <QRawFont>
#include <QWheelEvent>
#include <iostream>

#include "characterwidget.h"
#include "oitvar.h"
#include "ui_mainwindow.h"
#pragma comment(lib, "user32.lib")

// TODO:
//  add a local file text as user defined ini, custom default font
//      size and display string;
//  font type: .woff   .woff2   .eot

// The quick brown fox jumps over the lazy dog
constexpr auto g_def_string
    = "It is the time you have wasted for your rose that makes your "
      "rose so important.";
constexpr auto g_def_pt = 12;

MainWindow::MainWindow(int wnd_index, const QString &p, QWidget *parent)
    : QMainWindow(parent),
      m_wnd_index(wnd_index),
      m_path(p),
      m_wnd_char(nullptr),
      ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
    setWindowTitle("OITViewer");
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::initUI(const QStringList &names)
{
    /// widget_top
    // names
    for (auto i : names) {
        ui->comboBox_family->addItem(i);
    }
    if (names.size() == 1) {
        ui->comboBox_family->setEnabled(false);
    }
    connect(ui->comboBox_family, &QComboBox::currentTextChanged, this,
            &MainWindow::onNameChanged);
    // style
    ui->comboBox_style->addItems(
        m_fdb.styles(ui->comboBox_family->currentText()));
    connect(ui->comboBox_style, &QComboBox::currentTextChanged, this,
            &MainWindow::onStyleChanged);
    // sz
    const auto all_sz = m_fdb.standardSizes();
    for (auto i : all_sz) {
        ui->comboBox_sz->addItem(QString::number(i));
    }
    if (all_sz.contains(g_def_pt)) {
        ui->comboBox_sz->setCurrentText(QString::number(g_def_pt));
    }
    connect(ui->comboBox_sz, &QComboBox::currentTextChanged, this,
            &MainWindow::updatePreview);
    // info
    ui->label_info->setWordWrap(true);
    ui->label_info->setAlignment(Qt::AlignCenter);

    /// tab wnd
    ui->tabWidget->setTabText(ui->tabWidget->indexOf(ui->tab_text), "Text");
    ui->tabWidget->setTabText(ui->tabWidget->indexOf(ui->tab_sample), "Sample");
    QPushButton *btn_tab = new QPushButton(this);
    btn_tab->setFlat(true);
    btn_tab->setText("Copy as Image");
    btn_tab->setCursor(Qt::PointingHandCursor);
    connect(btn_tab, &QPushButton::clicked, this, [=]() {
        btn_tab->setEnabled(false);
        auto pix = ui->label_preview->grab();
        qApp->clipboard()->setPixmap(pix);
        btn_tab->setEnabled(true);
    });
    ui->tabWidget->setCornerWidget(btn_tab);

    //  add a tab page to paint all characters in font
    auto wnd_char_sa = new QScrollArea;
    m_wnd_char       = new CharacterWidget;
    wnd_char_sa->setFrameShape(QFrame::NoFrame);
    wnd_char_sa->setWidget(m_wnd_char);
    ui->tabWidget->addTab(wnd_char_sa, "Characters");

    connect(ui->tabWidget, &QTabWidget::currentChanged, this,
            &MainWindow::onTabChanged);
    // Seer filtered all context menu
    ui->lineEdit->setContextMenuPolicy(Qt::NoContextMenu);
    // not able to enable keyboard input
    // so only we use default text or paste from clipboard
    ui->lineEdit->setReadOnly(true);
    ui->lineEdit->setPlaceholderText(g_def_string);
    connect(ui->lineEdit, &QLineEdit::textChanged, this,
            &MainWindow::updatePreview);
    connect(ui->toolButton_clear, &QToolButton::clicked, ui->lineEdit,
            &QLineEdit::clear);
    connect(ui->toolButton_paste, &QToolButton::clicked, ui->lineEdit, [=]() {
        auto text = qApp->clipboard()->text().simplified().trimmed();
        if (text.isEmpty()) {
            return;
        }
        ui->lineEdit->setText(text);
        // goto updatePreview
    });

    ui->scrollArea->setFrameShape(QFrame::NoFrame);
    ui->label_preview->setWordWrap(true);
    ui->label_preview->installEventFilter(this);
}

bool MainWindow::init()
{
    const auto ft_id = m_fdb.addApplicationFont(m_path);
    if (-1 == ft_id) {
        std::cout << "addApplicationFont err" << std::endl;
        return false;
    }
    const QStringList names = m_fdb.applicationFontFamilies(ft_id);
    if (names.isEmpty()) {
        std::cout << "applicationFontFamilies err" << std::endl;
        return false;
    }
    initUI(names);
    // init data after initUI
    m_wnd_char->init(m_path);

    // visible before sending Read msg
    show();

    sendMsg2Seer(SEER_OIT_SUB_LOAD_OK, {});
    onTabChanged();
    onNameChanged();
    return true;
}

void MainWindow::onNameChanged()
{
    const auto name   = ui->comboBox_family->currentText();
    const auto styles = m_fdb.styles(name);
    ui->comboBox_style->clear();
    ui->comboBox_style->addItems(styles);
    ui->comboBox_style->setEnabled(styles.size() > 1);

    onStyleChanged();
}

void MainWindow::onStyleChanged()
{
    // if the size is smooth, make the item bold.
    // unless they are the same, no need to bold all items.
    QFont ft           = qApp->font();
    const auto cb_sz_t = ui->comboBox_sz->count();
    for (int i = 0; i < cb_sz_t; ++i) {
        ui->comboBox_sz->setItemData(i, QVariant(ft), Qt::FontRole);
    }

    const auto pt_sz = m_fdb.smoothSizes(ui->comboBox_family->currentText(),
                                         ui->comboBox_style->currentText());

    if (pt_sz.size() != cb_sz_t) {
        ft.setBold(true);
        for (int i = 0; i < cb_sz_t; ++i) {
            if (pt_sz.contains(ui->comboBox_sz->itemText(i).toInt())) {
                ui->comboBox_sz->setItemData(i, QVariant(ft), Qt::FontRole);
            }
        }
    }
    updatePreview();
    updateInfo();
}

void MainWindow::updateInfo()
{
    const QString name  = ui->comboBox_family->currentText();
    const QString style = ui->comboBox_style->currentText();

    QString text = QString::number(m_wnd_char->getCharCount());
    text += " Characters\n";

    const auto weight = m_fdb.weight(name, style);
    if (weight != -1) {
        text += "Weight: " + QString::number(weight) + "\n";
    }

    QString ib;
    if (m_fdb.bold(name, style)) {
        ib = "Bold";
    }
    if (m_fdb.italic(name, style)) {
        if (!ib.isEmpty()) {
            ib.append(" & ");
        }
        ib = "Italic";
    }
    if (!ib.isEmpty()) {
        ib.append("\n");
        text.append(ib);
    }

    if (m_fdb.isFixedPitch(name, style)) {
        text += "Fixed Pitch\n";
    }
    if (m_fdb.isScalable(name, style)) {
        text += "Scalable\n";
    }
    if (m_fdb.isSmoothlyScalable(name, style)) {
        text += "Smoothly Scalable\n";
    }
    if (m_fdb.isBitmapScalable(name, style)) {
        text += "Bitmap Scalable\n";
    }
    if (m_fdb.isPrivateFamily(name)) {
        text += "Private Family\n";
    }

    const auto ws = m_fdb.writingSystems(name);
    ib.clear();
    for (auto i : ws) {
        ib.append(m_fdb.writingSystemName(i));
        ib.append(", ");
    }
    if (!ib.isEmpty()) {
        ib = ib.trimmed();
        if (ib.endsWith(",")) {
            ib = ib.left(ib.size() - 1);
        }
        text.append(ib + "\n");
    }

    ui->label_info->setText(text.trimmed());
}

void MainWindow::onTabChanged()
{
    auto cur_wnd = ui->tabWidget->currentWidget();
    if (cur_wnd == ui->tab_text) {
        cur_wnd->layout()->addWidget(ui->scrollArea);
        ui->label_preview->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }
    else if (cur_wnd == ui->tab_sample) {
        cur_wnd->layout()->addWidget(ui->scrollArea);
        ui->label_preview->setAlignment(Qt::AlignCenter);
    }
    updatePreview();
}

void MainWindow::updatePreview()
{
    const auto name = ui->comboBox_family->currentText();
    QFont ft(name);
    ft.setPointSize(ui->comboBox_sz->currentText().toInt());
    // TODO: doesn't work
    ft.setStyleName(ui->comboBox_style->currentText());
    ui->label_preview->setFont(ft);
    m_wnd_char->updateFont(ft);

    QString text;
    auto cur_wnd = ui->tabWidget->currentWidget();
    if (cur_wnd == ui->tab_text) {
        text = ui->lineEdit->text().trimmed();
        if (text.isEmpty()) {
            text = g_def_string;
        }
    }
    else if (cur_wnd == ui->tab_sample) {
        const auto ws = m_fdb.writingSystems(name);
        for (auto i : ws) {
            text.append(m_fdb.writingSystemSample(i));
            text.append("\n");
        }
        text = text.trimmed();
    }

    ui->label_preview->setText(text);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != ui->label_preview || event->type() != QEvent::Wheel) {
        return QMainWindow::eventFilter(watched, event);
    }
    QWheelEvent *e = static_cast<QWheelEvent *>(event);
    if (e->modifiers() != Qt::ControlModifier) {
        return false;
    }
    if (e->angleDelta().y() == 0) {
        return true;
    }

    const auto index_cur = ui->comboBox_sz->currentIndex();
    int index_new        = index_cur;
    // zoom_in
    if (e->angleDelta().y() > 0) {
        if (index_cur >= ui->comboBox_sz->count() - 1) {
            return true;
        }
        index_new = index_cur + 1;
    }
    // zoom out
    else {
        if (index_cur == 0) {
            return true;
        }
        index_new = index_cur - 1;
    }
    ui->comboBox_sz->setCurrentIndex(index_new);

    return true;
}

//////////////////////////////////////////////
/// communicate with Seer
void MainWindow::doResize(const QSize &sz)
{
    setFixedSize(sz);
    std::cout << "resized " << sz.width() << " " << sz.height() << std::endl;
}

void MainWindow::onDPIChanged(qreal dpi)
{
    auto ft = qApp->font();
    ft.setPixelSize(dpi * 12);
    // ft.setPointSize(12);
    this->setFont(ft);

    std::cout << "dpi changed " << dpi << std::endl;
}

void MainWindow::onThemeChanged(int theme)
{
    auto pal = qApp->palette(this);
    /// light
    if (theme == 0) {
        ui->scrollAreaWidgetContents->setStyleSheet("");
        pal.setColor(QPalette::Window, "white");
        pal.setColor(QPalette::WindowText, "#333333");
        this->setPalette(pal);
        return;
    }
    /// dark
    // need to use qss to set tab color
    // maybe next time?
    ui->scrollAreaWidgetContents->setStyleSheet(
        "background:white; color: #333;");
}

QVariant MainWindow::getDataFromSeerMsg(const QByteArray &ba) const
{
    QDataStream ds(ba);
    ds.setVersion(QDataStream::Qt_5_15);
    QVariant v;
    ds >> v;
    return v;
}

void MainWindow::sendMsg2Seer(int sub_type, const QByteArray &d)
{
    auto h = FindWindowEx(NULL, NULL, L"SeerWindowClass", NULL);
    if (!h) {
        std::cout << "FindWindowEx NULL" << std::endl;
        return;
    }
    std::cout << "sendMsg2TopWnd " << m_wnd_index << " " << sub_type
              << std::endl;

    QByteArray ba;
    QDataStream ds(&ba, QIODevice::WriteOnly);
    ds.setVersion(QDataStream::Qt_5_15);
    OITData oitd{sub_type, m_wnd_index, d};
    ds << oitd;

    COPYDATASTRUCT cd;
    cd.cbData = ba.size();
    cd.lpData = (void *)ba.data();
    cd.dwData = SEER_OIT_MSG;
    SendMessage(h, WM_COPYDATA, 0, (LPARAM)(LPVOID)&cd);
}

bool MainWindow::nativeEvent(const QByteArray &eventType,
                             void *message,
                             long *result)
{
    MSG *m = (MSG *)message;
    if (m->message != WM_COPYDATA) {
        return QWidget::nativeEvent(eventType, message, result);
    }
    auto cds = (PCOPYDATASTRUCT)m->lParam;
    if (!cds) {
        return false;
    }

    switch (cds->dwData) {
    case SEER_OIT_ATTACHED: {
        std::cout << "" << std::endl;
        break;
    }
    case SEER_OIT_SIZE_CHANGED: {
        const QVariant v = getDataFromSeerMsg(
            QByteArray(reinterpret_cast<char *>(cds->lpData), cds->cbData));
        const QSize sz = v.toSize();
        std::cout << "SEER_OIT_SIZE_CHANGED " << sz.width() << " "
                  << sz.height() << std::endl;
        if (sz.isValid()) {
            doResize(sz);
        }
        break;
    }
    case SEER_OIT_DPI_CHANGED: {
        const QVariant v = getDataFromSeerMsg(
            QByteArray(reinterpret_cast<char *>(cds->lpData), cds->cbData));
        std::cout << "SEER_OIT_DPI_CHANGED " << v.toReal() << std::endl;
        onDPIChanged(v.toReal());
        break;
    }
    case SEER_OIT_THEME_CHANGED: {
        const QVariant v = getDataFromSeerMsg(
            QByteArray(reinterpret_cast<char *>(cds->lpData), cds->cbData));
        std::cout << "SEER_OIT_THEME_CHANGED " << v.toInt() << std::endl;
        onThemeChanged(v.toInt());
        break;
    }
    default: {
        break;
    }
    }
    return false;
}
