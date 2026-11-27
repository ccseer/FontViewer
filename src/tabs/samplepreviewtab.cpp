#include "samplepreviewtab.h"

#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QRawFont>
#include <QRegion>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>

#include "fontui.h"

namespace {
class SampleDivider : public QWidget {
public:
    explicit SampleDivider(qreal dpr, QWidget *parent = nullptr)
        : QWidget(parent), m_dpr(fontui::clampDpr(dpr))
    {
        setFocusPolicy(Qt::NoFocus);
        setFixedHeight(qMax(1, fontui::dp(1, m_dpr)));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setDpr(qreal dpr)
    {
        m_dpr = fontui::clampDpr(dpr);
        setFixedHeight(qMax(1, fontui::dp(1, m_dpr)));
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), fontui::frameBorderColor(palette()));
    }

private:
    qreal m_dpr = 1.0;
};

QString sampleStyleKey(const QFont &font)
{
    QStringList parts;
    parts << font.styleName() << QString::number(font.weight())
          << (font.italic() ? QStringLiteral("1") : QStringLiteral("0"))
          << QString::number(int(font.styleStrategy()));
    const auto tags = font.variableAxisTags();
    if (!tags.isEmpty()) {
        QList<QFont::Tag> sorted = tags;
        std::sort(sorted.begin(), sorted.end());
        for (const QFont::Tag &tag : sorted) {
            if (font.isVariableAxisSet(tag)) {
                parts << QStringLiteral("%1=%2").arg(
                    QString::fromLatin1(tag.toString()),
                    QString::number(font.variableAxisValue(tag), 'f', 3));
            }
        }
    }
    return parts.join(QLatin1Char('|'));
}
}  // namespace

SamplePreviewTab::SamplePreviewTab(QWidget *parent) : QWidget(parent)
{
    setFocusPolicy(Qt::NoFocus);

    auto *root = new QGridLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_toolbar = new QWidget(this);
    m_toolbar->setObjectName(QStringLiteral("sample_toolbar"));
    m_toolbar->setFocusPolicy(Qt::NoFocus);
    auto *tb_lay = new QHBoxLayout(m_toolbar);

    m_summary_label = new QLabel(m_toolbar);
    m_summary_label->setObjectName(QStringLiteral("sample_summary_label"));
    m_summary_label->setTextFormat(Qt::PlainText);
    syncCaptionFonts();

    m_btn_copy_all = new fontui::CompactActionButton(tr("Copy All"), m_dpr,
                                                     m_toolbar, false);
    m_btn_copy_all->setObjectName(QStringLiteral("sample_copy_all_btn"));
    m_btn_copy_all->setToolTip(tr("Copy all writing system samples"));
    connect(m_btn_copy_all, &QPushButton::clicked, this, [this]() {
        if (!m_all_samples.isEmpty()) {
            QApplication::clipboard()->setText(
                m_all_samples.join(QLatin1Char('\n')));
            emit sigCopyToastRequested(tr("Copied all samples"));
        }
        emit sigInteractionClearFocus();
    });

    tb_lay->addWidget(m_summary_label);
    tb_lay->addStretch(1);
    tb_lay->addWidget(m_btn_copy_all);

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("scrollArea_sample"));
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFocusPolicy(Qt::NoFocus);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->viewport()->setAutoFillBackground(false);
    m_scroll->viewport()->installEventFilter(this);
    if (QScrollBar *vsb = m_scroll->verticalScrollBar()) {
        vsb->setProperty("is_readonly", true);
        vsb->setFocusPolicy(Qt::NoFocus);
    }
    if (QScrollBar *hsb = m_scroll->horizontalScrollBar()) {
        hsb->setProperty("is_readonly", true);
        hsb->setFocusPolicy(Qt::NoFocus);
    }

    auto *contents = new QWidget(m_scroll);
    contents->setObjectName(QStringLiteral("scrollAreaWidgetContents_sample"));
    contents->setFocusPolicy(Qt::NoFocus);
    auto *contents_lay = new QVBoxLayout(contents);
    contents_lay->setContentsMargins(0, 0, 0, 0);
    contents_lay->setSpacing(0);

    m_specimen_host = new QWidget(contents);
    m_specimen_host->setObjectName(QStringLiteral("sample_specimen_host"));
    m_specimen_host->setFocusPolicy(Qt::NoFocus);
    m_specimen_host->installEventFilter(this);
    m_specimen_lay = new QVBoxLayout(m_specimen_host);
    contents_lay->addWidget(m_specimen_host);
    contents_lay->addStretch(1);

    m_scroll->setWidget(contents);

    auto *top_divider = new SampleDivider(m_dpr, this);
    m_dividers.append(top_divider);

    root->addWidget(m_toolbar, 0, 0);
    root->addWidget(top_divider, 1, 0);
    root->addWidget(m_scroll, 2, 0);
    root->setRowStretch(2, 1);

    updateLayoutMetrics();
    refreshThemeVisuals();
}

