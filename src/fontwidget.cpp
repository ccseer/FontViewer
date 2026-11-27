#include "fontwidget.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QSvgGenerator>
#include <QTabBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtMath>

#include <functional>

#include "bars/opentypefeaturesbar.h"
#include "bars/variableaxesbar.h"
#include "engine/unicodeblocks.h"
#include "fontui.h"
#include "tabs/characterstab.h"
#include "tabs/characterwidget.h"
#include "tabs/glyphinspectorwidget.h"
#include "tabs/samplepreviewtab.h"
#include "tabs/textpreviewtab.h"
#include "ui_fontwidget.h"

#define qprintt qDebug() << "[FontViewer]"

namespace {
constexpr auto g_def_pt = 16;
}  // namespace

FontWidget::FontWidget(QWidget *parent)
    : QWidget(parent), m_wnd_char(nullptr), ui(new Ui::FontWidget)
{
    ui->setupUi(this);
    qprintt << this;
}

FontWidget::~FontWidget()
{
    m_inspector.close();
    releaseApplicationFont();
    delete ui;
    qprintt << "~" << this;
}

void FontWidget::releaseApplicationFont()
{
    if (m_font_id != -1) {
        QFontDatabase::removeApplicationFont(m_font_id);
        m_font_id = -1;
    }
}

void FontWidget::initUI(const QStringList &names_raw)
{
    /// widget_top
    ui->widget_top->layout()->setContentsMargins(0, 0, 0, 0);
    // names
    QStringList names = names_raw;
    names.removeDuplicates();
    ui->comboBox_family->addItems(names);
    ui->comboBox_family->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    if (names.size() == 1) {
        ui->comboBox_family->setEnabled(false);
    }
    fontui::configureComboBox(ui->comboBox_family);
    // style
    ui->comboBox_style->addItems(
        QFontDatabase::styles(ui->comboBox_family->currentText()));
    fontui::configureComboBox(ui->comboBox_style);
    // sz
    const auto all_sz = QFontDatabase::standardSizes();
    for (auto i : all_sz) {
        ui->comboBox_sz->addItem(QString::number(i));
    }
    if (all_sz.contains(g_def_pt)) {
        ui->comboBox_sz->setCurrentText(QString::number(g_def_pt));
    }
    fontui::configureComboBox(ui->comboBox_sz);
    // The three header combos never take focus, but a hover wheel still changes
    // their value; swallow it like the toolbar controls.
    for (QComboBox *cb :
         {ui->comboBox_family, ui->comboBox_style, ui->comboBox_sz}) {
        cb->installEventFilter(this);
    }
    // info
    // breaks the layout!!!
    // ui->label_info->setWordWrap(true);
    ui->label_info->setAlignment(Qt::AlignCenter);

    m_label_name_collapsed = new QLabel(this);
    m_label_name_collapsed->setAlignment(Qt::AlignCenter);
    m_label_name_collapsed->setSizePolicy(QSizePolicy::Ignored,
                                          QSizePolicy::Preferred);
    m_label_name_collapsed->hide();
    auto font_bold = m_label_name_collapsed->font();
    font_bold.setBold(true);
    m_label_name_collapsed->setFont(font_bold);

    m_btn_toggle = fontui::makeIconButton("Collapse", this);
    connect(m_btn_toggle, &QPushButton::clicked, this, [this]() {
        setCollapsed(!isCollapsed());
        clearViewerFocus();
    });

    // Add to main grid layout at the same cell as widget_top
    // This keeps it at a fixed position relative to the top-right corner
    ui->gridLayout->addWidget(m_btn_toggle, 0, 0,
                              Qt::AlignTop | Qt::AlignRight);

    // Add collapsed label across the top row; internal Qt::AlignCenter keeps
    // it centered while side margins clear the top-right collapse button.
    ui->gridLayout->addWidget(m_label_name_collapsed, 0, 0);

    /// tab wnd
    ui->tabWidget->setFocusPolicy(Qt::NoFocus);
    if (auto *bar = ui->tabWidget->tabBar()) {
        bar->setFocusPolicy(Qt::NoFocus);
        for (QWidget *child : bar->findChildren<QWidget *>()) {
            child->setFocusPolicy(Qt::NoFocus);
        }
    }
    auto corner     = new QWidget(this);
    m_corner_layout = new QHBoxLayout(corner);
    m_corner_layout->setContentsMargins(0, 0, fontui::dp(4, m_dpr), 0);
    m_corner_layout->setSpacing(fontui::dp(2, m_dpr));

    m_btn_copy = fontui::makeIconButton("Copy as Image", this);
    m_btn_svg  = fontui::makeIconButton("Save as SVG", this);
    connect(m_btn_copy, &QPushButton::clicked, this, [this]() {
        copy();
        clearViewerFocus();
    });
    connect(m_btn_svg, &QPushButton::clicked, this, [this]() {
        saveAsSvg();
        clearViewerFocus();
    });

    m_corner_layout->addWidget(m_btn_copy);
    m_corner_layout->addWidget(m_btn_svg);
    ui->tabWidget->setCornerWidget(corner);
    connect(ui->tabWidget, &QTabWidget::currentChanged, this,
            &FontWidget::onTabChanged);

    // Group variable axes in one Typography section between the header and the
    // preview tabs.
    m_typo_group = new QGroupBox(tr("Typography"), this);
    m_typo_group->setFocusPolicy(Qt::NoFocus);
    auto *typo_lay = new QVBoxLayout(m_typo_group);
    typo_lay->setContentsMargins(
        fontui::dp(10, m_dpr), fontui::dp(8, m_dpr), fontui::dp(10, m_dpr),
        fontui::dp(8, m_dpr));
    typo_lay->setSpacing(fontui::dp(6, m_dpr));

    initAxesBar();
    typo_lay->addWidget(m_axes_bar);

    m_typo_group->hide();
    ui->gridLayout->removeWidget(ui->tabWidget);
    ui->gridLayout->addWidget(m_typo_group, 1, 0);
    ui->gridLayout->addWidget(ui->tabWidget, 2, 0);
    ui->gridLayout->setRowStretch(2, 1);
    updateLayoutMetrics();

    initTextTab();
    initSampleTab();
    initCharTab();
    initGlyphTab();
    initActions();

    connect(ui->comboBox_family, &QComboBox::currentTextChanged, this,
            &FontWidget::onNameChanged);
    connect(ui->comboBox_style, &QComboBox::currentTextChanged, this,
            &FontWidget::onStyleChanged);
    connect(ui->comboBox_sz, &QComboBox::currentTextChanged, this, [this]() {
        updateTabTextPreview();
        updateCollapsedLabel();
        emit sigFontSizeChanged(getCurrentFontSize());
    });
    for (QComboBox *cb :
         {ui->comboBox_family, ui->comboBox_style, ui->comboBox_sz}) {
        connect(cb, &QComboBox::activated, this,
                [this](int) { clearViewerFocus(); });
    }
}

