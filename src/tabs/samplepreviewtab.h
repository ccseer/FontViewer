#pragma once

#include <QColor>
#include <QFont>
#include <QPixmap>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

class QEvent;
class QHBoxLayout;
class QLabel;
class QPainter;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

class SamplePreviewTab : public QWidget {
    Q_OBJECT

public:
    explicit SamplePreviewTab(QWidget *parent = nullptr);

    void setDpr(qreal dpr);
    void updateSample(const QString &family,
                      const QFont &font,
                      const QString &fallback_sample = {});
    void setSampleFont(const QFont &font);

    QPixmap renderPixmap(qreal dpr) const;
    bool prepareSvgExport(QSize &size_out,
                          QColor &background_out,
                          std::function<void(QPainter &)> &draw_out) const;
    void refreshThemeVisuals();

    Q_SIGNAL void sigZoomRequested(int steps);
    Q_SIGNAL void sigInteractionClearFocus();
    Q_SIGNAL void sigCopyToastRequested(const QString &msg);

protected:
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateLayoutMetrics();
    void syncCaptionFonts();

    QWidget *m_toolbar           = nullptr;
    QLabel *m_summary_label      = nullptr;
    QPushButton *m_btn_copy_all  = nullptr;
    QScrollArea *m_scroll        = nullptr;
    QWidget *m_specimen_host     = nullptr;
    QVBoxLayout *m_specimen_lay  = nullptr;
    QVector<QVBoxLayout *> m_section_layouts;
    QVector<QHBoxLayout *> m_header_layouts;
    QVector<QLabel *> m_ws_labels;
    QVector<QLabel *> m_specimen_labels;
    QVector<QPushButton *> m_copy_buttons;
    QVector<QWidget *> m_dividers;
    QStringList m_all_samples;
    QString m_last_family;
    QString m_last_style_key;
    QString m_last_fallback;
    QFont m_sample_font;
    qreal m_dpr            = 1.0;
};