void SamplePreviewTab::setDpr(qreal dpr)
{
    const qreal clamped = fontui::clampDpr(dpr);
    if (qFuzzyCompare(m_dpr, clamped)) {
        return;
    }
    m_dpr = clamped;
    updateLayoutMetrics();
}

void SamplePreviewTab::updateLayoutMetrics()
{
    if (m_toolbar && m_toolbar->layout()) {
        m_toolbar->layout()->setContentsMargins(
            fontui::dp(12, m_dpr), fontui::dp(6, m_dpr), fontui::dp(12, m_dpr),
            fontui::dp(6, m_dpr));
        m_toolbar->layout()->setSpacing(fontui::dp(8, m_dpr));
    }
    if (m_btn_copy_all) {
        static_cast<fontui::CompactActionButton *>(m_btn_copy_all)
            ->setDpr(m_dpr);
    }
    if (m_specimen_lay) {
        const int pad_x = fontui::dp(16, m_dpr);
        const int pad_y = fontui::dp(12, m_dpr);
        m_specimen_lay->setContentsMargins(pad_x, pad_y, pad_x, pad_y);
        m_specimen_lay->setSpacing(fontui::dp(14, m_dpr));
    }
    for (QVBoxLayout *sl : m_section_layouts) {
        if (sl) {
            sl->setSpacing(fontui::dp(6, m_dpr));
        }
    }
    for (QHBoxLayout *hl : m_header_layouts) {
        if (hl) {
            hl->setSpacing(fontui::dp(8, m_dpr));
        }
    }
    for (QPushButton *b : m_copy_buttons) {
        if (b) {
            static_cast<fontui::CompactActionButton *>(b)->setDpr(m_dpr);
        }
    }
    for (QWidget *d : m_dividers) {
        if (d) {
            static_cast<SampleDivider *>(d)->setDpr(m_dpr);
        }
    }
    syncCaptionFonts();
}

void SamplePreviewTab::syncCaptionFonts()
{
    if (m_summary_label) {
        QFont sum_f = fontui::scaledCaptionFont(m_summary_label, m_dpr, 0.0, 9.0,
                                                0.95);
        if (const auto tnum = QFont::Tag::fromString("tnum"); tnum.has_value()) {
            sum_f.setFeature(*tnum, 1);
        }
        if (m_summary_label->font() != sum_f) {
            m_summary_label->setFont(sum_f);
        }
    }
    for (QLabel *ws : m_ws_labels) {
        if (!ws) {
            continue;
        }
        QFont ws_f = fontui::scaledCaptionFont(ws, m_dpr, -0.5, 8.5, 0.90);
        ws_f.setWeight(QFont::DemiBold);
        if (ws->font() != ws_f) {
            ws->setFont(ws_f);
        }
    }
}

