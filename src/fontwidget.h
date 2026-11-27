#pragma once

#include <QByteArray>
#include <QFont>
#include <QHash>
#include <QKeySequence>
#include <QVector>
#include <QWidget>

#include "engine/fontinspectorengine.h"
#include "fontshortcuts.h"

class CharacterWidget;
class CharactersTab;
class GlyphInspectorWidget;
class SamplePreviewTab;
class TextPreviewTab;
class VariableAxesBar;
class QAction;
class QGroupBox;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QSettings;

namespace Ui {
class FontWidget;
}

class FontWidget : public QWidget {
    Q_OBJECT
public:
    enum class PreviewFontRole {
        Text,
        Sample,
        Glyph,
    };

    FontWidget(QWidget *parent = nullptr);
    ~FontWidget() override;

    bool init(const QString &p);
    void loadShortcuts(QSettings &ini);
    void loadPresets(QSettings &ini);
    QAction *shortcutAction(FontShortcutAction action) const;
    QKeySequence shortcutSequence(FontShortcutAction action) const;
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
    QString getLastEditedText() const;
    void setCurrentFontSize(int s);
    int getCurrentFontSize() const;

    void setLineHeight(qreal h);
    qreal getLineHeight() const;
    void setLetterSpacing(qreal s);
    qreal getLetterSpacing() const;
    void setWaterfallEnabled(bool on);
    bool isWaterfallEnabled() const;
    void setInvertPreview(bool on);
    bool isInvertPreview() const;
    void setShowOutlinePoints(bool on);
    bool isShowOutlinePoints() const;
    void setCharCellSizeDp(int cell_dp);
    int charCellSizeDp() const;
    QStringList hiddenGuideIds() const;
    void setHiddenGuideIds(const QStringList &ids);
    void setFeatureEnabled(const QByteArray &tag, bool enabled);
    bool isFeatureEnabled(const QByteArray &tag) const;
    QVector<QPushButton *> featurePillButtons() const;
    QHash<QByteArray, bool> featureOverrides() const;

    QVector<FontNameRecord> readNameRecords();
    int currentFaceIndex() const
    {
        return m_face_index;
    }
    QHash<QByteArray, float> variableAxisValues() const;
    void setVariableAxisValue(const QByteArray &tag, float value);

    Q_SIGNAL void sigToast(const QString &msg);
    Q_SIGNAL void sigCollapsedChanged(bool c);
    Q_SIGNAL void sigFontSizeChanged(int pt);
    Q_SIGNAL void sigCharCellSizeChanged(int cell_dp);

protected:
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void initUI(const QStringList &names);
    void initAxesBar();
    void syncOpenTypeFeatures();
    void syncTypographyGroupVisibility();
    void updateLayoutMetrics();
    void initTextTab();
    void initSampleTab();
    void initCharTab();
    void initGlyphTab();
    void initActions();
    void applyShortcuts();
    void syncCharacterCodePoints();
    void updateResetFeatureActionState();
    QString tooltipWithShortcut(const QString &base,
                                  FontShortcutAction action) const;
    void refreshActiveTabFromAxes();
    void refreshActiveTabFromFeatures();
    void refreshTextWidgetsFromFont();
    void refreshGlyphInspectorFromFont();
    void updateGlyphInspectorFont(const QFont &glyph_font);
    void updateTabTextPreview();
    void updateSamplePreview();
    void updateInfo();
    void updateCollapsedLabel();
    void onStyleChanged();
    void onNameChanged();
    void onTabChanged();
    void zoomSize(int steps);
    void clearViewerFocus();
    void releaseApplicationFont();

    QFont buildPreviewFont(int pt_size, PreviewFontRole role) const;

    struct GlyphInspectData {
        FontDecorationMetrics deco;
        GlyphOutlineData outline;
        QString glyph_name;
    };
    GlyphInspectData inspectGlyphData(quint32 cp);

    FontInspectorEngine m_inspector;
    int m_face_index = 0;

    QGroupBox *m_typo_group             = nullptr;
    VariableAxesBar *m_axes_bar         = nullptr;
    bool m_text_dirty                   = false;
    bool m_sample_dirty                 = false;
    bool m_char_dirty                   = false;
    bool m_gi_dirty                     = false;

    TextPreviewTab *m_tab_text     = nullptr;
    SamplePreviewTab *m_tab_sample = nullptr;
    CharactersTab *m_chars_tab     = nullptr;
    CharacterWidget *m_wnd_char    = nullptr;
    QWidget *m_tab_char            = nullptr;
    GlyphInspectorWidget *m_wnd_gi = nullptr;
    QWidget *m_tab_gi              = nullptr;

    QHBoxLayout *m_corner_layout   = nullptr;
    QPushButton *m_btn_copy        = nullptr;
    QPushButton *m_btn_svg         = nullptr;
    QPushButton *m_btn_toggle      = nullptr;
    QLabel *m_label_name_collapsed = nullptr;

    FontShortcutMap m_shortcuts;
    QVector<QAction *> m_actions;

    int m_font_id       = -1;
    int m_theme         = 0;
    qreal m_dpr         = 1.0;
    bool m_is_collapsed = false;
    QString m_font_path;

    Ui::FontWidget *ui;
};