void FontWidget::initTextTab()
{
    m_tab_text = new TextPreviewTab(this);
    m_tab_text->setFontProvider([this](int pt) {
        return buildPreviewFont(pt, PreviewFontRole::Text);
    });
    ui->tabWidget->addTab(m_tab_text, tr("Text"));

    connect(m_tab_text, &TextPreviewTab::sigToast, this, &FontWidget::sigToast);
    connect(m_tab_text, &TextPreviewTab::sigSizeClicked, this,
            &FontWidget::setCurrentFontSize);
    connect(m_tab_text, &TextPreviewTab::sigZoomRequested, this,
            &FontWidget::zoomSize);
    connect(m_tab_text, &TextPreviewTab::sigLetterSpacingChanged, this,
            [this](qreal) { updateTabTextPreview(); });
    connect(m_tab_text, &TextPreviewTab::sigFeaturesChanged, this, [this]() {
        updateResetFeatureActionState();
        refreshActiveTabFromFeatures();
    });
    connect(m_tab_text, &TextPreviewTab::sigInteractionClearFocus, this,
            &FontWidget::clearViewerFocus);
}

void FontWidget::initSampleTab()
{
    m_tab_sample = new SamplePreviewTab(this);
    m_tab_sample->setDpr(m_dpr);
    ui->tabWidget->addTab(m_tab_sample, tr("Sample"));

    connect(m_tab_sample, &SamplePreviewTab::sigZoomRequested, this,
            &FontWidget::zoomSize);
    connect(m_tab_sample, &SamplePreviewTab::sigInteractionClearFocus, this,
            &FontWidget::clearViewerFocus);
    connect(m_tab_sample, &SamplePreviewTab::sigCopyToastRequested, this,
            &FontWidget::sigToast);
}

void FontWidget::initCharTab()
{
    m_chars_tab = new CharactersTab(this);
    m_chars_tab->setDpr(m_dpr);
    m_wnd_char = m_chars_tab->characterWidget();
    ui->tabWidget->addTab(m_chars_tab, "Characters");
    m_tab_char = m_chars_tab;
    if (m_wnd_char) {
        m_wnd_char->installEventFilter(this);
    }

    connect(m_chars_tab, &CharactersTab::sigCopyToastRequested, this,
            &FontWidget::sigToast);
    connect(m_chars_tab, &CharactersTab::sigCharCellSizeChanged, this,
            &FontWidget::sigCharCellSizeChanged);
    connect(m_chars_tab, &CharactersTab::sigInteractionClearFocus, this,
            &FontWidget::clearViewerFocus);
    connect(m_chars_tab, &CharactersTab::sigInsertTextRequested, this,
            [this](const QString &text) {
                if (m_tab_text && !text.isEmpty()) {
                    m_tab_text->setCurrentText(m_tab_text->currentText() + text);
                    ui->tabWidget->setCurrentWidget(m_tab_text);
                }
                clearViewerFocus();
            });
}

void FontWidget::initGlyphTab()
{
    auto *wnd_glyph_sa = new QScrollArea(this);
    m_wnd_gi           = new GlyphInspectorWidget(this);
    wnd_glyph_sa->setFrameShape(QFrame::NoFrame);
    wnd_glyph_sa->setFocusPolicy(Qt::NoFocus);
    wnd_glyph_sa->setWidgetResizable(true);
    wnd_glyph_sa->setWidget(m_wnd_gi);
    ui->tabWidget->addTab(wnd_glyph_sa, "Glyph Inspector");
    m_tab_gi = wnd_glyph_sa;

    if (m_chars_tab) {
        connect(m_chars_tab, &CharactersTab::selectionChangedCodePoint, this,
                [this](quint32 cp) {
                    const int pt             = getCurrentFontSize();
                    const GlyphInspectData d = inspectGlyphData(cp);
                    m_wnd_gi->updateGlyph(
                        cp, buildPreviewFont(pt, PreviewFontRole::Glyph),
                        m_font_path, d.deco, d.outline, d.glyph_name);
                    m_gi_dirty = false;

                    const int tab_index = ui->tabWidget->indexOf(m_tab_gi);
                    if (tab_index != -1) {
                        if (cp == 0) {
                            ui->tabWidget->setTabText(tab_index,
                                                      "Glyph Inspector");
                        }
                        else {
                            ui->tabWidget->setTabText(
                                tab_index,
                                QStringLiteral("Glyph Inspector - %1")
                                    .arg(unicode_blocks::formatCodePoint(cp)));
                        }
                    }
                });

        connect(m_chars_tab, &CharactersTab::inspectRequested, this,
                [this, wnd_glyph_sa](quint32 cp, bool scroll_to_preview) {
                    if (cp != 0 && m_wnd_gi
                        && m_wnd_gi->currentCodePoint() != cp) {
                        emit m_chars_tab->selectionChangedCodePoint(cp);
                    }
                    const int tab_index = ui->tabWidget->indexOf(m_tab_gi);
                    if (tab_index != -1) {
                        ui->tabWidget->setCurrentIndex(tab_index);
                        if (wnd_glyph_sa) {
                            QPointer<QScrollArea> sa(wnd_glyph_sa);
                            QTimer::singleShot(
                                0, wnd_glyph_sa, [sa, scroll_to_preview]() {
                                    if (!sa) {
                                        return;
                                    }
                                    if (QScrollBar *vsb
                                        = sa->verticalScrollBar()) {
                                        vsb->setValue(scroll_to_preview
                                                          ? vsb->maximum()
                                                          : 0);
                                    }
                                });
                        }
                    }
                });
    }
}

void FontWidget::initAxesBar()
{
    m_axes_bar = new VariableAxesBar(m_typo_group ? static_cast<QWidget *>(m_typo_group) : this);
    m_axes_bar->setDpr(m_dpr);
    connect(m_axes_bar, &VariableAxesBar::sigAxesChanged, this,
            &FontWidget::refreshActiveTabFromAxes);
    connect(m_axes_bar, &VariableAxesBar::sigInteractionClearFocus, this,
            &FontWidget::clearViewerFocus);
}

