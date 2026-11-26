#include "variableaxesbar.h"

#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QGridLayout>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtMath>
#include <QtNumeric>

#include "engine/fontinspectorengine.h"
#include "fontui.h"

namespace {
int axisStepsForRange(float min_val, float max_val)
{
    const double span = double(max_val) - double(min_val);
    if (span <= 0.0) {
        return 1;
    }
    if (span <= 100.0) {
        return 200;
    }
    return qBound(200, qRound(span), 1000);
}

float axisValueFromSlider(float min_val, float max_val, int steps, int pos)
{
    if (steps <= 0 || max_val <= min_val) {
        return min_val;
    }
    return min_val + (max_val - min_val) * (float(pos) / float(steps));
}

int axisSliderFromValue(float min_val, float max_val, int steps, float val)
{
    if (steps <= 0 || max_val <= min_val) {
        return 0;
    }
    const float t = qBound(0.0f, (val - min_val) / (max_val - min_val), 1.0f);
    return qRound(t * steps);
}

QString formatAxisValue(float val, float min_val, float max_val)
{
    const float span = max_val - min_val;
    if (span <= 20.0f) {
        return QString::number(val, 'f', 2);
    }
    if (qAbs(val - qRound(val)) < 0.05f) {
        return QString::number(qRound(val));
    }
    return QString::number(val, 'f', 1);
}

void syncAxisResetButton(QPushButton *btn,
                         const QString &title,
                         float current_val,
                         float target_val,
                         float min_val,
                         float max_val,
                         int steps)
{
    if (!btn) {
        return;
    }
    const float tol = qMax(0.01f, (max_val - min_val) / qMax(1, steps));
    btn->setEnabled(qAbs(current_val - target_val) > tol);
    btn->setToolTip(
        VariableAxesBar::tr("Reset %1 to %2")
            .arg(title, formatAxisValue(target_val, min_val, max_val)));
}
}  // namespace

VariableAxesBar::VariableAxesBar(QWidget *parent) : QWidget(parent)
{
    setFocusPolicy(Qt::NoFocus);

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(16);
    connect(m_timer, &QTimer::timeout, this, &VariableAxesBar::sigAxesChanged);

    m_root_layout = new QVBoxLayout(this);

    m_warning = new QLabel(
        tr("Variable font axes may be inactive under the current font backend."),
        this);
    m_warning->setWordWrap(true);
    m_warning->setFocusPolicy(Qt::NoFocus);
    m_warning->hide();
    m_root_layout->addWidget(m_warning);

    m_rows_host = new QWidget(this);
    m_rows_host->setFocusPolicy(Qt::NoFocus);
    m_grid = new QGridLayout(m_rows_host);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setColumnStretch(1, 1);
    m_root_layout->addWidget(m_rows_host);

    updateLayoutMetrics();
    hide();
}

void VariableAxesBar::setDpr(qreal dpr)
{
    const qreal clamped = fontui::clampDpr(dpr);
    if (qFuzzyCompare(m_dpr, clamped)) {
        return;
    }
    m_dpr = clamped;
    updateLayoutMetrics();
}

void VariableAxesBar::updateLayoutMetrics()
{
    if (m_root_layout) {
        m_root_layout->setContentsMargins(0, fontui::dp(2, m_dpr), 0,
                                          fontui::dp(2, m_dpr));
        m_root_layout->setSpacing(fontui::dp(4, m_dpr));
    }
    if (m_grid) {
        m_grid->setHorizontalSpacing(fontui::dp(8, m_dpr));
        m_grid->setVerticalSpacing(fontui::dp(4, m_dpr));
    }
    for (const AxisControl &c : m_controls) {
        if (c.value) {
            c.value->setMinimumWidth(
                QFontMetrics(c.value->font())
                    .horizontalAdvance(QStringLiteral("-1000.00"))
                + fontui::dp(4, m_dpr));
        }
        if (c.reset) {
            static_cast<fontui::CompactActionButton *>(c.reset)->setDpr(m_dpr);
        }
    }
}

void VariableAxesBar::resetProbe()
{
    m_gdi_probed   = false;
    m_gdi_inactive = false;
}

