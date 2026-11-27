#include "characterstab.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpacerItem>
#include <QStackedLayout>
#include <QTabBar>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <functional>

#include "engine/unicodeblocks.h"
#include "fontui.h"
#include "tabs/characterstabwidgets.h"
#include "tabs/characterwidget.h"

CharactersTab::CharactersTab(QWidget *parent) : QWidget(parent)
{
    setFocusPolicy(Qt::NoFocus);

    auto *root = new QGridLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_top.combo_block = new QComboBox(this);
    m_top.combo_block->setObjectName(QStringLiteral("chars_block_combo"));
    m_top.combo_block->setSizeAdjustPolicy(
        QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_top.combo_block->setMinimumContentsLength(8);
    fontui::configureComboBox(m_top.combo_block);
    m_top.combo_block->installEventFilter(this);
    if (QAbstractItemView *view = m_top.combo_block->view()) {
        view->installEventFilter(this);
    }

    m_top.filter_edit = new QLineEdit(this);
    m_top.filter_edit->setObjectName(QStringLiteral("chars_filter_edit"));
    // Explicit ClickFocus exception: allows space inside multi-word block
    // filters while Esc/Return/click/tab yields focus back to Seer.
    m_top.filter_edit->setFocusPolicy(Qt::ClickFocus);
    m_top.filter_edit->setClearButtonEnabled(true);
    m_top.filter_edit->setPlaceholderText(tr("Filter glyph, U+code, or name"));
    m_top.filter_edit->installEventFilter(this);

    m_top.filter_timer = new QTimer(this);
    m_top.filter_timer->setSingleShot(true);
    m_top.filter_timer->setInterval(80);

    auto *cell_host = new QWidget(this);
    cell_host->setFocusPolicy(Qt::NoFocus);
    auto *cell_lay = new QHBoxLayout(cell_host);
    cell_lay->setContentsMargins(0, 0, 0, 0);
    cell_lay->setSpacing(fontui::dp(4, m_dpr));
    auto *cell_lbl = new QLabel(tr("Cell"), cell_host);
    cell_lbl->setToolTip(tr("Cell size (dp) — Scroll with Ctrl+Wheel on the grid"));
    m_top.cell_slider = new QSlider(Qt::Horizontal, cell_host);
    m_top.cell_slider->setObjectName(QStringLiteral("chars_cell_slider"));
    m_top.cell_slider->setRange(CharacterWidget::kMinCellSizeDp,
                                CharacterWidget::kMaxCellSizeDp);
    m_top.cell_slider->setSingleStep(4);
    m_top.cell_slider->setPageStep(8);
    m_top.cell_slider->setValue(CharacterWidget::kDefCellSizeDp);
    m_top.cell_slider->setFocusPolicy(Qt::NoFocus);
    m_top.cell_slider->setToolTip(
        tr("Cell size (dp) — Scroll with Ctrl+Wheel on the grid, double-click to reset"));
    m_top.cell_slider->installEventFilter(this);
    m_top.cell_badge = new fontui::ValueBadge(m_dpr, QStringLiteral("80"), cell_host);
    m_top.cell_badge->setObjectName(QStringLiteral("chars_cell_badge"));
    m_top.cell_badge->setText(QString::number(CharacterWidget::kDefCellSizeDp));
    m_top.cell_badge->setToolTip(
        tr("Double-click to reset cell size to %1 dp")
            .arg(CharacterWidget::kDefCellSizeDp));
    m_top.cell_badge->installEventFilter(this);
    cell_lay->addWidget(cell_lbl);
    cell_lay->addWidget(m_top.cell_slider);
    cell_lay->addWidget(m_top.cell_badge);

    m_top.toolbar = new CharactersToolbar(m_top.combo_block, m_top.filter_edit,
                                          cell_host, this);

    m_surface_host = new QWidget(this);
    m_surface_host->setFocusPolicy(Qt::NoFocus);
    auto *surface_lay = new QVBoxLayout(m_surface_host);
    surface_lay->setSpacing(0);

    auto *frame     = new fontui::SurfaceFrame(m_surface_host);
    m_surface_frame = frame;

    auto *stack_host = new QWidget(frame);
    stack_host->setFocusPolicy(Qt::NoFocus);
    auto *stack_lay = new QStackedLayout(stack_host);
    stack_lay->setContentsMargins(0, 0, 0, 0);
    stack_lay->setSpacing(0);
    stack_lay->setStackingMode(QStackedLayout::StackAll);

    m_scroll_area = new QScrollArea(stack_host);
    m_scroll_area->setObjectName(QStringLiteral("chars_scroll_area"));
    m_scroll_area->setFrameShape(QFrame::NoFrame);
    m_scroll_area->setWidgetResizable(true);
    m_scroll_area->setFocusPolicy(Qt::NoFocus);
    m_scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll_area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll_area->viewport()->setAutoFillBackground(false);
    m_scroll_area->verticalScrollBar()->setProperty("is_readonly", true);
    m_scroll_area->verticalScrollBar()->setFocusPolicy(Qt::NoFocus);
    m_scroll_area->horizontalScrollBar()->setProperty("is_readonly", true);
    m_scroll_area->horizontalScrollBar()->setFocusPolicy(Qt::NoFocus);

    m_grid = new CharacterWidget(m_scroll_area);
    m_scroll_area->setWidget(m_grid);
    stack_lay->addWidget(m_scroll_area);

    m_empty_overlay = new QWidget(stack_host);
    m_empty_overlay->setObjectName(QStringLiteral("chars_empty_overlay"));
    m_empty_overlay->setFocusPolicy(Qt::NoFocus);
    m_empty_overlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    auto *empty_lay = new QVBoxLayout(m_empty_overlay);
    empty_lay->addStretch(1);
    m_empty_title = new QLabel(tr("No matching characters"), m_empty_overlay);
    m_empty_title->setAlignment(Qt::AlignCenter);
    m_empty_sub = new QLabel(
        tr("Clear the filter or switch Unicode block"), m_empty_overlay);
    m_empty_sub->setAlignment(Qt::AlignCenter);
    m_clear_filter
        = new fontui::CompactActionButton(tr("Clear Filter"), m_dpr,
                                          m_empty_overlay, false);
    m_clear_filter->setObjectName(QStringLiteral("chars_clear_filter_btn"));
    empty_lay->addWidget(m_empty_title);
    empty_lay->addWidget(m_empty_sub);
    empty_lay->addWidget(m_clear_filter, 0, Qt::AlignCenter);
    empty_lay->addStretch(1);
    m_empty_overlay->hide();
    stack_lay->addWidget(m_empty_overlay);

    frame->addContent(stack_host);
    surface_lay->addWidget(frame);

    // Selection Strip (fixed single-line height prevents grid jump)
    m_bottom.bar = new QWidget(this);
    m_bottom.bar->setObjectName(QStringLiteral("chars_selection_bar"));
    m_bottom.bar->setFocusPolicy(Qt::NoFocus);
    m_bottom.bar->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_bottom.bar->installEventFilter(this);
    auto *sel_lay = new QHBoxLayout(m_bottom.bar);
    sel_lay->setSizeConstraint(QLayout::SetNoConstraint);

    m_bottom.glyph = new QLabel(m_bottom.bar);
    m_bottom.glyph->setTextFormat(Qt::PlainText);
    m_bottom.glyph->setAlignment(Qt::AlignCenter);
    m_bottom.glyph->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto make_chip = [this](const char *obj_name) {
        auto *chip = new MetaCopyChip(m_dpr, m_bottom.bar);
        chip->setObjectName(QLatin1String(obj_name));
        connect(chip, &QPushButton::clicked, this, [this, chip]() {
            copyMetaValue(chip->fullText());
        });
        return chip;
    };
    auto make_sep = [this]() {
        auto *sep = new QLabel(QStringLiteral("·"), m_bottom.bar);
        sep->setTextFormat(Qt::PlainText);
        sep->setFocusPolicy(Qt::NoFocus);
        sep->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        return sep;
    };

    m_bottom.chip_code = make_chip("chars_meta_code_btn");
    m_bottom.sep_dec   = make_sep();
    m_bottom.chip_dec  = make_chip("chars_meta_dec_btn");
    m_bottom.sep_gid   = make_sep();
    m_bottom.chip_gid  = make_chip("chars_meta_gid_btn");
    m_bottom.sep_name  = make_sep();
    m_bottom.chip_name = make_chip("chars_meta_name_btn");
    m_bottom.sep_block = make_sep();

    m_bottom.meta = new QLabel(m_bottom.bar);
    m_bottom.meta->setObjectName(QStringLiteral("chars_sel_meta"));
    m_bottom.meta->setTextFormat(Qt::PlainText);
    m_bottom.meta->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QFont meta_f = m_bottom.meta->font();
    if (const auto tnum = QFont::Tag::fromString("tnum"); tnum.has_value()) {
        meta_f.setFeature(*tnum, 1);
    }
    m_bottom.meta->setFont(meta_f);

    m_bottom.btn_copy = new fontui::CompactActionButton(
        tr("Copy Char"), m_dpr, m_bottom.bar, false);
    m_bottom.btn_copy->setObjectName(QStringLiteral("chars_copy_text_btn"));
    m_bottom.btn_send = new fontui::CompactActionButton(
        tr("Send to Text →"), m_dpr, m_bottom.bar, false);
    m_bottom.btn_send->setObjectName(QStringLiteral("chars_insert_text_btn"));
    m_bottom.btn_inspect = new fontui::CompactActionButton(
        tr("Inspect →"), m_dpr, m_bottom.bar, false);
    m_bottom.btn_inspect->setObjectName(QStringLiteral("chars_inspect_btn"));

    m_bottom.btn_actions = new fontui::CompactActionButton(
        tr("Actions ▾"), m_dpr, m_bottom.bar, false);
    m_bottom.btn_actions->setObjectName(QStringLiteral("chars_actions_menu_btn"));

    m_bottom.actions_menu = new QMenu(this);
    m_bottom.actions_menu->setObjectName(QStringLiteral("chars_actions_menu"));
    m_bottom.actions_menu->setToolTipsVisible(true);
    m_bottom.actions_menu->installEventFilter(this);
    connect(m_bottom.actions_menu->addAction(tr("Copy Char")),
            &QAction::triggered, this, &CharactersTab::copySelectedChar);
    connect(m_bottom.actions_menu->addAction(tr("Send to Text →")),
            &QAction::triggered, this, &CharactersTab::sendSelectedCharToText);
    connect(m_bottom.actions_menu->addAction(tr("Inspect →")),
            &QAction::triggered, this, &CharactersTab::inspectSelectedChar);

    sel_lay->addWidget(m_bottom.glyph);
    sel_lay->addWidget(m_bottom.chip_code);
    sel_lay->addWidget(m_bottom.sep_dec);
    sel_lay->addWidget(m_bottom.chip_dec);
    sel_lay->addWidget(m_bottom.sep_gid);
    sel_lay->addWidget(m_bottom.chip_gid);
    sel_lay->addWidget(m_bottom.sep_name);
    sel_lay->addWidget(m_bottom.chip_name);
    sel_lay->addWidget(m_bottom.sep_block);
    sel_lay->addWidget(m_bottom.meta, 1);
    m_bottom.spacer = new QSpacerItem(fontui::dp(4, m_dpr), 0,
                                      QSizePolicy::MinimumExpanding,
                                      QSizePolicy::Minimum);
    sel_lay->addSpacerItem(m_bottom.spacer);
    sel_lay->addWidget(m_bottom.btn_copy);
    sel_lay->addWidget(m_bottom.btn_send);
    sel_lay->addWidget(m_bottom.btn_inspect);
    sel_lay->addWidget(m_bottom.btn_actions);

    root->addWidget(m_top.toolbar, 0, 0);
    root->addWidget(m_surface_host, 1, 0);
    root->addWidget(m_bottom.bar, 2, 0);
    root->setRowStretch(1, 1);

    // Signals
    connect(m_top.combo_block, &QComboBox::currentIndexChanged, this,
            [this](int idx) {
                updateBlockComboToolTip();
                const int blk
                    = idx >= 0 ? m_top.combo_block->itemData(idx).toInt()
                               : CharacterGridModel::kAllBlocks;
                if (m_model.setBlockFilter(blk)) {
                    applyFilteredToGrid();
                }
            });
    connect(m_top.combo_block, &QComboBox::activated, this,
            [this](int) { emit sigInteractionClearFocus(); });

    connect(m_top.filter_edit, &QLineEdit::textChanged, this, [this]() {
        m_top.filter_timer->start();
    });
    connect(m_top.filter_timer, &QTimer::timeout, this, [this]() {
        applySearchFilterText();
    });
    connect(m_top.filter_edit, &QLineEdit::returnPressed, this, [this]() {
        m_top.filter_timer->stop();
        applySearchFilterText();
        clearFilterFocus();
        if (m_model.filteredCount() > 0
            && (m_selected_cp == 0
                || !m_model.containsCodePoint(m_selected_cp))) {
            selectGridIndex(0);
        }
    });
    if (qApp) {
        connect(qApp, &QApplication::focusChanged, this,
                [this](QWidget *, QWidget *) { updateNavShortcutState(); });
    }

    connect(m_clear_filter, &QPushButton::clicked, this, [this]() {
        m_top.filter_timer->stop();
        {
            QSignalBlocker b1(m_top.filter_edit);
            m_top.filter_edit->clear();
        }
        {
            QSignalBlocker b2(m_top.combo_block);
            m_top.combo_block->setCurrentIndex(0);
        }
        updateBlockComboToolTip();
        const bool ch1 = m_model.setBlockFilter(CharacterGridModel::kAllBlocks);
        const bool ch2 = m_model.setSearchQuery(QString());
        if (ch1 || ch2) {
            applyFilteredToGrid();
        }
        clearFilterFocus();
    });

    connect(m_top.cell_slider, &QSlider::valueChanged, this, [this](int val) {
        setCellSizeDp(val, true);
        emit sigInteractionClearFocus();
    });
    connect(m_top.cell_slider, &QSlider::sliderPressed, this,
            &CharactersTab::sigInteractionClearFocus);

    connect(m_grid, &CharacterWidget::selectionChangedCodePoint, this,
            [this](quint32 cp) {
                m_selected_cp = cp;
                updateSelectionStrip();
                emit selectionChangedCodePoint(cp);
            });
    connect(m_grid, &CharacterWidget::characterDoubleClickedCodePoint, this,
            [this](quint32 cp) {
                emit characterDoubleClickedCodePoint(cp);
                emit inspectRequested(cp, true);
            });
    connect(m_grid, &CharacterWidget::interactionClearFocus, this,
            &CharactersTab::sigInteractionClearFocus);

    connect(m_bottom.btn_copy, &QPushButton::clicked, this,
            &CharactersTab::copySelectedChar);
    connect(m_bottom.btn_send, &QPushButton::clicked, this,
            &CharactersTab::sendSelectedCharToText);
    connect(m_bottom.btn_inspect, &QPushButton::clicked, this,
            &CharactersTab::inspectSelectedChar);
    connect(m_bottom.btn_actions, &QPushButton::clicked, this,
            &CharactersTab::openActionsMenu);

    initNavigationShortcuts();

    updateLayoutMetrics();
    updateSelectionStrip();
    refreshThemeVisuals();
    updateNavShortcutState();
}

CharactersTab::~CharactersTab()
{
    // Detach and close an open modal QMenu before QObject child destruction
    // unwinds its nested QMenu::exec() frame.
    if (m_bottom.actions_menu && m_bottom.actions_menu->isVisible()) {
        m_bottom.actions_menu->removeEventFilter(this);
        m_bottom.actions_menu->setParent(nullptr);
        m_bottom.actions_menu->close();
        m_bottom.actions_menu->deleteLater();
    }
}

void CharactersTab::setDpr(qreal dpr)
{
    const qreal clamped = fontui::clampDpr(dpr);
    if (qFuzzyCompare(m_dpr, clamped)) {
        return;
    }
    m_dpr = clamped;
    updateLayoutMetrics();
}

void CharactersTab::updateLayoutMetrics()
{
    if (m_top.cell_slider && m_top.cell_slider->parentWidget()) {
        if (auto *lay = m_top.cell_slider->parentWidget()->layout()) {
            lay->setSpacing(fontui::dp(4, m_dpr));
        }
        const int track_w
            = qMax(fontui::dp(58, m_dpr),
                   QFontMetrics(m_top.cell_slider->parentWidget()->font())
                       .horizontalAdvance(QStringLiteral("MMMMMM")));
        m_top.cell_slider->setMinimumWidth(track_w);
        m_top.cell_slider->setMaximumWidth(track_w + fontui::dp(24, m_dpr));
    }
    if (m_top.cell_badge) {
        auto *badge = static_cast<fontui::ValueBadge *>(m_top.cell_badge);
        badge->setDpr(m_dpr);
        badge->refreshFont();
    }
    if (m_bottom.actions_menu && m_bottom.actions_menu->font() != font()) {
        m_bottom.actions_menu->setFont(font());
    }
    if (m_top.combo_block && m_top.combo_block->view()) {
        m_top.combo_block->view()->setMinimumWidth(fontui::dp(240, m_dpr));
    }
    if (m_surface_host && m_surface_host->layout()) {
        m_surface_host->layout()->setContentsMargins(
            fontui::dp(8, m_dpr), fontui::dp(2, m_dpr), fontui::dp(8, m_dpr),
            fontui::dp(4, m_dpr));
    }
    if (m_surface_frame) {
        static_cast<fontui::SurfaceFrame *>(m_surface_frame)->setDpr(m_dpr);
    }
    if (m_clear_filter) {
        static_cast<fontui::CompactActionButton *>(m_clear_filter)
            ->setDpr(m_dpr);
    }
    if (m_bottom.bar && m_bottom.bar->layout()) {
        m_bottom.bar->layout()->setContentsMargins(
            fontui::dp(10, m_dpr), fontui::dp(4, m_dpr), fontui::dp(10, m_dpr),
            fontui::dp(8, m_dpr));
        m_bottom.bar->layout()->setSpacing(fontui::dp(2, m_dpr));
    }
    if (m_bottom.spacer) {
        m_bottom.spacer->changeSize(fontui::dp(4, m_dpr), 0,
                                    QSizePolicy::MinimumExpanding,
                                    QSizePolicy::Minimum);
    }
    if (m_bottom.meta) {
        QFont meta_f = font();
        if (const auto tnum = QFont::Tag::fromString("tnum"); tnum.has_value()) {
            meta_f.setFeature(*tnum, 1);
        }
        if (m_bottom.meta->font() != meta_f) {
            m_bottom.meta->setFont(meta_f);
        }
    }
    if (m_bottom.glyph) {
        const int glyph_box = fontui::dp(24, m_dpr);
        m_bottom.glyph->setFixedSize(glyph_box, glyph_box);
        if (m_selected_cp != 0) {
            QFont gf = m_glyph_font;
            gf.setPixelSize(qMax(12, fontui::dp(16, m_dpr)));
            m_bottom.glyph->setFont(gf);
        }
    }
    for (QPushButton *c : {m_bottom.chip_code, m_bottom.chip_dec,
                           m_bottom.chip_gid, m_bottom.chip_name}) {
        if (c) {
            auto *chip = static_cast<MetaCopyChip *>(c);
            chip->setDpr(m_dpr);
            chip->syncFontFromParent();
        }
    }
    for (QPushButton *b : {m_bottom.btn_copy, m_bottom.btn_send,
                           m_bottom.btn_inspect, m_bottom.btn_actions}) {
        if (b) {
            static_cast<fontui::CompactActionButton *>(b)->setDpr(m_dpr);
        }
    }
    if (m_bottom.bar) {
        const int btn_h
            = m_bottom.btn_copy ? m_bottom.btn_copy->sizeHint().height() : 0;
        const int chip_h
            = m_bottom.chip_code ? m_bottom.chip_code->sizeHint().height() : 0;
        const int glyph_h = m_bottom.glyph ? m_bottom.glyph->minimumHeight() : 0;
        const int bar_h
            = qMax(btn_h, qMax(chip_h, glyph_h)) + fontui::dp(12, m_dpr);
        m_bottom.bar->setFixedHeight(bar_h);
    }
    if (m_grid) {
        m_grid->setDpr(m_dpr);
    }
    reElideBlockComboItems();
    if (m_top.toolbar) {
        static_cast<CharactersToolbar *>(m_top.toolbar)->setDpr(m_dpr);
    }
    updateSelectionMetaElide();
}

void CharactersTab::reElideBlockComboItems()
{
    if (!m_top.combo_block || m_top.combo_block->count() == 0) {
        return;
    }
    QSignalBlocker blocker(m_top.combo_block);
    const QFontMetrics fm = m_top.combo_block->fontMetrics();
    const int max_label_w = fontui::dp(190, m_dpr);
    for (int i = 0; i < m_top.combo_block->count(); ++i) {
        const QString full_label
            = m_top.combo_block->itemData(i, Qt::ToolTipRole).toString();
        if (!full_label.isEmpty()) {
            m_top.combo_block->setItemText(
                i, fm.elidedText(full_label, Qt::ElideRight, max_label_w));
        }
    }
    updateBlockComboToolTip();
}

void CharactersTab::updateBlockComboToolTip()
{
    if (!m_top.combo_block) {
        return;
    }
    const int idx = m_top.combo_block->currentIndex();
    QString full  = idx >= 0
        ? m_top.combo_block->itemData(idx, Qt::ToolTipRole).toString()
        : QString();
    if (full.isEmpty()) {
        full = m_top.combo_block->currentText();
    }
    m_top.combo_block->setToolTip(full);
}

void CharactersTab::setCodePoints(const QVector<quint32> &code_points,
                                  const QVector<quint32> &glyph_ids,
                                  const QVector<QString> &glyph_names)
{
    m_glyph_name_provider = nullptr;
    m_top.filter_timer->stop();
    if (m_bottom.actions_menu && m_bottom.actions_menu->isVisible()) {
        m_bottom.actions_menu->close();
    }
    {
        QSignalBlocker b(m_top.filter_edit);
        m_top.filter_edit->clear();
    }
    m_model.setSearchQuery(QString());
    m_model.setAllCodePoints(code_points, glyph_ids, glyph_names);
    rebuildBlockCombo();
    m_selected_cp = 0;
    if (m_grid) {
        m_grid->setSelectedCodePoint(0);
    }
    applyFilteredToGrid();
}

void CharactersTab::setGlyphNameProvider(GlyphNameProvider provider)
{
    m_glyph_name_provider = std::move(provider);
    if (m_glyph_name_provider && isVisible() && ensureGlyphNamesLoaded()) {
        if (!m_model.searchQuery().isEmpty()) {
            applyFilteredToGrid();
        }
        else {
            updateSelectionStrip();
        }
    }
}

bool CharactersTab::ensureGlyphNamesLoaded()
{
    if (!m_glyph_name_provider) {
        return false;
    }
    GlyphNameProvider provider = std::move(m_glyph_name_provider);
    m_glyph_name_provider      = nullptr;
    const QVector<QString> names = provider();
    if (!names.isEmpty()) {
        m_model.setGlyphNames(names);
        return true;
    }
    return false;
}

void CharactersTab::applySearchFilterText()
{
    const QString text
        = m_top.filter_edit ? m_top.filter_edit->text() : QString();
    if (!text.trimmed().isEmpty()) {
        ensureGlyphNamesLoaded();
    }
    if (m_model.setSearchQuery(text)) {
        applyFilteredToGrid();
    }
}

void CharactersTab::rebuildBlockCombo()
{
    if (!m_top.combo_block) {
        return;
    }
    {
        QSignalBlocker blocker(m_top.combo_block);
        m_top.combo_block->clear();
        const QFontMetrics fm = m_top.combo_block->fontMetrics();
        const int max_label_w = fontui::dp(190, m_dpr);
        for (const auto &bucket : m_model.blockBuckets()) {
            const QString full_label
                = QStringLiteral("%1 (%2)").arg(bucket.name).arg(bucket.count);
            const QString shown = fm.elidedText(full_label, Qt::ElideRight,
                                                max_label_w);
            const int idx = m_top.combo_block->count();
            m_top.combo_block->addItem(shown, bucket.block_index);
            m_top.combo_block->setItemData(idx, full_label, Qt::ToolTipRole);
        }
        m_top.combo_block->setCurrentIndex(0);
    }
    updateBlockComboToolTip();
    if (m_top.combo_block->view()) {
        m_top.combo_block->view()->setMinimumWidth(fontui::dp(240, m_dpr));
    }
    if (m_top.toolbar) {
        static_cast<CharactersToolbar *>(m_top.toolbar)->refreshLayout();
    }
}

void CharactersTab::applyFilteredToGrid()
{
    if (!m_grid) {
        return;
    }
    const QVector<quint32> &filtered = m_model.filteredCodePoints();
    m_grid->setCodePoints(filtered);
    if (m_selected_cp != 0 && !m_model.containsCodePoint(m_selected_cp)) {
        m_selected_cp = 0;
        if (m_bottom.actions_menu && m_bottom.actions_menu->isVisible()) {
            m_bottom.actions_menu->close();
        }
        m_grid->setSelectedCodePoint(0);
        emit selectionChangedCodePoint(0);
    }
    updateSelectionStrip();
    updateEmptyState();
    if (m_scroll_area && m_scroll_area->verticalScrollBar()) {
        m_scroll_area->verticalScrollBar()->setValue(0);
    }
}

void CharactersTab::updateEmptyState()
{
    if (!m_empty_overlay) {
        return;
    }
    const bool has_total = m_model.totalCount() > 0;
    const bool has_rows  = m_model.filteredCount() > 0;
    m_empty_overlay->setVisible(!has_rows);
    if (!has_rows) {
        if (m_empty_title) {
            m_empty_title->setText(has_total ? tr("No matching characters")
                                             : tr("No characters in font"));
        }
        if (m_empty_sub) {
            m_empty_sub->setText(
                has_total ? tr("Clear the filter or switch Unicode block")
                          : QString());
            m_empty_sub->setVisible(has_total);
        }
        if (m_clear_filter) {
            m_clear_filter->setVisible(has_total);
        }
    }
}

void CharactersTab::updateFont(const QFont &font)
{
    m_glyph_font = font;
    if (m_grid) {
        m_grid->updateFont(font);
    }
    updateSelectionStrip();
}

void CharactersTab::setCellSizeDp(int cell_dp, bool from_user)
{
    const int clamped = qBound(CharacterWidget::kMinCellSizeDp, cell_dp,
                               CharacterWidget::kMaxCellSizeDp);
    const bool changed = (cellSizeDp() != clamped);
    if (m_grid) {
        m_grid->setCellSizeDp(clamped);
    }
    if (m_top.cell_slider && m_top.cell_slider->value() != clamped) {
        QSignalBlocker b(m_top.cell_slider);
        m_top.cell_slider->setValue(clamped);
    }
    if (m_top.cell_badge) {
        m_top.cell_badge->setText(QString::number(clamped));
        static_cast<fontui::ValueBadge *>(m_top.cell_badge)
            ->setModified(clamped != CharacterWidget::kDefCellSizeDp);
    }
    if (changed && from_user) {
        emit sigCharCellSizeChanged(clamped);
    }
}

int CharactersTab::cellSizeDp() const
{
    return m_grid ? m_grid->cellSizeDp() : CharacterWidget::kDefCellSizeDp;
}

void CharactersTab::stepCellSize(int delta_dp)
{
    if (delta_dp == 0) {
        return;
    }
    setCellSizeDp(cellSizeDp() + delta_dp, true);
}

void CharactersTab::updateSelectionStrip()
{
    const bool has_sel = (m_selected_cp != 0);
    if (m_bottom.glyph) {
        if (has_sel) {
            QFont gf = m_glyph_font;
            gf.setPixelSize(qMax(12, fontui::dp(16, m_dpr)));
            m_bottom.glyph->setFont(gf);
            m_bottom.glyph->setText(QString::fromUcs4(&m_selected_cp, 1));
            m_bottom.glyph->show();
        }
        else {
            m_bottom.glyph->clear();
            m_bottom.glyph->hide();
        }
    }
    if (has_sel) {
        ensureGlyphNamesLoaded();
        const QString code_str  = unicode_blocks::formatCodePoint(m_selected_cp);
        const QString dec_str   = QStringLiteral("Dec %1").arg(m_selected_cp);
        const quint32 gid       = m_model.glyphIdForCodePoint(m_selected_cp);
        const QString gid_str   = gid != 0 ? QStringLiteral("#%1").arg(gid) : QString();
        const QString name_str  = m_model.glyphNameForCodePoint(m_selected_cp);
        m_bottom.block_full     = unicode_blocks::blockName(m_selected_cp);

        if (m_bottom.chip_code) {
            static_cast<MetaCopyChip *>(m_bottom.chip_code)->setFullText(code_str);
        }
        if (m_bottom.chip_dec) {
            static_cast<MetaCopyChip *>(m_bottom.chip_dec)->setFullText(dec_str);
        }
        if (m_bottom.chip_gid) {
            static_cast<MetaCopyChip *>(m_bottom.chip_gid)->setFullText(gid_str);
        }
        if (m_bottom.chip_name) {
            static_cast<MetaCopyChip *>(m_bottom.chip_name)->setFullText(name_str);
        }

        QStringList parts;
        parts << code_str << dec_str;
        if (!gid_str.isEmpty()) {
            parts << gid_str;
        }
        if (!name_str.isEmpty()) {
            parts << name_str;
        }
        if (!m_bottom.block_full.isEmpty()) {
            parts << m_bottom.block_full;
        }
        m_bottom.meta_full = parts.join(QStringLiteral(" · "));
    }
    else {
        m_bottom.block_full.clear();
        for (QPushButton *c : {m_bottom.chip_code, m_bottom.chip_dec,
                               m_bottom.chip_gid, m_bottom.chip_name}) {
            if (c) {
                static_cast<MetaCopyChip *>(c)->setFullText(QString());
            }
        }
        m_bottom.meta_full = tr("Click or use arrow keys to select · Enter or "
                                "double-click to inspect");
    }
    updateSelectionMetaElide();
    refreshThemeVisuals();
}

void CharactersTab::copySelectedChar()
{
    if (m_selected_cp != 0) {
        const QString ch = QString::fromUcs4(&m_selected_cp, 1);
        QApplication::clipboard()->setText(ch);
        emit sigCopyToastRequested(
            tr("Copied '%1' (%2)")
                .arg(ch, unicode_blocks::formatCodePoint(m_selected_cp)));
    }
    emit sigInteractionClearFocus();
}

void CharactersTab::sendSelectedCharToText()
{
    if (m_selected_cp != 0) {
        emit sigInsertTextRequested(QString::fromUcs4(&m_selected_cp, 1));
    }
    emit sigInteractionClearFocus();
}

void CharactersTab::inspectSelectedChar()
{
    if (m_selected_cp != 0) {
        emit inspectRequested(m_selected_cp, false);
    }
    emit sigInteractionClearFocus();
}

void CharactersTab::openActionsMenu()
{
    if (!m_bottom.actions_menu || !m_bottom.btn_actions || m_selected_cp == 0) {
        return;
    }
    const QSize menu_sz = m_bottom.actions_menu->sizeHint();
    const QPoint btn_tl = m_bottom.btn_actions->mapToGlobal(QPoint(0, 0));
    const QPoint btn_br = m_bottom.btn_actions->mapToGlobal(
        QPoint(m_bottom.btn_actions->width(), m_bottom.btn_actions->height()));

    int x = btn_br.x() - menu_sz.width();
    int y = btn_tl.y() - menu_sz.height();
    const QScreen *screen = QGuiApplication::screenAt(btn_br);
    if (!screen) {
        screen = m_bottom.btn_actions->screen();
    }
    if (screen) {
        const QRect avail = screen->availableGeometry();
        if (y < avail.top()) {
            y = btn_br.y();
        }
        const int max_x
            = qMax(avail.left(), avail.right() - menu_sz.width() + 1);
        const int max_y
            = qMax(avail.top(), avail.bottom() - menu_sz.height() + 1);
        x = qBound(avail.left(), x, max_x);
        y = qBound(avail.top(), y, max_y);
    }
    QPointer<CharactersTab> self(this);
    m_bottom.actions_menu->exec(QPoint(x, y));
    if (!self) {
        return;
    }
    emit sigInteractionClearFocus();
}

void CharactersTab::copyMetaValue(const QString &value)
{
    if (!value.isEmpty()) {
        QApplication::clipboard()->setText(value);
        emit sigCopyToastRequested(tr("Copied '%1'").arg(value));
    }
    emit sigInteractionClearFocus();
}

void CharactersTab::setSelectionStripWidgetVisible(QWidget *w, bool visible)
{
    // Inspect explicit hidden state instead of isVisible() so off-tab
    // resets still hide stale controls while CharactersTab is hidden.
    if (w && w->isHidden() == visible) {
        w->setVisible(visible);
    }
}

void CharactersTab::resetEmptySelectionStrip(int bar_w, int content_w)
{
    setSelectionStripWidgetVisible(m_bottom.glyph, false);
    for (QWidget *w :
         {static_cast<QWidget *>(m_bottom.chip_code),
          static_cast<QWidget *>(m_bottom.sep_dec),
          static_cast<QWidget *>(m_bottom.chip_dec),
          static_cast<QWidget *>(m_bottom.sep_gid),
          static_cast<QWidget *>(m_bottom.chip_gid),
          static_cast<QWidget *>(m_bottom.sep_name),
          static_cast<QWidget *>(m_bottom.chip_name),
          static_cast<QWidget *>(m_bottom.sep_block),
          static_cast<QWidget *>(m_bottom.btn_copy),
          static_cast<QWidget *>(m_bottom.btn_send),
          static_cast<QWidget *>(m_bottom.btn_inspect),
          static_cast<QWidget *>(m_bottom.btn_actions)}) {
        setSelectionStripWidgetVisible(w, false);
    }
    setSelectionStripWidgetVisible(m_bottom.meta, true);

    if (m_bottom.meta_full.isEmpty()) {
        m_bottom.meta->clear();
        m_bottom.meta->setToolTip(QString());
    }
    else if (bar_w > 0 && content_w <= 0) {
        m_bottom.meta->clear();
        m_bottom.meta->setToolTip(m_bottom.meta_full);
    }
    else if (content_w > 0) {
        const QString elided = m_bottom.meta->fontMetrics().elidedText(
            m_bottom.meta_full, Qt::ElideRight, content_w);
        m_bottom.meta->setText(elided);
        m_bottom.meta->setToolTip(
            elided != m_bottom.meta_full ? m_bottom.meta_full : QString());
    }
    else {
        m_bottom.meta->setText(m_bottom.meta_full);
        m_bottom.meta->setToolTip(QString());
    }
}

void CharactersTab::updateSelectionBlockLabel(int bar_w, int avail_w,
                                              bool has_block,
                                              bool truncated_prefix)
{
    const bool show_block
        = has_block && (bar_w <= 0 || avail_w >= fontui::dp(24, m_dpr));

    setSelectionStripWidgetVisible(m_bottom.sep_block, show_block);
    setSelectionStripWidgetVisible(m_bottom.meta, show_block);
    if (show_block) {
        const QString elided = (bar_w > 0)
            ? m_bottom.meta->fontMetrics().elidedText(m_bottom.block_full,
                                                      Qt::ElideRight, avail_w)
            : m_bottom.block_full;
        m_bottom.meta->setText(elided);
        const bool truncated = truncated_prefix || (elided != m_bottom.block_full);
        m_bottom.meta->setToolTip(truncated ? m_bottom.meta_full : QString());
    }
    else {
        m_bottom.meta->clear();
        m_bottom.meta->setToolTip(m_bottom.meta_full);
    }
}

void CharactersTab::updateLeftSelectionChips(int bar_w, int left_avail,
                                             int spacing, int sep_w,
                                             bool has_dec, bool has_gid,
                                             bool has_name, bool has_block)
{
    auto chip = [](QPushButton *b) {
        return static_cast<MetaCopyChip *>(b);
    };
    const int code_w
        = chip(m_bottom.chip_code) ? chip(m_bottom.chip_code)->fullContentWidth() : 0;
    const int dec_w = has_dec ? chip(m_bottom.chip_dec)->fullContentWidth() : 0;
    const int gid_w = has_gid ? chip(m_bottom.chip_gid)->fullContentWidth() : 0;
    const int name_full_w
        = has_name ? chip(m_bottom.chip_name)->fullContentWidth() : 0;

    const bool show_code = (bar_w <= 0 || left_avail > 0);
    if (chip(m_bottom.chip_code)) {
        chip(m_bottom.chip_code)->setMaxWidth(
            (bar_w > 0 && show_code && code_w > left_avail) ? left_avail : -1);
    }
    setSelectionStripWidgetVisible(m_bottom.chip_code, show_code);
    int used_left = (show_code && chip(m_bottom.chip_code))
                        ? chip(m_bottom.chip_code)->sizeHint().width()
                        : 0;

    auto sep_plus_chip = [spacing, sep_w](int chip_w) {
        return 2 * spacing + sep_w + chip_w;
    };

    const bool show_gid
        = has_gid && (bar_w <= 0 || used_left + sep_plus_chip(gid_w) <= left_avail);
    if (show_gid) {
        used_left += sep_plus_chip(gid_w);
    }

    bool show_name = false;
    int name_cap_w = -1;
    if (has_name) {
        const int rem           = left_avail - used_left;
        const int min_name_slot = fontui::dp(52, m_dpr);
        if (bar_w <= 0 || rem >= sep_plus_chip(name_full_w)) {
            show_name  = true;
            name_cap_w = -1;
            used_left += sep_plus_chip(name_full_w);
        }
        else if (rem >= sep_plus_chip(min_name_slot)) {
            show_name  = true;
            name_cap_w = rem - (2 * spacing + sep_w);
            used_left += sep_plus_chip(name_cap_w);
        }
    }
    if (chip(m_bottom.chip_name)) {
        chip(m_bottom.chip_name)->setMaxWidth(name_cap_w);
    }

    const int min_block_reserve
        = has_block ? (2 * spacing + sep_w + fontui::dp(42, m_dpr)) : 0;
    const bool show_dec = has_dec
                          && (bar_w <= 0
                              || used_left + sep_plus_chip(dec_w) + min_block_reserve
                                     <= left_avail);
    if (show_dec) {
        used_left += sep_plus_chip(dec_w);
    }

    setSelectionStripWidgetVisible(m_bottom.sep_dec, show_dec);
    setSelectionStripWidgetVisible(m_bottom.chip_dec, show_dec);
    setSelectionStripWidgetVisible(m_bottom.sep_gid, show_gid);
    setSelectionStripWidgetVisible(m_bottom.chip_gid, show_gid);
    setSelectionStripWidgetVisible(m_bottom.sep_name, show_name);
    setSelectionStripWidgetVisible(m_bottom.chip_name, show_name);

    const int block_avail = (bar_w > 0)
        ? qMax(0, left_avail - used_left - (2 * spacing + sep_w))
        : 100000;
    const bool truncated_prefix
        = !show_dec || (has_gid && !show_gid) || (has_name && !show_name)
          || (name_cap_w >= 0);
    updateSelectionBlockLabel(bar_w, block_avail, has_block, truncated_prefix);
}

void CharactersTab::updateSelectedSelectionStrip(int bar_w, int content_w,
                                                 int spacing)
{
    auto chip = [](QPushButton *b) {
        return static_cast<MetaCopyChip *>(b);
    };

    const bool has_dec
        = chip(m_bottom.chip_dec) && !chip(m_bottom.chip_dec)->fullText().isEmpty();
    const bool has_gid
        = chip(m_bottom.chip_gid) && !chip(m_bottom.chip_gid)->fullText().isEmpty();
    const bool has_name
        = chip(m_bottom.chip_name) && !chip(m_bottom.chip_name)->fullText().isEmpty();
    const bool has_block = !m_bottom.block_full.isEmpty();

    const int glyph_w    = m_bottom.glyph ? fontui::dp(24, m_dpr) : 0;
    const int action_gap = fontui::dp(4, m_dpr);
    const int sep_w      = m_bottom.sep_dec ? m_bottom.sep_dec->sizeHint().width() : 0;

    int inline_w   = 0;
    int inline_cnt = 0;
    for (QPushButton *b :
         {m_bottom.btn_copy, m_bottom.btn_send, m_bottom.btn_inspect}) {
        if (b) {
            inline_w += b->sizeHint().width();
            ++inline_cnt;
        }
    }
    if (inline_cnt > 1) {
        inline_w += (inline_cnt - 1) * spacing;
    }
    const int menu_w
        = m_bottom.btn_actions ? m_bottom.btn_actions->sizeHint().width() : 0;

    const int code_w
        = chip(m_bottom.chip_code) ? chip(m_bottom.chip_code)->fullContentWidth() : 0;
    const int gid_w = has_gid ? chip(m_bottom.chip_gid)->fullContentWidth() : 0;
    const int name_full_w
        = has_name ? chip(m_bottom.chip_name)->fullContentWidth() : 0;

    // 3 layout spacings: glyph->code, last_left->spacer, spacer->action(s)
    const int trailing_gaps = 3 * spacing + action_gap;
    const int min_left_for_inline
        = glyph_w + spacing + code_w
          + (has_gid ? (2 * spacing + sep_w + gid_w) : 0)
          + (has_name ? (2 * spacing + sep_w
                         + qMin(name_full_w, fontui::dp(76, m_dpr)))
                      : 0)
          + fontui::dp(48, m_dpr);

    const bool fold_actions
        = (bar_w > 0)
          && (content_w < inline_w + 2 * spacing + action_gap + min_left_for_inline);

    for (QPushButton *b :
         {m_bottom.btn_copy, m_bottom.btn_send, m_bottom.btn_inspect}) {
        setSelectionStripWidgetVisible(b, !fold_actions);
    }
    setSelectionStripWidgetVisible(m_bottom.btn_actions, fold_actions);
    if (!fold_actions && m_bottom.actions_menu && m_bottom.actions_menu->isVisible()) {
        m_bottom.actions_menu->close();
    }

    const int actions_used = fold_actions ? menu_w : inline_w;
    const int left_avail   = (bar_w > 0)
        ? qMax(0, content_w - actions_used - trailing_gaps - glyph_w)
        : 100000;

    setSelectionStripWidgetVisible(m_bottom.glyph, true);
    m_bottom.glyph->setToolTip(m_bottom.meta_full);

    updateLeftSelectionChips(bar_w, left_avail, spacing, sep_w, has_dec, has_gid,
                             has_name, has_block);
}

void CharactersTab::updateSelectionMetaElide()
{
    if (!m_bottom.meta) {
        return;
    }

    int bar_w = m_bottom.bar ? m_bottom.bar->width() : 0;
    if (width() > 0) {
        bar_w = bar_w > 0 ? qMin(bar_w, width()) : width();
    }

    int content_w = 0;
    int spacing   = fontui::dp(2, m_dpr);
    if (bar_w > 0 && m_bottom.bar && m_bottom.bar->layout()) {
        const QMargins margins = m_bottom.bar->layout()->contentsMargins();
        spacing   = m_bottom.bar->layout()->spacing();
        content_w = qMax(0, bar_w - margins.left() - margins.right());
    }

    if (m_selected_cp == 0) {
        resetEmptySelectionStrip(bar_w, content_w);
    }
    else {
        updateSelectedSelectionStrip(bar_w, content_w, spacing);
    }
}

void CharactersTab::refreshThemeVisuals()
{
    const QPalette &pal = palette();
    const QColor muted  = fontui::mutedTextColor(pal);
    QPalette mp = pal;
    mp.setColor(QPalette::WindowText, muted);
    if (m_bottom.meta) {
        m_bottom.meta->setPalette(mp);
    }
    for (QLabel *sep : {m_bottom.sep_dec, m_bottom.sep_gid,
                        m_bottom.sep_name, m_bottom.sep_block}) {
        if (sep) {
            sep->setPalette(mp);
            sep->update();
        }
    }
    if (m_empty_title) {
        QPalette tp = pal;
        tp.setColor(QPalette::WindowText, pal.color(QPalette::WindowText));
        m_empty_title->setPalette(tp);
    }
    if (m_empty_sub) {
        m_empty_sub->setPalette(mp);
    }
    if (m_bottom.glyph) {
        QPalette gp = pal;
        gp.setColor(QPalette::WindowText, pal.color(QPalette::WindowText));
        m_bottom.glyph->setPalette(gp);
        m_bottom.glyph->update();
    }
    if (m_bottom.actions_menu) {
        m_bottom.actions_menu->setPalette(pal);
    }
    if (m_surface_frame) {
        m_surface_frame->update();
    }
    if (m_grid) {
        m_grid->update();
    }
    if (m_top.filter_edit) {
        m_top.filter_edit->update();
    }
    if (m_top.combo_block) {
        m_top.combo_block->update();
    }
    if (m_top.cell_slider) {
        m_top.cell_slider->update();
    }
    if (m_top.cell_badge) {
        m_top.cell_badge->update();
    }
    for (QPushButton *b :
         {m_bottom.chip_code, m_bottom.chip_dec, m_bottom.chip_gid,
          m_bottom.chip_name, m_bottom.btn_copy, m_bottom.btn_send,
          m_bottom.btn_inspect, m_bottom.btn_actions, m_clear_filter}) {
        if (b) {
            b->update();
        }
    }
}

void CharactersTab::changeEvent(QEvent *event)
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
    else if (event->type() == QEvent::EnabledChange) {
        updateNavShortcutState();
    }
}

