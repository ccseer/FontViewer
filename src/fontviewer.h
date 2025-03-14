#pragma once

#include "seer/viewerbase.h"

class FontWidget;

class FontViewer : public ViewerBase {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID ViewerBase_iid FILE "fontviewer.json")
    Q_INTERFACES(ViewerBase)
public:
    explicit FontViewer(QWidget *parent = nullptr);

    QString name() const override
    {
        return "fontviewer";
    }
    QSize getContentSize() const override;
    void onCopyTriggered() override;
    void updateDPR(qreal) override;

protected:
    void loadImpl(QBoxLayout *layout_content,
                  QHBoxLayout *layout_control_bar) override;

    FontWidget *m_view;
};