void VariableAxesBar::rebuild(const FontInspectorEngine &engine,
                              int face_index,
                              const QString &family,
                              bool reset_to_defaults)
{
    if (m_timer) {
        m_timer->stop();
    }

    for (const AxisControl &ctrl : m_controls) {
        if (ctrl.slider) {
            ctrl.slider->removeEventFilter(this);
        }
    }
    m_controls.clear();

    while (QLayoutItem *item = m_grid->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }

    const auto &axes = engine.inspectFace(face_index).variable_axes;
    if (axes.isEmpty()) {
        m_values.clear();
        m_warning->hide();
        hide();
        return;
    }

    QHash<QByteArray, float> next_values;
    for (int row = 0; row < axes.size(); ++row) {
        const FontVariableAxisInfo &ax = axes[row];
        float cur_val                  = ax.default_value;
        if (!reset_to_defaults && m_values.contains(ax.tag)) {
            cur_val
                = qBound(ax.min_value, m_values.value(ax.tag), ax.max_value);
        }
        next_values.insert(ax.tag, cur_val);

        const QString title
            = (ax.name == QString::fromLatin1(ax.tag))
                  ? ax.name
                  : QStringLiteral("%1 (%2)").arg(
                        ax.name, QString::fromLatin1(ax.tag));
        auto *name_lbl = new QLabel(title, m_rows_host);
        name_lbl->setFocusPolicy(Qt::NoFocus);

        const int steps = axisStepsForRange(ax.min_value, ax.max_value);
        auto *slider    = new QSlider(Qt::Horizontal, m_rows_host);
        slider->setRange(0, steps);
        slider->setValue(
            axisSliderFromValue(ax.min_value, ax.max_value, steps, cur_val));
        slider->setFocusPolicy(Qt::NoFocus);
        slider->setEnabled(ax.max_value > ax.min_value);
        slider->installEventFilter(this);

        auto *val_lbl = new QLabel(
            formatAxisValue(cur_val, ax.min_value, ax.max_value), m_rows_host);
        val_lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        val_lbl->setMinimumWidth(
            QFontMetrics(val_lbl->font())
                .horizontalAdvance(QStringLiteral("-1000.00"))
            + fontui::dp(4, m_dpr));
        val_lbl->setFocusPolicy(Qt::NoFocus);

        auto *reset_btn = new fontui::CompactActionButton(
            tr("Reset"), m_dpr, m_rows_host, true);
        syncAxisResetButton(reset_btn, title, cur_val, ax.default_value,
                            ax.min_value, ax.max_value, steps);

        m_grid->addWidget(name_lbl, row, 0);
        m_grid->addWidget(slider, row, 1);
        m_grid->addWidget(val_lbl, row, 2);
        m_grid->addWidget(reset_btn, row, 3);

        AxisControl ctrl;
        ctrl.tag         = ax.tag;
        ctrl.title       = title;
        ctrl.min_val     = ax.min_value;
        ctrl.default_val = ax.default_value;
        ctrl.style_val   = ax.default_value;
        ctrl.max_val     = ax.max_value;
        ctrl.steps       = steps;
        ctrl.slider      = slider;
        ctrl.value       = val_lbl;
        ctrl.reset       = reset_btn;
        m_controls.append(ctrl);

        const QByteArray tag = ax.tag;
        connect(slider, &QSlider::sliderPressed, this,
                &VariableAxesBar::sigInteractionClearFocus);
        connect(slider, &QSlider::valueChanged, this, [this, tag](int pos) {
            for (const AxisControl &c : m_controls) {
                if (c.tag == tag) {
                    const float v = axisValueFromSlider(c.min_val, c.max_val,
                                                        c.steps, pos);
                    m_values.insert(tag, v);
                    if (c.value) {
                        c.value->setText(
                            formatAxisValue(v, c.min_val, c.max_val));
                    }
                    syncAxisResetButton(c.reset, c.title, v, c.style_val,
                                        c.min_val, c.max_val, c.steps);
                    break;
                }
            }
            emit sigInteractionClearFocus();
            if (m_timer && !m_timer->isActive()) {
                m_timer->start();
            }
        });
        connect(reset_btn, &QPushButton::clicked, this, [this, tag]() {
            for (const AxisControl &c : m_controls) {
                if (c.tag == tag) {
                    setAxisValue(tag, c.style_val);
                    break;
                }
            }
            emit sigInteractionClearFocus();
        });
    }

    m_values = std::move(next_values);
    show();
    probeBackend(engine, face_index, family);
}

void VariableAxesBar::setAxisValue(const QByteArray &tag, float value)
{
    for (const AxisControl &c : m_controls) {
        if (c.tag != tag) {
            continue;
        }
        const float finite  = qIsFinite(value) ? value : c.style_val;
        const float clamped = qBound(c.min_val, finite, c.max_val);
        m_values.insert(tag, clamped);
        if (c.slider) {
            QSignalBlocker blocker(c.slider);
            c.slider->setValue(
                axisSliderFromValue(c.min_val, c.max_val, c.steps, clamped));
        }
        if (c.value) {
            c.value->setText(formatAxisValue(clamped, c.min_val, c.max_val));
        }
        syncAxisResetButton(c.reset, c.title, clamped, c.style_val, c.min_val,
                            c.max_val, c.steps);
        if (m_timer) {
            m_timer->stop();
        }
        emit sigAxesChanged();
        return;
    }
}