void FontWidget::syncTypographyGroupVisibility()
{
    const bool has_axes = m_axes_bar && !m_axes_bar->isHidden();
    if (m_typo_group) {
        m_typo_group->setVisible(!m_is_collapsed && has_axes);
    }
}

void FontWidget::updateLayoutMetrics()
{
    if (m_corner_layout) {
        m_corner_layout->setContentsMargins(0, 0, fontui::dp(4, m_dpr), 0);
        m_corner_layout->setSpacing(fontui::dp(2, m_dpr));
    }
    if (m_typo_group && m_typo_group->layout()) {
        m_typo_group->layout()->setContentsMargins(
            fontui::dp(10, m_dpr), fontui::dp(8, m_dpr), fontui::dp(10, m_dpr),
            fontui::dp(8, m_dpr));
        m_typo_group->layout()->setSpacing(fontui::dp(6, m_dpr));
    }
    if (ui && ui->gridLayout) {
        const int pad = fontui::dp(8, m_dpr);
        ui->gridLayout->setContentsMargins(pad, pad, pad, pad);
        ui->gridLayout->setSpacing(fontui::dp(6, m_dpr));
    }
    if (m_axes_bar) {
        m_axes_bar->setDpr(m_dpr);
    }
    if (m_tab_text) {
        m_tab_text->setDpr(m_dpr);
    }
    if (m_tab_sample) {
        m_tab_sample->setDpr(m_dpr);
    }
    if (m_chars_tab) {
        m_chars_tab->setDpr(m_dpr);
    }
    if (m_wnd_gi) {
        m_wnd_gi->setDpr(m_dpr);
    }
}

void FontWidget::updateResetFeatureActionState()
{
    if (QAction *act = shortcutAction(FontShortcutAction::ResetOpenType)) {
        act->setEnabled(m_tab_text && m_tab_text->hasOpenTypeOverrides());
    }
}

QHash<QByteArray, float> FontWidget::variableAxisValues() const
{
    return m_axes_bar ? m_axes_bar->axisValues() : QHash<QByteArray, float>{};
}

void FontWidget::setVariableAxisValue(const QByteArray &tag, float value)
{
    if (m_axes_bar) {
        m_axes_bar->setAxisValue(tag, value);
    }
}

QVector<FontNameRecord> FontWidget::readNameRecords()
{
    if (!m_inspector.isValid() && !m_font_path.isEmpty()) {
        m_inspector.openFile(m_font_path);
    }
    if (!m_inspector.isValid()) {
        return {};
    }
    const int face = qBound(0, m_face_index, m_inspector.faceCount() - 1);
    return m_inspector.inspectFace(face).name_records;
}

bool FontWidget::init(const QString &p)
{
    // An instance binds one font, but a repeated init() must not leak the
    // previous registration.
    releaseApplicationFont();
    m_font_path  = p;
    m_face_index = 0;
    if (m_axes_bar) {
        m_axes_bar->resetProbe();
    }
    m_inspector.openFile(p);
    m_font_id = QFontDatabase::addApplicationFont(p);
    if (-1 == m_font_id) {
        qprintt << "addApplicationFont err";
        m_inspector.close();
        return false;
    }
    const QStringList names = QFontDatabase::applicationFontFamilies(m_font_id);
    if (names.isEmpty()) {
        qprintt << "applicationFontFamilies err";
        releaseApplicationFont();
        m_inspector.close();
        return false;
    }
    initUI(names);

    onNameChanged();
    onTabChanged();
    if (m_chars_tab && m_char_dirty) {
        m_chars_tab->updateFont(
            buildPreviewFont(getCurrentFontSize(), PreviewFontRole::Glyph));
        m_char_dirty = false;
    }
    updateTheme(m_theme);
    clearViewerFocus();
    return true;
}

void FontWidget::clearViewerFocus()
{
    QWidget *focus = QApplication::focusWidget();
    if (focus && (focus == this || isAncestorOf(focus))) {
        focus->clearFocus();
    }
}

QFont FontWidget::buildPreviewFont(int pt_size, PreviewFontRole role) const
{
    const QString name  = ui->comboBox_family->currentText();
    const QString style = ui->comboBox_style->currentText();

    QFont ft(name);
    ft.setPointSize(qMax(1, pt_size));

    const auto &axes = m_inspector.inspectFace(m_face_index).variable_axes;
    if (axes.isEmpty()) {
        // doesn't work: https://bugreports.qt.io/browse/QTBUG-69499
        // ft.setStyleName(ui->comboBox_style->currentText());
        if (auto weight = QFontDatabase::weight(name, style); weight != -1) {
            ft.setWeight(QFont::Weight(weight));
        }
        // there are things like Demi-Italic exists, but we can't support these
        ft.setItalic(QFontDatabase::italic(name, style));
    }
    else {
        const auto &axis_vals = m_axes_bar ? m_axes_bar->axisValues()
                                           : QHash<QByteArray, float>{};
        bool has_wght         = false;
        bool has_ital_or_slnt = false;
        for (const FontVariableAxisInfo &ax : axes) {
            if (ax.tag == QByteArrayLiteral("wght")) {
                has_wght = true;
            }
            if (ax.tag == QByteArrayLiteral("ital")
                || ax.tag == QByteArrayLiteral("slnt")) {
                has_ital_or_slnt = true;
            }
            const float val = axis_vals.value(ax.tag, ax.default_value);
            if (const auto tag = QFont::Tag::fromString(ax.tag);
                tag.has_value()) {
                ft.setVariableAxis(*tag, val);
            }
        }
        if (!has_wght) {
            if (auto weight = QFontDatabase::weight(name, style);
                weight != -1) {
                ft.setWeight(QFont::Weight(weight));
            }
        }
        if (!has_ital_or_slnt) {
            ft.setItalic(QFontDatabase::italic(name, style));
        }
    }

    if (role == PreviewFontRole::Text) {
        const qreal spacing    = getLetterSpacing();
        const bool has_spacing = !qFuzzyIsNull(spacing);
        if (m_tab_text) {
            const auto &overrides = m_tab_text->openTypeOverrides();
            for (auto it = overrides.cbegin(); it != overrides.cend(); ++it) {
                if (has_spacing
                    && OpenTypeFeaturesBar::isLigatureFeatureTag(it.key())) {
                    continue;
                }
                if (it.key() == QByteArrayLiteral("kern")) {
                    ft.setKerning(it.value());
                }
                if (const auto tag = QFont::Tag::fromString(it.key());
                    tag.has_value()) {
                    ft.setFeature(*tag, it.value() ? 1u : 0u);
                }
            }
        }
        if (has_spacing) {
            ft.setLetterSpacing(QFont::AbsoluteSpacing, spacing);
        }
    }
    return ft;
}