void CharactersTab::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateSelectionMetaElide();
}

void CharactersTab::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (ensureGlyphNamesLoaded()) {
        if (!m_model.searchQuery().isEmpty()) {
            applyFilteredToGrid();
        }
        else {
            updateSelectionStrip();
        }
    }
    updateNavShortcutState();
}

void CharactersTab::hideEvent(QHideEvent *event)
{
    if (m_bottom.actions_menu && m_bottom.actions_menu->isVisible()) {
        m_bottom.actions_menu->close();
    }
    QWidget::hideEvent(event);
    updateNavShortcutState();
}

void CharactersTab::initNavigationShortcuts()
{
    auto add_nav = [this](const QKeySequence &seq, int key,
                          Qt::KeyboardModifiers mods = Qt::NoModifier) {
        auto *sc = new QShortcut(seq, this);
        sc->setContext(Qt::WindowShortcut);
        connect(sc, &QShortcut::activated, this,
                [this, key, mods]() { handleNavigationKey(key, mods); });
        m_nav_shortcuts.append(sc);
    };

    add_nav(QKeySequence(Qt::Key_Left), Qt::Key_Left);
    add_nav(QKeySequence(Qt::Key_Right), Qt::Key_Right);
    add_nav(QKeySequence(Qt::Key_Up), Qt::Key_Up);
    add_nav(QKeySequence(Qt::Key_Down), Qt::Key_Down);
    add_nav(QKeySequence(Qt::Key_Home), Qt::Key_Home);
    add_nav(QKeySequence(Qt::Key_End), Qt::Key_End);
    add_nav(QKeySequence(Qt::Key_PageUp), Qt::Key_PageUp);
    add_nav(QKeySequence(Qt::Key_PageDown), Qt::Key_PageDown);
    add_nav(QKeySequence(Qt::Key_Return), Qt::Key_Return);
    add_nav(QKeySequence(Qt::Key_Enter), Qt::Key_Enter);
    add_nav(QKeySequence(Qt::SHIFT | Qt::Key_Return), Qt::Key_Return,
            Qt::ShiftModifier);
    add_nav(QKeySequence(Qt::SHIFT | Qt::Key_Enter), Qt::Key_Enter,
            Qt::ShiftModifier);
}