void VariableAxesBar::syncForStyle(const FontInspectorEngine &engine,
                                   int face_index,
                                   const QString &family,
                                   const QString &style)
{
    std::optional<FontNamedInstance> inst;
    if (!family.isEmpty()) {
        // DirectWrite also registers named instances as standalone family names
        // (e.g. family "Roboto Condensed" with style "Regular"). Match the
        // family's subfamily suffix before falling back to bare style so
        // "Regular" under "Roboto Condensed" resolves to "Condensed Regular".
        QString suffix;
        int best_len = 0;
        for (const auto &rec : engine.inspectFace(face_index).name_records) {
            if (rec.label == QLatin1String("Family")
                || rec.label == QLatin1String("Typographic Family")) {
                if (rec.value.size() > best_len
                    && family.startsWith(rec.value + QLatin1Char(' '),
                                         Qt::CaseInsensitive)) {
                    best_len = rec.value.size();
                    suffix   = family.mid(rec.value.size()).trimmed();
                }
            }
        }
        if (!suffix.isEmpty()) {
            if (FontInspectorEngine::normalizeStyleName(style)
                == QLatin1String("regular")) {
                inst = engine.matchNamedInstance(face_index, suffix);
            }
            else {
                inst = engine.matchNamedInstance(
                    face_index, suffix + QLatin1Char(' ') + style);
                if (!inst.has_value()) {
                    inst = engine.matchNamedInstance(face_index, suffix);
                }
            }
        }
    }
    if (!inst.has_value()) {
        inst = engine.matchNamedInstance(face_index, style);
    }
    if (!inst.has_value()) {
        return;
    }
    const auto &axes = engine.inspectFace(face_index).variable_axes;
    const int n      = qMin(axes.size(), inst->coords.size());
    for (int i = 0; i < n; ++i) {
        const QByteArray &tag = axes[i].tag;
        const float val
            = qBound(axes[i].min_value, inst->coords[i], axes[i].max_value);
        m_values.insert(tag, val);
        for (AxisControl &c : m_controls) {
            if (c.tag == tag) {
                c.style_val = val;
                if (c.slider) {
                    QSignalBlocker blocker(c.slider);
                    c.slider->setValue(axisSliderFromValue(
                        c.min_val, c.max_val, c.steps, val));
                }
                if (c.value) {
                    c.value->setText(
                        formatAxisValue(val, c.min_val, c.max_val));
                }
                syncAxisResetButton(c.reset, c.title, val, c.style_val,
                                    c.min_val, c.max_val, c.steps);
                break;
            }
        }
    }
}

void VariableAxesBar::probeBackend(const FontInspectorEngine &engine,
                                   int face_index,
                                   const QString &family)
{
    if (!m_warning) {
        return;
    }
    if (m_gdi_probed) {
        m_warning->setVisible(m_gdi_inactive);
        return;
    }

    const auto &axes      = engine.inspectFace(face_index).variable_axes;
    bool has_varying_axis = false;
    for (const FontVariableAxisInfo &ax : axes) {
        if (ax.min_value < ax.max_value
            && QFont::Tag::fromString(ax.tag).has_value()) {
            has_varying_axis = true;
            break;
        }
    }
    if (!has_varying_axis) {
        m_warning->hide();
        return;
    }

    m_gdi_probed   = true;
    QString sample = engine.probeSampleForFace(face_index);
    if (sample.isEmpty()) {
        sample = QStringLiteral("Hamburgefonstiv");
    }

    auto render_probe = [&sample](const QFont &f) {
        QImage img(160, 64, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::white);
        QPainter p(&img);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        p.setFont(f);
        p.setPen(Qt::black);
        p.drawText(QPointF(8.0, 46.0), sample);
        p.end();
        return img;
    };

    bool any_effect = false;
    for (const FontVariableAxisInfo &ax : axes) {
        if (ax.min_value >= ax.max_value) {
            continue;
        }
        const auto tag = QFont::Tag::fromString(ax.tag);
        if (!tag.has_value()) {
            continue;
        }
        QFont f_min(family);
        QFont f_max(family);
        f_min.setPointSize(48);
        f_max.setPointSize(48);
        f_min.setVariableAxis(*tag, ax.min_value);
        f_max.setVariableAxis(*tag, ax.max_value);

        const qreal w_min
            = QFontMetricsF(f_min, this).horizontalAdvance(sample);
        const qreal w_max
            = QFontMetricsF(f_max, this).horizontalAdvance(sample);
        if (qAbs(w_min - w_max) >= 0.05
            || render_probe(f_min) != render_probe(f_max)) {
            any_effect = true;
            break;
        }
    }

    m_gdi_inactive = !any_effect;
    m_warning->setVisible(m_gdi_inactive);
}

bool VariableAxesBar::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Wheel) {
        for (const AxisControl &c : m_controls) {
            if (watched == c.slider) {
                static_cast<QWheelEvent *>(event)->accept();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}