void FontWidget::setCollapsed(bool c)
{
    if (m_is_collapsed == c) {
        return;
    }
    m_is_collapsed = c;

    ui->widget_top_left->setVisible(!c);
    ui->label_info->setVisible(!c);
    m_label_name_collapsed->setVisible(c);
    syncTypographyGroupVisibility();
    if (c && m_tab_text) {
        m_tab_text->closeAaPanel();
    }

    updateTheme(m_theme);
    emit sigCollapsedChanged(c);
}

void FontWidget::setLineHeight(qreal h)
{
    if (m_tab_text) {
        m_tab_text->setLineHeight(h);
    }
}

qreal FontWidget::getLineHeight() const
{
    return m_tab_text ? m_tab_text->lineHeight() : 1.4;
}

void FontWidget::setLetterSpacing(qreal s)
{
    if (m_tab_text) {
        m_tab_text->setLetterSpacing(s);
    }
}

qreal FontWidget::getLetterSpacing() const
{
    return m_tab_text ? m_tab_text->letterSpacing() : 0.0;
}

void FontWidget::setWaterfallEnabled(bool on)
{
    if (m_tab_text) {
        m_tab_text->setWaterfallEnabled(on);
    }
}

bool FontWidget::isWaterfallEnabled() const
{
    return m_tab_text ? m_tab_text->isWaterfallEnabled() : false;
}

void FontWidget::setInvertPreview(bool on)
{
    if (m_tab_text) {
        m_tab_text->setInvertPreview(on);
    }
}

bool FontWidget::isInvertPreview() const
{
    return m_tab_text ? m_tab_text->isInvertPreview() : false;
}

void FontWidget::clearPreviewUndoHistory()
{
    if (m_tab_text) {
        m_tab_text->clearUndoHistory();
    }
}

void FontWidget::syncCharacterCodePoints()
{
    if (!m_chars_tab) {
        return;
    }
    if (m_inspector.isValid()) {
        const int face = m_face_index;
        m_chars_tab->setCodePoints(
            m_inspector.faceCodePoints(face),
            m_inspector.faceCodePointGlyphIds(face));
        m_chars_tab->setGlyphNameProvider(
            [self = QPointer<FontWidget>(this), face]() {
                if (!self || !self->m_inspector.isValid()) {
                    return QVector<QString>{};
                }
                return self->m_inspector.faceGlyphNames(face);
            });
    }
    else {
        m_chars_tab->setCodePoints({});
    }
    if (m_wnd_gi && m_wnd_gi->hasSelection()) {
        m_wnd_gi->clearSelection();
        const int tab_index = ui->tabWidget->indexOf(m_tab_gi);
        if (tab_index != -1) {
            ui->tabWidget->setTabText(tab_index, "Glyph Inspector");
        }
    }
}

void FontWidget::syncOpenTypeFeatures()
{
    if (!m_tab_text) {
        return;
    }
    // Technical validation gate (Qt 6.8.3 headers): Qt provides no per-font
    // OpenType `locl` language override on Windows DirectWrite, so keep the
    // bar free of non-functional language selectors while honoring GSUB/GPOS
    // features via QFont::setFeature().
    m_tab_text->setOpenTypeFeatures(
        m_inspector.isValid()
            ? m_inspector.inspectFace(m_face_index).ot_features
            : QVector<FontFeatureInfo>{});
    updateResetFeatureActionState();
}

void FontWidget::onNameChanged()
{
    const auto name = ui->comboBox_family->currentText();
    const auto styles = QFontDatabase::styles(name);
    {
        QSignalBlocker blocker(ui->comboBox_style);
        ui->comboBox_style->clear();
        ui->comboBox_style->addItems(styles);
    }
    ui->comboBox_style->setEnabled(styles.size() > 1);

    m_face_index = m_inspector.resolveFaceIndex(
        name, ui->comboBox_style->currentText());
    if (m_axes_bar) {
        m_axes_bar->rebuild(m_inspector, m_face_index, name, true);
    }
    syncCharacterCodePoints();
    syncOpenTypeFeatures();
    syncTypographyGroupVisibility();

    onStyleChanged();
}

void FontWidget::onStyleChanged()
{
    const QString name  = ui->comboBox_family->currentText();
    const QString style = ui->comboBox_style->currentText();
    const int new_face  = m_inspector.resolveFaceIndex(name, style);
    if (new_face != m_face_index) {
        m_face_index = new_face;
        if (m_axes_bar) {
            m_axes_bar->rebuild(m_inspector, m_face_index, name, false);
        }
        syncCharacterCodePoints();
        syncOpenTypeFeatures();
        syncTypographyGroupVisibility();
    }
    if (m_axes_bar) {
        m_axes_bar->syncForStyle(m_inspector, m_face_index, name, style);
    }

    // if the size is smooth, make the item bold.
    // unless they are the same, no need to bold all items.
    QFont ft           = this->font();
    const auto cb_sz_t = ui->comboBox_sz->count();
    for (int i = 0; i < cb_sz_t; ++i) {
        ui->comboBox_sz->setItemData(i, QVariant(ft), Qt::FontRole);
    }

    const auto pt_sz = QFontDatabase::smoothSizes(name, style);

    if (pt_sz.size() != cb_sz_t) {
        ft.setBold(true);
        for (int i = 0; i < cb_sz_t; ++i) {
            if (pt_sz.contains(ui->comboBox_sz->itemText(i).toInt())) {
                ui->comboBox_sz->setItemData(i, QVariant(ft), Qt::FontRole);
            }
        }
    }
    updateTabTextPreview();
    updateSamplePreview();
    updateInfo();
    updateCollapsedLabel();
}

void FontWidget::updateCollapsedLabel()
{
    if (!m_label_name_collapsed || !ui) {
        return;
    }
    const QString family = ui->comboBox_family->currentText();
    const QString style  = ui->comboBox_style->currentText();
    const int pt         = getCurrentFontSize();

    QStringList parts;
    if (!family.isEmpty()) {
        parts << family;
    }
    if (!style.isEmpty()) {
        QString style_text = style;
        bool got_axis      = false;
        if (m_axes_bar && !m_axes_bar->isHidden()) {
            const auto &axes
                = m_inspector.inspectFace(m_face_index).variable_axes;
            for (const FontVariableAxisInfo &ax : axes) {
                if (ax.tag == QByteArrayLiteral("wght")) {
                    const float w = m_axes_bar->axisValues().value(
                        ax.tag, ax.default_value);
                    style_text = QStringLiteral("%1 (%2)").arg(
                        style, QString::number(qRound(w)));
                    got_axis = true;
                    break;
                }
            }
        }
        if (!got_axis) {
            if (const int weight = QFontDatabase::weight(family, style);
                weight != -1) {
                style_text = QStringLiteral("%1 (%2)").arg(
                    style, QString::number(weight));
            }
        }
        parts << style_text;
    }
    if (pt > 0) {
        parts << QStringLiteral("%1 pt").arg(pt);
    }
    m_label_name_collapsed->setText(parts.join(QStringLiteral(" · ")));
}