bool CharactersTab::isInputOrPopupFocused() const
{
    if (QApplication::activePopupWidget() != nullptr
        || QApplication::activeModalWidget() != nullptr
        || (m_bottom.actions_menu && m_bottom.actions_menu->isVisible())) {
        return true;
    }
    if (m_top.combo_block && m_top.combo_block->view()
        && m_top.combo_block->view()->isVisible()) {
        return true;
    }
    for (QWidget *w = QApplication::focusWidget(); w != nullptr;
         w          = w->parentWidget()) {
        if (qobject_cast<QLineEdit *>(w) != nullptr
            || qobject_cast<QTextEdit *>(w) != nullptr
            || qobject_cast<QPlainTextEdit *>(w) != nullptr
            || qobject_cast<QAbstractSpinBox *>(w) != nullptr
            || qobject_cast<QAbstractItemView *>(w) != nullptr
            || qobject_cast<QComboBox *>(w) != nullptr
            || qobject_cast<QAbstractButton *>(w) != nullptr
            || qobject_cast<QAbstractSlider *>(w) != nullptr
            || qobject_cast<QTabBar *>(w) != nullptr
            || qobject_cast<QMenu *>(w) != nullptr
            || qobject_cast<QMenuBar *>(w) != nullptr) {
            return true;
        }
    }
    return false;
}

