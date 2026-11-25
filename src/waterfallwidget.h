#pragma once

#include <QColor>
#include <QFont>
#include <QRectF>
#include <QVector>
#include <QWidget>

#include <functional>

class WaterfallWidget : public QWidget {
    Q_OBJECT

public:
    using FontProvider = std::function<QFont(int)>;

    explicit WaterfallWidget(QWidget *parent = nullptr);

    void setFontProvider(FontProvider provider);
    void setSizes(const QVector<int> &sizes_pt);
    void setCurrentSize(int pt);
    void setPreviewText(const QString &text);
    void setInverted(bool inverted);
    void refreshFont();
    bool isInverted() const
    {
        return m_inverted;
    }

    QVector<int> sizes() const
    {
        return m_sizes;
    }
    int currentSize() const
    {
        return m_current;
    }
    QRectF rowRect(int pt) const;
    QSize contentSize() const;
    QColor baseColor() const;
    void renderContent(QPainter *painter);

    QSize sizeHint() const override;

    Q_SIGNAL void sigSizeClicked(int pt);
    Q_SIGNAL void sigZoomRequested(int steps);

protected:
    void changeEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    struct Row {
        int pt = 0;
        QRectF rect;
    };

    void relayout();
    QFont rowFont(int pt) const;
    QString sizeLabel(int pt) const;
    QColor textColor() const;
    QColor mutedTextColor() const;
    void drawRows(QPainter &painter, qreal height);

    FontProvider m_font_provider;
    QVector<int> m_sizes;
    QVector<Row> m_rows;
    QString m_text;
    int m_current       = 16;
    bool m_inverted     = false;
    qreal m_label_width = 60.0;
    qreal m_content_height = 0.0;
};