void FontWidget::updateInfo()
{
    const QString name  = ui->comboBox_family->currentText();
    const QString style = ui->comboBox_style->currentText();

    const int char_count
        = m_inspector.isValid() ? m_inspector.faceCodePoints(m_face_index).size()
                                : (m_wnd_char ? m_wnd_char->getCharCount() : 0);
    QString text = QString::number(char_count);
    text += " Characters\n";

    const auto weight = QFontDatabase::weight(name, style);
    if (weight != -1) {
        text += "Weight: " + QString::number(weight) + "\n";
    }

    if (QFontDatabase::isFixedPitch(name, style)) {
        text += "Fixed Pitch\n";
    }

    QString line;
    if (QFontDatabase::isScalable(name, style)) {
        line += "Scalable, ";
    }
    if (QFontDatabase::isSmoothlyScalable(name, style)) {
        line += "Smoothly Scalable, ";
    }
    if (QFontDatabase::isBitmapScalable(name, style)) {
        line += "Bitmap Scalable, ";
    }
    if (!line.isEmpty()) {
        line = line.trimmed();
        line = line.left(line.size() - 1);
        text.append(line + "\n");
    }

    if (QFontDatabase::isPrivateFamily(name)) {
        text += "Private Family\n";
    }

    const auto ws = QFontDatabase::writingSystems(name);
    line.clear();
    for (auto i : ws) {
        line.append(QFontDatabase::writingSystemName(i));
        line.append(", ");
    }
    if (!line.isEmpty()) {
        line = line.trimmed();
        if (line.endsWith(",")) {
            line = line.left(line.size() - 1);
        }
        text.append(line + "\n");
    }

    ui->label_info->setText(text.trimmed());
}

void FontWidget::onTabChanged()
{
    // Switching tabs is a chrome interaction: leaving the caret in a hidden
    // editor would keep the host's editing guard armed.
    clearViewerFocus();

    auto cur_wnd = ui->tabWidget->currentWidget();
    if (cur_wnd != m_tab_text && m_tab_text) {
        m_tab_text->closeAaPanel();
    }
    if (cur_wnd == m_tab_text) {
        if (m_text_dirty) {
            refreshTextWidgetsFromFont();
            m_text_dirty = false;
        }
        else {
            updateTabTextPreview();
        }
        if (m_tab_text) {
            m_tab_text->syncStackIndex();
        }
    }
    else if (cur_wnd == m_tab_sample) {
        if (m_sample_dirty && m_tab_sample) {
            m_tab_sample->setSampleFont(
                buildPreviewFont(getCurrentFontSize(), PreviewFontRole::Sample));
            m_sample_dirty = false;
        }
        else {
            updateSamplePreview();
        }
    }
    else if (cur_wnd == m_tab_char && m_char_dirty) {
        if (m_chars_tab) {
            m_chars_tab->updateFont(
                buildPreviewFont(getCurrentFontSize(), PreviewFontRole::Glyph));
        }
        m_char_dirty = false;
    }
    else if (cur_wnd == m_tab_gi && m_gi_dirty) {
        refreshGlyphInspectorFromFont();
        m_gi_dirty = false;
    }
}

void FontWidget::refreshTextWidgetsFromFont()
{
    if (!m_tab_text) {
        return;
    }
    const int pt = getCurrentFontSize();
    m_tab_text->applyFont(buildPreviewFont(pt, PreviewFontRole::Text), pt);
}

FontWidget::GlyphInspectData FontWidget::inspectGlyphData(quint32 cp)
{
    GlyphInspectData data;
    const auto &axis_vals = m_axes_bar ? m_axes_bar->axisValues()
                                       : QHash<QByteArray, float>{};
    data.deco = m_inspector.decorationMetrics(m_face_index, axis_vals);
    if (cp != 0) {
        data.outline = m_inspector.glyphOutline(m_face_index, cp, axis_vals);
        data.glyph_name
            = m_inspector.glyphIdentity(m_face_index, cp).glyph_name;
    }
    return data;
}

void FontWidget::updateGlyphInspectorFont(const QFont &glyph_font)
{
    if (!m_wnd_gi) {
        return;
    }
    const quint32 cp = m_wnd_gi->hasSelection()
                           ? m_wnd_gi->currentCodePoint()
                           : 0u;
    const GlyphInspectData d = inspectGlyphData(cp);
    m_wnd_gi->updateFont(glyph_font, d.deco, d.outline, d.glyph_name);
}

void FontWidget::refreshGlyphInspectorFromFont()
{
    if (!m_wnd_gi) {
        return;
    }
    updateGlyphInspectorFont(
        buildPreviewFont(getCurrentFontSize(), PreviewFontRole::Glyph));
}

void FontWidget::refreshActiveTabFromFeatures()
{
    m_text_dirty = true;

    auto cur_wnd = ui->tabWidget->currentWidget();
    if (cur_wnd == m_tab_text) {
        refreshTextWidgetsFromFont();
        m_text_dirty = false;
    }
}

void FontWidget::setShowOutlinePoints(bool on)
{
    if (m_wnd_gi) {
        m_wnd_gi->setShowOutlinePoints(on);
    }
}

bool FontWidget::isShowOutlinePoints() const
{
    return m_wnd_gi ? m_wnd_gi->isShowOutlinePoints() : false;
}

void FontWidget::setCharCellSizeDp(int cell_dp)
{
    if (m_chars_tab) {
        m_chars_tab->setCellSizeDp(cell_dp, false);
    }
}

int FontWidget::charCellSizeDp() const
{
    return m_chars_tab ? m_chars_tab->cellSizeDp()
                       : CharacterWidget::kDefCellSizeDp;
}

QStringList FontWidget::hiddenGuideIds() const
{
    return m_wnd_gi ? m_wnd_gi->hiddenGuideIds() : QStringList{};
}

void FontWidget::setHiddenGuideIds(const QStringList &ids)
{
    if (m_wnd_gi) {
        m_wnd_gi->setHiddenGuideIds(ids);
    }
}