void CharactersTab::updateNavShortcutState()
{
    const bool enable = isVisible() && isEnabled() && !isInputOrPopupFocused();
    for (QShortcut *sc : m_nav_shortcuts) {
        if (sc) {
            sc->setEnabled(enable);
        }
    }
}

bool CharactersTab::handleNavigationKey(int key, Qt::KeyboardModifiers mods)
{
    if (!isVisible() || !isEnabled() || isInputOrPopupFocused()) {
        return false;
    }
    const int cols = m_grid ? qMax(1, m_grid->columnCount()) : 1;
    switch (key) {
    case Qt::Key_Left:
        navigateByDelta(-1);
        return true;
    case Qt::Key_Right:
        navigateByDelta(1);
        return true;
    case Qt::Key_Up:
        navigateByDelta(-cols);
        return true;
    case Qt::Key_Down:
        navigateByDelta(cols);
        return true;
    case Qt::Key_Home:
        navigateByDelta(0, true, 0);
        return true;
    case Qt::Key_End:
        navigateByDelta(0, true, -1);
        return true;
    case Qt::Key_PageUp: {
        const int vh = (m_scroll_area && m_scroll_area->viewport())
                           ? m_scroll_area->viewport()->height()
                           : 240;
        const int cell_px = m_grid ? qMax(1, m_grid->cellSizePx()) : 44;
        const int rows    = qMax(1, vh / cell_px);
        navigateByDelta(-rows * cols);
        return true;
    }
    case Qt::Key_PageDown: {
        const int vh = (m_scroll_area && m_scroll_area->viewport())
                           ? m_scroll_area->viewport()->height()
                           : 240;
        const int cell_px = m_grid ? qMax(1, m_grid->cellSizePx()) : 44;
        const int rows    = qMax(1, vh / cell_px);
        navigateByDelta(rows * cols);
        return true;
    }
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (m_selected_cp != 0) {
            emit inspectRequested(m_selected_cp,
                                  (mods & Qt::ShiftModifier) != 0);
        }
        emit sigInteractionClearFocus();
        return true;
    default:
        return false;
    }
}

