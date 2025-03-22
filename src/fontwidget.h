#pragma once

#include <QWidget>

class CharacterWidget;

namespace Ui {
class FontWidget;
}

class FontWidget : public QWidget {
    Q_OBJECT
public:
    FontWidget(QWidget *parent = nullptr);
    ~FontWidget() override;

    bool init(const QString &p);
    void updateDPR(qreal r);
    void copy();

    void setCurrentText(const QString &t);
    QString getCurrentText() const;
    void setCurrentFontSize(int s);
    int getCurrentFontSize() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

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
