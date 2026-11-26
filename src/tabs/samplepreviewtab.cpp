#include "samplepreviewtab.h"

#include <QEvent>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QRegion>
#include <QScrollArea>
#include <QWheelEvent>
#include <QtMath>

#include "fontui.h"

SamplePreviewTab::SamplePreviewTab(QWidget *parent) : QWidget(parent)
{
    setFocusPolicy(Qt::NoFocus);

    auto *root = new QGridLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("scrollArea_sample"));
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFocusPolicy(Qt::NoFocus);

    auto *contents = new QWidget(m_scroll);
    contents->setObjectName(
        QStringLiteral("scrollAreaWidgetContents_sample"));
    contents->setFocusPolicy(Qt::NoFocus);
    m_contents_layout = new QGridLayout(contents);
    const int pad     = fontui::dp(9, m_dpr);
    m_contents_layout->setContentsMargins(pad, pad, pad, pad);

    m_label = new QLabel(contents);
    m_label->setObjectName(QStringLiteral("label_sample"));
    m_label->setFocusPolicy(Qt::NoFocus);
    m_label->setTextFormat(Qt::PlainText);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setWordWrap(true);
    m_label->installEventFilter(this);

    m_contents_layout->addWidget(m_label, 0, 0);
    m_scroll->setWidget(contents);
    root->addWidget(m_scroll, 0, 0);
}

void SamplePreviewTab::setDpr(qreal dpr)
{
    const qreal clamped = fontui::clampDpr(dpr);
    if (qFuzzyCompare(m_dpr, clamped)) {
        return;
    }
    m_dpr = clamped;
    if (m_contents_layout) {
        const int pad = fontui::dp(9, m_dpr);
        m_contents_layout->setContentsMargins(pad, pad, pad, pad);
    }
}

void SamplePreviewTab::updateSample(const QString &family, const QFont &font)
{
    if (!m_label) {
        return;
    }
    QString text;
    const auto ws = QFontDatabase::writingSystems(family);
    for (auto i : ws) {
        text.append(QFontDatabase::writingSystemSample(i));
        text.append(QLatin1Char('\n'));
    }
    m_label->setText(text.trimmed());
    m_label->setFont(font);
}

void SamplePreviewTab::setSampleFont(const QFont &font)
{
    if (m_label) {
        m_label->setFont(font);
    }
}

QPixmap SamplePreviewTab::renderPixmap(qreal dpr) const
{
    if (!m_label) {
        return {};
    }
    QPixmap pix(
        QSize(qCeil(m_label->width() * dpr), qCeil(m_label->height() * dpr)));
    if (pix.isNull()) {
        return {};
    }
    pix.setDevicePixelRatio(dpr);
    pix.fill(m_label->palette().color(m_label->backgroundRole()));
    QPainter painter(&pix);
    m_label->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    painter.end();
    return pix;
}

bool SamplePreviewTab::prepareSvgExport(
    QSize &size_out,
    QColor &background_out,
    std::function<void(QPainter &)> &draw_out) const
{
    if (!m_label) {
        return false;
    }
    size_out = m_label->size();
    background_out
        = m_label->palette().color(m_label->backgroundRole());
    draw_out = [label = m_label](QPainter &p) {
        label->render(&p, QPoint(), QRegion(), QWidget::DrawChildren);
    };
    return true;
}

bool SamplePreviewTab::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_label && event->type() == QEvent::Wheel) {
        auto *wheel = static_cast<QWheelEvent *>(event);
        if (wheel->modifiers() != Qt::ControlModifier) {
            return false;
        }
        if (wheel->angleDelta().y() == 0) {
            wheel->accept();
            return true;
        }
        emit sigZoomRequested(wheel->angleDelta().y() > 0 ? 1 : -1);
        emit sigInteractionClearFocus();
        wheel->accept();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
