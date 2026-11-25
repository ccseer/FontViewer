#pragma once

#include <QByteArray>
#include <QFont>
#include <QHash>
#include <QPixmap>
#include <QVector>
#include <QWidget>

#include "fontinspectorengine.h"

class CharacterWidget;
class FontPreviewEdit;
class GlyphInspectorWidget;
class WaterfallWidget;
class QCheckBox;
class QComboBox;
class QFrame;
class QGridLayout;
class QLabel;
class QPushButton;
class QScrollArea;
class QSlider;
class QStackedWidget;
class QTimer;
class QVBoxLayout;

namespace Ui {
class FontWidget;
}

class FontWidget : public QWidget {
    Q_OBJECT
public:
    enum class PreviewFontRole {
        Text,
        Glyph,
    };

    FontWidget(QWidget *parent = nullptr);
    ~FontWidget() override;

    bool init(const QString &p);
    void updateDPR(qreal r);
    void updateTheme(int theme);
    void copy();
    void saveAsSvg();
    bool saveAsSvgTo(const QString &path, QString *error_out = nullptr);
    void clearPreviewUndoHistory();

    void setCollapsed(bool c);
    bool isCollapsed() const
    {
        return m_is_collapsed;
    }

    void setCurrentText(const QString &t);
    QString getCurrentText() const;
    QString getLastEditedText() const
    {
        return m_last_edited;
    }
    void setCurrentFontSize(int s);
    int getCurrentFontSize() const;

    void setLineHeight(qreal h);
    qreal getLineHeight() const
    {
        return m_line_height;
    }
    void setLetterSpacing(qreal s);
    qreal getLetterSpacing() const
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
    void setShowOutlinePoints(bool on);
    bool isShowOutlinePoints() const;
    void setFeatureEnabled(const QByteArray &tag, bool enabled);
    bool isFeatureEnabled(const QByteArray &tag) const;
    QVector<QPushButton *> featurePillButtons() const;

    QVector<FontNameRecord> readNameRecords();
    int currentFaceIndex() const
    {
        return m_face_index;
    }
    QHash<QByteArray, float> variableAxisValues() const
    {
        return m_axis_values;
    }
    void setVariableAxisValue(const QByteArray &tag, float value);

    Q_SIGNAL void sigToast(const QString &msg);
    Q_SIGNAL void sigCollapsedChanged(bool c);
    Q_SIGNAL void sigFontSizeChanged(int pt);

protected:
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void initUI(const QStringList &names);
    void initAxesBar();
    void initFeaturesBar();
    void initTextTab();
    void initSampleTab();
    void initCharTab();
    void initGlyphTab();
    void rebuildAxesBar(bool reset_to_defaults);
    void rebuildFeaturesBar();
    void updateFeaturePillsState();
    void applyFeaturesNotePalette();
    void syncAxesForStyle(const QString &style);
    void probeVariableAxisBackend();
    void refreshActiveTabFromAxes();
    void refreshActiveTabFromFeatures();
    void refreshTextWidgetsFromFont();
    void refreshGlyphInspectorFromFont();
    void updateGlyphInspectorFont(const QFont &glyph_font);
    void updateTabTextPreview();
    void updateSamplePreview();
    void updateInfo();
    void onStyleChanged();
    void onNameChanged();
    void onTabChanged();
    void onPresetChanged(int index);
    void onUserEdited();
    void zoomSize(int steps);
    void applyInvertPreview();
    void ensureRecentItem();
    void syncWaterfall();
    void ensureWaterfallRowVisible(int pt);
    void ensureWaterfallRowVisibleLater();
    void repositionAaPanel();
    void clearViewerFocus();
    void releaseApplicationFont();

    QFont buildPreviewFont(int pt_size, PreviewFontRole role) const;
    QVector<int> waterfallSizes() const;
    QString waterfallText() const;
    QSizeF prepareEditDocumentSize() const;
    void drawEditDocument(QPainter &painter, const QSizeF &doc_size) const;
    QPixmap renderEditPixmap();
    QPixmap renderSamplePixmap();

    struct AxisControl {
        QByteArray tag;
        float min_val     = 0.0f;
        float default_val = 0.0f;
        float max_val     = 0.0f;
        int steps         = 1;
        QSlider *slider   = nullptr;
        QLabel *value     = nullptr;
    };

    FontInspectorEngine m_inspector;
    int m_face_index = 0;
    QHash<QByteArray, float> m_axis_values;
    QVector<AxisControl> m_axis_controls;
    QWidget *m_axes_bar          = nullptr;
    QVBoxLayout *m_axes_root     = nullptr;
    QLabel *m_axes_warning       = nullptr;
    QWidget *m_axes_rows_host    = nullptr;
    QGridLayout *m_axes_grid     = nullptr;
    QTimer *m_axis_timer         = nullptr;

    struct FeaturePillControl {
        QByteArray tag;
        bool default_on    = false;
        bool is_ligature   = false;
        QString tooltip;
        QPushButton *button = nullptr;
    };
    QHash<QByteArray, bool> m_feature_overrides;
    QVector<FeaturePillControl> m_feature_controls;
    QWidget *m_features_bar        = nullptr;
    QVBoxLayout *m_features_root   = nullptr;
    QLabel *m_features_note        = nullptr;
    QPushButton *m_features_reset  = nullptr;
    QScrollArea *m_features_scroll = nullptr;
    QWidget *m_features_pills_host = nullptr;

    bool m_gdi_probed            = false;
    bool m_gdi_inactive          = false;
    bool m_text_dirty            = false;
    bool m_sample_dirty          = false;
    bool m_char_dirty            = false;
    bool m_gi_dirty              = false;

    CharacterWidget *m_wnd_char    = nullptr;
    QWidget *m_tab_char            = nullptr;
    GlyphInspectorWidget *m_wnd_gi = nullptr;
    QWidget *m_tab_gi              = nullptr;

    FontPreviewEdit *m_edit         = nullptr;
    WaterfallWidget *m_waterfall    = nullptr;
    QScrollArea *m_scroll_waterfall = nullptr;
    QStackedWidget *m_stack         = nullptr;
    QComboBox *m_combo_preset       = nullptr;
    QSlider *m_slider_line          = nullptr;
    QSlider *m_slider_space         = nullptr;
    QLabel *m_label_line            = nullptr;
    QLabel *m_label_space           = nullptr;
    QPushButton *m_btn_invert       = nullptr;
    QPushButton *m_btn_aa           = nullptr;
    QFrame *m_aa_panel              = nullptr;
    QWidget *m_toolbar              = nullptr;
    QCheckBox *m_chk_waterfall      = nullptr;

    QPushButton *m_btn_copy        = nullptr;
    QPushButton *m_btn_svg         = nullptr;
    QPushButton *m_btn_toggle      = nullptr;
    QLabel *m_label_name_collapsed = nullptr;

    int m_font_id            = -1;
    int m_theme              = 0;
    bool m_is_collapsed      = false;
    bool m_waterfall_enabled = false;
    bool m_invert_preview    = false;
    bool m_recent_present         = false;
    bool m_ensure_pending         = false;
    bool m_waterfall_needs_scroll = false;
    qreal m_line_height      = 1.4;
    qreal m_letter_spacing   = 0.0;
    QString m_last_edited;
    QString m_font_path;

    Ui::FontWidget *ui;
};
