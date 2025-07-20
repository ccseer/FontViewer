#pragma once

#include "seer/viewerbase.h"

class FontWidget;
class QSettings;

class FontViewer : public ViewerBase {
    Q_OBJECT
public:
    explicit FontViewer(QWidget *parent = nullptr);
    ~FontViewer() override;

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
    QString getIniPath() const;

    QSettings *m_ini;
    FontWidget *m_view;
};

/////////////////////////////////////////////////////////////////
class FontPlugin : public QObject, public ViewerPluginInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID ViewerPluginInterface_iid FILE "../bin/plugin.json")
    Q_INTERFACES(ViewerPluginInterface)
public:
    ViewerBase *createViewer(QWidget *parent = nullptr) override
    {
        return new FontViewer(parent);
    }
};