void FontWidget::setFeatureEnabled(const QByteArray &tag, bool enabled)
{
    if (m_tab_text) {
        m_tab_text->setFeatureEnabled(tag, enabled);
        updateResetFeatureActionState();
    }
}

bool FontWidget::isFeatureEnabled(const QByteArray &tag) const
{
    return m_tab_text ? m_tab_text->isFeatureEnabled(tag) : false;
}

QVector<QPushButton *> FontWidget::featurePillButtons() const
{
    return m_tab_text ? m_tab_text->featurePillButtons()
                      : QVector<QPushButton *>{};
}

QHash<QByteArray, bool> FontWidget::featureOverrides() const
{
    static const QHash<QByteArray, bool> kEmpty;
    return m_tab_text ? m_tab_text->openTypeOverrides() : kEmpty;
}

void FontWidget::refreshActiveTabFromAxes()
{
    m_text_dirty   = true;
    m_sample_dirty = true;
    m_char_dirty   = true;
    m_gi_dirty     = true;

    auto cur_wnd = ui->tabWidget->currentWidget();
    if (cur_wnd == m_tab_text) {
        refreshTextWidgetsFromFont();
        m_text_dirty = false;
    }
    else if (cur_wnd == m_tab_sample) {
        if (m_tab_sample) {
            m_tab_sample->setSampleFont(
                buildPreviewFont(getCurrentFontSize(), PreviewFontRole::Sample));
        }
        m_sample_dirty = false;
    }
    else if (cur_wnd == m_tab_char) {
        if (m_chars_tab) {
            m_chars_tab->updateFont(
                buildPreviewFont(getCurrentFontSize(), PreviewFontRole::Glyph));
        }
        m_char_dirty = false;
    }
    else if (cur_wnd == m_tab_gi) {
        refreshGlyphInspectorFromFont();
        m_gi_dirty = false;
    }
    updateCollapsedLabel();
}

void FontWidget::updateTabTextPreview()
{
    if (!m_tab_text || !m_wnd_char) {
        return;
    }
    const int pt            = getCurrentFontSize();
    const QFont text_font   = buildPreviewFont(pt, PreviewFontRole::Text);
    const QFont sample_font = buildPreviewFont(pt, PreviewFontRole::Sample);
    const QFont glyph_font  = buildPreviewFont(pt, PreviewFontRole::Glyph);

    m_tab_text->applyFont(text_font, pt);
    m_text_dirty = false;

    if (ui->tabWidget->currentWidget() == m_tab_char) {
        if (m_chars_tab) {
            m_chars_tab->updateFont(glyph_font);
        }
        m_char_dirty = false;
    }
    else {
        m_char_dirty = true;
    }

    if (m_wnd_gi) {
        updateGlyphInspectorFont(glyph_font);
        m_gi_dirty = false;
    }
    // The Sample tab shares the family/style/size/variable-axis state without
    // Text tab OpenType overrides or letter spacing.
    if (m_tab_sample) {
        m_tab_sample->setSampleFont(sample_font);
        m_sample_dirty = false;
    }
}

void FontWidget::updateSamplePreview()
{
    if (!m_tab_sample) {
        return;
    }
    m_tab_sample->updateSample(
        ui->comboBox_family->currentText(),
        buildPreviewFont(getCurrentFontSize(), PreviewFontRole::Sample),
        m_inspector.probeSampleForFace(m_face_index));
    m_sample_dirty = false;
}

void FontWidget::copy()
{
    qApp->setOverrideCursor(Qt::BusyCursor);

    QPixmap pix;
    auto cur_tab    = ui->tabWidget->currentWidget();
    const qreal dpr = qMax(m_dpr, devicePixelRatioF());

    if (cur_tab == m_tab_text && m_tab_text) {
        pix = m_tab_text->renderPixmap(dpr);
    }
    else if (cur_tab == m_tab_sample && m_tab_sample) {
        pix = m_tab_sample->renderPixmap(dpr);
    }
    else if (cur_tab == m_tab_gi) {
        if (!m_wnd_gi || !m_wnd_gi->hasSelection()) {
            qApp->restoreOverrideCursor();
            emit sigToast("Please select a character first");
            return;
        }
        pix = m_wnd_gi->getPreviewPixmap();
        if (pix.isNull()) {
            qprintt << __FUNCTION__ << "glyph inspector pix.isNull";
            qApp->restoreOverrideCursor();
            emit sigToast("Failed to copy preview");
            return;
        }
    }
    else if (m_wnd_char) {
        // Characters tab
        pix = m_wnd_char->getSelectedCharPix();
        if (pix.isNull()) {
            qprintt << __FUNCTION__ << "pix.isNull";
            qApp->restoreOverrideCursor();
            emit sigToast("Please select a character first");
            return;
        }
    }

    if (pix.isNull()) {
        qApp->restoreOverrideCursor();
        emit sigToast("Failed to copy preview");
        return;
    }

    qApp->clipboard()->setPixmap(pix);
    qApp->restoreOverrideCursor();
}

void FontWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange
        || event->type() == QEvent::ApplicationPaletteChange) {
        updateTheme(m_theme);
    }
}

bool FontWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Wheel) {
        auto *wheel = static_cast<QWheelEvent *>(event);
        if (watched == ui->comboBox_family || watched == ui->comboBox_style
            || watched == ui->comboBox_sz) {
            // Steppers, sliders and combos change their value on a hover wheel
            // even without focus; only swallowing the event keeps them stable.
            wheel->accept();
            return true;
        }

        if (watched == m_wnd_char) {
            if (wheel->modifiers() != Qt::ControlModifier) {
                return false;
            }
            if (wheel->angleDelta().y() == 0) {
                wheel->accept();
                return true;
            }
            if (m_chars_tab) {
                m_chars_tab->stepCellSize(wheel->angleDelta().y() > 0 ? 4 : -4);
            }
            clearViewerFocus();
            wheel->accept();
            return true;
        }
        return false;
    }
    return QWidget::eventFilter(watched, event);
}

void FontWidget::zoomSize(int steps)
{
    if (steps == 0) {
        return;
    }
    if (ui && ui->tabWidget && ui->tabWidget->currentWidget() == m_tab_char
        && m_chars_tab) {
        m_chars_tab->stepCellSize(steps * 4);
        return;
    }
    const int count = ui->comboBox_sz->count();
    if (count == 0) {
        return;
    }
    const int current = ui->comboBox_sz->currentIndex();
    const int index   = qBound(0, current + steps, count - 1);
    if (index != current) {
        ui->comboBox_sz->setCurrentIndex(index);
    }
}