void CharactersTab::navigateByDelta(int delta, bool to_edge, int edge_index)
{
    const QVector<quint32> &filtered = m_model.filteredCodePoints();
    const int total                  = filtered.size();
    if (total == 0) {
        return;
    }

    if (to_edge) {
        const int target = edge_index < 0 ? (total - 1)
                                          : qBound(0, edge_index, total - 1);
        selectGridIndex(target);
        return;
    }

    int cur_idx = -1;
    if (m_selected_cp != 0) {
        auto it
            = std::lower_bound(filtered.cbegin(), filtered.cend(), m_selected_cp);
        if (it != filtered.cend() && *it == m_selected_cp) {
            cur_idx = int(it - filtered.cbegin());
        }
    }

    if (cur_idx < 0) {
        selectGridIndex(delta < 0 ? (total - 1) : 0);
        return;
    }

    const int cols = m_grid ? qMax(1, m_grid->columnCount()) : 1;
    const int next = cur_idx + delta;
    if (next < 0) {
        // Keep column position on Up at row 0; jump to row 0 (same col) on PageUp.
        if (delta == -cols) {
            selectGridIndex(cur_idx);
        }
        else if (delta < -cols) {
            selectGridIndex(qBound(0, cur_idx % cols, total - 1));
        }
        else {
            selectGridIndex(0);
        }
        return;
    }
    if (next >= total) {
        const int last_row = (total - 1) / cols;
        const int cur_row  = cur_idx / cols;
        if (delta == cols && cur_row == last_row) {
            selectGridIndex(cur_idx);
        }
        else {
            selectGridIndex(total - 1);
        }
        return;
    }
    selectGridIndex(next);
}

