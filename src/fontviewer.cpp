#include "fontviewer.h"

#include <qt_windows.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QtMath>

#include "fontinfo.h"
#include "fontwidget.h"
#include "seer/viewerhelper.h"

#define qprintt qDebug() << "[FontViewer]"

namespace {
constexpr auto g_ini_user_text      = "user_text";
constexpr auto g_ini_font_size      = "font_size";
constexpr auto g_ini_collapsed      = "collapsed";
constexpr auto g_ini_line_height    = "line_height";
constexpr auto g_ini_letter_spacing = "letter_spacing";
constexpr auto g_ini_waterfall      = "waterfall_enabled";
constexpr auto g_ini_invert         = "invert_preview";

constexpr int g_def_pt                = 16;
constexpr double g_def_line_height    = 1.4;
constexpr double g_def_letter_spacing = 0.0;
}  // namespace

FontViewer::FontViewer(QWidget *parent)
    : ViewerBase(parent), m_ini(nullptr), m_view(nullptr)
{
}

FontViewer::~FontViewer()
{
    if (m_ini && m_view) {
        m_ini->setValue(g_ini_user_text, m_view->getLastEditedText());
        if (m_font_size_valid) {
            m_ini->setValue(g_ini_font_size, m_view->getCurrentFontSize());
        }
        m_ini->setValue(g_ini_collapsed, m_view->isCollapsed());
        m_ini->setValue(g_ini_line_height, m_view->getLineHeight());
        m_ini->setValue(g_ini_letter_spacing, m_view->getLetterSpacing());
        m_ini->setValue(g_ini_waterfall, m_view->isWaterfallEnabled());
        m_ini->setValue(g_ini_invert, m_view->isInvertPreview());
    }
}

QSize FontViewer::getContentSize() const
{
    return options()->dpr() * QSize(850, 800);
}

void FontViewer::onCopyTriggered()
{
    if (m_view) {
        m_view->copy();
    }
}

void FontViewer::updateDPR(qreal r)
{
    if (m_view) {
        m_view->updateDPR(r);
    }
}

void FontViewer::updateTheme(int theme)
{
    if (m_view) {
        m_view->updateTheme(theme);
    }
}

QString FontViewer::getIniPath() const
{
    const QString filename = name() % ".ini";
    QString dir            = seer::getDLLPath();
    if (dir.isEmpty()) {
        dir = QCoreApplication::applicationDirPath();
    }
    dir.replace("\\", "/");
    if (!dir.endsWith("/")) {
        dir.append("/");
    }
    return dir + filename;
}

void FontViewer::loadImpl(QBoxLayout *layout_content,
                          QHBoxLayout *layout_control_bar)
{
    if (layout_control_bar) {
        layout_control_bar->addStretch();
    }
    m_view = new FontWidget(this);
    layout_content->addWidget(m_view);
    connect(m_view, &FontWidget::sigToast, this, [this](const QString &msg) {
        emit sigCommand(VCT_ShowToastMsg, msg);
    });
    if (!m_view->init(options()->path())) {
        emit sigCommand(VCT_StateChange, VCV_Error);
        return;
    }
    m_ini = new QSettings(getIniPath(), QSettings::IniFormat, this);
    // A missing key must not turn into 0 pt, and an out-of-range value must not
    // be written back on close unless the user later picks a valid size.
    if (!m_ini->contains(g_ini_font_size)) {
        m_font_size_valid = true;
        m_view->setCurrentFontSize(g_def_pt);
    }
    else {
        bool size_ok          = false;
        const int stored_size = m_ini->value(g_ini_font_size).toInt(&size_ok);
        m_font_size_valid
            = (size_ok && stored_size >= 1 && stored_size <= 999);
        m_view->setCurrentFontSize(m_font_size_valid ? stored_size : g_def_pt);
    }
    connect(m_view, &FontWidget::sigFontSizeChanged, this,
            [this](int) { m_font_size_valid = true; });
    m_view->setCollapsed(m_ini->value(g_ini_collapsed, false).toBool());
    bool lh_ok          = false;
    const double lh_val = m_ini->value(g_ini_line_height, g_def_line_height)
                              .toDouble(&lh_ok);
    m_view->setLineHeight((lh_ok && qIsFinite(lh_val)) ? lh_val
                                                       : g_def_line_height);
    bool ls_ok          = false;
    const double ls_val = m_ini->value(g_ini_letter_spacing, g_def_letter_spacing)
                              .toDouble(&ls_ok);
    m_view->setLetterSpacing((ls_ok && qIsFinite(ls_val)) ? ls_val
                                                          : g_def_letter_spacing);
    m_view->setCurrentText(m_ini->value(g_ini_user_text).toString());
    m_view->clearPreviewUndoHistory();
    m_view->setWaterfallEnabled(
        m_ini->value(g_ini_waterfall, false).toBool());
    m_view->setInvertPreview(m_ini->value(g_ini_invert, false).toBool());

    updateTheme(options()->theme());
    updateDPR(options()->dpr());

    emit sigCommand(VCT_StateChange, VCV_Loaded);
}

void FontViewer::loadFileInfo()
{
    const auto records = readFontNameTable(options()->path());
    if (records.isEmpty()) {
        return;
    }
    QVector<QPair<QString, QString>> props;
    props.reserve(records.size());
    for (const auto &r : records) {
        qprintt << "font info:" << r.label << r.value;
        props.append({r.label, r.value});
    }
    emit sigCommand(VCT_AppendProperty, QVariant::fromValue(props));
}
