#pragma once

#include <QWidget>

class CharacterWidget;
class GlyphInspectorWidget;
class QPushButton;
class QLabel;

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
    void updateTheme(int theme);
    void copy();
    void saveAsSvg();

    void setCollapsed(bool c);
    bool isCollapsed() const
    {
        return m_is_collapsed;
    }

    void setCurrentText(const QString &t);
    QString getCurrentText() const;
    void setCurrentFontSize(int s);
    int getCurrentFontSize() const;

    Q_SIGNAL void sigToast(const QString &msg);
    Q_SIGNAL void sigCollapsedChanged(bool c);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void initUI(const QStringList &names);
    void updateTabTextPreview();
    void updateInfo();
    void onStyleChanged();
    void onNameChanged();
    void onTabChanged();

    CharacterWidget *m_wnd_char    = nullptr;
    GlyphInspectorWidget *m_wnd_gi = nullptr;
    QWidget *m_tab_gi              = nullptr;

    QPushButton *m_btn_copy        = nullptr;
    QPushButton *m_btn_svg         = nullptr;
    QPushButton *m_btn_toggle      = nullptr;
    QLabel *m_label_name_collapsed = nullptr;

    int m_theme         = 0;
    bool m_is_collapsed = false;
    QString m_font_path;

    Ui::FontWidget *ui;
};