void SamplePreviewTab::updateSample(const QString &family,
                                    const QFont &font,
                                    const QString &fallback_sample)
{
    const QString style_key = sampleStyleKey(font);
    if (!m_specimen_labels.isEmpty() && family == m_last_family
        && style_key == m_last_style_key
        && fallback_sample == m_last_fallback) {
        setSampleFont(font);
        return;
    }
    m_last_family    = family;
    m_last_style_key = style_key;
    m_last_fallback  = fallback_sample;
    m_sample_font    = font;

    // Preserve the top toolbar divider cleanly while rebuilding specimen items.
    QWidget *top_divider = m_dividers.isEmpty() ? nullptr : m_dividers.first();
    m_section_layouts.clear();
    m_header_layouts.clear();
    m_ws_labels.clear();
    m_specimen_labels.clear();
    m_copy_buttons.clear();
    m_dividers.clear();
    if (top_divider) {
        m_dividers.append(top_divider);
    }
    m_all_samples.clear();

    if (m_specimen_lay) {
        while (QLayoutItem *item = m_specimen_lay->takeAt(0)) {
            if (QWidget *w = item->widget()) {
                delete w;
            }
            delete item;
        }
    }

    struct ScriptItem {
        QString name;
        QString sample;
    };
    QVector<ScriptItem> entries;
    const QString fb      = fallback_sample.trimmed();
    const QRawFont raw    = QRawFont::fromFont(font);
    const auto ws_list    = QFontDatabase::writingSystems(family);
    auto fontSupportsAny  = [&raw](const QString &text) {
        if (!raw.isValid()) {
            return true;
        }
        const QVector<quint32> gids = raw.glyphIndexesForString(text);
        return std::any_of(gids.cbegin(), gids.cend(),
                           [](quint32 gid) { return gid != 0; });
    };
    bool used_fallback_section = false;
    for (QFontDatabase::WritingSystem ws : ws_list) {
        QString s = QFontDatabase::writingSystemSample(ws).trimmed();
        if (s.isEmpty()) {
            continue;
        }
        if (!fontSupportsAny(s)) {
            if (!fb.isEmpty() && !used_fallback_section
                && (ws == QFontDatabase::Any || ws == QFontDatabase::Symbol)) {
                s                     = fb;
                used_fallback_section = true;
            }
            else {
                continue;
            }
        }
        entries.append({QFontDatabase::writingSystemName(ws), s});
    }
    if (entries.isEmpty()) {
        const QString def_fb
            = fb.isEmpty() ? QStringLiteral("Hamburgefonstiv") : fb;
        entries.append({tr("Default"), def_fb});
    }

    if (m_summary_label) {
        m_summary_label->setText(
            entries.size() == 1
                ? tr("1 Writing System")
                : tr("%1 Writing Systems").arg(entries.size()));
    }
    if (m_btn_copy_all) {
        m_btn_copy_all->setEnabled(!entries.isEmpty());
    }

    for (int i = 0; i < entries.size(); ++i) {
        const ScriptItem &item = entries.at(i);
        m_all_samples.append(item.sample);

        auto *section = new QWidget(m_specimen_host);
        section->setFocusPolicy(Qt::NoFocus);
        auto *sec_lay = new QVBoxLayout(section);
        sec_lay->setContentsMargins(0, 0, 0, 0);
        sec_lay->setSpacing(fontui::dp(6, m_dpr));
        m_section_layouts.append(sec_lay);

        auto *hdr_row = new QHBoxLayout();
        hdr_row->setContentsMargins(0, 0, 0, 0);
        hdr_row->setSpacing(fontui::dp(8, m_dpr));
        m_header_layouts.append(hdr_row);

        auto *ws_lbl = new QLabel(item.name, section);
        ws_lbl->setTextFormat(Qt::PlainText);
        m_ws_labels.append(ws_lbl);

        auto *copy_btn = new fontui::CompactActionButton(tr("Copy"), m_dpr,
                                                         section, false);
        copy_btn->setObjectName(QStringLiteral("sample_copy_btn"));
        copy_btn->setToolTip(tr("Copy %1 sample").arg(item.name));
        connect(copy_btn, &QPushButton::clicked, this,
                [this, text = item.sample, name = item.name]() {
                    QApplication::clipboard()->setText(text);
                    emit sigCopyToastRequested(tr("Copied %1 sample").arg(name));
                    emit sigInteractionClearFocus();
                });
        m_copy_buttons.append(copy_btn);

        hdr_row->addWidget(ws_lbl);
        hdr_row->addStretch(1);
        hdr_row->addWidget(copy_btn);

        auto *spec_lbl = new QLabel(section);
        spec_lbl->setObjectName(
            i == 0 ? QStringLiteral("label_sample")
                   : QStringLiteral("label_sample_%1").arg(i));
        spec_lbl->setFocusPolicy(Qt::NoFocus);
        spec_lbl->setTextFormat(Qt::PlainText);
        spec_lbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        spec_lbl->setWordWrap(true);
        spec_lbl->setText(item.sample);
        spec_lbl->setFont(m_sample_font);
        spec_lbl->installEventFilter(this);
        m_specimen_labels.append(spec_lbl);

        sec_lay->addLayout(hdr_row);
        sec_lay->addWidget(spec_lbl);
        m_specimen_lay->addWidget(section);

        if (i + 1 < entries.size()) {
            auto *div = new SampleDivider(m_dpr, m_specimen_host);
            m_dividers.append(div);
            m_specimen_lay->addWidget(div);
        }
    }

    refreshThemeVisuals();
    syncCaptionFonts();
}