void CharactersTab::selectGridIndex(int index)
{
    const QVector<quint32> &filtered = m_model.filteredCodePoints();
    if (index < 0 || index >= filtered.size()) {
        return;
    }
    const quint32 cp = filtered.at(index);
    if (m_selected_cp == cp) {
        if (m_grid && m_grid->selectedCodePoint() != cp) {
            m_grid->setSelectedCodePoint(cp);
        }
        ensureIndexVisible(index);
        return;
    }
    m_selected_cp    = cp;
    if (m_grid) {
        m_grid->setSelectedCodePoint(cp);
    }
    updateSelectionStrip();
    emit selectionChangedCodePoint(cp);
    ensureIndexVisible(index);
}

void CharactersTab::ensureIndexVisible(int index)
{
    if (!m_scroll_area || !m_grid) {
        return;
    }
    const QRect r = m_grid->cellRectForIndex(index);
    if (!r.isValid()) {
        return;
    }
    const int ymargin = r.height() / 2 + fontui::dp(6, m_dpr);
    m_scroll_area->ensureVisible(r.center().x(), r.center().y(), 0, ymargin);
}

void CharactersTab::keyPressEvent(QKeyEvent *event)
{
    if (handleNavigationKey(event->key(), event->modifiers())) {
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void CharactersTab::clearFilterFocus()
{
    if (m_top.filter_edit && m_top.filter_edit->hasFocus()) {
        m_top.filter_edit->clearFocus();
    }
    emit sigInteractionClearFocus();
    updateNavShortcutState();
}

bool CharactersTab::eventFilter(QObject *watched, QEvent *event)
{
    if (((m_top.combo_block && watched == m_top.combo_block->view())
         || (m_bottom.actions_menu && watched == m_bottom.actions_menu))
        && (event->type() == QEvent::Show || event->type() == QEvent::Hide)) {
        updateNavShortcutState();
        return false;
    }
    if (watched == m_bottom.bar && event->type() == QEvent::Resize) {
        updateSelectionMetaElide();
        return false;
    }
    if (event->type() == QEvent::MouseButtonDblClick
        && (watched == m_top.cell_slider || watched == m_top.cell_badge)) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            setCellSizeDp(CharacterWidget::kDefCellSizeDp, true);
            emit sigInteractionClearFocus();
            mouse->accept();
            return true;
        }
    }
    if (event->type() == QEvent::Wheel
        && (watched == m_top.combo_block || watched == m_top.cell_slider)) {
        auto *wheel = static_cast<QWheelEvent *>(event);
        wheel->accept();
        return true;
    }
    if (watched == m_top.filter_edit) {
        if (event->type() == QEvent::InputMethod) {
            auto *ime              = static_cast<QInputMethodEvent *>(event);
            m_filter_ime_composing = !ime->preeditString().isEmpty();
            return false;
        }
        if (event->type() == QEvent::FocusOut) {
            m_filter_ime_composing = false;
            return false;
        }
        if (event->type() == QEvent::ShortcutOverride) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Escape
                && !(key->modifiers()
                     & (Qt::ControlModifier | Qt::AltModifier))) {
                event->accept();
                return true;
            }
            return false;
        }
        if (event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Escape
                && !(key->modifiers()
                     & (Qt::ControlModifier | Qt::AltModifier))) {
                if (m_filter_ime_composing) {
                    return false;
                }
                if (!m_top.filter_edit->text().isEmpty()) {
                    m_top.filter_timer->stop();
                    {
                        QSignalBlocker b(m_top.filter_edit);
                        m_top.filter_edit->clear();
                    }
                    if (m_model.setSearchQuery(QString())) {
                        applyFilteredToGrid();
                    }
                }
                key->accept();
                QMetaObject::invokeMethod(
                    this, [this]() { clearFilterFocus(); },
                    Qt::QueuedConnection);
                return true;
            }
            if (m_filter_ime_composing) {
                return false;
            }
            if ((key->key() == Qt::Key_Down
                 && key->modifiers() == Qt::NoModifier)
                || key->key() == Qt::Key_Return
                || key->key() == Qt::Key_Enter) {
                m_top.filter_timer->stop();
                applySearchFilterText();
                clearFilterFocus();
                if (m_model.filteredCount() > 0
                    && (m_selected_cp == 0
                        || !m_model.containsCodePoint(m_selected_cp))) {
                    selectGridIndex(0);
                }
                key->accept();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}
