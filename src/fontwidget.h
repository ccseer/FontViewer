#pragma once

#include <QFontDatabase>
#include <QWidget>

class CharacterWidget;

namespace Ui {
class FontWidget;
}

class FontWidget : public QWidget {
    Q_OBJECT

public:
    FontWidget(QWidget *parent = nullptr);
    ~FontWidget();

    bool init(const QString &p);
    void copy();
    void updateDPR(qreal r);

protected:
    virtual bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void initUI(const QStringList &names);
    void updateTabTextPreview();
    void updateInfo();
    void onStyleChanged();
    void onNameChanged();
    void onTabChanged();

    CharacterWidget *m_wnd_char;
    Ui::FontWidget *ui;
};