void SamplePreviewTab::setSampleFont(const QFont &font)
{
    m_sample_font = font;
    for (QLabel *lbl : m_specimen_labels) {
        if (lbl) {
            lbl->setFont(font);
        }
    }
}

QPixmap SamplePreviewTab::renderPixmap(qreal dpr) const
{
    if (!m_specimen_host) {
        return {};
    }
    const QSize sz = m_specimen_host->size();
    if (!sz.isValid() || sz.isEmpty()) {
        return {};
    }
    QPixmap pix(QSize(qCeil(sz.width() * dpr), qCeil(sz.height() * dpr)));
    if (pix.isNull()) {
        return {};
    }
    pix.setDevicePixelRatio(dpr);
    pix.fill(m_specimen_host->palette().color(m_specimen_host->backgroundRole()));

    QVector<bool> prev_hidden;
    prev_hidden.reserve(m_copy_buttons.size());
    for (QPushButton *b : m_copy_buttons) {
        prev_hidden.append(b ? b->isHidden() : true);
        if (b) {
            b->hide();
        }
    }

    QPainter painter(&pix);
    m_specimen_host->render(&painter, QPoint(), QRegion(),
                            QWidget::DrawChildren);
    painter.end();

    for (int i = 0; i < m_copy_buttons.size(); ++i) {
        if (QPushButton *b = m_copy_buttons.at(i)) {
            b->setHidden(prev_hidden.at(i));
        }
    }
    return pix;
}

bool SamplePreviewTab::prepareSvgExport(
    QSize &size_out,
    QColor &background_out,
    std::function<void(QPainter &)> &draw_out) const
{
    if (!m_specimen_host) {
        return false;
    }
    size_out = m_specimen_host->size();
    if (!size_out.isValid() || size_out.isEmpty()) {
        return false;
    }
    background_out
        = m_specimen_host->palette().color(m_specimen_host->backgroundRole());
    QPointer<QWidget> host(m_specimen_host);
    QVector<QPointer<QPushButton>> safe_buttons;
    safe_buttons.reserve(m_copy_buttons.size());
    for (QPushButton *b : m_copy_buttons) {
        safe_buttons.append(b);
    }
    draw_out = [host, safe_buttons](QPainter &p) {
        if (!host) {
            return;
        }
        QVector<bool> prev_hidden;
        prev_hidden.reserve(safe_buttons.size());
        for (const QPointer<QPushButton> &b : safe_buttons) {
            prev_hidden.append(b ? b->isHidden() : true);
            if (b) {
                b->hide();
            }
        }
        host->render(&p, QPoint(), QRegion(), QWidget::DrawChildren);
        for (int i = 0; i < safe_buttons.size(); ++i) {
            if (const QPointer<QPushButton> &b = safe_buttons.at(i)) {
                b->setHidden(prev_hidden.at(i));
            }
        }
    };
    return true;
}

void SamplePreviewTab::refreshThemeVisuals()
{
    const QPalette &pal = palette();
    const QColor muted  = fontui::mutedTextColor(pal);

    if (m_summary_label) {
        QPalette mp = pal;
        mp.setColor(QPalette::WindowText, muted);
        m_summary_label->setPalette(mp);
    }
    for (QLabel *ws : m_ws_labels) {
        if (ws) {
            QPalette wp = pal;
            wp.setColor(QPalette::WindowText, muted);
            ws->setPalette(wp);
        }
    }
    for (QLabel *spec : m_specimen_labels) {
        if (spec) {
            QPalette sp = pal;
            sp.setColor(QPalette::WindowText, pal.color(QPalette::WindowText));
            spec->setPalette(sp);
            spec->update();
        }
    }
    for (QWidget *div : m_dividers) {
        if (div) {
            div->update();
        }
    }
    if (m_btn_copy_all) {
        m_btn_copy_all->update();
    }
    for (QPushButton *b : m_copy_buttons) {
        if (b) {
            b->update();
        }
    }
}

void SamplePreviewTab::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::StyleChange
        || event->type() == QEvent::FontChange) {
        updateLayoutMetrics();
        refreshThemeVisuals();
    }
    else if (event->type() == QEvent::PaletteChange
             || event->type() == QEvent::ApplicationPaletteChange) {
        refreshThemeVisuals();
    }
}

bool SamplePreviewTab::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Wheel
        && (watched == m_specimen_host
            || (m_scroll && watched == m_scroll->viewport())
            || std::find(m_specimen_labels.cbegin(), m_specimen_labels.cend(),
                         watched)
                   != m_specimen_labels.cend())) {
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
