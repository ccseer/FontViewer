#include "fontviewer.h"

#include "fontwidget.h"

FontViewer::FontViewer(QWidget *parent) : ViewerBase(parent), m_view(nullptr)
{
    //
}

QSize FontViewer::getContentSize() const
{
    return m_d->d->dpr * QSize(850, 800);
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

void FontViewer::loadImpl(QBoxLayout *layout_content, QHBoxLayout *)
{
    m_view = new FontWidget(this);
    layout_content->addWidget(m_view);
    m_view->init(m_d->d->path);

    updateTheme(m_d->d->theme);
    updateDPR(m_d->d->dpr);

    emit sigCommand(ViewCommandType::VCT_StateChange, VCV_Loaded);
}
