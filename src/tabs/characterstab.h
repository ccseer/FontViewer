#pragma once

#include <QFont>
#include <QString>
#include <QVector>
#include <QWidget>
#include <QtGlobal>

#include <functional>

#include "tabs/charactergridmodel.h"

class CharacterWidget;
class QComboBox;
class QEvent;
class QHideEvent;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class QShortcut;
class QShowEvent;
class QSlider;
class QSpacerItem;
class QTimer;

class CharactersTab : public QWidget {
    Q_OBJECT

public:
    using GlyphNameProvider = std::function<QVector<QString>()>;

    explicit CharactersTab(QWidget *parent = nullptr);
    ~CharactersTab() override;

    void setDpr(qreal dpr);
    void setCodePoints(const QVector<quint32> &code_points,
                       const QVector<quint32> &glyph_ids   = {},
                       const QVector<QString> &glyph_names = {});
    void setGlyphNameProvider(GlyphNameProvider provider);
    void updateFont(const QFont &font);

    void setCellSizeDp(int cell_dp, bool from_user = false);
    int cellSizeDp() const;
    void stepCellSize(int delta_dp);

    CharacterWidget *characterWidget() const
    {
        return m_grid;
    }

signals:
    void selectionChangedCodePoint(quint32 cp);
    void characterDoubleClickedCodePoint(quint32 cp);
    void inspectRequested(quint32 cp, bool scroll_to_preview);
    void sigInsertTextRequested(const QString &text);
    void sigCopyToastRequested(const QString &msg);
    void sigCharCellSizeChanged(int cell_dp);
    void sigInteractionClearFocus();

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool ensureGlyphNamesLoaded();
    void applySearchFilterText();
    void initNavigationShortcuts();
    void updateNavShortcutState();
    bool isInputOrPopupFocused() const;
    bool handleNavigationKey(int key, Qt::KeyboardModifiers mods);
    void navigateByDelta(int delta, bool to_edge = false, int edge_index = 0);
    void selectGridIndex(int index);
    void ensureIndexVisible(int index);
    void rebuildBlockCombo();
    void reElideBlockComboItems();
    void updateBlockComboToolTip();
    void applyFilteredToGrid();
    void updateEmptyState();
    void updateSelectionStrip();
    void updateSelectionMetaElide();
    void setSelectionStripWidgetVisible(QWidget *w, bool visible);
    void resetEmptySelectionStrip(int bar_w, int content_w);
    void updateSelectedSelectionStrip(int bar_w, int content_w, int spacing);
    void updateLeftSelectionChips(int bar_w, int left_avail, int spacing,
                                  int sep_w, bool has_dec, bool has_gid,
                                  bool has_name, bool has_block);
    void updateSelectionBlockLabel(int bar_w, int avail_w, bool has_block,
                                   bool truncated_prefix);
    void copySelectedChar();
    void sendSelectedCharToText();
    void inspectSelectedChar();
    void openActionsMenu();
    void copyMetaValue(const QString &value);
    void updateLayoutMetrics();
    void refreshThemeVisuals();
    void clearFilterFocus();

    CharacterGridModel m_model;

    struct {
        QWidget *toolbar       = nullptr;
        QComboBox *combo_block = nullptr;
        QLineEdit *filter_edit = nullptr;
        QSlider *cell_slider   = nullptr;
        QLabel *cell_badge     = nullptr;
        QTimer *filter_timer   = nullptr;
    } m_top;

    QWidget *m_surface_host      = nullptr;
    QWidget *m_surface_frame     = nullptr;
    QScrollArea *m_scroll_area   = nullptr;
    CharacterWidget *m_grid      = nullptr;
    QWidget *m_empty_overlay     = nullptr;
    QLabel *m_empty_title        = nullptr;
    QLabel *m_empty_sub          = nullptr;
    QPushButton *m_clear_filter  = nullptr;

    struct {
        QWidget *bar             = nullptr;
        QLabel *glyph            = nullptr;
        QPushButton *chip_code   = nullptr;
        QLabel *sep_dec          = nullptr;
        QPushButton *chip_dec    = nullptr;
        QLabel *sep_gid          = nullptr;
        QPushButton *chip_gid    = nullptr;
        QLabel *sep_name         = nullptr;
        QPushButton *chip_name   = nullptr;
        QLabel *sep_block        = nullptr;
        QLabel *meta             = nullptr;
        QSpacerItem *spacer      = nullptr;
        QPushButton *btn_copy    = nullptr;
        QPushButton *btn_send    = nullptr;
        QPushButton *btn_inspect = nullptr;
        QPushButton *btn_actions = nullptr;
        QMenu *actions_menu      = nullptr;
        QString block_full;
        QString meta_full;
    } m_bottom;

    QVector<QShortcut *> m_nav_shortcuts;
    GlyphNameProvider m_glyph_name_provider;
    QFont m_glyph_font;
    quint32 m_selected_cp        = 0;
    qreal m_dpr                  = 1.0;
    bool m_filter_ime_composing  = false;
};
