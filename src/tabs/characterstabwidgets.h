#pragma once

#include <QPushButton>
#include <QString>
#include <QWidget>
#include <QtGlobal>

class QComboBox;
class QEvent;
class QLineEdit;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;

// Responsive top toolbar for CharactersTab: lays out block selector, filter
// input, and cell-size controls in 1, 2, or 3 rows without clipping.
class CharactersToolbar : public QWidget {
public:
    CharactersToolbar(QComboBox *block_combo,
                      QLineEdit *filter_edit,
                      QWidget *cell_host,
                      QWidget *parent = nullptr);

    void setDpr(qreal dpr);
    void refreshLayout();

    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void updateMetrics();
    int singleRowHeight() const;
    int doLayout(int avail_w, bool test_only) const;

    QComboBox *m_block_combo = nullptr;
    QLineEdit *m_filter_edit = nullptr;
    QWidget *m_cell_host     = nullptr;
    qreal m_dpr              = 1.0;
    int m_margin_lr          = 8;
    int m_margin_top         = 6;
    int m_margin_bot         = 4;
    int m_h_spacing          = 8;
    int m_v_spacing          = 4;
};

// Lightweight custom-painted button displaying clickable metadata tokens while
// preserving the full untruncated string for copy-on-click.
class MetaCopyChip : public QPushButton {
public:
    explicit MetaCopyChip(qreal dpr, QWidget *parent = nullptr);

    void syncFontFromParent();
    void setDpr(qreal dpr);
    void setFullText(const QString &value);
    void setMaxWidth(int max_w);
    void refreshMetrics();

    const QString &fullText() const
    {
        return m_full_text;
    }

    int fullContentWidth() const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void changeEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    int heightForMetrics() const;
    int widthForText(const QString &t) const;
    void recomputeDisplay();

    qreal m_dpr = 1.0;
    QString m_full_text;
    int m_max_width     = -1;
    bool m_syncing_font = false;
};