void FontWidget::updateDPR(qreal r)
{
    m_dpr = fontui::clampDpr(r);
    updateLayoutMetrics();
    updateTheme(m_theme);
}

void FontWidget::setCurrentText(const QString &t)
{
    if (m_tab_text) {
        m_tab_text->setCurrentText(t);
    }
}

QString FontWidget::getCurrentText() const
{
    return m_tab_text ? m_tab_text->currentText() : QString();
}

QString FontWidget::getLastEditedText() const
{
    return m_tab_text ? m_tab_text->lastEditedText() : QString();
}

void FontWidget::setCurrentFontSize(int s)
{
    int size = s;
    if (size < 1 || size > 999) {
        size = g_def_pt;
    }

    int index = ui->comboBox_sz->findText(QString::number(size));
    if (index == -1) {
        index = ui->comboBox_sz->count();
        for (int i = 0; i < ui->comboBox_sz->count(); ++i) {
            if (ui->comboBox_sz->itemText(i).toInt() > size) {
                index = i;
                break;
            }
        }
        ui->comboBox_sz->insertItem(index, QString::number(size));
    }
    ui->comboBox_sz->setCurrentIndex(index);
}

int FontWidget::getCurrentFontSize() const
{
    return ui->comboBox_sz->currentText().toInt();
}

void FontWidget::updateTheme(int theme)
{
    m_theme = theme;

    if (m_btn_copy && m_btn_svg) {
        const qreal ui_dpr  = m_dpr;
        const qreal pix_dpr = qMax(ui_dpr, devicePixelRatioF());

        // Match Host compact action-chip proportions (22*dpr button, 14*dpr
        // Lucide SVG icon) so corner icons fit cleanly within the tab bar.
        const int btn_sz  = fontui::dp(22, ui_dpr);
        const int icon_sz = fontui::dp(14, ui_dpr);

        const QColor color = palette().color(QPalette::WindowText);
        auto icon_copy     = fontui::svgIcon(fontui::kSvgCopyImage, color,
                                             icon_sz, pix_dpr);
        auto icon_svg      = fontui::svgIcon(fontui::kSvgSaveSvg, color,
                                             icon_sz, pix_dpr);

        m_btn_copy->setFixedSize(btn_sz, btn_sz);
        m_btn_copy->setIconSize(QSize(icon_sz, icon_sz));
        m_btn_copy->setIcon(icon_copy);

        m_btn_svg->setFixedSize(btn_sz, btn_sz);
        m_btn_svg->setIconSize(QSize(icon_sz, icon_sz));
        m_btn_svg->setIcon(icon_svg);

        if (m_btn_toggle) {
            m_btn_toggle->setFixedSize(btn_sz, btn_sz);
            m_btn_toggle->setIconSize(QSize(icon_sz, icon_sz));
            auto icon_toggle = fontui::svgIcon(
                m_is_collapsed ? fontui::kSvgChevronDown
                               : fontui::kSvgChevronUp,
                color, icon_sz, pix_dpr);
            m_btn_toggle->setIcon(icon_toggle);
            m_btn_toggle->setToolTip(tooltipWithShortcut(
                m_is_collapsed ? tr("Expand") : tr("Collapse"),
                FontShortcutAction::ToggleCollapse));
        }
        if (m_label_name_collapsed) {
            const int pad_x = btn_sz + fontui::dp(8, ui_dpr);
            m_label_name_collapsed->setContentsMargins(pad_x, 0, pad_x, 0);
        }
    }

    if (m_wnd_gi) {
        m_wnd_gi->updateTheme(theme);
    }
    if (m_tab_sample) {
        m_tab_sample->refreshThemeVisuals();
    }
    if (m_chars_tab) {
        m_chars_tab->update();
    }
    if (m_tab_text) {
        m_tab_text->applyInvertPreview();
        m_tab_text->updateWaterfall();
        if (auto *fb = m_tab_text->featuresBar()) {
            fb->applyPalette();
        }
    }
}

bool FontWidget::saveAsSvgTo(const QString &path, QString *error_out)
{
    auto fail = [error_out](const QString &msg) {
        if (error_out) {
            *error_out = msg;
        }
        return false;
    };

    auto cur_tab         = ui->tabWidget->currentWidget();
    const bool is_text   = (cur_tab == m_tab_text);
    const bool is_sample = (cur_tab == m_tab_sample);
    const bool is_gi     = (cur_tab == m_tab_gi);

    QSize sz;
    std::function<void(QPainter &)> draw_fn;
    QColor background;

    if (is_text && m_tab_text) {
        m_tab_text->prepareSvgExport(sz, background, draw_fn);
    }
    else if (is_sample && m_tab_sample) {
        m_tab_sample->prepareSvgExport(sz, background, draw_fn);
    }
    else if (is_gi) {
        if (!m_wnd_gi || !m_wnd_gi->hasSelection()) {
            return fail(QStringLiteral("Please select a character first"));
        }
        QPixmap preview = m_wnd_gi->getPreviewPixmap();
        if (preview.isNull()) {
            return fail(QStringLiteral("Failed to generate preview"));
        }
        sz      = preview.deviceIndependentSize().toSize();
        draw_fn = [preview](QPainter &p) { p.drawPixmap(0, 0, preview); };
    }
    else if (m_wnd_char) {
        const auto info = m_wnd_char->selectedCharInfo();
        if (info.code_point == 0) {
            return fail(QStringLiteral("Please select a character first"));
        }
        const int side = qMax(64, info.square_size * 2);
        sz             = QSize(side, side);
        QFont ft       = info.font;
        ft.setPixelSize(side / 2);
        draw_fn = [cp = info.code_point, ft, side](QPainter &p) {
            p.setRenderHints(QPainter::Antialiasing
                             | QPainter::TextAntialiasing);
            p.setFont(ft);
            p.drawText(QRect(0, 0, side, side), Qt::AlignCenter,
                       QString::fromUcs4(&cp, 1));
        };
    }

    if (!draw_fn || !sz.isValid() || sz.isEmpty()) {
        return fail(QStringLiteral("Nothing to save"));
    }

    QSvgGenerator gen;
    gen.setFileName(path);
    gen.setSize(sz);
    gen.setViewBox(QRect(QPoint(0, 0), sz));
    // Layouts are measured at the widget's logical DPI; match it so font sizes
    // in the SVG stay proportional to the viewBox instead of 72-DPI scaled.
    gen.setResolution(qMax(72, logicalDpiX()));
    gen.setTitle(ui->comboBox_family->currentText());

    QPainter p(&gen);
    if (!p.isActive()) {
        return fail(QStringLiteral("Failed to save: ") + path);
    }
    if (background.isValid()) {
        p.fillRect(QRect(QPoint(0, 0), sz), background);
    }
    draw_fn(p);
    p.end();
    return true;
}

