#pragma once

#include <QFont>
#include <QPixmap>
#include <QString>
#include <QVector>
#include <QWidget>
#include <QtGlobal>

class QEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;

class CharacterWidget : public QWidget {
    Q_OBJECT

public:
    static constexpr int kMinCellSizeDp = 28;
    static constexpr int kMaxCellSizeDp = 80;
    static constexpr int kDefCellSizeDp = 44;

    struct SelectedCharInfo {
        quint32 code_point = 0;
        QChar ch;
        QFont font;
        int square_size = 0;

        QString text() const
        {
            return (code_point > 0 && code_point <= 0x10FFFFu)
                       ? QString::fromUcs4(&code_point, 1)
                       : QString();
        }
    };

    explicit CharacterWidget(QWidget *parent = nullptr);

    void setDpr(qreal dpr);
    void setCellSizeDp(int cell_dp);
    int cellSizeDp() const
    {
        return m_cell_dp;
    }
    int cellSizePx() const;
    int columnCount() const;
    QRect cellRectForIndex(int index) const;

    void init(const QString &path);
    void setCodePoints(const QVector<quint32> &code_points);
    void setSelectedCodePoint(quint32 code_point);

    int getCharCount() const
    {
        return m_code_points.size();
    }
    quint32 selectedCodePoint() const
    {
        return m_last_key;
    }
    QPixmap getSelectedCharPix();
    SelectedCharInfo selectedCharInfo() const;
    const QFont &currentFont() const
    {
        return m_glyph_font;
    }

    bool hasHeightForWidth() const override
    {
        return true;
    }
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

public slots:
    void updateFont(const QFont &font);

signals:
    void selectionChangedCodePoint(quint32 cp);
    void characterDoubleClickedCodePoint(quint32 cp);
    void selectionChanged(QChar ch);
    void characterDoubleClicked(QChar ch);
    void interactionClearFocus();

protected:
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    struct GridMetrics {
        int cell_px  = 0;
        int cols     = 1;
        int origin_x = 0;
        int pad_y    = 0;
    };

    GridMetrics computeGridMetrics() const;
    int indexAtPos(const QPoint &pt) const;
    int indexOfCodePoint(quint32 cp) const;
    void updateCell(int index);
    quint32 codePointAtIndex(int index) const;
    QRect cellRect(int index, int cols, int cell_px, int origin_x, int pad_y) const;
    int glyphPixelSize() const;
    void syncGlyphFont();
    void updateGridGeometry();
    void emitSelection(quint32 cp);

    QVector<quint32> m_code_points;
    QFont m_base_font;
    QFont m_glyph_font;
    quint32 m_last_key = 0;
    int m_hover_index  = -1;
    int m_cell_dp      = kDefCellSizeDp;
    qreal m_dpr        = 1.0;
};
