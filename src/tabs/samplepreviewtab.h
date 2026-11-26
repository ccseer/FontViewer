#pragma once

#include <QColor>
#include <QPixmap>
#include <QSize>
#include <QWidget>

#include <functional>

class QGridLayout;
class QLabel;
class QPainter;
class QScrollArea;

class SamplePreviewTab : public QWidget {
    Q_OBJECT

public:
    explicit SamplePreviewTab(QWidget *parent = nullptr);

    void setDpr(qreal dpr);
    void updateSample(const QString &family, const QFont &font);
    void setSampleFont(const QFont &font);

    QPixmap renderPixmap(qreal dpr) const;
    bool prepareSvgExport(QSize &size_out,
                          QColor &background_out,
                          std::function<void(QPainter &)> &draw_out) const;

    Q_SIGNAL void sigZoomRequested(int steps);
    Q_SIGNAL void sigInteractionClearFocus();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QScrollArea *m_scroll           = nullptr;
    QLabel *m_label                 = nullptr;
    QGridLayout *m_contents_layout  = nullptr;
    qreal m_dpr                     = 1.0;
};