void FontWidget::saveAsSvg()
{
    auto cur_tab     = ui->tabWidget->currentWidget();
    const bool is_gi = (cur_tab == m_tab_gi);
    const bool is_char
        = (cur_tab != m_tab_text && cur_tab != m_tab_sample && !is_gi);

    quint32 selected_cp = 0;
    if ((is_gi || is_char) && m_wnd_char) {
        selected_cp = m_wnd_char->selectedCharInfo().code_point;
    }

    const QString dir
        = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString family = ui->comboBox_family->currentText();
    QString stem         = family;
    if (selected_cp != 0) {
        stem += QLatin1Char('_') + unicode_blocks::formatCodePoint(selected_cp);
    }
    stem.replace(QRegularExpression(R"([\/:*?"<>|])"), "_");
    const QString path = dir + "/" + stem + ".svg";

    QString err;
    if (!saveAsSvgTo(path, &err)) {
        emit sigToast(err);
        return;
    }

    QFontMetrics fm(this->font());
    emit sigToast("Saved: "
                  + fm.elidedText(path, Qt::ElideMiddle, this->width() * 0.7));
}

QString FontWidget::tooltipWithShortcut(const QString &base,
                                        FontShortcutAction action) const
{
    const QString hint = m_shortcuts.text(action);
    return hint.isEmpty() ? base : QStringLiteral("%1 (%2)").arg(base, hint);
}

void FontWidget::initActions()
{
    if (!m_actions.isEmpty()) {
        return;
    }
    m_actions.resize(kFontShortcutActionCount);

    auto add_action = [this](FontShortcutAction id,
                             const std::function<void()> &handler) {
        auto *act = new QAction(FontShortcutMap::title(id), this);
        act->setShortcutContext(Qt::WindowShortcut);
        connect(act, &QAction::triggered, this, [handler]() { handler(); });
        addAction(act);
        m_actions[static_cast<int>(id)] = act;
        return act;
    };

    add_action(FontShortcutAction::ToggleCollapse, [this]() {
        setCollapsed(!isCollapsed());
        clearViewerFocus();
    });
    add_action(FontShortcutAction::CopyImage, [this]() {
        copy();
        clearViewerFocus();
    });
    add_action(FontShortcutAction::SaveSvg, [this]() {
        saveAsSvg();
        clearViewerFocus();
    });
    add_action(FontShortcutAction::ToggleInvert, [this]() {
        setInvertPreview(!isInvertPreview());
        clearViewerFocus();
    });
    add_action(FontShortcutAction::ToggleWaterfall, [this]() {
        setWaterfallEnabled(!isWaterfallEnabled());
        clearViewerFocus();
    });
    add_action(FontShortcutAction::ToggleOutlinePoints, [this]() {
        setShowOutlinePoints(!isShowOutlinePoints());
        clearViewerFocus();
    });
    QAction *reset_act
        = add_action(FontShortcutAction::ResetOpenType, [this]() {
              if (m_tab_text) {
                  m_tab_text->resetOpenTypeFeatures();
                  updateResetFeatureActionState();
              }
              clearViewerFocus();
          });
    reset_act->setEnabled(m_tab_text && m_tab_text->hasOpenTypeOverrides());

    add_action(FontShortcutAction::ZoomIn, [this]() { zoomSize(1); });
    add_action(FontShortcutAction::ZoomOut, [this]() { zoomSize(-1); });

    add_action(FontShortcutAction::TabText, [this]() {
        if (ui && ui->tabWidget && m_tab_text) {
            ui->tabWidget->setCurrentWidget(m_tab_text);
        }
    });
    add_action(FontShortcutAction::TabSample, [this]() {
        if (ui && ui->tabWidget && m_tab_sample) {
            ui->tabWidget->setCurrentWidget(m_tab_sample);
        }
    });
    add_action(FontShortcutAction::TabCharacters, [this]() {
        if (ui && ui->tabWidget && m_tab_char) {
            ui->tabWidget->setCurrentWidget(m_tab_char);
        }
    });
    add_action(FontShortcutAction::TabGlyphInspector, [this]() {
        if (ui && ui->tabWidget && m_tab_gi) {
            ui->tabWidget->setCurrentWidget(m_tab_gi);
        }
    });

    applyShortcuts();
}

void FontWidget::loadShortcuts(QSettings &ini)
{
    m_shortcuts.load(ini);
    applyShortcuts();
}

void FontWidget::loadPresets(QSettings &ini)
{
    if (m_tab_text) {
        m_tab_text->loadPresets(ini);
    }
}

void FontWidget::applyShortcuts()
{
    for (int i = 0; i < m_actions.size(); ++i) {
        if (QAction *act = m_actions[i]) {
            act->setShortcut(
                m_shortcuts.sequence(static_cast<FontShortcutAction>(i)));
        }
    }

    if (m_btn_toggle) {
        m_btn_toggle->setToolTip(tooltipWithShortcut(
            m_is_collapsed ? tr("Expand") : tr("Collapse"),
            FontShortcutAction::ToggleCollapse));
    }
    if (m_btn_copy) {
        m_btn_copy->setToolTip(tooltipWithShortcut(
            tr("Copy as Image"), FontShortcutAction::CopyImage));
    }
    if (m_btn_svg) {
        m_btn_svg->setToolTip(tooltipWithShortcut(
            tr("Save as SVG"), FontShortcutAction::SaveSvg));
    }
    if (m_tab_text) {
        m_tab_text->updateShortcutTooltips(
            m_shortcuts.text(FontShortcutAction::ToggleInvert),
            m_shortcuts.text(FontShortcutAction::ToggleWaterfall),
            m_shortcuts.text(FontShortcutAction::ResetOpenType));
    }
    if (m_wnd_gi) {
        m_wnd_gi->setOutlineShortcutHint(
            m_shortcuts.text(FontShortcutAction::ToggleOutlinePoints));
    }
}

QAction *FontWidget::shortcutAction(FontShortcutAction action) const
{
    const int idx = static_cast<int>(action);
    if (idx < 0 || idx >= m_actions.size()) {
        return nullptr;
    }
    return m_actions[idx];
}

QKeySequence FontWidget::shortcutSequence(FontShortcutAction action) const
{
    return m_shortcuts.sequence(action);
}
