#pragma once

#include <QByteArray>
#include <QHash>
#include <QVector>
#include <QWidget>

class FontInspectorEngine;
class QGridLayout;
class QLabel;
class QPushButton;
class QSlider;
class QTimer;
class QVBoxLayout;

class VariableAxesBar : public QWidget {
    Q_OBJECT

public:
    explicit VariableAxesBar(QWidget *parent = nullptr);

    void setDpr(qreal dpr);
    void resetProbe();
    void rebuild(const FontInspectorEngine &engine,
                 int face_index,
                 const QString &family,
                 bool reset_to_defaults);
    void syncForStyle(const FontInspectorEngine &engine,
                      int face_index,
                      const QString &family,
                      const QString &style);
    void setAxisValue(const QByteArray &tag, float value);

    const QHash<QByteArray, float> &axisValues() const
    {
        return m_values;
    }

    Q_SIGNAL void sigAxesChanged();
    Q_SIGNAL void sigInteractionClearFocus();

protected:
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct AxisControl {
        QByteArray tag;
        QString title;
        float min_val       = 0.0f;
        float default_val   = 0.0f;
        float style_val     = 0.0f;
        float max_val       = 0.0f;
        int steps           = 1;
        QSlider *slider     = nullptr;
        QLabel *value       = nullptr;
        QPushButton *reset  = nullptr;
    };

    void probeBackend(const FontInspectorEngine &engine,
                      int face_index,
                      const QString &family);
    void updateLayoutMetrics();

    QHash<QByteArray, float> m_values;
    QVector<AxisControl> m_controls;
    QLabel *m_warning          = nullptr;
    QWidget *m_rows_host       = nullptr;
    QGridLayout *m_grid        = nullptr;
    QVBoxLayout *m_root_layout = nullptr;
    QTimer *m_timer            = nullptr;
    qreal m_dpr                = 1.0;
    bool m_gdi_probed          = false;
    bool m_gdi_inactive        = false;
};
