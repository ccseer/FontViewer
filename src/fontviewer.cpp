#include "fontviewer.h"

#include <qt_windows.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include "fontwidget.h"
#include "seer/viewer_helper.h"

namespace {
constexpr auto g_ini_user_text = "user_text";
constexpr auto g_ini_font_size = "font_size";
}  // namespace

FontViewer::FontViewer(QWidget *parent)
    : ViewerBase(parent), m_ini(nullptr), m_view(nullptr)
{
}

FontViewer::~FontViewer()
{
    if (m_ini && m_view) {
        m_ini->setValue(g_ini_user_text, m_view->getCurrentText());
        m_ini->setValue(g_ini_font_size, m_view->getCurrentFontSize());
    }
}

QSize FontViewer::getContentSize() const
{
    const auto sz_def = m_d->d->dpr * QSize(850, 800);
    auto cmd          = property(g_property_key_cmd).toStringList();
    if (!cmd.isEmpty()) {
        auto parsed = seer::parseViewerSizeFromConfig(cmd);
        qDebug() << "getContentSize: parsed" << parsed << cmd;
        if (parsed.isValid()) {
            return parsed;
        }
    }
    return sz_def;
}

void FontViewer::onCopyTriggered()
{
    m_view->copy();
}

void FontViewer::updateDPR(qreal r)
{
    m_d->d->dpr = r;
    m_view->updateDPR(r);
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
    if (!m_view->init(m_d->d->path)) {
        emit sigCommand(ViewCommandType::VCT_StateChange, VCV_Error);
        return;
    }
    m_ini = new QSettings(getIniPath(), QSettings::IniFormat, this);
    m_view->setCurrentText(m_ini->value(g_ini_user_text).toString());
    m_view->setCurrentFontSize(m_ini->value(g_ini_font_size).toInt());

    updateTheme(m_d->d->theme);
    updateDPR(m_d->d->dpr);

    emit sigCommand(ViewCommandType::VCT_StateChange, VCV_Loaded);
}
