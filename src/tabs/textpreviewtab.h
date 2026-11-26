#pragma once

#include <QColor>
#include <QFont>
#include <QPixmap>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

class FontPreviewEdit;
class WaterfallWidget;
class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QPainter;
class QPushButton;
class QScrollArea;
class QSettings;
class QSlider;
class QStackedWidget;

class TextPreviewTab : public QWidget {
    Q_OBJECT

public:
    using FontProvider = std::function<QFont(int)>;
    struct CustomPreset {
        QString title;
        QString content;
    };

    explicit TextPreviewTab(QWidget *parent = nullptr);

    void setDpr(qreal dpr);
    void setFontProvider(FontProvider provider);
    void loadPresets(QSettings &ini);
    void setCustomPresets(const QVector<CustomPreset> &presets);
    void applyFont(const QFont &text_font, int current_pt);
    void clearUndoHistory();
    void closeAaPanel();

    void setCurrentText(const QString &t);
    QString currentText() const;
    QString lastEditedText() const
    {
        return m_last_edited;
    }

    void setLineHeight(qreal h);
    qreal lineHeight() const
    {
        return m_line_height;
    }

    void setLetterSpacing(qreal s);
    qreal letterSpacing() const
    {
        return m_letter_spacing;
    }

    void setWaterfallEnabled(bool on);
    bool isWaterfallEnabled() const
    {
        return m_waterfall_enabled;
    }

    void setInvertPreview(bool on);
    bool isInvertPreview() const
    {
        return m_invert_preview;
    }

    void applyInvertPreview();
    void updateWaterfall();
    void syncStackIndex();
    void updateShortcutTooltips(const QString &invert_hint,
                                const QString &waterfall_hint);

    QPixmap renderPixmap(qreal dpr) const;
    bool prepareSvgExport(QSize &size_out,
                          QColor &background_out,
                          std::function<void(QPainter &)> &draw_out) const;

    Q_SIGNAL void sigToast(const QString &msg);
    Q_SIGNAL void sigSizeClicked(int pt);
    Q_SIGNAL void sigZoomRequested(int steps);
    Q_SIGNAL void sigLetterSpacingChanged(qreal s);
    Q_SIGNAL void sigInteractionClearFocus();

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void onPresetChanged(int index);
    void ensureRecentItem();
    void onUserEdited();
    void syncWaterfall();
    void ensureWaterfallRowVisible(int pt);
    void ensureWaterfallRowVisibleLater();
    void repositionAaPanel();

    QVector<int> waterfallSizes() const;
    QString waterfallText() const;
    QSizeF prepareEditDocumentSize() const;
    void drawEditDocument(QPainter &painter, const QSizeF &doc_size) const;
    void updateLayoutMetrics();
    bool hasCustomState() const;
    void updateResetButtonState();
    void resetPreviewSettings();

    FontProvider m_font_provider;
    int m_current_pt = 16;
    qreal m_dpr      = 1.0;

    FontPreviewEdit *m_edit         = nullptr;
    WaterfallWidget *m_waterfall    = nullptr;
    QScrollArea *m_scroll_waterfall = nullptr;
    QStackedWidget *m_stack         = nullptr;
    QWidget *m_surface_frame        = nullptr;
    QWidget *m_surface_host         = nullptr;
    QComboBox *m_combo_preset       = nullptr;
    QWidget *m_line_host            = nullptr;
    QSlider *m_slider_line          = nullptr;
    QSlider *m_slider_space         = nullptr;
    QLabel *m_label_line            = nullptr;
    QLabel *m_label_space           = nullptr;
    QPushButton *m_btn_invert       = nullptr;
    QPushButton *m_btn_aa           = nullptr;
    QPushButton *m_btn_reset        = nullptr;
    QFrame *m_aa_panel              = nullptr;
    QWidget *m_toolbar              = nullptr;
    QCheckBox *m_chk_waterfall      = nullptr;

    bool m_waterfall_enabled      = false;
    bool m_invert_preview         = false;
    bool m_recent_present         = false;
    bool m_ensure_pending         = false;
    bool m_waterfall_needs_scroll = false;
    qreal m_line_height           = 1.4;
    qreal m_letter_spacing        = 0.0;
    QString m_last_edited;
    QString m_invert_hint;
    QString m_waterfall_hint;
};
