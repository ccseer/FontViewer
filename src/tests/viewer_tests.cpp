// Offscreen regression suite for the viewer: text tab editing, type-scale
// waterfall, focus contract, preference persistence and the FreeType-backed
// inspector. Golden fonts ship in tests/fixtures and are baked in through
// FONTVIEWER_FIXTURES_DIR; FONTVIEWER_TEST_FONT and FONTVIEWER_TTC_TEST_FONT
// override them when set, and nothing is downloaded at test time.

#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QInputMethodEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPointer>
#include <QPushButton>
#include <QRawFont>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QSlider>
#include <QSvgRenderer>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtNumeric>

#include <functional>
#include <limits>

#include "bars/opentypefeaturesbar.h"
#include "bars/variableaxesbar.h"
#include "engine/fontinspectorengine.h"
#include "engine/unicodeblocks.h"
#include "fontshortcuts.h"
#include "fontui.h"
#include "fontviewer.h"
#include "fontwidget.h"
#include "gitversion.h"
#include "tabs/charactergridmodel.h"
#include "tabs/characterstab.h"
#include "tabs/characterwidget.h"
#include "tabs/fontpreviewedit.h"
#include "tabs/glyphinspectorwidget.h"
#include "tabs/samplepreviewtab.h"
#include "tabs/textpreviewtab.h"
#include "tabs/waterfallwidget.h"

namespace {
QString fixturePath(const char *name)
{
#ifdef FONTVIEWER_FIXTURES_DIR
    const QString path = QString::fromUtf8(FONTVIEWER_FIXTURES_DIR)
                         + QLatin1Char('/') + QString::fromUtf8(name);
    if (QFileInfo::exists(path)) {
        return path;
    }
#else
    Q_UNUSED(name);
#endif
    return {};
}

QString fixtureFont()
{
    const QString path = qEnvironmentVariable("FONTVIEWER_TEST_FONT");
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        return path;
    }
    return fixturePath("Roboto-Regular.ttf");
}

QString systemTtcFont()
{
    const QString env = qEnvironmentVariable("FONTVIEWER_TTC_TEST_FONT");
    if (!env.isEmpty() && QFileInfo::exists(env)) {
        return env;
    }
    for (const char *candidate : {"C:/Windows/Fonts/msgothic.ttc",
                                  "C:/Windows/Fonts/msyh.ttc"}) {
        const QString path = QString::fromLatin1(candidate);
        if (QFileInfo::exists(path)) {
            return path;
        }
    }
    return {};
}

void sendWheel(QWidget *target, int delta, Qt::KeyboardModifiers mods)
{
    const QPointF pos(target->width() / 2.0, target->height() / 2.0);
    QWheelEvent event(pos, target->mapToGlobal(pos.toPoint()), QPoint(0, 0),
                      QPoint(0, delta), Qt::NoButton, mods, Qt::NoScrollPhase,
                      false);
    QApplication::sendEvent(target, &event);
}

bool everyBlockHasLineHeight(const QTextDocument *doc, qreal expected)
{
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        if (!qFuzzyCompare(block.blockFormat().lineHeight(), expected)) {
            return false;
        }
    }
    return true;
}

class TestFontViewer : public FontViewer {
public:
    TestFontViewer(QString ini_path, QWidget *parent = nullptr)
        : FontViewer(parent), m_ini_path(std::move(ini_path))
    {
    }

protected:
    QString getIniPath() const override
    {
        return m_ini_path;
    }

private:
    QString m_ini_path;
};

class EscapeProbeHost : public QWidget {
public:
    int esc_key_press_count = 0;

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape) {
            ++esc_key_press_count;
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }
};

QPalette makeThemePalette(bool dark)
{
    QPalette pal;
    const QColor window = dark ? QColor(0x12, 0x12, 0x12) : QColor(0xFF, 0xFF, 0xFF);
    const QColor base   = dark ? QColor(0x16, 0x16, 0x16) : QColor(0xFF, 0xFF, 0xFF);
    const QColor text   = dark ? QColor(0xEC, 0xEF, 0xF1) : QColor(0x21, 0x21, 0x21);
    const QColor hi     = dark ? QColor(0x00, 0x77, 0xB6) : QColor(0x00, 0x6F, 0xA8);
    pal.setColor(QPalette::All, QPalette::Window, window);
    pal.setColor(QPalette::All, QPalette::WindowText, text);
    pal.setColor(QPalette::All, QPalette::Base, base);
    pal.setColor(QPalette::All, QPalette::Text, text);
    pal.setColor(QPalette::All, QPalette::Highlight, hi);
    pal.setColor(QPalette::All, QPalette::HighlightedText, Qt::white);
    return pal;
}

bool hasPixelNearColor(const QImage &img, const QColor &target, int tol = 18)
{
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = img.pixelColor(x, y);
            if (qAbs(c.red() - target.red()) <= tol
                && qAbs(c.green() - target.green()) <= tol
                && qAbs(c.blue() - target.blue()) <= tol) {
                return true;
            }
        }
    }
    return false;
}
}  // namespace

class ViewerTest : public QObject {
    Q_OBJECT

private slots:
    void escReleaseRunsAfterTheEventLoop();
    void escOverridesHostShortcutAndPreservesIme();
    void ctrlWheelAsksForZoomOnly();
    void pasteIsTruncatedWithoutSplittingSurrogates();
    void pasteUndoesInOneStep();
    void imeEscDoesNotPropagateToParent();
    void lineHeightReachesEveryBlock();
    void lineHeightPassUndoesAsOneStep();
    void setPreviewTextClearsUndoStack();
    void waterfallSizesAreSortedAndDeduplicated();
    void waterfallFiltersNonPositiveSizes();
    void waterfallEmptySizesAndLargeLabelFontRelayout();
    void waterfallClickReportsRow();
    void waterfallSplitsBareAndCtrlWheel();
    void waterfallInvertedMutedLabelContrastsWithBase();
    void waterfallRefreshFontUpdatesLayout();

    void fontSizeOutOfRangeFallsBack();
    void nonFiniteSpacingMetricsFallBack();
    void presetKeepsTheEditedBuffer();
    void sampleTabSharesThePointSize();
    void spacingSliderStepsByHalfPixel();
    void chromeControlsClearEditorFocus();
    void initialFocusAndTabStayOutsideViewer();
    void hoverWheelIsSwallowedByCombosAndSliders();
    void narrowToolbarMovesMetricsToNonModalAaPanel();
    void deferredShowScrollsWaterfallToCurrentSize();
    void widgetPaletteDrivesInvertPreview();
    void svgExportRendersAtWidgetDpi();
    void copyAndSvgMatchThemeAndInvertColorsWithoutFocusRing();
    void fontViewerIniPersistenceAndInvalidFontLifecycle();

    void inspectorEngineParsesStaticAndVariableFixtures();
    void inspectorEngineTracksMvarDecorationMetrics();
    void inspectorEngineHandlesMissingPostTable();
    void inspectorEngineDecodesMacRomanNameRecords();
    void inspectorEngineResolvesTtcAndLoadFileInfoLatches();
    void inspectorEngineParsesSyntheticTtcCollection();
    void variableAxesBarControlsFocusWheelAndReset();
    void namedInstanceSyncsOrPreservesAxisCoords();
    void variableAxisDragDefersHiddenCharactersRebuild();
    void glyphInspectorBandsNoOverlapAndNoHorizontalScroll();

    void openTypeFeatureParserGroupsPriorityAndParams();
    void openTypePillsIsolationSpacingResetAndNonPersistence();
    void glyphOutlineExtractionMatchesQtAndVariableAxes();
    void glyphInspectorShowOutlinePointsUiAndIniPersistence();
    void characterWidgetHandlesUninitializedFontSafely();
    void characterWidgetLargePointSizeDoesNotOverflowSquareSize();
    void openTypeFeatureParserDeduplicatesRepeatedScriptAndLangTables();
    void openTypeFeatureParserBoundsOverlappingScriptAndLangTables();
    void inspectorEngineHandlesLargeNameAliasTableWithoutQuadraticScan();
    void inspectorEngineClampsExtremeVarAxisFixedConversion();
    void glyphPreviewHandlesExtremeContourEndWithoutOverflow();
    void glyphPreviewSkipsChordBetweenAdjacentOffCubicHandles();
    void tabWidgetHasSingleGridEntryAndNoBlankGapOnStaticFont();
    void shortcutsSeedEmptyAndMissingIniAndAllowOverride();
    void shortcutsExecuteActionsWhileFocusedEditorAcceptsText();
    void getContentSizeIgnoresPluginCmdAndScalesDpr();
    void glyphInspectorSmallDescentAndNarrowViewportNoOverlapOrOverflow();
    void uiUsesDefaultCursorNoCjkPresetAndGroupsTypography();
    void featurePillsLightModeContrastAndCheckedIndicator();
    void dprScalingUpdatesLayoutMetricsAndIcons();
    void iniPresetsSeedLoadElideAndPreserveUserText();
    void toolbarReflowsWhenResetTogglesAndAvoidsDanglingPresetSeparator();
    void openTypeFeatureParserBoundsEmptyScriptLangAndOobOffsets();
    void characterWidgetTooltipEscapesHtmlAndHidesOnEmptySpace();
    void paletteChangeUpdatesToolbarIconsSeparatorAndEmptyLabel();
    void customPresetDoesNotWidenDropdownOrSplitSurrogateTitle();
    void waterfallScrollsToNewlyInsertedLargeCustomSizeWhileVisible();
    void customShortcutCannotBreakFixedZoomShortcuts();
    void glyphGuidePillsToggleCanvasAndNarrowViewport();
    void glyphGuideHideAllShowAllStateMachine();
    void glyphGuideIniPersistenceAndLosslessPreservation();
    void openTypeDrawerSampleIsolationAndFlowSeparators();
    void openTypeMutexGroupsAndSingleSignal();
    void flowLayoutWrapsNextGroupWhenSeparatorOverflowsBoundary();
    void sliderBadgesStayCloserToOwnSliderAndDrawerUsesClear();
    void charactersTabAdaptiveColumnsAndScrollReachability();
    void charactersNonBmpFullPipelineAndOverflowCount();
    void charactersFilterPrecedenceBlockAndFocusYield();
    void charactersSelectionStripAndCellSizeIniPersistence();
    void charactersMultiFaceTtcEnumeration();
    void charactersOptimizationsAndStripElide();
    void glyphGuideToggleAnimationAndImmediateLogicalCache();
    void charactersGlyphNameLookupAndKeyboardNavigation();
    void sampleTabScriptSectionsCopyAndIsolation();
    void charactersHostContractToolbarWrapAndSelectionBounds();
    void characterGridModelHexWordLookupAndUnassigned();
    void glyphInspectorLongNameOverflowFontClearAndEngineEdgeCases();
    void reviewFixesSampleOffTabPuaAndHiDpiScaling();
    void reviewFixesCharactersMenuLifetimeSliderFocusAndLazyNames();
    void reviewFixesHostPixelFontsNarrowBoundsAndSvgExportLifetime();
    void p1OffTabClearSelectionStripAndP2GlyphIdReset();
    void tabHeightForWidthDoesNotInflateMinimumHeight();
};

void ViewerTest::escReleaseRunsAfterTheEventLoop()
{
    FontPreviewEdit edit;
    edit.show();
    QVERIFY(QTest::qWaitForWindowExposed(&edit));
    edit.setFocus(Qt::MouseFocusReason);
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget *>(&edit));

    // The host resolves its Esc action from a queued call; releasing focus
    // synchronously would race it, so the widget must still hold focus here.
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&edit, &esc);
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget *>(&edit));

    QTRY_VERIFY(QApplication::focusWidget() != &edit);
}

void ViewerTest::escOverridesHostShortcutAndPreservesIme()
{
    QWidget host;
    auto *edit = new FontPreviewEdit(&host);
    edit->resize(300, 120);
    host.resize(320, 160);

    int host_esc_count = 0;
    auto *esc_action  = new QAction(&host);
    esc_action->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(esc_action, &QAction::triggered, &host,
            [&host_esc_count]() { ++host_esc_count; });
    host.addAction(esc_action);

    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    edit->setFocus(Qt::MouseFocusReason);
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget *>(edit));

    // During IME composition, Escape must not trigger the host window action
    // and must keep focus in the editor.
    QInputMethodEvent preedit(QStringLiteral("ni"), {});
    QApplication::sendEvent(edit, &preedit);
    QTest::keyClick(edit, Qt::Key_Escape);
    QApplication::processEvents();
    QCOMPARE(host_esc_count, 0);
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget *>(edit));

    // Commit/clear preedit, then Escape must still override the host QAction
    // on the first press and release focus cleanly.
    QInputMethodEvent end_preedit;
    QApplication::sendEvent(edit, &end_preedit);
    QTest::keyClick(edit, Qt::Key_Escape);
    QCOMPARE(host_esc_count, 0);
    QTRY_VERIFY(QApplication::focusWidget() != edit);

    // Once focus has left the editor, the next Escape reaches the host action.
    QTest::keyClick(&host, Qt::Key_Escape);
    QCOMPARE(host_esc_count, 1);
}

void ViewerTest::ctrlWheelAsksForZoomOnly()
{
    FontPreviewEdit edit;
    edit.show();
    QSignalSpy spy(&edit, &FontPreviewEdit::sigZoomRequested);

    sendWheel(edit.viewport(), 120, Qt::ControlModifier);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 1);

    sendWheel(edit.viewport(), -120, Qt::ControlModifier);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toInt(), -1);

    // A bare wheel belongs to the scroll area, not to the zoom path.
    sendWheel(edit.viewport(), 120, Qt::NoModifier);
    QCOMPARE(spy.count(), 2);
}

void ViewerTest::pasteIsTruncatedWithoutSplittingSurrogates()
{
    FontPreviewEdit edit;
    edit.show();
    QSignalSpy toast(&edit, &FontPreviewEdit::sigToast);

    QApplication::clipboard()->setText(
        QString(FontPreviewEdit::kMaxPasteChars + 500, QLatin1Char('a')));
    edit.paste();
    QCOMPARE(edit.toPlainText().size(), FontPreviewEdit::kMaxPasteChars);
    QCOMPARE(toast.count(), 1);

    // 9999 characters followed by a surrogate pair: cutting at exactly 10000
    // would leave a lone high surrogate behind.
    QString text(9999, QLatin1Char('a'));
    text += QChar(0xD83D);
    text += QChar(0xDE00);
    text += QStringLiteral("tail");
    edit.clear();
    QApplication::clipboard()->setText(text);
    edit.paste();
    const QString pasted = edit.toPlainText();
    QCOMPARE(pasted.size(), 9999);
    QVERIFY(!pasted.at(pasted.size() - 1).isHighSurrogate());
}

void ViewerTest::pasteUndoesInOneStep()
{
    FontPreviewEdit edit;
    edit.setPreviewText(QStringLiteral("seed"));
    QApplication::clipboard()->setText(QStringLiteral("alpha\nbeta"));
    edit.paste();
    QCOMPARE(edit.toPlainText(), QStringLiteral("alpha\nbetaseed"));
    QVERIFY(everyBlockHasLineHeight(edit.document(), 140.0));

    edit.document()->undo();
    QCOMPARE(edit.toPlainText(), QStringLiteral("seed"));
}

void ViewerTest::imeEscDoesNotPropagateToParent()
{
    EscapeProbeHost host;
    auto *edit = new FontPreviewEdit(&host);
    edit->resize(280, 120);
    host.resize(300, 140);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    edit->setFocus(Qt::MouseFocusReason);

    QInputMethodEvent preedit(QStringLiteral("zhong"), {});
    QApplication::sendEvent(edit, &preedit);
    QTest::keyClick(edit, Qt::Key_Escape);
    QApplication::processEvents();

    QCOMPARE(host.esc_key_press_count, 0);
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget *>(edit));
}

void ViewerTest::lineHeightReachesEveryBlock()
{
    FontPreviewEdit edit;
    edit.setPlainText(QStringLiteral("one\ntwo\nthree"));
    edit.setLineHeight(2.0);
    QVERIFY(everyBlockHasLineHeight(edit.document(), 200.0));

    edit.setLineHeight(1.0);
    QVERIFY(everyBlockHasLineHeight(edit.document(), 100.0));
}

void ViewerTest::lineHeightPassUndoesAsOneStep()
{
    FontPreviewEdit edit;
    edit.setPlainText(QStringLiteral("one\ntwo\nthree"));
    edit.setLineHeight(2.0);
    edit.document()->clearUndoRedoStacks();

    edit.setLineHeight(1.2);
    QVERIFY(!everyBlockHasLineHeight(edit.document(), 200.0));

    // The whole pass is wrapped in one edit block, so a single undo restores
    // every block instead of peeling one block at a time.
    edit.document()->undo();
    QVERIFY(everyBlockHasLineHeight(edit.document(), 200.0));
}

void ViewerTest::setPreviewTextClearsUndoStack()
{
    FontPreviewEdit edit;
    edit.setPreviewText(QStringLiteral("one\ntwo"));
    QVERIFY(!edit.document()->isUndoAvailable());
    QVERIFY(everyBlockHasLineHeight(edit.document(), 140.0));
}

void ViewerTest::waterfallSizesAreSortedAndDeduplicated()
{
    WaterfallWidget wf;
    wf.setSizes({36, 10, 36, 18, 10});
    QCOMPARE(wf.sizes(), QVector<int>({10, 18, 36}));
}

void ViewerTest::waterfallFiltersNonPositiveSizes()
{
    WaterfallWidget wf;
    wf.setSizes({-4, 0, 24, 12, 0});
    QCOMPARE(wf.sizes(), QVector<int>({12, 24}));
}

void ViewerTest::waterfallEmptySizesAndLargeLabelFontRelayout()
{
    WaterfallWidget wf;
    wf.resize(420, 400);
    wf.setSizes({10, 16});
    QVERIFY(wf.contentSize().height() > 0);

    wf.setSizes({});
    QCOMPARE(wf.contentSize().height(), 0);
    QCOMPARE(wf.sizeHint().height(), 0);

    QFont ui_font = wf.font();
    ui_font.setPointSize(24);
    wf.setFont(ui_font);
    wf.setSizes({10});
    const QFontMetrics fm(wf.font());
    QVERIFY(wf.rowRect(10).height() >= fm.height() + 12.0);
}

void ViewerTest::waterfallClickReportsRow()
{
    WaterfallWidget wf;
    wf.resize(420, 600);
    wf.setSizes({10, 18, 36});
    wf.setPreviewText(QStringLiteral("The quick brown fox"));
    wf.show();
    QApplication::processEvents();

    QSignalSpy spy(&wf, &WaterfallWidget::sigSizeClicked);
    const QRectF row = wf.rowRect(18);
    QVERIFY(row.isValid());

    const QPointF center = row.center();
    QMouseEvent press(QEvent::MouseButtonPress, center,
                      wf.mapToGlobal(center.toPoint()), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&wf, &press);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 18);
}

void ViewerTest::waterfallSplitsBareAndCtrlWheel()
{
    WaterfallWidget wf;
    wf.resize(420, 600);
    wf.setSizes({10, 36});
    wf.show();
    QApplication::processEvents();

    QSignalSpy spy(&wf, &WaterfallWidget::sigZoomRequested);

    sendWheel(&wf, 120, Qt::ControlModifier);
    QCOMPARE(spy.count(), 1);

    // A bare wheel must stay scrollable: it is ignored, not consumed.
    sendWheel(&wf, 120, Qt::NoModifier);
    QCOMPARE(spy.count(), 1);

    // Zero-delta Ctrl+Wheel (e.g. trackpad horizontal pan while Ctrl is down)
    // must still be consumed rather than leaking to the parent scroll area.
    const QPointF center(wf.width() / 2.0, wf.height() / 2.0);
    QWheelEvent zero_ctrl(center, wf.mapToGlobal(center.toPoint()), QPoint(0, 0),
                          QPoint(120, 0), Qt::NoButton, Qt::ControlModifier,
                          Qt::NoScrollPhase, false);
    zero_ctrl.ignore();
    QApplication::sendEvent(&wf, &zero_ctrl);
    QVERIFY(zero_ctrl.isAccepted());
    QCOMPARE(spy.count(), 1);
}

void ViewerTest::waterfallInvertedMutedLabelContrastsWithBase()
{
    WaterfallWidget wf;
    QPalette pal = wf.palette();
    pal.setColor(QPalette::Base, Qt::white);
    pal.setColor(QPalette::Text, Qt::black);
    pal.setColor(QPalette::PlaceholderText, QColor(0, 0, 0, 128));
    wf.setPalette(pal);
    wf.resize(420, 200);
    wf.setSizes({10, 16});
    wf.setCurrentSize(16);
    wf.setPreviewText(QStringLiteral("Sample"));
    wf.setInverted(true);

    const QSize sz = wf.contentSize();
    QImage img(sz, QImage::Format_ARGB32_Premultiplied);
    img.fill(wf.baseColor());
    QPainter painter(&img);
    wf.renderContent(&painter);
    painter.end();

    const QRect r = wf.rowRect(10).toRect();
    QVERIFY(r.isValid());
    const QRgb bg     = wf.baseColor().rgb();
    bool has_contrast = false;
    for (int y = r.top(); y <= r.bottom() && y < img.height(); ++y) {
        for (int x = r.left(); x < qMin(r.left() + 80, img.width()); ++x) {
            if (img.pixel(x, y) != bg) {
                has_contrast = true;
                break;
            }
        }
        if (has_contrast) {
            break;
        }
    }
    QVERIFY(has_contrast);
}

void ViewerTest::waterfallRefreshFontUpdatesLayout()
{
    int scale = 1;
    WaterfallWidget wf;
    wf.setFontProvider([&scale](int pt) {
        QFont f;
        f.setPointSize(pt * scale);
        return f;
    });
    wf.resize(420, 400);
    wf.setSizes({10, 16});
    const int initial_h = wf.contentSize().height();

    scale = 4;
    wf.refreshFont();
    QVERIFY(wf.contentSize().height() > initial_h);
}

void ViewerTest::fontSizeOutOfRangeFallsBack()
{
    const QString font_path = fixtureFont();
    if (font_path.isEmpty()) {
        QSKIP("set FONTVIEWER_TEST_FONT to a font file to run viewer tests");
    }

    FontWidget view;
    QVERIFY(view.init(font_path));
    QCOMPARE(view.getCurrentFontSize(), 16);

    view.setCurrentFontSize(0);
    QCOMPARE(view.getCurrentFontSize(), 16);
    view.setCurrentFontSize(5000);
    QCOMPARE(view.getCurrentFontSize(), 16);

    // A plausible custom size is inserted in ascending order, not appended.
    view.setCurrentFontSize(40);
    QCOMPARE(view.getCurrentFontSize(), 40);
}

void ViewerTest::nonFiniteSpacingMetricsFallBack()
{
    const QString font_path = fixtureFont();
    if (font_path.isEmpty()) {
        QSKIP("set FONTVIEWER_TEST_FONT to a font file to run viewer tests");
    }

    FontWidget view;
    QVERIFY(view.init(font_path));
    view.setLineHeight(qQNaN());
    QVERIFY(qFuzzyCompare(view.getLineHeight(), 1.4));
    view.setLineHeight(qInf());
    QVERIFY(qFuzzyCompare(view.getLineHeight(), 1.4));

    view.setLetterSpacing(qQNaN());
    QVERIFY(qFuzzyIsNull(view.getLetterSpacing()));
    view.setLetterSpacing(-qInf());
    QVERIFY(qFuzzyIsNull(view.getLetterSpacing()));
}

void ViewerTest::presetKeepsTheEditedBuffer()
{
    const QString font_path = fixtureFont();
    if (font_path.isEmpty()) {
        QSKIP("set FONTVIEWER_TEST_FONT to a font file to run viewer tests");
    }

    FontWidget view;
    QVERIFY(view.init(font_path));
    view.setCurrentText(QStringLiteral("edited text"));
    QCOMPARE(view.getLastEditedText(), QStringLiteral("edited text"));

    QComboBox *presets = nullptr;
    for (QComboBox *cb : view.findChildren<QComboBox *>()) {
        if (cb->findText(QStringLiteral("Pangram")) >= 0) {
            presets = cb;
            break;
        }
    }
    QVERIFY(presets != nullptr);

    presets->setCurrentIndex(presets->findText(QStringLiteral("Paragraph")));
    QCOMPARE(view.getLastEditedText(), QStringLiteral("edited text"));
    QVERIFY(view.getCurrentText() != QStringLiteral("edited text"));

    // "Last used" restores the buffer after a preset replaced the editor text.
    presets->setCurrentIndex(presets->findText(QStringLiteral("Last used")));
    QCOMPARE(view.getCurrentText(), QStringLiteral("edited text"));
}

void ViewerTest::sampleTabSharesThePointSize()
{
    const QString font_path = fixtureFont();
    if (font_path.isEmpty()) {
        QSKIP("set FONTVIEWER_TEST_FONT to a font file to run viewer tests");
    }

    FontWidget view;
    QVERIFY(view.init(font_path));
    auto *sample = view.findChild<QLabel *>(QStringLiteral("label_sample"));
    QVERIFY(sample != nullptr);
    QVERIFY(!sample->text().isEmpty());

    view.setCurrentFontSize(30);
    QCOMPARE(qRound(sample->font().pointSizeF()), 30);
}

void ViewerTest::spacingSliderStepsByHalfPixel()
{
    const QString font_path = fixtureFont();
    if (font_path.isEmpty()) {
        QSKIP("set FONTVIEWER_TEST_FONT to a font file to run viewer tests");
    }

    FontWidget view;
    QVERIFY(view.init(font_path));
    QSlider *spacing_slider = nullptr;
    for (QSlider *slider : view.findChildren<QSlider *>()) {
        if (slider->minimum() == -10 && slider->maximum() == 40) {
            spacing_slider = slider;
            break;
        }
    }
    QVERIFY(spacing_slider != nullptr);
    spacing_slider->setValue(1);
    QVERIFY(qFuzzyCompare(view.getLetterSpacing(), 0.5));
}

void ViewerTest::chromeControlsClearEditorFocus()
{
    const QString font_path = fixtureFont();
    if (font_path.isEmpty()) {
        QSKIP("set FONTVIEWER_TEST_FONT to a font file to run viewer tests");
    }

    FontWidget view;
    QVERIFY(view.init(font_path));
    view.resize(800, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto *edit = view.findChild<FontPreviewEdit *>();
    QVERIFY(edit != nullptr);
    edit->setFocus(Qt::MouseFocusReason);
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget *>(edit));

    QPushButton *toggle_btn = nullptr;
    for (QPushButton *btn : view.findChildren<QPushButton *>()) {
        if (btn->toolTip().startsWith(QLatin1String("Collapse"))) {
            toggle_btn = btn;
            break;
        }
    }
    QVERIFY(toggle_btn != nullptr);
    toggle_btn->click();

    QWidget *focus = QApplication::focusWidget();
    QVERIFY(!focus || (focus != &view && !view.isAncestorOf(focus)));

    // Pressing a slider without changing its value still clears editor focus.
    edit->setFocus(Qt::MouseFocusReason);
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget *>(edit));
    QSlider *line_slider = nullptr;
    for (QSlider *slider : view.findChildren<QSlider *>()) {
        if (slider->minimum() == 10 && slider->maximum() == 25) {
            line_slider = slider;
            break;
        }
    }
    QVERIFY(line_slider != nullptr);
    emit line_slider->sliderPressed();
    focus = QApplication::focusWidget();
    QVERIFY(!focus || (focus != &view && !view.isAncestorOf(focus)));
}

void ViewerTest::widgetPaletteDrivesInvertPreview()
{
    const QString font_path = fixtureFont();
    if (font_path.isEmpty()) {
        QSKIP("set FONTVIEWER_TEST_FONT to a font file to run viewer tests");
    }

    FontWidget view;
    QVERIFY(view.init(font_path));

    QPalette pal = view.palette();
    const QColor bg(245, 240, 230);
    const QColor fg(20, 30, 60);
    pal.setColor(QPalette::Base, bg);
    pal.setColor(QPalette::Text, fg);
    view.setPalette(pal);

    view.setInvertPreview(true);

    auto *edit = view.findChild<FontPreviewEdit *>();
    auto *wf   = view.findChild<WaterfallWidget *>();
    QVERIFY(edit != nullptr);
    QVERIFY(wf != nullptr);
    QCOMPARE(edit->palette().color(QPalette::Base), fg);
    QCOMPARE(edit->palette().color(QPalette::Text), bg);
    QCOMPARE(wf->baseColor(), fg);

    const QColor bg2(30, 30, 35);
    const QColor fg2(235, 235, 240);
    pal.setColor(QPalette::Base, bg2);
    pal.setColor(QPalette::Text, fg2);
    view.setPalette(pal);
    QCOMPARE(edit->palette().color(QPalette::Base), fg2);
    QCOMPARE(edit->palette().color(QPalette::Text), bg2);
}

void ViewerTest::svgExportRendersAtWidgetDpi()
{
    const QString font_path = fixtureFont();
    if (font_path.isEmpty()) {
        QSKIP("set FONTVIEWER_TEST_FONT to a font file to run viewer tests");
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    FontWidget view;
    QVERIFY(view.init(font_path));
    view.resize(700, 500);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const QString edit_svg = dir.filePath(QStringLiteral("edit.svg"));
    QString err;
    QVERIFY2(view.saveAsSvgTo(edit_svg, &err), qPrintable(err));
    QSvgRenderer edit_renderer(edit_svg);
    QVERIFY(edit_renderer.isValid());
    QVERIFY(!edit_renderer.defaultSize().isEmpty());

    view.setWaterfallEnabled(true);
    QApplication::processEvents();
    const QString wf_svg = dir.filePath(QStringLiteral("waterfall.svg"));
    QVERIFY2(view.saveAsSvgTo(wf_svg, &err), qPrintable(err));

    QFile f(wf_svg);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray svg_bytes = f.readAll();
    QVERIFY(!svg_bytes.isEmpty());

    QSvgRenderer wf_renderer(svg_bytes);
    QVERIFY(wf_renderer.isValid());
    auto *wf = view.findChild<WaterfallWidget *>();
    QVERIFY(wf != nullptr);
    QCOMPARE(wf_renderer.viewBox().size(), wf->contentSize());
}

void ViewerTest::initialFocusAndTabStayOutsideViewer()
{
    const QString font_path = fixtureFont();
    if (font_path.isEmpty()) {
        QSKIP("set FONTVIEWER_TEST_FONT to a font file to run viewer tests");
    }

    QWidget host;
    auto *lay    = new QVBoxLayout(&host);
    auto *before = new QPushButton(QStringLiteral("Before"), &host);
    auto *view   = new FontWidget(&host);
    auto *after  = new QPushButton(QStringLiteral("After"), &host);
    lay->addWidget(before);
    lay->addWidget(view);
    lay->addWidget(after);

    QVERIFY(view->init(font_path));
    host.resize(800, 600);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

    QWidget *focus = QApplication::focusWidget();
    QVERIFY(!focus || (focus != view && !view->isAncestorOf(focus)));

    auto *edit = view->findChild<FontPreviewEdit *>();
    QVERIFY(edit != nullptr);
    QCOMPARE(edit->focusPolicy(), Qt::ClickFocus);
    auto *filter_edit = view->findChild<QLineEdit *>(
        QStringLiteral("chars_filter_edit"));
    QVERIFY(filter_edit != nullptr);
    QCOMPARE(filter_edit->focusPolicy(), Qt::ClickFocus);
    for (QWidget *child : view->findChildren<QWidget *>()) {
        if (child == edit || child == filter_edit
            || child->window() != view->window()) {
            continue;
        }
        const QByteArray label
            = QStringLiteral("%1 (%2)")
                  .arg(QLatin1String(child->metaObject()->className()),
                       child->objectName())
                  .toUtf8();
        QVERIFY2(child->focusPolicy() == Qt::NoFocus, label.constData());
    }

    before->setFocus();
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget *>(before));
    QTest::keyClick(before, Qt::Key_Tab);
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget *>(after));
    QTest::keyClick(after, Qt::Key_Backtab);
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget *>(before));

    edit->setFocus(Qt::MouseFocusReason);
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget *>(edit));
    edit->clearFocus();
    QVERIFY(QApplication::focusWidget() != edit);
}

void ViewerTest::hoverWheelIsSwallowedByCombosAndSliders()
{
    const QString font_path = fixtureFont();
    if (font_path.isEmpty()) {
        QSKIP("set FONTVIEWER_TEST_FONT to a font file to run viewer tests");
    }

    FontWidget view;
    QVERIFY(view.init(font_path));
    view.resize(800, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    for (QComboBox *cb : view.findChildren<QComboBox *>()) {
        const int before_idx = cb->currentIndex();
        const QPointF pos(cb->width() / 2.0, cb->height() / 2.0);
        QWheelEvent ev(pos, cb->mapToGlobal(pos.toPoint()), QPoint(0, 0),
                       QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                       Qt::NoScrollPhase, false);
        ev.ignore();
        QApplication::sendEvent(cb, &ev);
        QVERIFY(ev.isAccepted());
        QCOMPARE(cb->currentIndex(), before_idx);
    }

    for (QSlider *slider : view.findChildren<QSlider *>()) {
        const int before_val = slider->value();
        const QPointF pos(slider->width() / 2.0, slider->height() / 2.0);
        QWheelEvent ev(pos, slider->mapToGlobal(pos.toPoint()), QPoint(0, 0),
                       QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                       Qt::NoScrollPhase, false);
        ev.ignore();
        QApplication::sendEvent(slider, &ev);
        QVERIFY(ev.isAccepted());
        QCOMPARE(slider->value(), before_val);
    }
}

void ViewerTest::narrowToolbarMovesMetricsToNonModalAaPanel()
{
    const QString font_path = fixtureFont();
    if (font_path.isEmpty()) {
        QSKIP("set FONTVIEWER_TEST_FONT to a font file to run viewer tests");
    }

    FontWidget view;
    QVERIFY(view.init(font_path));
    view.resize(850, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QApplication::processEvents();

    QPushButton *aa_btn = nullptr;
    for (QPushButton *btn : view.findChildren<QPushButton *>()) {
        if (btn->text() == QLatin1String("Aa")) {
            aa_btn = btn;
            break;
        }
    }
    QVERIFY(aa_btn != nullptr);
    QVERIFY(!aa_btn->isVisible());

    QFrame *aa_panel = nullptr;
    for (QFrame *f : view.findChildren<QFrame *>()) {
        if (f->frameShape() == QFrame::StyledPanel
            && f->frameShadow() == QFrame::Raised) {
            aa_panel = f;
            break;
        }
    }
    QVERIFY(aa_panel != nullptr);
    QVERIFY(!aa_panel->isVisible());

    view.resize(384, 500);
    QApplication::processEvents();
    QVERIFY(aa_btn->isVisible());
    QVERIFY(!aa_panel->isVisible());

    QWidget *toolbar = aa_btn->parentWidget();
    QVERIFY(toolbar != nullptr);
    for (QWidget *w : toolbar->findChildren<QWidget *>(
             QString(), Qt::FindDirectChildrenOnly)) {
        if (w->isVisible()) {
            QVERIFY(w->geometry().right() < toolbar->width());
            QVERIFY(w->geometry().left() >= 0);
        }
    }

    QSlider *line_slider = nullptr;
    for (QSlider *s : view.findChildren<QSlider *>()) {
        if (s->minimum() == 10 && s->maximum() == 25) {
            line_slider = s;
            break;
        }
    }
    QVERIFY(line_slider != nullptr);
    QVERIFY(aa_panel->isAncestorOf(line_slider));

    aa_btn->click();
    QApplication::processEvents();
    QVERIFY(aa_panel->isVisible());
    QWidget *focus = QApplication::focusWidget();
    QVERIFY(!focus || (focus != &view && !view.isAncestorOf(focus)));

    // Keep the panel non-modal: typing and value updates still work.
    line_slider->setValue(20);
    QVERIFY(qFuzzyCompare(view.getLineHeight(), 2.0));

    // Resizing narrow enough for Aa to wrap to row 2 must reposition Aa panel
    // after the toolbar relayouts rather than leaving it at stale Y.
    view.resize(210, 500);
    QApplication::processEvents();
    QVERIFY(aa_btn->isVisible());
    QVERIFY(aa_panel->isVisible());
    const int aa_bottom_in_tab
        = aa_btn->mapTo(aa_panel->parentWidget(), QPoint(0, aa_btn->height())).y();
    QVERIFY(aa_panel->geometry().top() >= aa_bottom_in_tab);

    view.resize(850, 600);
    QApplication::processEvents();
    QVERIFY(!aa_btn->isVisible());
    QVERIFY(!aa_panel->isVisible());
    QVERIFY(!aa_panel->isAncestorOf(line_slider));

    // Also verify 384px narrow toolbar with Features button visible does not clip.
    const QString vf_path = fixturePath("Roboto-VariableFont_wdth,wght.ttf");
    if (!vf_path.isEmpty()) {
        FontWidget vf_view;
        QVERIFY(vf_view.init(vf_path));
        vf_view.resize(384, 520);
        vf_view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&vf_view));
        QApplication::processEvents();
        auto *feat_btn = vf_view.findChild<QPushButton *>(
            QStringLiteral("text_features_btn"));
        QVERIFY(feat_btn != nullptr);
        QVERIFY(feat_btn->isVisible());
        QWidget *vf_tb = feat_btn->parentWidget();
        QVERIFY(vf_tb != nullptr);
        vf_view.setFeatureEnabled(QByteArrayLiteral("smcp"), true);
        vf_view.setInvertPreview(true);
        QApplication::processEvents();
        const int narrow_tb_h = vf_tb->height();
        for (QWidget *w : vf_tb->findChildren<QWidget *>(
                 QString(), Qt::FindDirectChildrenOnly)) {
            if (w->isVisible()) {
                QVERIFY(w->geometry().right() < vf_tb->width());
                QVERIFY(w->geometry().left() >= 0);
                QVERIFY(w->geometry().bottom() < vf_tb->height());
            }
        }
        // Widening back from wrapped narrow mode must recover single-row height.
        vf_view.resize(850, 520);
        QApplication::processEvents();
        QVERIFY(vf_tb->height() < narrow_tb_h);
    }
}

void ViewerTest::deferredShowScrollsWaterfallToCurrentSize()
{
    const QString font_path = fixtureFont();
    if (font_path.isEmpty()) {
        QSKIP("set FONTVIEWER_TEST_FONT to a font file to run viewer tests");
    }

    FontWidget view;
    QVERIFY(view.init(font_path));
    view.setCurrentFontSize(72);
    view.setWaterfallEnabled(true);
    QApplication::processEvents();

    view.resize(600, 300);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QApplication::processEvents();

    auto *wf = view.findChild<WaterfallWidget *>();
    QVERIFY(wf != nullptr);
    QVERIFY(wf->parentWidget() != nullptr);
    auto *sa = qobject_cast<QScrollArea *>(wf->parentWidget()->parentWidget());
    QVERIFY(sa != nullptr);
    QTRY_VERIFY(sa->verticalScrollBar()->value() > 0);
    const QRectF row = wf->rowRect(72);
    const int val    = sa->verticalScrollBar()->value();
    QVERIFY(row.bottom() <= val + sa->viewport()->height() + 8);
}

void ViewerTest::copyAndSvgMatchThemeAndInvertColorsWithoutFocusRing()
{
    const QString font_path = fixtureFont();
    if (font_path.isEmpty()) {
        QSKIP("set FONTVIEWER_TEST_FONT to a font file to run viewer tests");
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    FontWidget view;
    QVERIFY(view.init(font_path));
    view.setCurrentText(QStringLiteral("MMMM"));
    view.resize(700, 500);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QApplication::processEvents();

    auto *edit = view.findChild<FontPreviewEdit *>();
    QVERIFY(edit != nullptr);
    edit->setFocus(Qt::MouseFocusReason);

    struct Case {
        bool dark;
        bool invert;
    };
    const Case cases[] = {
        {false, false},
        {true, false},
        {false, true},
        {true, true},
    };
    for (int i = 0; i < 4; ++i) {
        const QPalette pal = makeThemePalette(cases[i].dark);
        view.setPalette(pal);
        view.setInvertPreview(cases[i].invert);
        view.updateTheme(cases[i].dark ? 1 : 0);
        QApplication::processEvents();

        const QColor expected_bg = cases[i].invert
                                       ? pal.color(QPalette::Text)
                                       : pal.color(QPalette::Base);
        const QColor expected_fg = cases[i].invert
                                       ? pal.color(QPalette::Base)
                                       : pal.color(QPalette::Text);

        view.copy();
        const QImage copied = QApplication::clipboard()->pixmap().toImage();
        QVERIFY(!copied.isNull());
        QCOMPARE(copied.pixelColor(0, 0), expected_bg);
        QVERIFY(hasPixelNearColor(copied, expected_fg));

        const QString svg_path
            = dir.filePath(QStringLiteral("edit_%1.svg").arg(i));
        QString err;
        QVERIFY2(view.saveAsSvgTo(svg_path, &err), qPrintable(err));
        QSvgRenderer renderer(svg_path);
        QVERIFY(renderer.isValid());
        QImage svg_img(renderer.viewBox().size(),
                       QImage::Format_ARGB32_Premultiplied);
        svg_img.fill(Qt::transparent);
        QPainter p(&svg_img);
        renderer.render(&p);
        p.end();
        QCOMPARE(svg_img.pixelColor(0, 0), expected_bg);
        QVERIFY(hasPixelNearColor(svg_img, expected_fg));
    }

    // Verify Waterfall export matches theme and inverted colors as well.
    view.setWaterfallEnabled(true);
    QApplication::processEvents();
    for (int i = 0; i < 4; ++i) {
        const QPalette pal = makeThemePalette(cases[i].dark);
        view.setPalette(pal);
        view.setInvertPreview(cases[i].invert);
        view.updateTheme(cases[i].dark ? 1 : 0);
        QApplication::processEvents();

        const QColor expected_bg = cases[i].invert
                                       ? pal.color(QPalette::Text)
                                       : pal.color(QPalette::Base);
        const QColor expected_fg = cases[i].invert
                                       ? pal.color(QPalette::Base)
                                       : pal.color(QPalette::Text);

        view.copy();
        const QImage copied = QApplication::clipboard()->pixmap().toImage();
        QVERIFY(!copied.isNull());
        QCOMPARE(copied.pixelColor(0, 0), expected_bg);
        QVERIFY(hasPixelNearColor(copied, expected_fg));

        const QString wf_svg
            = dir.filePath(QStringLiteral("wf_%1.svg").arg(i));
        QString err;
        QVERIFY2(view.saveAsSvgTo(wf_svg, &err), qPrintable(err));
        QSvgRenderer renderer(wf_svg);
        QVERIFY(renderer.isValid());
        QImage svg_img(renderer.viewBox().size(),
                       QImage::Format_ARGB32_Premultiplied);
        svg_img.fill(Qt::transparent);
        QPainter p(&svg_img);
        renderer.render(&p);
        p.end();
        QCOMPARE(svg_img.pixelColor(0, 0), expected_bg);
        QVERIFY(hasPixelNearColor(svg_img, expected_fg));
    }
    view.setWaterfallEnabled(false);

    // Verify Sample tab dark export uses Window background rather than Base.
    auto *tabs = view.findChild<QTabWidget *>();
    QVERIFY(tabs != nullptr);
    tabs->setCurrentIndex(1);
    const QPalette dark_pal = makeThemePalette(true);
    view.setPalette(dark_pal);
    view.updateTheme(1);
    QApplication::processEvents();

    view.copy();
    const QImage sample_img = QApplication::clipboard()->pixmap().toImage();
    QVERIFY(!sample_img.isNull());
    QCOMPARE(sample_img.pixelColor(0, 0), dark_pal.color(QPalette::Window));
}

void ViewerTest::fontViewerIniPersistenceAndInvalidFontLifecycle()
{
    const QString font_path = fixtureFont();
    if (font_path.isEmpty()) {
        QSKIP("set FONTVIEWER_TEST_FONT to a font file to run viewer tests");
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini_path = dir.filePath(QStringLiteral("fontviewer.ini"));

    // Invalid font path must emit VCV_Error and leave no ini file written.
    {
        ViewOptionsPrivate d;
        d.path = dir.filePath(QStringLiteral("missing.ttf"));
        ViewOptions opts;
        opts.d_ptr = &d;

        TestFontViewer viewer(ini_path);
        QSignalSpy spy(&viewer, &ViewerBase::sigCommand);
        viewer.load(nullptr, &opts);
        QVERIFY(!spy.isEmpty());
        QCOMPARE(spy.last().at(0).toInt(), int(VCT_StateChange));
        QCOMPARE(spy.last().at(1).toInt(), int(VCV_Error));
    }
    QVERIFY(!QFileInfo::exists(ini_path));

    // Invalid font_size=0 in ini falls back to 16 pt and stays untouched on close.
    {
        QSettings seed(ini_path, QSettings::IniFormat);
        seed.setValue(QStringLiteral("font_size"), 0);
        seed.setValue(QStringLiteral("user_text"), QStringLiteral("kept text"));
        seed.sync();
    }
    {
        ViewOptionsPrivate d;
        d.path = font_path;
        ViewOptions opts;
        opts.d_ptr = &d;

        TestFontViewer viewer(ini_path);
        viewer.load(nullptr, &opts);
        auto *fw = viewer.findChild<FontWidget *>();
        QVERIFY(fw != nullptr);
        QCOMPARE(fw->getCurrentFontSize(), 16);

        // Selecting a built-in preset must not clobber user_text on close.
        QComboBox *presets = nullptr;
        for (QComboBox *cb : fw->findChildren<QComboBox *>()) {
            if (cb->findText(QStringLiteral("Paragraph")) >= 0) {
                presets = cb;
                break;
            }
        }
        QVERIFY(presets != nullptr);
        presets->setCurrentIndex(presets->findText(QStringLiteral("Paragraph")));
        fw->setLineHeight(1.9);
        fw->setLetterSpacing(3.5);
        fw->setWaterfallEnabled(true);
        fw->setInvertPreview(true);
    }
    {
        QSettings check(ini_path, QSettings::IniFormat);
        QCOMPARE(check.value(QStringLiteral("font_size")).toInt(), 0);
        QCOMPARE(check.value(QStringLiteral("user_text")).toString(),
                 QStringLiteral("kept text"));
        QVERIFY(qFuzzyCompare(check.value(QStringLiteral("line_height")).toDouble(),
                              1.9));
        QVERIFY(qFuzzyCompare(
            check.value(QStringLiteral("letter_spacing")).toDouble(), 3.5));
        QCOMPARE(check.value(QStringLiteral("waterfall_enabled")).toBool(),
                 true);
        QCOMPARE(check.value(QStringLiteral("invert_preview")).toBool(), true);
    }

    // Reopen and verify all persisted fields reload cleanly.
    {
        ViewOptionsPrivate d;
        d.path = font_path;
        ViewOptions opts;
        opts.d_ptr = &d;

        TestFontViewer viewer(ini_path);
        viewer.load(nullptr, &opts);
        auto *fw = viewer.findChild<FontWidget *>();
        QVERIFY(fw != nullptr);
        QCOMPARE(fw->getCurrentText(), QStringLiteral("kept text"));
        QVERIFY(qFuzzyCompare(fw->getLineHeight(), 1.9));
        QVERIFY(qFuzzyCompare(fw->getLetterSpacing(), 3.5));
        QVERIFY(fw->isWaterfallEnabled());
        QVERIFY(fw->isInvertPreview());
    }
}

void ViewerTest::inspectorEngineParsesStaticAndVariableFixtures()
{
    const QString static_path = fixturePath("Roboto-Regular.ttf");
    const QString roboto_vf   = fixturePath("Roboto-VariableFont_wdth,wght.ttf");
    const QString noto_vf     = fixturePath("NotoSans-VariableFont_wdth,wght.ttf");
    QVERIFY(!static_path.isEmpty());
    QVERIFY(!roboto_vf.isEmpty());
    QVERIFY(!noto_vf.isEmpty());

    FontInspectorEngine engine;
    QVERIFY(engine.openFile(static_path));
    const FontInspectionData &static_data = engine.inspectFace(0);
    QVERIFY(!static_data.name_records.isEmpty());
    QVERIFY(static_data.variable_axes.isEmpty());
    QVERIFY(static_data.named_instances.isEmpty());
    QVERIFY(static_data.decoration_metrics.units_per_em > 0);
    QVERIFY(static_data.decoration_metrics.has_underline);
    QVERIFY(static_data.decoration_metrics.has_strikeout);

    for (const QString &vf_path : {roboto_vf, noto_vf}) {
        QVERIFY(engine.openFile(vf_path));
        const FontInspectionData &vf_data = engine.inspectFace(0);
        QVERIFY(!vf_data.name_records.isEmpty());
        QVERIFY(vf_data.variable_axes.size() >= 2);
        bool has_wght = false;
        bool has_wdth = false;
        for (const FontVariableAxisInfo &ax : vf_data.variable_axes) {
            if (ax.tag == QByteArrayLiteral("wght")) {
                has_wght = true;
                QVERIFY(ax.min_value < ax.max_value);
            }
            if (ax.tag == QByteArrayLiteral("wdth")) {
                has_wdth = true;
                QVERIFY(ax.min_value < ax.max_value);
            }
        }
        QVERIFY(has_wght);
        QVERIFY(has_wdth);
        QVERIFY(!vf_data.named_instances.isEmpty());
    }
}

void ViewerTest::inspectorEngineTracksMvarDecorationMetrics()
{
    const QString noto_vf = fixturePath("NotoSans-VariableFont_wdth,wght.ttf");
    QVERIFY(!noto_vf.isEmpty());

    FontInspectorEngine engine;
    QVERIFY(engine.openFile(noto_vf));

    const FontDecorationMetrics def_deco = engine.decorationMetrics(0);
    QVERIFY(def_deco.units_per_em > 0);
    QVERIFY(def_deco.has_underline);
    QVERIFY(def_deco.has_strikeout);

    // Apply extreme wght then reset back to empty (default) coords and verify
    // MVAR never leaves stale values behind.
    QHash<QByteArray, float> heavy;
    heavy.insert(QByteArrayLiteral("wght"), 900.0f);
    const FontDecorationMetrics heavy_deco = engine.decorationMetrics(0, heavy);
    QVERIFY(heavy_deco.units_per_em == def_deco.units_per_em);
    QVERIFY(heavy_deco.has_underline);
    QVERIFY(heavy_deco.has_strikeout);
    QVERIFY(heavy_deco.strikeout_pos != def_deco.strikeout_pos);

    // Calling decorationMetrics() repeatedly with identical non-default coords
    // must keep the active MVAR deltas instead of clobbering back to baseline.
    const FontDecorationMetrics heavy_repeat
        = engine.decorationMetrics(0, heavy);
    QCOMPARE(heavy_repeat.strikeout_pos, heavy_deco.strikeout_pos);
    QCOMPARE(heavy_repeat.strikeout_size, heavy_deco.strikeout_size);
    QCOMPARE(heavy_repeat.underline_pos, heavy_deco.underline_pos);
    QCOMPARE(heavy_repeat.underline_thick, heavy_deco.underline_thick);

    const FontDecorationMetrics reset_deco = engine.decorationMetrics(0);
    QCOMPARE(reset_deco.underline_pos, def_deco.underline_pos);
    QCOMPARE(reset_deco.underline_thick, def_deco.underline_thick);
    QCOMPARE(reset_deco.strikeout_pos, def_deco.strikeout_pos);
    QCOMPARE(reset_deco.strikeout_size, def_deco.strikeout_size);

    // End-to-end UI flow: drag wght on Text tab -> click glyph on Characters
    // tab -> switch to Glyph Inspector tab must retain heavy MVAR metrics.
    FontWidget view;
    QVERIFY(view.init(noto_vf));
    view.resize(850, 650);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    view.setVariableAxisValue(QByteArrayLiteral("wght"), 900.0f);
    auto *tabs     = view.findChild<QTabWidget *>();
    auto *char_wgt = view.findChild<CharacterWidget *>();
    auto *gi       = view.findChild<GlyphInspectorWidget *>();
    QVERIFY(tabs != nullptr);
    QVERIFY(char_wgt != nullptr);
    QVERIFY(gi != nullptr);

    tabs->setCurrentIndex(2);
    QApplication::processEvents();
    emit char_wgt->selectionChangedCodePoint(quint32('A'));
    QApplication::processEvents();
    tabs->setCurrentIndex(3);
    QApplication::processEvents();

    QCOMPARE(gi->decorationMetrics().strikeout_pos, heavy_deco.strikeout_pos);
}

void ViewerTest::inspectorEngineHandlesMissingPostTable()
{
    const QString static_path = fixturePath("Roboto-Regular.ttf");
    QVERIFY(!static_path.isEmpty());

    QFile src(static_path);
    QVERIFY(src.open(QIODevice::ReadOnly));
    QByteArray sfnt = src.readAll();
    QVERIFY(sfnt.size() >= 12);

    const quint16 num_tables
        = (quint16(uchar(sfnt[4])) << 8) | uchar(sfnt[5]);
    bool renamed_post = false;
    for (quint16 i = 0; i < num_tables && 12 + i * 16 + 4 <= sfnt.size(); ++i) {
        const int off = 12 + i * 16;
        if (sfnt.mid(off, 4) == QByteArrayLiteral("post")) {
            sfnt[off] = 'x';
            renamed_post = true;
            break;
        }
    }
    QVERIFY(renamed_post);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString no_post_path = dir.filePath(QStringLiteral("no_post.ttf"));
    QFile dst(no_post_path);
    QVERIFY(dst.open(QIODevice::WriteOnly));
    QCOMPARE(dst.write(sfnt), sfnt.size());
    dst.close();

    FontInspectorEngine engine;
    QVERIFY(engine.openFile(no_post_path));
    const FontDecorationMetrics deco = engine.decorationMetrics(0);
    QVERIFY(!deco.has_underline);
    QVERIFY(deco.has_strikeout);
}

void ViewerTest::inspectorEngineResolvesTtcAndLoadFileInfoLatches()
{
    const QString static_path = fixturePath("Roboto-Regular.ttf");
    QVERIFY(!static_path.isEmpty());

    // Standalone path-based reading must work independently of Qt registration.
    const auto standalone_records
        = FontInspectorEngine::readNameRecords(static_path);
    QVERIFY(!standalone_records.isEmpty());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini_path = dir.filePath(QStringLiteral("fontviewer.ini"));

    ViewOptionsPrivate d;
    d.path = static_path;
    ViewOptions opts;
    opts.d_ptr = &d;

    TestFontViewer viewer(ini_path);
    viewer.load(nullptr, &opts);
    QSignalSpy spy(&viewer, &ViewerBase::sigCommand);
    static QStringList captured_font_info_logs;
    captured_font_info_logs.clear();
    const QtMessageHandler prev_handler = qInstallMessageHandler(
        [](QtMsgType type, const QMessageLogContext &ctx, const QString &msg) {
            Q_UNUSED(type);
            Q_UNUSED(ctx);
            if (msg.contains(QStringLiteral("font info:"))) {
                captured_font_info_logs.append(msg);
            }
        });
    viewer.loadFileInfo();
    viewer.loadFileInfo();
    qInstallMessageHandler(prev_handler);
    QVERIFY(captured_font_info_logs.isEmpty());
    int append_count = 0;
    for (const auto &call : spy) {
        if (call.at(0).toInt() == int(VCT_AppendProperty)) {
            ++append_count;
        }
    }
    QCOMPARE(append_count, 1);

    const QString ttc_path = systemTtcFont();
    if (!ttc_path.isEmpty()) {
        FontInspectorEngine ttc_engine;
        QVERIFY(ttc_engine.openFile(ttc_path));
        QVERIFY(ttc_engine.faceCount() > 1);
        QVERIFY(!ttc_engine.inspectFace(0).name_records.isEmpty());
        QVERIFY(!ttc_engine.inspectFace(1).name_records.isEmpty());
        const int face1 = ttc_engine.resolveFaceIndex(QString(), QString());
        QVERIFY(face1 >= 0 && face1 < ttc_engine.faceCount());
        if (ttc_path.endsWith(QLatin1String("msgothic.ttc"),
                              Qt::CaseInsensitive)) {
            QCOMPARE(ttc_engine.resolveFaceIndex(
                         QString::fromUtf8("ＭＳ Ｐゴシック"),
                         QString::fromUtf8("標準")),
                     2);
        }
        else if (ttc_path.endsWith(QLatin1String("msyh.ttc"),
                                   Qt::CaseInsensitive)) {
            QCOMPARE(ttc_engine.resolveFaceIndex(
                         QString::fromUtf8("微软雅黑"), QStringLiteral("Regular")),
                     0);
            QCOMPARE(ttc_engine.resolveFaceIndex(
                         QStringLiteral("Microsoft YaHei UI"),
                         QStringLiteral("Regular")),
                     1);
        }
    }
}

void ViewerTest::variableAxesBarControlsFocusWheelAndReset()
{
    const QString static_path = fixturePath("Roboto-Regular.ttf");
    const QString vf_path     = fixturePath("NotoSans-VariableFont_wdth,wght.ttf");
    QVERIFY(!static_path.isEmpty());
    QVERIFY(!vf_path.isEmpty());

    // Static font hides the variable axes bar.
    {
        FontWidget static_view;
        QVERIFY(static_view.init(static_path));
        static_view.resize(800, 600);
        static_view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&static_view));

        auto *axes = static_view.findChild<VariableAxesBar *>();
        QVERIFY(axes == nullptr || !axes->isVisible());
    }

    // Variable font shows axis sliders + Reset buttons, keeps them NoFocus,
    // swallows hover wheel, and resets cleanly.
    FontWidget view;
    QVERIFY(view.init(vf_path));
    view.resize(850, 650);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto *axes = view.findChild<VariableAxesBar *>();
    QVERIFY(axes != nullptr);
    QVector<QPushButton *> resets;
    for (QPushButton *b : axes->findChildren<QPushButton *>()) {
        if (b->text() == QLatin1String("Reset")) {
            QCOMPARE(b->focusPolicy(), Qt::NoFocus);
            QVERIFY(b->isVisible());
            resets.append(b);
        }
    }
    QVERIFY(resets.size() >= 2);

    QSlider *wght_slider = nullptr;
    for (QSlider *s : view.findChildren<QSlider *>()) {
        QCOMPARE(s->focusPolicy(), Qt::NoFocus);
        if (s->minimum() == 0 && s->maximum() >= 200) {
            wght_slider = s;
            break;
        }
    }
    QVERIFY(wght_slider != nullptr);

    const int before_val = wght_slider->value();
    const QPointF center(wght_slider->width() / 2.0,
                         wght_slider->height() / 2.0);
    QWheelEvent ev(center, wght_slider->mapToGlobal(center.toPoint()),
                   QPoint(0, 0), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                   Qt::NoScrollPhase, false);
    ev.ignore();
    QApplication::sendEvent(wght_slider, &ev);
    QVERIFY(ev.isAccepted());
    QCOMPARE(wght_slider->value(), before_val);

    view.setVariableAxisValue(QByteArrayLiteral("wght"), 750.0f);
    QVERIFY(qAbs(view.variableAxisValues().value(QByteArrayLiteral("wght"))
                 - 750.0f)
            < 1.0f);

    view.setVariableAxisValue(QByteArrayLiteral("wght"), qQNaN());
    QVERIFY(qAbs(view.variableAxisValues().value(QByteArrayLiteral("wght"))
                 - 400.0f)
            < 1.0f);

    view.setVariableAxisValue(QByteArrayLiteral("wght"), 750.0f);
    resets.first()->click();
    QVERIFY(qAbs(view.variableAxisValues().value(QByteArrayLiteral("wght"))
                 - 400.0f)
            < 1.0f);
}

void ViewerTest::namedInstanceSyncsOrPreservesAxisCoords()
{
    const QString vf_path = fixturePath("Roboto-VariableFont_wdth,wght.ttf");
    QVERIFY(!vf_path.isEmpty());

    FontWidget view;
    QVERIFY(view.init(vf_path));
    view.resize(850, 650);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QComboBox *style_cb = view.findChild<QComboBox *>(QStringLiteral("comboBox_style"));
    QVERIFY(style_cb != nullptr);

    const int bold_idx = style_cb->findText(QStringLiteral("Bold"));
    QVERIFY(bold_idx >= 0);
    style_cb->setCurrentIndex(bold_idx);
    QApplication::processEvents();
    QVERIFY(qAbs(view.variableAxisValues().value(QByteArrayLiteral("wght"))
                 - 700.0f)
            < 5.0f);

    // Dragging an axis away from Bold and clicking Reset restores Bold (700),
    // not the Regular fvar default (400).
    auto *axes = view.findChild<VariableAxesBar *>();
    QVERIFY(axes != nullptr);
    const auto axis_resets = axes->findChildren<QPushButton *>();
    QVERIFY(!axis_resets.isEmpty());
    QVERIFY(!axis_resets.first()->isEnabled());
    view.setVariableAxisValue(QByteArrayLiteral("wght"), 500.0f);
    QApplication::processEvents();
    QVERIFY(axis_resets.first()->isEnabled());
    axis_resets.first()->click();
    QApplication::processEvents();
    QVERIFY(qAbs(view.variableAxisValues().value(QByteArrayLiteral("wght"))
                 - 700.0f)
            < 5.0f);
    QVERIFY(!axis_resets.first()->isEnabled());

    const int condensed_idx = style_cb->findText(QStringLiteral("Condensed"));
    if (condensed_idx >= 0) {
        style_cb->setCurrentIndex(condensed_idx);
        QApplication::processEvents();
        QVERIFY(qAbs(view.variableAxisValues().value(QByteArrayLiteral("wdth"))
                     - 75.0f)
                < 2.0f);
    }

    // Switching to an unmatched custom/synthetic style must not reset custom coords.
    view.setVariableAxisValue(QByteArrayLiteral("wght"), 633.0f);
    QVERIFY(qAbs(view.variableAxisValues().value(QByteArrayLiteral("wght"))
                 - 633.0f)
            < 1.0f);
    {
        QSignalBlocker blocker(style_cb);
        style_cb->addItem(QStringLiteral("Custom Unmatched Oblique"));
    }
    style_cb->setCurrentIndex(style_cb->count() - 1);
    QApplication::processEvents();
    QVERIFY(qAbs(view.variableAxisValues().value(QByteArrayLiteral("wght"))
                 - 633.0f)
            < 1.0f);
}

void ViewerTest::variableAxisDragDefersHiddenCharactersRebuild()
{
    const QString vf_path = fixturePath("NotoSans-VariableFont_wdth,wght.ttf");
    QVERIFY(!vf_path.isEmpty());

    FontWidget view;
    QVERIFY(view.init(vf_path));
    view.setLetterSpacing(4.5);
    view.resize(850, 650);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto *tabs     = view.findChild<QTabWidget *>();
    auto *edit     = view.findChild<FontPreviewEdit *>();
    auto *char_wgt = view.findChild<CharacterWidget *>();
    QVERIFY(tabs != nullptr);
    QVERIFY(edit != nullptr);
    QVERIFY(char_wgt != nullptr);

    QCOMPARE(tabs->currentIndex(), 0);
    QVERIFY(!char_wgt->isVisible());

    // Glyph role on CharacterWidget is initialized without letter spacing.
    QCOMPARE(char_wgt->currentFont().family(), edit->font().family());
    QVERIFY(qFuzzyIsNull(char_wgt->currentFont().letterSpacing()));
    QVERIFY(!qFuzzyIsNull(edit->font().letterSpacing()));

    const float initial_char_wght
        = char_wgt->currentFont().variableAxisValue("wght");
    QVERIFY(initial_char_wght > 0.0f);

    QSlider *wght_slider = nullptr;
    for (QSlider *s : view.findChildren<QSlider *>()) {
        if (s->minimum() == 0 && s->maximum() >= 200) {
            wght_slider = s;
            break;
        }
    }
    QVERIFY(wght_slider != nullptr);
    wght_slider->setValue(wght_slider->maximum());
    QTest::qWait(40);
    QApplication::processEvents();

    // Active Text tab updates to max wght, while hidden Characters tab defers.
    QVERIFY(edit->font().variableAxisValue("wght") > 800.0f);
    QCOMPARE(char_wgt->currentFont().variableAxisValue("wght"),
             initial_char_wght);

    // Entering Characters tab applies the deferred font update with Glyph role.
    tabs->setCurrentIndex(2);
    QApplication::processEvents();
    QVERIFY(char_wgt->currentFont().variableAxisValue("wght") > 800.0f);
    QVERIFY(qFuzzyIsNull(char_wgt->currentFont().letterSpacing()));
}

void ViewerTest::glyphInspectorBandsNoOverlapAndNoHorizontalScroll()
{
    const QString vf_path = fixturePath("NotoSans-VariableFont_wdth,wght.ttf");
    QVERIFY(!vf_path.isEmpty());

    FontInspectorEngine engine;
    QVERIFY(engine.openFile(vf_path));
    const FontDecorationMetrics deco = engine.decorationMetrics(0);

    QScrollArea sa;
    sa.setWidgetResizable(true);
    auto *gi = new GlyphInspectorWidget(&sa);
    sa.setWidget(gi);
    sa.resize(380, 560);
    sa.show();
    QVERIFY(QTest::qWaitForWindowExposed(&sa));

    QFont ft(QStringLiteral("Noto Sans"));
    ft.setPointSize(16);
    // U+2F00 sits in the long "CJK Radicals Supplement / Kangxi Radicals / ..."
    // block description that previously stretched QFormLayout horizontally.
    gi->updateGlyph(QChar(0x2F00), ft, vf_path, deco);
    QApplication::processEvents();
    QApplication::processEvents();

    QCOMPARE(sa.horizontalScrollBar()->maximum(), 0);
    for (QGroupBox *gb : gi->findChildren<QGroupBox *>()) {
        if (!gb->isVisible()) {
            continue;
        }
        const QRect gbrect = gb->rect();
        for (QLabel *lbl : gb->findChildren<QLabel *>()) {
            if (!lbl->isVisible()) {
                continue;
            }
            const int hfw = lbl->hasHeightForWidth()
                                ? lbl->heightForWidth(lbl->width())
                                : lbl->sizeHint().height();
            const QRect mapped(lbl->mapTo(gb, QPoint(0, 0)), lbl->size());
            QVERIFY2(lbl->width() > 0, qPrintable(lbl->text()));
            QVERIFY2(lbl->height() + 1 >= hfw, qPrintable(lbl->text()));
            QVERIFY2(gbrect.contains(mapped), qPrintable(lbl->text()));
        }
    }
    auto *preview = gi->findChild<GlyphPreviewWidget *>();
    QVERIFY(preview != nullptr);
    sa.ensureWidgetVisible(preview);
    QApplication::processEvents();
    QVERIFY(preview->isVisible());
    preview->repaint();
    QApplication::processEvents();

    bool saw_underline = false;
    bool saw_strikeout = false;
    for (QLabel *lbl : gi->findChildren<QLabel *>()) {
        const QString t = lbl->text();
        if (t.contains(QLatin1String("FUnits"))) {
            if (t.contains(QString::number(deco.underline_pos))
                && t.contains(QString::number(deco.underline_thick))) {
                saw_underline = true;
            }
            if (t.contains(QString::number(deco.strikeout_pos))
                && t.contains(QString::number(deco.strikeout_size))) {
                saw_strikeout = true;
            }
        }
    }
    QVERIFY(saw_underline);
    QVERIFY(saw_strikeout);
    QVERIFY(preview->lastUnderlineBand().isValid());
    QVERIFY(preview->lastStrikeoutBand().isValid());

    const QVector<QRectF> labels = preview->lastLabelRects();
    QVERIFY(!labels.isEmpty());
    const QRectF bounds(preview->rect());
    for (int i = 0; i < labels.size(); ++i) {
        QVERIFY(bounds.contains(labels[i]));
        for (int j = i + 1; j < labels.size(); ++j) {
            QVERIFY(!labels[i].intersects(labels[j]));
        }
    }

    // Also stress-test label placement at 72 pt where ascent/descent clamp
    // into the corner areas near LSB/RSB/origin/advance labels.
    ft.setPointSize(72);
    gi->updateGlyph(QChar('g'), ft, vf_path, deco);
    preview->repaint();
    QApplication::processEvents();
    const QVector<QRectF> large_labels = preview->lastLabelRects();
    QVERIFY(!large_labels.isEmpty());
    for (int i = 0; i < large_labels.size(); ++i) {
        QVERIFY(bounds.contains(large_labels[i]));
        for (int j = i + 1; j < large_labels.size(); ++j) {
            QVERIFY(!large_labels[i].intersects(large_labels[j]));
        }
    }

    // Stress-test minimum preview dimensions at extreme point sizes where
    // ascent/descent lie far offscreen so vertical sweeps still search inside
    // bounds without falling back to overlapping rects.
    GlyphPreviewWidget compact_preview;
    compact_preview.resize(compact_preview.minimumSize());
    compact_preview.show();
    QVERIFY(QTest::qWaitForWindowExposed(&compact_preview));
    const QRectF compact_bounds(compact_preview.rect());
    for (int pt : {120, 200}) {
        ft.setPointSize(pt);
        compact_preview.setGlyphData(QChar('A'), ft, QRectF(0, -pt, pt, pt * 1.2),
                                     pt * 0.6, deco);
        compact_preview.repaint();
        const QVector<QRectF> compact_labels = compact_preview.lastLabelRects();
        QVERIFY(!compact_labels.isEmpty());
        for (int i = 0; i < compact_labels.size(); ++i) {
            QVERIFY(compact_bounds.contains(compact_labels[i]));
            for (int j = i + 1; j < compact_labels.size(); ++j) {
                QVERIFY(!compact_labels[i].intersects(compact_labels[j]));
            }
        }
    }
}

namespace {
void appendU16(QByteArray &out, quint16 v)
{
    out.append(char((v >> 8) & 0xFF));
    out.append(char(v & 0xFF));
}

void appendU32(QByteArray &out, quint32 v)
{
    out.append(char((v >> 24) & 0xFF));
    out.append(char((v >> 16) & 0xFF));
    out.append(char((v >> 8) & 0xFF));
    out.append(char(v & 0xFF));
}

QByteArray makeUtf16Be(const QString &s)
{
    QByteArray out;
    out.reserve(s.size() * 2);
    for (QChar ch : s) {
        appendU16(out, ch.unicode());
    }
    return out;
}

quint32 readU32Be(const QByteArray &b, int off)
{
    return (quint32(uchar(b[off])) << 24) | (quint32(uchar(b[off + 1])) << 16)
           | (quint32(uchar(b[off + 2])) << 8) | quint32(uchar(b[off + 3]));
}

quint16 readU16Be(const QByteArray &b, int off)
{
    return (quint16(uchar(b[off])) << 8) | quint16(uchar(b[off + 1]));
}

void writeU32Be(QByteArray &b, int off, quint32 v)
{
    b[off]     = char((v >> 24) & 0xFF);
    b[off + 1] = char((v >> 16) & 0xFF);
    b[off + 2] = char((v >> 8) & 0xFF);
    b[off + 3] = char(v & 0xFF);
}

int findSfntTableDirOffset(const QByteArray &sfnt_bytes, const char *tag4)
{
    if (sfnt_bytes.size() < 12) {
        return -1;
    }
    const int num_tables = readU16Be(sfnt_bytes, 4);
    const QByteArray tag(tag4, 4);
    for (int i = 0; i < num_tables; ++i) {
        const int off = 12 + i * 16;
        if (off + 16 <= sfnt_bytes.size() && sfnt_bytes.mid(off, 4) == tag) {
            return off;
        }
    }
    return -1;
}

QByteArray buildCraftedNameTable(const QVector<QPair<quint16, QString>> &entries)
{
    QByteArray strings;
    QByteArray records;
    for (const auto &e : entries) {
        const QByteArray utf16 = makeUtf16Be(e.second);
        appendU16(records, 3);       // platformID = Windows
        appendU16(records, 1);       // encodingID = Unicode BMP
        appendU16(records, 0x0409);  // languageID = en-US
        appendU16(records, e.first);
        appendU16(records, quint16(utf16.size()));
        appendU16(records, quint16(strings.size()));
        strings.append(utf16);
    }
    QByteArray table;
    const quint16 storage_off = quint16(6 + records.size());
    appendU16(table, 0);
    appendU16(table, quint16(entries.size()));
    appendU16(table, storage_off);
    table.append(records);
    table.append(strings);
    return table;
}

QByteArray buildMacRomanNameTable(quint16 name_id, const QByteArray &mac_roman)
{
    QByteArray records;
    appendU16(records, 1);  // platformID = Macintosh
    appendU16(records, 0);  // encodingID = Roman
    appendU16(records, 0);  // languageID = English
    appendU16(records, name_id);
    appendU16(records, quint16(mac_roman.size()));
    appendU16(records, 0);  // stringOffset, the only string starts at 0

    QByteArray table;
    appendU16(table, 0);  // format
    appendU16(table, 1);  // count
    appendU16(table, quint16(6 + records.size()));
    table.append(records);
    table.append(mac_roman);
    return table;
}

QByteArray buildCraftedGsubOrGpos(
    const QVector<QByteArray> &all_feature_tags,
    const QVector<quint16> &default_lang_indices,
    const QVector<QPair<QByteArray, QVector<quint16>>> &extra_langs,
    const QHash<quint16, QByteArray> &feature_params_by_index)
{
    // ScriptList: single Script "latn"
    QByteArray script_table;
    const int lang_rec_bytes = extra_langs.size() * 6;
    const int default_lang_off = 4 + lang_rec_bytes;

    QByteArray lang_tables;
    auto buildLangSys = [](const QVector<quint16> &indices) {
        QByteArray ls;
        appendU16(ls, 0);       // lookupOrderOffset
        appendU16(ls, 0xFFFF);  // requiredFeatureIndex
        appendU16(ls, quint16(indices.size()));
        for (quint16 idx : indices) {
            appendU16(ls, idx);
        }
        return ls;
    };

    const QByteArray def_ls = buildLangSys(default_lang_indices);
    lang_tables.append(def_ls);

    appendU16(script_table, quint16(default_lang_off));
    appendU16(script_table, quint16(extra_langs.size()));
    for (const auto &lang : extra_langs) {
        const quint16 ls_off = quint16(default_lang_off + lang_tables.size());
        script_table.append(lang.first.left(4).leftJustified(4, ' '));
        appendU16(script_table, ls_off);
        lang_tables.append(buildLangSys(lang.second));
    }
    script_table.append(lang_tables);

    QByteArray script_list;
    appendU16(script_list, 1);  // scriptCount
    script_list.append("latn", 4);
    appendU16(script_list, 8);  // offset from scriptList start
    script_list.append(script_table);

    // FeatureList
    QByteArray feature_list;
    const int feat_header_size = 2 + all_feature_tags.size() * 6;
    QByteArray feat_tables;
    QByteArray params_blob;

    int running_feat_off             = feat_header_size;
    // First compute total size of FeatureTable array
    const int total_feat_tables_size = all_feature_tags.size() * 4;
    int params_abs_cursor            = feat_header_size + total_feat_tables_size;

    appendU16(feature_list, quint16(all_feature_tags.size()));
    for (int i = 0; i < all_feature_tags.size(); ++i) {
        feature_list.append(all_feature_tags[i].left(4).leftJustified(4, ' '));
        appendU16(feature_list, quint16(running_feat_off));

        QByteArray ft;
        const auto param_it = feature_params_by_index.constFind(quint16(i));
        if (param_it != feature_params_by_index.constEnd()) {
            const int rel_off = params_abs_cursor - running_feat_off;
            appendU16(ft, quint16(rel_off));
            params_blob.append(param_it.value());
            params_abs_cursor += param_it.value().size();
        }
        else {
            appendU16(ft, 0);
        }
        appendU16(ft, 0);  // lookupCount = 0
        feat_tables.append(ft);
        running_feat_off += ft.size();
    }
    feature_list.append(feat_tables);
    feature_list.append(params_blob);

    QByteArray lookup_list;
    appendU16(lookup_list, 0);

    const quint16 script_list_off  = 10;
    const quint16 feature_list_off = quint16(script_list_off + script_list.size());
    const quint16 lookup_list_off
        = quint16(feature_list_off + feature_list.size());

    QByteArray ot;
    appendU16(ot, 1);  // majorVersion
    appendU16(ot, 0);  // minorVersion
    appendU16(ot, script_list_off);
    appendU16(ot, feature_list_off);
    appendU16(ot, lookup_list_off);
    ot.append(script_list);
    ot.append(feature_list);
    ot.append(lookup_list);
    return ot;
}

QByteArray buildSyntheticSfntWithTables(
    const QByteArray &base_ttf,
    const QHash<QByteArray, QByteArray> &replacements)
{
    if (base_ttf.size() < 12) {
        return {};
    }
    const quint16 orig_count = readU16Be(base_ttf, 4);
    if (base_ttf.size() < 12 + orig_count * 16) {
        return {};
    }

    QMap<QByteArray, QByteArray> tables;
    for (int i = 0; i < orig_count; ++i) {
        const int rec_off     = 12 + i * 16;
        const QByteArray tag  = base_ttf.mid(rec_off, 4);
        const quint32 offset  = readU32Be(base_ttf, rec_off + 8);
        const quint32 length  = readU32Be(base_ttf, rec_off + 12);
        if (qint64(offset) + qint64(length) <= base_ttf.size()) {
            tables.insert(tag, base_ttf.mid(int(offset), int(length)));
        }
    }
    for (auto it = replacements.cbegin(); it != replacements.cend(); ++it) {
        tables.insert(it.key().left(4), it.value());
    }

    const quint16 count = quint16(tables.size());
    quint16 max_pow2    = 1;
    quint16 selector    = 0;
    while (max_pow2 * 2 <= count) {
        max_pow2 *= 2;
        ++selector;
    }
    const quint16 search_range = quint16(max_pow2 * 16);
    const quint16 range_shift  = quint16(count * 16 - search_range);

    QByteArray out;
    out.append(base_ttf.mid(0, 4));
    appendU16(out, count);
    appendU16(out, search_range);
    appendU16(out, selector);
    appendU16(out, range_shift);

    quint32 data_offset = quint32(12 + count * 16);
    QByteArray data_blob;
    for (auto it = tables.cbegin(); it != tables.cend(); ++it) {
        const QByteArray &tbl = it.value();
        out.append(it.key().left(4));
        appendU32(out, 0);
        appendU32(out, data_offset);
        appendU32(out, quint32(tbl.size()));

        data_blob.append(tbl);
        while ((data_blob.size() % 4) != 0) {
            data_blob.append('\0');
        }
        data_offset = quint32(12 + count * 16 + data_blob.size());
    }
    out.append(data_blob);
    return out;
}

QByteArray buildSyntheticTtc(const QByteArray &base_ttf,
                             const QByteArray &name0,
                             const QByteArray &name1)
{
    QByteArray face0
        = buildSyntheticSfntWithTables(base_ttf, {{"name", name0}});
    QByteArray face1
        = buildSyntheticSfntWithTables(base_ttf, {{"name", name1}});
    if (face0.isEmpty() || face1.isEmpty()) {
        return {};
    }

    const quint32 header_size = 12 + 2 * 4;
    const quint32 off0        = header_size;
    const quint32 off1        = off0 + quint32(face0.size());

    // Table offsets in an sfnt directory are absolute from the start of the
    // collection, so both directories must be rebased after concatenation.
    auto rebase = [](QByteArray &sfnt, quint32 delta) {
        const quint16 count = readU16Be(sfnt, 4);
        for (int i = 0; i < count; ++i) {
            const int rec_off = 12 + i * 16;
            const quint32 val = readU32Be(sfnt, rec_off + 8) + delta;
            sfnt[rec_off + 8]  = char((val >> 24) & 0xFF);
            sfnt[rec_off + 9]  = char((val >> 16) & 0xFF);
            sfnt[rec_off + 10] = char((val >> 8) & 0xFF);
            sfnt[rec_off + 11] = char(val & 0xFF);
        }
    };
    rebase(face0, off0);
    rebase(face1, off1);

    QByteArray out;
    out.append("ttcf", 4);
    appendU32(out, 0x00010000);
    appendU32(out, 2);
    appendU32(out, off0);
    appendU32(out, off1);
    out.append(face0);
    out.append(face1);
    return out;
}
}  // namespace

void ViewerTest::inspectorEngineParsesSyntheticTtcCollection()
{
    const QString static_path = fixturePath("Roboto-Regular.ttf");
    QVERIFY(!static_path.isEmpty());

    QFile base_file(static_path);
    QVERIFY(base_file.open(QIODevice::ReadOnly));
    const QByteArray base_bytes = base_file.readAll();

    // A two-face collection built from one fixture keeps TTC coverage
    // deterministic; the system-font assertions in the latch test are optional.
    const QByteArray name_face0 = buildCraftedNameTable(
        {{1, QStringLiteral("TtcFaceZero")},
         {2, QStringLiteral("Regular")}});
    const QByteArray name_face1 = buildCraftedNameTable(
        {{1, QStringLiteral("TtcFaceOne")},
         {2, QStringLiteral("Bold")}});
    const QByteArray synth_ttc
        = buildSyntheticTtc(base_bytes, name_face0, name_face1);
    QVERIFY(!synth_ttc.isEmpty());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ttc_path = dir.filePath(QStringLiteral("two_faces.ttc"));
    QFile ttc_file(ttc_path);
    QVERIFY(ttc_file.open(QIODevice::WriteOnly));
    QCOMPARE(ttc_file.write(synth_ttc), qint64(synth_ttc.size()));
    ttc_file.close();

    FontInspectorEngine engine;
    QVERIFY(engine.openFile(ttc_path));
    QCOMPARE(engine.faceCount(), 2);
    QVERIFY(!engine.inspectFace(0).name_records.isEmpty());
    QVERIFY(!engine.inspectFace(1).name_records.isEmpty());
    QCOMPARE(engine.resolveFaceIndex(QStringLiteral("TtcFaceOne"),
                                     QStringLiteral("Bold")),
             1);
    QCOMPARE(engine.resolveFaceIndex(QStringLiteral("TtcFaceZero"),
                                     QStringLiteral("Regular")),
             0);
    QCOMPARE(engine.resolveFaceIndex(QStringLiteral("MissingFamily"),
                                     QStringLiteral("Regular")),
             0);
}

void ViewerTest::inspectorEngineDecodesMacRomanNameRecords()
{
    const QString base_path = fixturePath("Roboto-Regular.ttf");
    QVERIFY(!base_path.isEmpty());
    QFile f(base_path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray base_bytes = f.readAll();

    // 0x8E is "é" in Mac Roman but a C1 control character in Latin-1, so a
    // Latin-1 decode would not produce the expected family name.
    const QByteArray name_tbl
        = buildMacRomanNameTable(1, QByteArray("Caf\x8E", 4));
    const QByteArray synth_ttf
        = buildSyntheticSfntWithTables(base_bytes, {{"name", name_tbl}});
    QVERIFY(!synth_ttf.isEmpty());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString synth_path = dir.filePath(QStringLiteral("mac_name.ttf"));
    QFile out_file(synth_path);
    QVERIFY(out_file.open(QIODevice::WriteOnly));
    QCOMPARE(out_file.write(synth_ttf), qint64(synth_ttf.size()));
    out_file.close();

    FontInspectorEngine engine;
    QVERIFY(engine.openFile(synth_path));

    QString family;
    for (const FontNameRecord &rec : engine.inspectFace(0).name_records) {
        if (rec.label == QLatin1String("Family")) {
            family = rec.value;
            break;
        }
    }
    QCOMPARE(family, QStringLiteral("Caf\u00E9"));
}

void ViewerTest::openTypeFeatureParserGroupsPriorityAndParams()
{
    const QString base_path = fixturePath("Roboto-Regular.ttf");
    QVERIFY(!base_path.isEmpty());
    QFile f(base_path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray base_bytes = f.readAll();

    const QByteArray name_tbl = buildCraftedNameTable({
        {1, QStringLiteral("SynthOT")},
        {2, QStringLiteral("Regular")},
        {256, QStringLiteral("Alternate Single Story a")},
        {257, QStringLiteral("Slashed Zero Form")},
    });

    QByteArray ss01_params;
    appendU16(ss01_params, 0);    // version
    appendU16(ss01_params, 256);  // uiLabelNameId

    QByteArray cv01_params;
    appendU16(cv01_params, 0);    // format
    appendU16(cv01_params, 257);  // featUiLabelNameId
    appendU16(cv01_params, 0);
    appendU16(cv01_params, 0);
    appendU16(cv01_params, 0);
    appendU16(cv01_params, 0);
    appendU16(cv01_params, 0);

    QByteArray ss02_bad_ver_params;
    appendU16(ss02_bad_ver_params, 1);    // unsupported version != 0
    appendU16(ss02_bad_ver_params, 256);

    QByteArray ss03_low_name_params;
    appendU16(ss03_low_name_params, 0);  // version 0
    appendU16(ss03_low_name_params, 1);  // reserved nameID < 256 (Family)

    // Deliberately include "unrf" (unreferenced by any LangSys), out-of-order
    // tags ("salt", "cv01", "ss01", "smcp", "liga"), and a "locl" feature
    // referenced under "ZHS ". Also include "clig"/"ccmp" default-on features
    // and malformed FeatureParams on "ss02"/"ss03".
    const QByteArray gsub_tbl = buildCraftedGsubOrGpos(
        {"salt", "cv01", "ss01", "unrf", "smcp", "liga", "locl", "clig", "ccmp",
         "ss02", "ss03"},
        {0, 1, 2, 4, 5, 7, 8, 9, 10}, {{"ZHS ", {6}}},
        {{1, cv01_params},
         {2, ss01_params},
         {9, ss02_bad_ver_params},
         {10, ss03_low_name_params}});
    const QByteArray gpos_tbl
        = buildCraftedGsubOrGpos({"kern", "liga", "locl"}, {0, 1, 2}, {}, {});

    const QByteArray synth_ttf = buildSyntheticSfntWithTables(
        base_bytes, {{"name", name_tbl}, {"GSUB", gsub_tbl}, {"GPOS", gpos_tbl}});
    QVERIFY(!synth_ttf.isEmpty());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString synth_path = dir.filePath(QStringLiteral("synth_ot.ttf"));
    QFile out_file(synth_path);
    QVERIFY(out_file.open(QIODevice::WriteOnly));
    QCOMPARE(out_file.write(synth_ttf), qint64(synth_ttf.size()));
    out_file.close();

    FontInspectorEngine engine;
    QVERIFY(engine.openFile(synth_path));
    const FontInspectionData info = engine.inspectFace(0);

    QStringList tags;
    QHash<QByteArray, FontFeatureInfo> by_tag;
    for (const FontFeatureInfo &feat : info.ot_features) {
        tags.append(QString::fromLatin1(feat.tag));
        by_tag.insert(feat.tag, feat);
    }

    // Unreferenced "unrf" and "locl" (GSUB or GPOS) must not appear in
    // ot_features, and "liga" must be deduplicated across GSUB and GPOS.
    QVERIFY(!tags.contains(QStringLiteral("unrf")));
    QVERIFY(!tags.contains(QStringLiteral("locl")));
    QCOMPARE(tags.count(QStringLiteral("liga")), 1);
    QCOMPARE(tags,
             (QStringList{"liga", "clig", "kern", "smcp", "ss01", "ss02",
                          "ss03", "cv01", "ccmp", "salt"}));

    QVERIFY(by_tag["liga"].default_on);
    QVERIFY(by_tag["clig"].default_on);
    QVERIFY(by_tag["ccmp"].default_on);
    QVERIFY(by_tag["kern"].default_on);
    QVERIFY(!by_tag["smcp"].default_on);
    QVERIFY(!by_tag["ss01"].default_on);
    QVERIFY(!by_tag["cv01"].default_on);
    QVERIFY(!by_tag["salt"].default_on);

    QCOMPARE(by_tag["ss01"].friendly_name,
             QStringLiteral("Alternate Single Story a"));
    QCOMPARE(by_tag["cv01"].friendly_name,
             QStringLiteral("Slashed Zero Form"));
    QCOMPARE(by_tag["ss02"].friendly_name, QStringLiteral("Stylistic Set 2"));
    QCOMPARE(by_tag["ss03"].friendly_name, QStringLiteral("Stylistic Set 3"));
    QVERIFY(info.locl_languages.contains(QStringLiteral("ZHS")));

    // Real fixture sanity check.
    const QString vf_path = fixturePath("NotoSans-VariableFont_wdth,wght.ttf");
    QVERIFY(!vf_path.isEmpty());
    QVERIFY(engine.openFile(vf_path));
    QVERIFY(!engine.inspectFace(0).ot_features.isEmpty());
}

void ViewerTest::openTypePillsIsolationSpacingResetAndNonPersistence()
{
    const QString roboto_vf = fixturePath("Roboto-VariableFont_wdth,wght.ttf");
    QVERIFY(!roboto_vf.isEmpty());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini_path = dir.filePath(QStringLiteral("fontviewer.ini"));

    {
        ViewOptionsPrivate d;
        d.path = roboto_vf;
        ViewOptions opts;
        opts.d_ptr = &d;

        TestFontViewer viewer(ini_path);
        viewer.load(nullptr, &opts);
        viewer.resize(900, 680);
        viewer.show();
        QVERIFY(QTest::qWaitForWindowExposed(&viewer));
        auto *view = viewer.findChild<FontWidget *>();
        QVERIFY(view != nullptr);
        auto *text_tab = view->findChild<TextPreviewTab *>();
        auto *feat_bar = view->findChild<OpenTypeFeaturesBar *>();
        auto *feat_btn = view->findChild<QPushButton *>(
            QStringLiteral("text_features_btn"));
        QVERIFY(text_tab != nullptr && feat_bar != nullptr && feat_btn != nullptr);
        QCOMPARE(feat_bar->parentWidget(), static_cast<QWidget *>(text_tab));
        QVERIFY(feat_btn->isVisible());
        QVERIFY(!feat_bar->isVisible());
        feat_btn->click();
        QApplication::processEvents();
        QVERIFY(feat_bar->isVisible());

        const auto pills = view->featurePillButtons();
        QVERIFY(!pills.isEmpty());

        // locl technical gate: no ineffective OpenType language combo box.
        for (QComboBox *cb : view->findChildren<QComboBox *>()) {
            const QString tip = cb->toolTip().toLower();
            QVERIFY(!tip.contains(QStringLiteral("locl")));
        }

        for (QPushButton *pill : pills) {
            QCOMPARE(pill->focusPolicy(), Qt::NoFocus);
            // Custom paint reads underMouse(); hover events need WA_Hover.
            QVERIFY(pill->testAttribute(Qt::WA_Hover));
        }

        // Choose a feature whose pill exists in Roboto VF (prefer smcp or liga).
        QByteArray toggle_tag;
        bool initial_on = false;
        for (const QByteArray &candidate :
             {QByteArrayLiteral("smcp"), QByteArrayLiteral("c2sc"),
              QByteArrayLiteral("tnum"), QByteArrayLiteral("liga")}) {
            for (QPushButton *pill : pills) {
                if (pill->text().startsWith(QString::fromLatin1(candidate))) {
                    toggle_tag = candidate;
                    initial_on = view->isFeatureEnabled(toggle_tag);
                    break;
                }
            }
            if (!toggle_tag.isEmpty()) {
                break;
            }
        }
        QVERIFY(!toggle_tag.isEmpty());

        view->setFeatureEnabled(toggle_tag, !initial_on);
        QApplication::processEvents();
        QCOMPARE(view->isFeatureEnabled(toggle_tag), !initial_on);

        const auto qt_tag = QFont::Tag::fromString(toggle_tag);
        QVERIFY(qt_tag.has_value());

        auto *edit     = view->findChild<FontPreviewEdit *>();
        auto *char_wgt = view->findChild<CharacterWidget *>();
        auto *gi       = view->findChild<GlyphInspectorWidget *>();
        auto *tabs     = view->findChild<QTabWidget *>();
        QVERIFY(edit != nullptr);
        QVERIFY(char_wgt != nullptr);
        QVERIFY(gi != nullptr);
        QVERIFY(tabs != nullptr);
        QVERIFY(edit->font().isFeatureSet(*qt_tag));
        QCOMPARE(edit->font().featureValue(*qt_tag),
                 quint32(!initial_on ? 1 : 0));

        // Character grid & Glyph Inspector share PreviewFontRole::Glyph and must
        // stay untouched by OpenType feature toggles.
        QCOMPARE(char_wgt->currentFont().family(), edit->font().family());
        QVERIFY(char_wgt->currentFont().featureTags().isEmpty());
        tabs->setCurrentIndex(2);
        QApplication::processEvents();
        emit char_wgt->selectionChangedCodePoint(quint32('A'));
        QApplication::processEvents();
        tabs->setCurrentIndex(3);
        QApplication::processEvents();
        QCOMPARE(gi->currentFont().family(), edit->font().family());
        QVERIFY(gi->currentFont().featureTags().isEmpty());
        tabs->setCurrentIndex(0);
        QApplication::processEvents();

        // Setting non-zero letter spacing must disable ligature pills (liga/clig/
        // dlig/hlig), suppress ligature overrides on the preview font so Qt
        // does not keep them forced on, and re-enable/restore at 0.
        QPushButton *liga_pill = nullptr;
        QPushButton *dlig_pill = nullptr;
        for (QPushButton *pill : pills) {
            if (pill->text().startsWith(QStringLiteral("liga"))) {
                liga_pill = pill;
            }
            else if (pill->text().startsWith(QStringLiteral("dlig"))) {
                dlig_pill = pill;
            }
        }
        QVERIFY(liga_pill != nullptr);
        QVERIFY(liga_pill->isEnabled());
        if (dlig_pill) {
            view->setFeatureEnabled(QByteArrayLiteral("dlig"), true);
            QVERIFY(edit->font().isFeatureSet(QFont::Tag("dlig")));
        }

        view->setLetterSpacing(2.0);
        QApplication::processEvents();
        QVERIFY(!liga_pill->isEnabled());
        QVERIFY(liga_pill->toolTip().contains(QStringLiteral("letter spacing"),
                                              Qt::CaseInsensitive));
        if (dlig_pill) {
            QVERIFY(!dlig_pill->isEnabled());
            QVERIFY(!edit->font().isFeatureSet(QFont::Tag("dlig")));
        }

        view->setLetterSpacing(0.0);
        QApplication::processEvents();
        QVERIFY(liga_pill->isEnabled());
        if (dlig_pill) {
            QVERIFY(dlig_pill->isEnabled());
            QVERIFY(edit->font().isFeatureSet(QFont::Tag("dlig")));
            view->setFeatureEnabled(QByteArrayLiteral("dlig"), false);
        }

        // Clicking a pill or OpenType Reset must clear FontPreviewEdit focus.
        QPushButton *reset_btn = feat_bar->findChild<QPushButton *>(
            QStringLiteral("opentype_reset_btn"));
        QVERIFY(reset_btn != nullptr);
        QVERIFY(reset_btn->isVisible());
        edit->setFocus(Qt::MouseFocusReason);
        QCOMPARE(QApplication::focusWidget(), static_cast<QWidget *>(edit));
        reset_btn->click();
        QApplication::processEvents();
        QCOMPARE(view->isFeatureEnabled(toggle_tag), initial_on);
        QWidget *focus = QApplication::focusWidget();
        QVERIFY(!focus || (focus != view && !view->isAncestorOf(focus)));

        QPushButton *toggle_pill = nullptr;
        for (QPushButton *pill : pills) {
            if (pill->text().startsWith(QString::fromLatin1(toggle_tag))) {
                toggle_pill = pill;
                break;
            }
        }
        QVERIFY(toggle_pill != nullptr);
        edit->setFocus(Qt::MouseFocusReason);
        QCOMPARE(QApplication::focusWidget(), static_cast<QWidget *>(edit));
        toggle_pill->click();
        QApplication::processEvents();
        QCOMPARE(view->isFeatureEnabled(toggle_tag), !initial_on);
        QVERIFY(reset_btn->isVisible());
        focus = QApplication::focusWidget();
        QVERIFY(!focus || (focus != view && !view->isAncestorOf(focus)));

        reset_btn->click();
        QApplication::processEvents();
        QCOMPARE(view->isFeatureEnabled(toggle_tag), initial_on);

        // Note label stays empty unless letter spacing disables ligatures, and
        // its palette follows runtime theme/palette Mid color changes.
        QLabel *note_lbl = nullptr;
        for (QLabel *lbl : feat_bar->findChildren<QLabel *>()) {
            if (lbl->wordWrap()) {
                note_lbl = lbl;
                break;
            }
        }
        QVERIFY(note_lbl != nullptr);
        QVERIFY(note_lbl->text().isEmpty());
        view->setLetterSpacing(2.0);
        QApplication::processEvents();
        QVERIFY(note_lbl->text().contains(QStringLiteral("letter spacing"),
                                          Qt::CaseInsensitive));
        view->setLetterSpacing(0.0);
        QApplication::processEvents();
        QVERIFY(note_lbl->text().isEmpty());

        QPalette themed = makeThemePalette(true);
        const QColor custom_mid(0x88, 0x99, 0xAA);
        themed.setColor(QPalette::All, QPalette::Mid, custom_mid);
        view->setPalette(themed);
        QApplication::processEvents();
        QCOMPARE(note_lbl->palette().color(QPalette::WindowText), custom_mid);

        // Leave an override enabled across close to prove non-persistence.
        view->setFeatureEnabled(toggle_tag, !initial_on);
        QCOMPARE(view->isFeatureEnabled(toggle_tag), !initial_on);
    }

    // Many-feature fonts (Noto Sans VF has 40+ features) must stay focus-free,
    // cap the expanded drawer scroll area at 156*dpr with read-only NoFocus
    // scrollbars, and keep tabs usable.
    {
        const QString noto_vf = fixturePath("NotoSans-VariableFont_wdth,wght.ttf");
        QVERIFY(!noto_vf.isEmpty());
        FontWidget noto_view;
        QVERIFY(noto_view.init(noto_vf));
        noto_view.resize(850, 800);
        noto_view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&noto_view));
        QApplication::processEvents();

        auto *noto_feat_btn = noto_view.findChild<QPushButton *>(
            QStringLiteral("text_features_btn"));
        QVERIFY(noto_feat_btn != nullptr);
        noto_feat_btn->click();
        QApplication::processEvents();

        const auto pills = noto_view.featurePillButtons();
        QVERIFY(pills.size() >= 30);
        auto *noto_tabs = noto_view.findChild<QTabWidget *>();
        QVERIFY(noto_tabs != nullptr);
        QVERIFY(noto_tabs->height() >= 250);
        auto *feat_bar = noto_view.findChild<OpenTypeFeaturesBar *>();
        QVERIFY(feat_bar != nullptr);
        auto *feat_scroll = feat_bar->findChild<QScrollArea *>(
            QStringLiteral("opentype_features_scroll"));
        QVERIFY(feat_scroll != nullptr);
        QVERIFY(feat_scroll->height() <= 156);
        QVERIFY(feat_scroll->verticalScrollBar()
                    ->property("is_readonly")
                    .toBool());
        QCOMPARE(feat_scroll->verticalScrollBar()->focusPolicy(), Qt::NoFocus);
        QVERIFY(feat_scroll->verticalScrollBar()->maximum() > 0);
        for (QWidget *child : noto_view.findChildren<QWidget *>()) {
            if (qobject_cast<FontPreviewEdit *>(child)
                || child->objectName() == QLatin1String("chars_filter_edit")
                || child->window() != noto_view.window()) {
                continue;
            }
            QCOMPARE(child->focusPolicy(), Qt::NoFocus);
        }
    }

    {
        ViewOptionsPrivate d;
        d.path = roboto_vf;
        ViewOptions opts;
        opts.d_ptr = &d;

        TestFontViewer viewer2(ini_path);
        viewer2.load(nullptr, &opts);
        auto *view2 = viewer2.findChild<FontWidget *>();
        QVERIFY(view2 != nullptr);
        for (QPushButton *pill : view2->featurePillButtons()) {
            if (pill->text().startsWith(QStringLiteral("smcp"))
                || pill->text().startsWith(QStringLiteral("c2sc"))
                || pill->text().startsWith(QStringLiteral("tnum"))) {
                QVERIFY(!pill->isChecked());
            }
        }
    }
}

void ViewerTest::glyphOutlineExtractionMatchesQtAndVariableAxes()
{
    const QString vf_path = fixturePath("NotoSans-VariableFont_wdth,wght.ttf");
    QVERIFY(!vf_path.isEmpty());

    FontInspectorEngine engine;
    QVERIFY(engine.openFile(vf_path));

    const GlyphOutlineData light
        = engine.glyphOutline(0, QChar('B'), {{"wght", 400.0f}});
    const GlyphOutlineData bold
        = engine.glyphOutline(0, QChar('B'), {{"wght", 800.0f}});
    QVERIFY(light.valid);
    QVERIFY(bold.valid);
    QVERIFY(light.contour_count >= 3);
    QVERIFY(light.point_count > 10);
    QCOMPARE(light.points.size(), light.tags.size());
    QCOMPARE(light.contour_ends.size(), light.contour_count);

    bool has_on  = false;
    bool has_off = false;
    for (auto kind : light.tags) {
        has_on |= (kind == GlyphOutlineData::PointKind::OnCurve);
        has_off |= (kind == GlyphOutlineData::PointKind::OffConic);
    }
    QVERIFY(has_on);
    QVERIFY(has_off);
    QVERIFY(light.points != bold.points);

    const GlyphOutlineData missing
        = engine.glyphOutline(0, QChar(0xE0FF), {});
    QVERIFY(!missing.valid);

    // Verify mapped screen coordinates in GlyphPreviewWidget stay within <= 1 px
    // of QRawFont::pathForGlyph bounds under both default and bold wght axes,
    // even while OpenType features are active for text preview.
    FontWidget fw;
    QVERIFY(fw.init(vf_path));
    fw.resize(900, 1000);
    fw.show();
    QVERIFY(QTest::qWaitForWindowExposed(&fw));

    auto *char_wgt = fw.findChild<CharacterWidget *>();
    auto *gi       = fw.findChild<GlyphInspectorWidget *>();
    auto *tabs     = fw.findChild<QTabWidget *>();
    QVERIFY(char_wgt != nullptr);
    QVERIFY(gi != nullptr);
    QVERIFY(tabs != nullptr);
    tabs->setCurrentIndex(2);
    QApplication::processEvents();
    gi->setShowOutlinePoints(true);
    tabs->setCurrentIndex(3);
    QApplication::processEvents();

    // Toggle an OT feature if available to ensure GlyphPreviewWidget stays
    // aligned cleanly.
    const auto pills = fw.featurePillButtons();
    if (!pills.isEmpty()) {
        fw.setFeatureEnabled(pills.first()->text().left(4).toLatin1(),
                             !pills.first()->isChecked());
    }

    for (float wght : {400.0f, 750.0f}) {
        fw.setVariableAxisValue(QByteArrayLiteral("wght"), wght);
        if (wght == 400.0f) {
            emit char_wgt->selectionChangedCodePoint(quint32('H'));
        }
        QApplication::processEvents();
        const QFont glyph_font = gi->currentFont();
        const GlyphOutlineData &outline = gi->outlineData();
        QVERIFY(outline.valid);

        auto *preview = gi->previewWidget();
        QVERIFY(preview != nullptr);
        if (auto *sa = qobject_cast<QScrollArea *>(tabs->currentWidget())) {
            sa->ensureWidgetVisible(preview);
        }
        preview->repaint();
        QApplication::processEvents();

        const QVector<QPointF> screen_pts = preview->lastOutlineScreenPoints();
        QCOMPARE(screen_pts.size(), outline.point_count);

        qreal min_x = screen_pts.first().x();
        qreal max_x = min_x;
        qreal min_y = screen_pts.first().y();
        qreal max_y = min_y;
        for (const QPointF &sp : screen_pts) {
            min_x = qMin(min_x, sp.x());
            max_x = qMax(max_x, sp.x());
            min_y = qMin(min_y, sp.y());
            max_y = qMax(max_y, sp.y());
        }
        const QRectF ft_bbox(min_x, min_y, max_x - min_x, max_y - min_y);

        // Compute Qt QRawFont outline bounds at the exact same display font and
        // placement used by GlyphPreviewWidget.
        QFont display_font = glyph_font;
        display_font.setPointSizeF(qMax(72.0, glyph_font.pointSizeF() * 4.0));
        QRawFont raw = QRawFont::fromFont(display_font);
        QVERIFY(raw.isValid());
        const QVector<quint32> idxs = raw.glyphIndexesForString(QString('H'));
        QVERIFY(!idxs.isEmpty());
        const qreal adv = raw.advancesForGlyphIndexes({idxs[0]}).at(0).x();
        const qreal tx  = preview->width() / 2.0 - adv / 2.0;
        const qreal ty
            = preview->height() / 2.0 + (raw.ascent() - raw.descent()) / 2.0;
        const QRectF qt_bbox
            = raw.pathForGlyph(idxs[0]).boundingRect().translated(tx, ty);

        const QString dbg = QString(
            "fam=%1 pt=%2 ft=%3,%4,%5x%6 qt=%7,%8,%9x%10 rawPx=%11 adv=%12")
                                .arg(glyph_font.family())
                                .arg(glyph_font.pointSizeF())
                                .arg(ft_bbox.left())
                                .arg(ft_bbox.top())
                                .arg(ft_bbox.width())
                                .arg(ft_bbox.height())
                                .arg(qt_bbox.left())
                                .arg(qt_bbox.top())
                                .arg(qt_bbox.width())
                                .arg(qt_bbox.height())
                                .arg(raw.pixelSize())
                                .arg(adv);
        QVERIFY2(qAbs(ft_bbox.left() - qt_bbox.left()) <= 1.0, qPrintable(dbg));
        QVERIFY2(qAbs(ft_bbox.right() - qt_bbox.right()) <= 1.0, qPrintable(dbg));
        QVERIFY2(qAbs(ft_bbox.top() - qt_bbox.top()) <= 1.0, qPrintable(dbg));
        QVERIFY2(qAbs(ft_bbox.bottom() - qt_bbox.bottom()) <= 1.0, qPrintable(dbg));
    }
}

void ViewerTest::glyphInspectorShowOutlinePointsUiAndIniPersistence()
{
    const QString font_path = fixturePath("Roboto-Regular.ttf");
    QVERIFY(!font_path.isEmpty());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini_path = dir.filePath(QStringLiteral("fontviewer.ini"));

    {
        ViewOptionsPrivate d;
        d.path = font_path;
        ViewOptions opts;
        opts.d_ptr = &d;

        TestFontViewer viewer(ini_path);
        viewer.load(nullptr, &opts);
        auto *view = viewer.findChild<FontWidget *>();
        QVERIFY(view != nullptr);
        QVERIFY(!view->isShowOutlinePoints());

        auto *gi = view->findChild<GlyphInspectorWidget *>();
        QVERIFY(gi != nullptr);

        auto *outline_btn = gi->findChild<QPushButton *>(
            QStringLiteral("guide_pill_outline"));
        QVERIFY(outline_btn != nullptr);
        QCOMPARE(outline_btn->text(), QStringLiteral("Outline"));
        QCOMPARE(outline_btn->focusPolicy(), Qt::NoFocus);
        QVERIFY(!outline_btn->isChecked());

        FontInspectorEngine engine;
        QVERIFY(engine.openFile(font_path));
        const GlyphOutlineData outline
            = engine.glyphOutline(0, QChar('A'), {});
        QVERIFY(outline.valid);
        gi->updateGlyph(QChar('A'), QFont(QStringLiteral("Roboto"), 16),
                        font_path, engine.decorationMetrics(0), outline);

        bool saw_contours = false;
        bool saw_points   = false;
        for (QLabel *lbl : gi->findChildren<QLabel *>()) {
            if (lbl->text() == QString::number(outline.contour_count)) {
                saw_contours = true;
            }
            if (lbl->text() == QString::number(outline.point_count)) {
                saw_points = true;
            }
        }
        QVERIFY(saw_contours);
        QVERIFY(saw_points);

        outline_btn->setChecked(true);
        QVERIFY(view->isShowOutlinePoints());

        // Outline points still render via QRawFont::unitsPerEm fallback when
        // decorationMetrics is omitted, malformed outlines are ignored safely,
        // and clearSelection() clears currentChar().
        auto *tabs    = view->findChild<QTabWidget *>();
        auto *preview = gi->previewWidget();
        QVERIFY(tabs != nullptr);
        QVERIFY(preview != nullptr);
        viewer.resize(900, 900);
        viewer.show();
        QVERIFY(QTest::qWaitForWindowExposed(&viewer));
        tabs->setCurrentIndex(3);
        QApplication::processEvents();
        if (auto *sa = qobject_cast<QScrollArea *>(tabs->currentWidget())) {
            sa->ensureWidgetVisible(preview);
        }
        gi->updateGlyph(QChar('A'), QFont(QStringLiteral("Roboto"), 16),
                        font_path, {}, outline);
        preview->repaint();
        QCOMPARE(preview->lastOutlineScreenPoints().size(), outline.point_count);

        GlyphOutlineData malformed = outline;
        malformed.contour_ends     = {-2};
        preview->setGlyphData(QChar('A'), QFont(QStringLiteral("Roboto"), 16),
                              QRectF(0, 0, 10, 10), 10.0, {}, malformed);
        preview->repaint();

        gi->clearSelection();
        QVERIFY(!gi->hasSelection());
        QVERIFY(gi->currentChar().isNull());
    }

    {
        QSettings check_ini(ini_path, QSettings::IniFormat);
        QVERIFY(check_ini.value(QStringLiteral("show_outline_points")).toBool());
        for (const QString &k : check_ini.allKeys()) {
            QVERIFY(!k.contains(QStringLiteral("feature"), Qt::CaseInsensitive));
        }

        ViewOptionsPrivate d;
        d.path = font_path;
        ViewOptions opts;
        opts.d_ptr = &d;

        TestFontViewer viewer2(ini_path);
        viewer2.load(nullptr, &opts);
        auto *view2 = viewer2.findChild<FontWidget *>();
        QVERIFY(view2 != nullptr);
        QVERIFY(view2->isShowOutlinePoints());
    }
}

void ViewerTest::characterWidgetHandlesUninitializedFontSafely()
{
    const QString font_path = fixturePath("Roboto-Regular.ttf");
    QVERIFY(!font_path.isEmpty());

    CharacterWidget cw;
    cw.init(font_path);
    cw.resize(256, 256);
    cw.show();
    QVERIFY(QTest::qWaitForWindowExposed(&cw));
    const QWidget &cw_widget = cw;
    QVERIFY(cw_widget.sizeHint().width() > 0);
    QVERIFY(cw_widget.sizeHint().height() > 0);
    cw.repaint();

    const QPoint pt(8, 8);
    QMouseEvent move(QEvent::MouseMove, pt, cw.mapToGlobal(pt), Qt::NoButton,
                     Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&cw, &move);

    QSignalSpy spy(&cw, &CharacterWidget::selectionChanged);
    QMouseEvent press(QEvent::MouseButtonPress, pt, cw.mapToGlobal(pt),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&cw, &press);
    QCOMPARE(spy.count(), 1);
}

void ViewerTest::characterWidgetLargePointSizeDoesNotOverflowSquareSize()
{
    const QString font_path = fixturePath("Roboto-Regular.ttf");
    QVERIFY(!font_path.isEmpty());

    CharacterWidget cw;
    cw.init(font_path);
    QFont large_ft(QStringLiteral("Roboto"));
    large_ft.setPointSize(280);
    cw.updateFont(large_ft);

    // Grid cell size stays decoupled from large reading point sizes.
    QCOMPARE(cw.cellSizePx(), CharacterWidget::kDefCellSizeDp);

    // Few trailing cells in a partial row must not paint past the last glyph or
    // emit non-zero code points when clicking empty space.
    QVector<quint32> sample = {'A', 'B', 'C'};
    cw.setCodePoints(sample);
    const int w = 6 * cw.cellSizePx() + 16;
    cw.resize(w, cw.cellSizePx() + 16);
    cw.show();
    QVERIFY(QTest::qWaitForWindowExposed(&cw));
    QApplication::processEvents();

    const int cols     = cw.columnCount();
    const int origin_x = qMax(0, (cw.width() - cols * cw.cellSizePx()) / 2);
    const QImage rendered = cw.grab().toImage();
    const QColor base_col = cw.palette().base().color();
    // Col 0 must render its left border and Row 0 must render its top border.
    QVERIFY(rendered.pixelColor(origin_x, 4 + cw.cellSizePx() / 2) != base_col);
    QVERIFY(rendered.pixelColor(origin_x + cw.cellSizePx() / 2, 4) != base_col);
    for (int y = 4; y < cw.cellSizePx(); ++y) {
        for (int x = origin_x + 3 * cw.cellSizePx() + 2; x < cw.width(); ++x) {
            QCOMPARE(rendered.pixelColor(x, y), base_col);
        }
    }

    QSignalSpy spy(&cw, &CharacterWidget::selectionChangedCodePoint);
    const QPoint empty_pt(origin_x + 4 * cw.cellSizePx(), cw.cellSizePx() / 2);
    QMouseEvent press(QEvent::MouseButtonPress, empty_pt,
                      cw.mapToGlobal(empty_pt), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(&cw, &press);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toUInt(), 0u);
}

void ViewerTest::openTypeFeatureParserDeduplicatesRepeatedScriptAndLangTables()
{
    const QString base_path = fixturePath("Roboto-Regular.ttf");
    QVERIFY(!base_path.isEmpty());
    QFile f(base_path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray base_bytes = f.readAll();

    // Build a GSUB table with 250 ScriptRecords pointing to one Script table
    // with 250 LangSysRecords pointing to one LangSys table with 250 indices.
    // Without deduplication this forces 250^3 (~15.6M) QHash lookups.
    constexpr int kRepeat = 250;
    QByteArray lang_sys;
    appendU16(lang_sys, 0);       // lookupOrderOffset
    appendU16(lang_sys, 0xFFFF);  // requiredFeatureIndex
    appendU16(lang_sys, kRepeat);
    for (int i = 0; i < kRepeat; ++i) {
        appendU16(lang_sys, quint16(i % 2));
    }

    QByteArray script_tbl;
    const quint16 shared_lang_off = quint16(4 + kRepeat * 6);
    appendU16(script_tbl, shared_lang_off);
    appendU16(script_tbl, kRepeat);
    for (int i = 0; i < kRepeat; ++i) {
        script_tbl.append("ZHS ", 4);
        appendU16(script_tbl, shared_lang_off);
    }
    script_tbl.append(lang_sys);

    QByteArray script_list;
    const quint16 shared_script_off = quint16(2 + kRepeat * 6);
    appendU16(script_list, kRepeat);
    for (int i = 0; i < kRepeat; ++i) {
        script_list.append("latn", 4);
        appendU16(script_list, shared_script_off);
    }
    script_list.append(script_tbl);

    QByteArray feature_list;
    const quint16 feat_off = quint16(2 + 2 * 6);
    appendU16(feature_list, 2);
    feature_list.append("liga", 4);
    appendU16(feature_list, feat_off);
    feature_list.append("locl", 4);
    appendU16(feature_list, quint16(feat_off + 4));
    appendU16(feature_list, 0);
    appendU16(feature_list, 0);
    appendU16(feature_list, 0);
    appendU16(feature_list, 0);

    QByteArray lookup_list;
    appendU16(lookup_list, 0);

    const quint16 script_list_off  = 10;
    const quint16 feature_list_off = quint16(script_list_off + script_list.size());
    const quint16 lookup_list_off
        = quint16(feature_list_off + feature_list.size());

    QByteArray gsub;
    appendU16(gsub, 1);
    appendU16(gsub, 0);
    appendU16(gsub, script_list_off);
    appendU16(gsub, feature_list_off);
    appendU16(gsub, lookup_list_off);
    gsub.append(script_list);
    gsub.append(feature_list);
    gsub.append(lookup_list);

    const QByteArray synth_ttf
        = buildSyntheticSfntWithTables(base_bytes, {{"GSUB", gsub}});
    QVERIFY(!synth_ttf.isEmpty());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString synth_path = dir.filePath(QStringLiteral("dup_ot.ttf"));
    QFile out_file(synth_path);
    QVERIFY(out_file.open(QIODevice::WriteOnly));
    QCOMPARE(out_file.write(synth_ttf), qint64(synth_ttf.size()));
    out_file.close();

    QElapsedTimer timer;
    timer.start();
    FontInspectorEngine engine;
    QVERIFY(engine.openFile(synth_path));
    const qint64 elapsed_ms = timer.elapsed();

    const FontInspectionData &info = engine.inspectFace(0);
    QVERIFY(info.locl_languages.contains(QStringLiteral("ZHS")));
    bool has_liga = false;
    for (const FontFeatureInfo &feat : info.ot_features) {
        if (feat.tag == QByteArrayLiteral("liga")) {
            has_liga = true;
            break;
        }
    }
    QVERIFY(has_liga);
    QVERIFY2(elapsed_ms < 250,
             qPrintable(QString("parse took %1 ms").arg(elapsed_ms)));
}

void ViewerTest::inspectorEngineClampsExtremeVarAxisFixedConversion()
{
    const QString vf_path = fixturePath("NotoSans-VariableFont_wdth,wght.ttf");
    QVERIFY(!vf_path.isEmpty());
    QFile f(vf_path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray base_bytes = f.readAll();

    // Locate the fvar table and raise wght maxValue to 0x7FFFFFFF so
    // fixedToFloat() rounds up to 32768.0f and tests floatToFixed() clamping.
    const quint16 num_tables = readU16Be(base_bytes, 4);
    QByteArray fvar_bytes;
    for (int i = 0; i < num_tables; ++i) {
        const int rec = 12 + i * 16;
        if (base_bytes.mid(rec, 4) == QByteArrayLiteral("fvar")) {
            const quint32 off = readU32Be(base_bytes, rec + 8);
            const quint32 len = readU32Be(base_bytes, rec + 12);
            fvar_bytes        = base_bytes.mid(int(off), int(len));
            break;
        }
    }
    QVERIFY(fvar_bytes.size() >= 16);
    const quint16 axes_off   = readU16Be(fvar_bytes, 4);
    const quint16 axis_count = readU16Be(fvar_bytes, 8);
    const quint16 axis_size  = readU16Be(fvar_bytes, 10);
    bool patched_wght        = false;
    for (int a = 0; a < axis_count; ++a) {
        const int off = axes_off + a * axis_size;
        if (fvar_bytes.mid(off, 4) == QByteArrayLiteral("wght")) {
            fvar_bytes[off + 12] = char(0x7F);
            fvar_bytes[off + 13] = char(0xFF);
            fvar_bytes[off + 14] = char(0xFF);
            fvar_bytes[off + 15] = char(0xFF);
            patched_wght         = true;
            break;
        }
    }
    QVERIFY(patched_wght);

    const QByteArray synth_vf
        = buildSyntheticSfntWithTables(base_bytes, {{"fvar", fvar_bytes}});
    QVERIFY(!synth_vf.isEmpty());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString synth_path = dir.filePath(QStringLiteral("max_fvar.ttf"));
    QFile out_file(synth_path);
    QVERIFY(out_file.open(QIODevice::WriteOnly));
    QCOMPARE(out_file.write(synth_vf), qint64(synth_vf.size()));
    out_file.close();

    FontInspectorEngine ref_engine;
    QVERIFY(ref_engine.openFile(vf_path));
    const GlyphOutlineData min_outline
        = ref_engine.glyphOutline(0, QChar('B'), {{"wght", 100.0f}});
    const GlyphOutlineData max_outline
        = ref_engine.glyphOutline(0, QChar('B'), {{"wght", 900.0f}});
    QVERIFY(min_outline.valid && max_outline.valid);
    QVERIFY(min_outline.points != max_outline.points);

    FontInspectorEngine engine;
    QVERIFY(engine.openFile(synth_path));
    const GlyphOutlineData clamped_outline
        = engine.glyphOutline(0, QChar('B'), {{"wght", 40000.0f}});
    QVERIFY(clamped_outline.valid);
    QVERIFY(clamped_outline.points != min_outline.points);
    QCOMPARE(clamped_outline.points, max_outline.points);
}

void ViewerTest::glyphPreviewHandlesExtremeContourEndWithoutOverflow()
{
    const QString font_path = fixturePath("Roboto-Regular.ttf");
    QVERIFY(!font_path.isEmpty());

    FontWidget fw;
    QVERIFY(fw.init(font_path));
    auto *cw = fw.findChild<CharacterWidget *>();
    QVERIFY(cw != nullptr);

    GlyphPreviewWidget preview;
    preview.resize(260, 280);
    preview.setShowOutlinePoints(true);

    GlyphOutlineData malformed;
    malformed.valid         = true;
    malformed.contour_count = 1;
    malformed.point_count   = 2;
    malformed.points        = {QPointF(0.0, 0.0), QPointF(100.0, 100.0)};
    malformed.tags
        = {GlyphOutlineData::OnCurve, GlyphOutlineData::OnCurve};
    malformed.contour_ends = {std::numeric_limits<int>::max()};

    FontDecorationMetrics deco;
    deco.units_per_em = 1000;
    preview.setGlyphData(QChar('A'), cw->currentFont(), QRectF(0, 0, 10, 10),
                         10.0, deco, malformed);
    preview.show();
    QVERIFY(QTest::qWaitForWindowExposed(&preview));
    preview.repaint();
    QCOMPARE(preview.lastOutlineScreenPoints().size(), 2);
}

void ViewerTest::openTypeFeatureParserBoundsOverlappingScriptAndLangTables()
{
    const QString base_path = fixturePath("Roboto-Regular.ttf");
    QVERIFY(!base_path.isEmpty());
    QFile f(base_path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray base_bytes = f.readAll();

    // Craft 1,500 staggered Script offsets (every 2 bytes) pointing into an
    // ASCII-encoded overlapping pool with 2,304 distinct relLang offsets so
    // exact-offset deduplication misses staggered Script and LangSys tables.
    constexpr int kStagger  = 1500;
    constexpr int kNumFeats = 3000;
    QByteArray feature_list;
    const quint16 feat_off = quint16(2 + kNumFeats * 6);
    appendU16(feature_list, kNumFeats);
    for (int i = 0; i < kNumFeats; ++i) {
        feature_list.append(i == 2827 ? "liga" : "smcp", 4);
        appendU16(feature_list, feat_off);
    }
    appendU16(feature_list, 0);
    appendU16(feature_list, 0);

    constexpr quint16 kScriptListOff  = 10;
    constexpr quint16 kFeatureListOff = kScriptListOff + 2 + kStagger * 6;
    const quint16 kLookupListOff      = quint16(kFeatureListOff + feature_list.size());
    const int kPoolStart              = kLookupListOff + 2;

    QByteArray script_list;
    appendU16(script_list, kStagger);
    for (int s = 0; s < kStagger; ++s) {
        script_list.append("latn", 4);
        appendU16(script_list, quint16((kPoolStart - kScriptListOff) + s * 2));
    }

    // Pool layout: [0..10000) holds ASCII words 0x3030..0x5F5F (values
    // 12336..24415) so every 4-byte tag is printable ASCII and every
    // langSysCount/relLang produces distinct LangSys offsets in [39356, 54435);
    // [10000..38000) holds 0x0B0B (2827) so every staggered LangSys reads
    // featCount = 2827 and valid featureIndex = 2827 ("liga").
    constexpr int kLangRel = 10000;
    QByteArray pool(38000, '\0');
    for (int i = 0; i < kLangRel; i += 2) {
        const int idx = i / 2;
        pool[i]       = char(0x30 + (idx / 48) % 48);
        pool[i + 1]   = char(0x30 + idx % 48);
    }
    for (int i = kLangRel; i + 1 < pool.size(); i += 2) {
        pool[i]     = char(0x0B);
        pool[i + 1] = char(0x0B);
    }

    QByteArray gsub;
    appendU16(gsub, 1);
    appendU16(gsub, 0);
    appendU16(gsub, kScriptListOff);
    appendU16(gsub, kFeatureListOff);
    appendU16(gsub, kLookupListOff);
    gsub.append(script_list);
    gsub.append(feature_list);
    appendU16(gsub, 0);  // lookupCount = 0
    gsub.append(pool);

    const QByteArray synth_ttf
        = buildSyntheticSfntWithTables(base_bytes, {{"GSUB", gsub}});
    QVERIFY(!synth_ttf.isEmpty());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString synth_path = dir.filePath(QStringLiteral("stagger_ot.ttf"));
    QFile out_file(synth_path);
    QVERIFY(out_file.open(QIODevice::WriteOnly));
    QCOMPARE(out_file.write(synth_ttf), qint64(synth_ttf.size()));
    out_file.close();

    QElapsedTimer timer;
    timer.start();
    FontInspectorEngine engine;
    QVERIFY(engine.openFile(synth_path));
    const qint64 elapsed_ms = timer.elapsed();

    QVERIFY(!engine.inspectFace(0).ot_features.isEmpty());
    QVERIFY2(elapsed_ms < 250,
             qPrintable(QString("staggered parse took %1 ms").arg(elapsed_ms)));
}

void ViewerTest::inspectorEngineHandlesLargeNameAliasTableWithoutQuadraticScan()
{
    const QString base_path = fixturePath("Roboto-Regular.ttf");
    QVERIFY(!base_path.isEmpty());
    QFile f(base_path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray base_bytes = f.readAll();

    // Craft 5,000 unique Platform 3 / UTF-16BE records all sharing nameID = 1.
    // O(N^2) QStringList::contains would perform ~12.5M case-insensitive checks.
    constexpr int kCount = 5000;
    QByteArray records;
    QByteArray strings;
    records.reserve(kCount * 12);
    strings.reserve(kCount * 10);
    for (int i = 0; i < kCount; ++i) {
        const QByteArray utf16
            = makeUtf16Be(QStringLiteral("F%1").arg(i, 4, 10, QChar('0')));
        appendU16(records, 3);
        appendU16(records, 1);
        appendU16(records, 0x0409);
        appendU16(records, 1);
        appendU16(records, quint16(utf16.size()));
        appendU16(records, quint16(strings.size()));
        strings.append(utf16);
    }

    QByteArray name_tbl;
    appendU16(name_tbl, 0);
    appendU16(name_tbl, kCount);
    appendU16(name_tbl, quint16(6 + records.size()));
    name_tbl.append(records);
    name_tbl.append(strings);

    const QByteArray synth_ttf
        = buildSyntheticSfntWithTables(base_bytes, {{"name", name_tbl}});
    QVERIFY(!synth_ttf.isEmpty());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString synth_path = dir.filePath(QStringLiteral("many_names.ttf"));
    QFile out_file(synth_path);
    QVERIFY(out_file.open(QIODevice::WriteOnly));
    QCOMPARE(out_file.write(synth_ttf), qint64(synth_ttf.size()));
    out_file.close();

    QElapsedTimer timer;
    timer.start();
    FontInspectorEngine engine;
    QVERIFY(engine.openFile(synth_path));
    const qint64 elapsed_ms = timer.elapsed();

    QCOMPARE(engine.resolveFaceIndex(QStringLiteral("f4999"), QString()), 0);
    QVERIFY2(elapsed_ms < 250,
             qPrintable(QString("name parse took %1 ms").arg(elapsed_ms)));
}

void ViewerTest::glyphPreviewSkipsChordBetweenAdjacentOffCubicHandles()
{
    const QString font_path = fixturePath("Roboto-Regular.ttf");
    QVERIFY(!font_path.isEmpty());

    FontWidget fw;
    QVERIFY(fw.init(font_path));
    auto *cw = fw.findChild<CharacterWidget *>();
    QVERIFY(cw != nullptr);

    GlyphPreviewWidget preview;
    preview.resize(320, 320);
    preview.setShowOutlinePoints(true);

    // P0(OnCurve) -> P1(OffCubic) -> P2(OffCubic) -> P3(OnCurve) forming a wide
    // horizontal bridge across y = 500 FUnits. Keep P0->P1 and P2->P3 away
    // from the vertical origin/advance lines so only tangent handles paint
    // handle orange at their midpoints, while the center (x = 350 FUnits)
    // must NOT have a chord drawn between P1 and P2.
    GlyphOutlineData cubic;
    cubic.valid         = true;
    cubic.contour_count = 1;
    cubic.point_count   = 4;
    cubic.points        = {
        QPointF(150.0, 0.0),
        QPointF(150.0, 500.0),
        QPointF(550.0, 500.0),
        QPointF(550.0, 0.0),
    };
    cubic.tags = {
        GlyphOutlineData::OnCurve,
        GlyphOutlineData::OffCubic,
        GlyphOutlineData::OffCubic,
        GlyphOutlineData::OnCurve,
    };
    cubic.contour_ends = {3};

    FontDecorationMetrics deco;
    deco.units_per_em = 1000;
    preview.setGlyphData(QChar(' '), cw->currentFont(), QRectF(0, 0, 10, 10),
                         10.0, deco, cubic);
    preview.show();
    QVERIFY(QTest::qWaitForWindowExposed(&preview));
    preview.repaint();

    const QVector<QPointF> pts = preview.lastOutlineScreenPoints();
    QCOMPARE(pts.size(), 4);

    const QImage img = preview.grab().toImage();

    auto hasHandleNear = [&img](const QPointF &pt) {
        const int cx = qRound(pt.x());
        const int cy = qRound(pt.y());
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                const int x = cx + dx;
                const int y = cy + dy;
                if (x < 0 || y < 0 || x >= img.width() || y >= img.height()) {
                    continue;
                }
                const QColor c = img.pixelColor(x, y);
                if (c.red() - c.blue() >= 40 && c.green() > c.blue()) {
                    return true;
                }
            }
        }
        return false;
    };

    const QPointF p0_p1_mid = (pts[0] + pts[1]) * 0.5;
    const QPointF p1_p2_mid = (pts[1] + pts[2]) * 0.5;
    const QPointF p2_p3_mid = (pts[2] + pts[3]) * 0.5;
    QVERIFY(hasHandleNear(p0_p1_mid));
    QVERIFY(!hasHandleNear(p1_p2_mid));
    QVERIFY(hasHandleNear(p2_p3_mid));
}

void ViewerTest::tabWidgetHasSingleGridEntryAndNoBlankGapOnStaticFont()
{
    const QString static_path = fixturePath("Roboto-Regular.ttf");
    QVERIFY(!static_path.isEmpty());

    FontWidget view;
    QVERIFY(view.init(static_path));
    view.resize(800, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QApplication::processEvents();

    auto *grid = qobject_cast<QGridLayout *>(view.layout());
    auto *tabs = view.findChild<QTabWidget *>();
    QVERIFY(grid != nullptr);
    QVERIFY(tabs != nullptr);

    int tab_item_count = 0;
    for (int i = 0; i < grid->count(); ++i) {
        if (grid->itemAt(i)->widget() == tabs) {
            ++tab_item_count;
        }
    }
    QCOMPARE(tab_item_count, 1);
    QVERIFY(tabs->height() > 350);
}

void ViewerTest::shortcutsSeedEmptyAndMissingIniAndAllowOverride()
{
    const QString font_path = fixturePath("Roboto-Regular.ttf");
    QVERIFY(!font_path.isEmpty());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini_path = dir.filePath(QStringLiteral("fontviewer.ini"));

    // Seed an empty value, a custom override, an explicit "none", and an
    // invalid Space binding; missing keys and empty values must be written with
    // defaults into [Shortcuts].
    {
        QSettings seed(ini_path, QSettings::IniFormat);
        seed.beginGroup(QStringLiteral("Shortcuts"));
        seed.setValue(QStringLiteral("toggle_collapse"), QString());
        seed.setValue(QStringLiteral("toggle_invert"),
                      QStringLiteral("Ctrl+Alt+I"));
        seed.setValue(QStringLiteral("save_svg"), QStringLiteral("none"));
        seed.setValue(QStringLiteral("toggle_waterfall"),
                      QStringLiteral("Space"));
        // Fixed zoom shortcuts must ignore any manual INI overrides.
        seed.setValue(QStringLiteral("zoom_in"), QStringLiteral("F1"));
        seed.setValue(QStringLiteral("zoom_out"), QStringLiteral("none"));
        seed.endGroup();
        seed.sync();
    }

    {
        ViewOptionsPrivate d;
        d.path = font_path;
        ViewOptions opts;
        opts.d_ptr = &d;

        TestFontViewer viewer(ini_path);
        viewer.load(nullptr, &opts);
        auto *fw = viewer.findChild<FontWidget *>();
        QVERIFY(fw != nullptr);

        QCOMPARE(fw->shortcutSequence(FontShortcutAction::ToggleCollapse),
                 FontShortcutMap::defaultSequence(
                     FontShortcutAction::ToggleCollapse));
        QCOMPARE(fw->shortcutSequence(FontShortcutAction::ToggleInvert),
                 QKeySequence(QStringLiteral("Ctrl+Alt+I")));
        QVERIFY(fw->shortcutSequence(FontShortcutAction::SaveSvg).isEmpty());
        QCOMPARE(fw->shortcutSequence(FontShortcutAction::ToggleWaterfall),
                 FontShortcutMap::defaultSequence(
                     FontShortcutAction::ToggleWaterfall));
        QCOMPARE(fw->shortcutSequence(FontShortcutAction::ZoomIn),
                 QKeySequence(QStringLiteral("Ctrl+="),
                              QKeySequence::PortableText));
        QCOMPARE(fw->shortcutSequence(FontShortcutAction::ZoomOut),
                 QKeySequence(QStringLiteral("Ctrl+-"),
                              QKeySequence::PortableText));

        QPushButton *invert_btn = nullptr;
        for (QPushButton *b : fw->findChildren<QPushButton *>()) {
            if (b->text() == QLatin1String("Invert")) {
                invert_btn = b;
                break;
            }
        }
        QVERIFY(invert_btn != nullptr);
        QVERIFY(invert_btn->toolTip().contains(
            QKeySequence(QStringLiteral("Ctrl+Alt+I"))
                .toString(QKeySequence::NativeText)));
    }

    QSettings check(ini_path, QSettings::IniFormat);
    check.beginGroup(QStringLiteral("Shortcuts"));
    for (int i = 0; i < kFontShortcutActionCount; ++i) {
        const auto action = static_cast<FontShortcutAction>(i);
        if (!FontShortcutMap::isCustomizable(action)) {
            QVERIFY(FontShortcutMap::iniKey(action).isEmpty());
            continue;
        }
        const QString key = FontShortcutMap::iniKey(action).section('/', 1);
        QVERIFY2(check.contains(key), qPrintable(key));
        QVERIFY2(!check.value(key).toString().trimmed().isEmpty(),
                 qPrintable(key));
    }
    QVERIFY(!check.contains(QStringLiteral("zoom_in")));
    QVERIFY(!check.contains(QStringLiteral("zoom_out")));
    QCOMPARE(
        check.value(QStringLiteral("toggle_collapse")).toString(),
        FontShortcutMap::defaultSequence(FontShortcutAction::ToggleCollapse)
            .toString(QKeySequence::PortableText));
    QCOMPARE(check.value(QStringLiteral("toggle_invert")).toString(),
             QStringLiteral("Ctrl+Alt+I"));
    QCOMPARE(check.value(QStringLiteral("save_svg")).toString(),
             QStringLiteral("none"));
    check.endGroup();
}

void ViewerTest::shortcutsExecuteActionsWhileFocusedEditorAcceptsText()
{
    const QString vf_path = fixturePath("Roboto-VariableFont_wdth,wght.ttf");
    QVERIFY(!vf_path.isEmpty());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini_path = dir.filePath(QStringLiteral("fontviewer.ini"));

    {
        QSettings seed(ini_path, QSettings::IniFormat);
        seed.setValue(QStringLiteral("Shortcuts/toggle_invert"),
                      QStringLiteral("I"));
        seed.sync();
    }

    ViewOptionsPrivate d;
    d.path = vf_path;
    ViewOptions opts;
    opts.d_ptr = &d;

    TestFontViewer viewer(ini_path);
    viewer.load(nullptr, &opts);
    viewer.resize(900, 700);
    viewer.show();
    QVERIFY(QTest::qWaitForWindowExposed(&viewer));
    QApplication::processEvents();

    auto *fw   = viewer.findChild<FontWidget *>();
    auto *tabs = viewer.findChild<QTabWidget *>();
    auto *edit = viewer.findChild<FontPreviewEdit *>();
    QVERIFY(fw != nullptr);
    QVERIFY(tabs != nullptr);
    QVERIFY(edit != nullptr);

    // Confirm all shortcut actions fire even when focus is outside FontWidget.
    QWidget *focus = QApplication::focusWidget();
    QVERIFY(!focus || (focus != fw && !fw->isAncestorOf(focus)));

    QTest::keyClick(&viewer, Qt::Key_B, Qt::ControlModifier);
    QVERIFY(fw->isCollapsed());
    QTest::keyClick(&viewer, Qt::Key_B, Qt::ControlModifier);
    QVERIFY(!fw->isCollapsed());

    QTest::keyClick(&viewer, Qt::Key_W,
                    Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(fw->isWaterfallEnabled());
    QTest::keyClick(&viewer, Qt::Key_W,
                    Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(!fw->isWaterfallEnabled());

    QTest::keyClick(&viewer, Qt::Key_O,
                    Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(fw->isShowOutlinePoints());
    QTest::keyClick(&viewer, Qt::Key_O,
                    Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(!fw->isShowOutlinePoints());

    const int initial_pt = fw->getCurrentFontSize();
    QTest::keyClick(&viewer, Qt::Key_Equal, Qt::ControlModifier);
    QVERIFY(fw->getCurrentFontSize() > initial_pt);
    QTest::keyClick(&viewer, Qt::Key_Minus, Qt::ControlModifier);
    QCOMPARE(fw->getCurrentFontSize(), initial_pt);

    QTest::keyClick(&viewer, Qt::Key_2, Qt::ControlModifier);
    QCOMPARE(tabs->currentIndex(), 1);
    QTest::keyClick(&viewer, Qt::Key_3, Qt::ControlModifier);
    QCOMPARE(tabs->currentIndex(), 2);
    QTest::keyClick(&viewer, Qt::Key_4, Qt::ControlModifier);
    QCOMPARE(tabs->currentIndex(), 3);
    QTest::keyClick(&viewer, Qt::Key_1, Qt::ControlModifier);
    QCOMPARE(tabs->currentIndex(), 0);

    // OpenType Reset action triggers when an override is active.
    const auto pills = fw->featurePillButtons();
    if (!pills.isEmpty()) {
        const QByteArray tag = pills.first()->text().left(4).toLatin1();
        const bool was_on    = fw->isFeatureEnabled(tag);
        fw->setFeatureEnabled(tag, !was_on);
        QCOMPARE(fw->isFeatureEnabled(tag), !was_on);
        QTest::keyClick(&viewer, Qt::Key_R,
                        Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(fw->isFeatureEnabled(tag), was_on);
    }

    // Copy as Image shortcut (Ctrl+Shift+C) populates clipboard.
    QApplication::clipboard()->clear();
    QTest::keyClick(&viewer, Qt::Key_C,
                    Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(!QApplication::clipboard()->pixmap().isNull());

    // While FontPreviewEdit has focus, a bare-letter override ("I") must be
    // accepted by the editor as text input instead of toggling Invert.
    const QString before_text = edit->toPlainText();
    edit->setFocus(Qt::MouseFocusReason);
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget *>(edit));
    QTest::keyClick(edit, Qt::Key_I);
    QApplication::processEvents();
    QVERIFY(!fw->isInvertPreview());
    QVERIFY(edit->toPlainText() != before_text);
}

void ViewerTest::getContentSizeIgnoresPluginCmdAndScalesDpr()
{
    const QString font_path = fixturePath("Roboto-Regular.ttf");
    QVERIFY(!font_path.isEmpty());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini_path = dir.filePath(QStringLiteral("fontviewer.ini"));

    ViewOptionsPrivate d;
    d.path                                   = font_path;
    d.dpr                                    = 1.5;
    d.extras[ViewOptionsKeys::kKeyPluginCmd] = QStringList{"-p", "50"};
    ViewOptions opts;
    opts.d_ptr = &d;

    TestFontViewer viewer(ini_path);
    viewer.load(nullptr, &opts);
    QCOMPARE(viewer.getContentSize(), QSize(qRound(850 * 1.5), 800 * 1.5));
}

void ViewerTest::glyphInspectorSmallDescentAndNarrowViewportNoOverlapOrOverflow()
{
    const QString font_path = fixturePath("Roboto-Regular.ttf");
    QVERIFY(!font_path.isEmpty());

    FontInspectorEngine engine;
    QVERIFY(engine.openFile(font_path));
    const FontDecorationMetrics deco = engine.decorationMetrics(0);

    // Narrow scroll area (320px wide, forced vertical scrollbar) must not
    // overflow horizontally when displaying long CJK Unicode block text.
    QScrollArea sa;
    sa.setWidgetResizable(true);
    sa.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    auto *gi = new GlyphInspectorWidget(&sa);
    sa.setWidget(gi);
    sa.resize(320, 460);
    sa.show();
    QVERIFY(QTest::qWaitForWindowExposed(&sa));

    QFont ft(QStringLiteral("Roboto"));
    ft.setPointSize(16);
    gi->updateGlyph(QChar(0x2F00), ft, font_path, deco);
    QApplication::processEvents();
    QApplication::processEvents();
    QCOMPARE(sa.horizontalScrollBar()->maximum(), 0);

    // Stress-test baseline + descent near each other at minimum preview size
    // with a font that has small/zero descent (using a space / tiny bbox).
    GlyphPreviewWidget preview;
    preview.resize(preview.minimumSize());
    preview.show();
    QVERIFY(QTest::qWaitForWindowExposed(&preview));

    ft.setPointSize(16);
    preview.setGlyphData(QChar('A'), ft, QRectF(0, -12, 10, 12), 12.0, deco);
    preview.repaint();
    const QVector<QRectF> rects = preview.lastLabelRects();
    QVERIFY(!rects.isEmpty());
    const QRectF bounds(preview.rect());
    for (int i = 0; i < rects.size(); ++i) {
        QVERIFY(bounds.contains(rects[i]));
        for (int j = i + 1; j < rects.size(); ++j) {
            QVERIFY(!rects[i].intersects(rects[j]));
        }
    }

    // Standard size: underline (negative underline_pos) must remain strictly
    // below baseline and never jump above baseline's line or invert order.
    QVERIFY(deco.has_underline && deco.underline_pos < 0);
    preview.resize(400, 360);
    QApplication::processEvents();
    preview.repaint();
    const QVector<QRectF> wide_rects = preview.lastLabelRects();
    const QStringList wide_texts     = preview.lastLabelTexts();
    QCOMPARE(wide_rects.size(), wide_texts.size());
    int baseline_idx  = -1;
    int underline_idx = -1;
    int descent_idx   = -1;
    for (int i = 0; i < wide_texts.size(); ++i) {
        if (wide_texts[i] == QLatin1String("baseline")) {
            baseline_idx = i;
        }
        else if (wide_texts[i].startsWith(QLatin1String("underline"))) {
            underline_idx = i;
        }
        else if (wide_texts[i] == QLatin1String("descent")) {
            descent_idx = i;
        }
    }
    QVERIFY(baseline_idx >= 0 && underline_idx >= 0 && descent_idx >= 0);
    QVERIFY2(wide_rects[baseline_idx].bottom()
                 <= wide_rects[underline_idx].top(),
             qPrintable(QString("baseline=(%1..%2) underline=(%3..%4)")
                            .arg(wide_rects[baseline_idx].top())
                            .arg(wide_rects[baseline_idx].bottom())
                            .arg(wide_rects[underline_idx].top())
                            .arg(wide_rects[underline_idx].bottom())));
    QVERIFY2(wide_rects[underline_idx].top()
                 >= preview.lastUnderlineBand().top(),
             qPrintable(QString("underline=(%1..%2) bandTop=%3")
                            .arg(wide_rects[underline_idx].top())
                            .arg(wide_rects[underline_idx].bottom())
                            .arg(preview.lastUnderlineBand().top())));
    QVERIFY2(wide_rects[descent_idx].top() > wide_rects[baseline_idx].bottom(),
             qPrintable(QString("descent=(%1..%2) baseline=(%3..%4) "
                                "underline=(%5..%6)")
                            .arg(wide_rects[descent_idx].top())
                            .arg(wide_rects[descent_idx].bottom())
                            .arg(wide_rects[baseline_idx].top())
                            .arg(wide_rects[baseline_idx].bottom())
                            .arg(wide_rects[underline_idx].top())
                            .arg(wide_rects[underline_idx].bottom())));
    for (int i = 0; i < wide_rects.size(); ++i) {
        QVERIFY(QRectF(preview.rect()).contains(wide_rects[i]));
        for (int j = i + 1; j < wide_rects.size(); ++j) {
            QVERIFY(!wide_rects[i].intersects(wide_rects[j]));
        }
    }
}

void ViewerTest::uiUsesDefaultCursorNoCjkPresetAndGroupsTypography()
{
    const QString roboto_vf = fixturePath("Roboto-VariableFont_wdth,wght.ttf");
    QVERIFY(!roboto_vf.isEmpty());

    FontWidget view;
    QVERIFY(view.init(roboto_vf));
    view.resize(850, 800);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QApplication::processEvents();

    // No button should alter the hover cursor to PointingHandCursor.
    const auto buttons = view.findChildren<QPushButton *>();
    QVERIFY(!buttons.isEmpty());
    for (const QPushButton *btn : buttons) {
        QCOMPARE(btn->cursor().shape(), Qt::ArrowCursor);
    }

    // Preset combobox must not contain the non-universal CJK preset and must be
    // wide enough to show its longest item without truncation.
    QComboBox *presets = nullptr;
    for (QComboBox *cb : view.findChildren<QComboBox *>()) {
        if (cb->findText(QStringLiteral("Paragraph")) >= 0) {
            presets = cb;
            break;
        }
    }
    QVERIFY(presets != nullptr);
    QCOMPARE(presets->findText(QStringLiteral("CJK Mixed")), -1);
    const int digits_idx = presets->findText(QStringLiteral("Digits & Symbols"));
    QVERIFY(digits_idx >= 0);
    QVERIFY(presets->width()
            >= presets->fontMetrics().horizontalAdvance(
                   QStringLiteral("Digits & Symbols"))
                   + 36);

    // Variable axes live in the Typography group box (which collapses cleanly
    // with the top header section), while OpenType features live inside the
    // Text preview tab drawer (collapsed by default).
    auto *axes     = view.findChild<VariableAxesBar *>();
    auto *feat     = view.findChild<OpenTypeFeaturesBar *>();
    auto *text_tab = view.findChild<TextPreviewTab *>();
    QVERIFY(axes != nullptr);
    QVERIFY(feat != nullptr);
    QVERIFY(text_tab != nullptr);
    auto *group = qobject_cast<QGroupBox *>(axes->parentWidget());
    QVERIFY(group != nullptr);
    QCOMPARE(feat->parentWidget(), static_cast<QWidget *>(text_tab));
    QVERIFY(!feat->isVisible());
    QCOMPARE(group->title(), QStringLiteral("Typography"));
    QVERIFY(group->isVisible());

    view.setCollapsed(true);
    QApplication::processEvents();
    QVERIFY(!group->isVisible());
    // Collapsed header shows family, weight/style, and current point size.
    QLabel *collapsed_lbl = nullptr;
    for (QLabel *lbl : view.findChildren<QLabel *>()) {
        if (lbl->isVisible() && lbl->alignment().testFlag(Qt::AlignCenter)
            && lbl->font().bold()
            && lbl->text().contains(QStringLiteral("pt"))) {
            collapsed_lbl = lbl;
            break;
        }
    }
    QVERIFY(collapsed_lbl != nullptr);
    QVERIFY(collapsed_lbl->text().contains(QStringLiteral("400")));
    QVERIFY(collapsed_lbl->text().contains(QStringLiteral("16 pt")));
    view.setCurrentFontSize(24);
    view.setVariableAxisValue(QByteArrayLiteral("wght"), 650.0f);
    QApplication::processEvents();
    QVERIFY(collapsed_lbl->text().contains(QStringLiteral("650")));
    QVERIFY(collapsed_lbl->text().contains(QStringLiteral("24 pt")));
    view.setCurrentFontSize(16);
    view.setVariableAxisValue(QByteArrayLiteral("wght"), 400.0f);

    view.setCollapsed(false);
    QApplication::processEvents();
    QVERIFY(group->isVisible());

    // Text toolbar Reset restores preview parameters while keeping text and
    // preset, without changing toolbar height.
    auto *edit_box = text_tab->findChild<FontPreviewEdit *>();
    auto *waterfall_area = text_tab->findChild<QScrollArea *>(
        QStringLiteral("text_waterfall_scroll"));
    QVERIFY(edit_box != nullptr && waterfall_area != nullptr);
    QVERIFY(edit_box->verticalScrollBar()->property("is_readonly").toBool());
    QVERIFY(edit_box->horizontalScrollBar()->property("is_readonly").toBool());
    QVERIFY(waterfall_area->verticalScrollBar()
                ->property("is_readonly")
                .toBool());
    QVERIFY(waterfall_area->horizontalScrollBar()
                ->property("is_readonly")
                .toBool());
    QPushButton *reset_text_btn = text_tab->findChild<QPushButton *>(
        QStringLiteral("text_toolbar_reset_btn"));
    QVERIFY(reset_text_btn != nullptr);
    QVERIFY(!reset_text_btn->isVisible());

    QWidget *toolbar = presets->parentWidget();
    QVERIFY(toolbar != nullptr);
    const int tb_h_before = toolbar->height();
    const int para_idx    = presets->findText(QStringLiteral("Paragraph"));
    QVERIFY(para_idx >= 0);
    presets->setCurrentIndex(para_idx);
    QApplication::processEvents();
    // Changing preset/text alone does not trigger parameter Reset.
    QVERIFY(!reset_text_btn->isVisible());

    view.setInvertPreview(true);
    view.setWaterfallEnabled(true);
    view.setLineHeight(2.0);
    view.setLetterSpacing(3.0);
    QApplication::processEvents();
    QVERIFY(reset_text_btn->isVisible());
    QCOMPARE(toolbar->height(), tb_h_before);
    QSlider *height_slider = nullptr;
    for (QSlider *s : text_tab->findChildren<QSlider *>()) {
        if (s->maximum() == 25) {
            height_slider = s;
            break;
        }
    }
    QVERIFY(height_slider != nullptr);
    QVERIFY(!height_slider->isEnabled());

    reset_text_btn->click();
    QApplication::processEvents();
    QVERIFY(!reset_text_btn->isVisible());
    QVERIFY(!view.isInvertPreview());
    QVERIFY(!view.isWaterfallEnabled());
    QVERIFY(height_slider->isEnabled());
    QCOMPARE(view.getLineHeight(), 1.4);
    QCOMPARE(view.getLetterSpacing(), 0.0);
    QCOMPARE(presets->currentText(), QStringLiteral("Paragraph"));
    QCOMPARE(toolbar->height(), tb_h_before);
}

void ViewerTest::featurePillsLightModeContrastAndCheckedIndicator()
{
    const QString roboto_vf = fixturePath("Roboto-VariableFont_wdth,wght.ttf");
    QVERIFY(!roboto_vf.isEmpty());

    FontWidget view;
    QVERIFY(view.init(roboto_vf));

    // Mirror Host MaterialStyle light palette where QPalette::Button is
    // transparent (0, 0, 0, 0). Unchecked pills must remain light/neutral.
    QPalette light = makeThemePalette(false);
    light.setColor(QPalette::All, QPalette::Window, QColor(0xFF, 0xFF, 0xFF));
    light.setColor(QPalette::All, QPalette::WindowText, QColor(0x21, 0x21, 0x21));
    light.setColor(QPalette::All, QPalette::Button, QColor(0, 0, 0, 0));
    light.setColor(QPalette::All, QPalette::Highlight, QColor(0x00, 0x6F, 0xA8));
    light.setColor(QPalette::All, QPalette::HighlightedText, QColor(0xFF, 0xFF, 0xFF));
    view.setPalette(light);
    view.updateTheme(0);
    view.resize(850, 800);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QApplication::processEvents();

    auto *feat_btn = view.findChild<QPushButton *>(
        QStringLiteral("text_features_btn"));
    QVERIFY(feat_btn != nullptr);
    feat_btn->click();
    QApplication::processEvents();

    QPushButton *pill = nullptr;
    for (QPushButton *b : view.featurePillButtons()) {
        if (!b->isChecked()) {
            pill = b;
            break;
        }
    }
    QVERIFY(pill != nullptr);

    const QImage unchecked_img = pill->grab().toImage();
    QVERIFY(!unchecked_img.isNull());
    auto *feat = view.findChild<OpenTypeFeaturesBar *>();
    QVERIFY(feat != nullptr);
    // Sample near left-inside of unchecked pill; lightness must be high (>210),
    // not muddy dark gray caused by QPalette::Button alpha(130).
    const QColor unchecked_bg
        = unchecked_img.pixelColor(unchecked_img.width() / 2, 4);
    QVERIFY2(unchecked_bg.lightness() > 210,
             qPrintable(QString("Unexpected dark pill fill lightness: %1")
                            .arg(unchecked_bg.lightness())));

    const int bar_h_before  = feat->height();
    const int pill_y_before = pill->mapTo(&view, QPoint(0, 0)).y();

    pill->setChecked(true);
    QApplication::processEvents();
    const int bar_h_after  = feat->height();
    const int pill_y_after = pill->mapTo(&view, QPoint(0, 0)).y();
    QCOMPARE(bar_h_after, bar_h_before);
    QCOMPARE(pill_y_after, pill_y_before);

    const QImage checked_img = pill->grab().toImage();
    QVERIFY(checked_img != unchecked_img);
    // Checked pill draws a subtle accent badge on the left (near accent hue).
    bool saw_accent = false;
    for (int y = 0; y < checked_img.height() && !saw_accent; ++y) {
        for (int x = 0; x < checked_img.width() / 2; ++x) {
            const QColor c = checked_img.pixelColor(x, y);
            if (c.blue() > c.red() + 40) {
                saw_accent = true;
                break;
            }
        }
    }
    QVERIFY(saw_accent);
}

void ViewerTest::dprScalingUpdatesLayoutMetricsAndIcons()
{
    const QString roboto_vf = fixturePath("Roboto-VariableFont_wdth,wght.ttf");
    QVERIFY(!roboto_vf.isEmpty());

    FontWidget view;
    QVERIFY(view.init(roboto_vf));
    view.resize(850, 800);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QApplication::processEvents();

    auto *cw   = view.findChild<CharacterWidget *>();
    auto *gi   = view.findChild<GlyphInspectorWidget *>();
    auto *feat = view.findChild<OpenTypeFeaturesBar *>();
    QVERIFY(cw != nullptr && gi != nullptr && feat != nullptr);
    auto *preview = gi->previewWidget();
    QVERIFY(preview != nullptr);

    QPushButton *copy_btn = nullptr;
    for (QPushButton *b : view.findChildren<QPushButton *>()) {
        if (b->toolTip().startsWith(QStringLiteral("Copy as Image"))) {
            copy_btn = b;
            break;
        }
    }
    QVERIFY(copy_btn != nullptr);

    const int margin_1x = view.layout()->contentsMargins().left();
    const int cw_w_1x   = static_cast<QWidget *>(cw)->sizeHint().width();
    const int min_pv_1x = preview->minimumWidth();
    const int pv_hint_h_1x = static_cast<const QWidget *>(preview)->sizeHint().height();
    auto *line_val = view.findChild<QLabel *>(
        QStringLiteral("text_line_value_label"));
    QVERIFY(line_val != nullptr);
    const int badge_w_1x = line_val->width();
    const int btn_1x    = copy_btn->width();
    const int icon_1x   = copy_btn->iconSize().width();

    view.updateDPR(2.0);
    QApplication::processEvents();

    QCOMPARE(view.layout()->contentsMargins().left(), margin_1x * 2);
    QCOMPARE(preview->minimumWidth(), min_pv_1x * 2);
    QCOMPARE(static_cast<const QWidget *>(preview)->sizeHint().height(),
             pv_hint_h_1x * 2);
    QVERIFY(static_cast<const QWidget *>(preview)->sizeHint().height()
            >= preview->minimumHeight());
    QVERIFY(line_val->width() > badge_w_1x);
    QCOMPARE(copy_btn->width(), btn_1x * 2);
    QCOMPARE(copy_btn->iconSize().width(), icon_1x * 2);
    QVERIFY(static_cast<QWidget *>(cw)->sizeHint().width() >= cw_w_1x);

    // Waterfall disables Height slider & its value badge; badge must paint
    // dimmed state rather than full-contrast active text.
    const QImage badge_enabled = line_val->grab().toImage();
    view.setWaterfallEnabled(true);
    QApplication::processEvents();
    QVERIFY(!line_val->isEnabled());
    const QImage badge_disabled = line_val->grab().toImage();
    QVERIFY(badge_disabled != badge_enabled);
}

void ViewerTest::iniPresetsSeedLoadElideAndPreserveUserText()
{
    const QString font_path = fixturePath("Roboto-VariableFont_wdth,wght.ttf");
    QVERIFY(!font_path.isEmpty());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini_path = dir.filePath(QStringLiteral("fontviewer.ini"));

    auto find_preset_combo = [](QObject *root) -> QComboBox * {
        for (QComboBox *cb : root->findChildren<QComboBox *>()) {
            if (cb->findText(QStringLiteral("Paragraph")) >= 0) {
                return cb;
            }
        }
        return nullptr;
    };

    int min_w_builtin = 0;
    // 1. Fresh INI: seeds the 3 default example presets into [Presets] and
    //    appends them below a separator without widening the combobox.
    {
        ViewOptionsPrivate d;
        d.path = font_path;
        ViewOptions opts;
        opts.d_ptr = &d;

        TestFontViewer viewer(ini_path);
        viewer.load(nullptr, &opts);
        auto *fw = viewer.findChild<FontWidget *>();
        QVERIFY(fw != nullptr);

        QComboBox *presets = find_preset_combo(fw);
        QVERIFY(presets != nullptr);
        QCOMPARE(presets->count(), 7);
        QCOMPARE(presets->itemData(3, Qt::UserRole + 1).toInt(), 3);
        QVERIFY(presets->findText(QStringLiteral("Albus_Dumbledore")) >= 4);
        QVERIFY(presets->findText(QStringLiteral("Best_App")) >= 4);
        QVERIFY(presets->findText(QStringLiteral("BoJack")) >= 4);
        min_w_builtin = presets->minimumWidth();
        QVERIFY(min_w_builtin > 0);
    }
    {
        QFile raw_ini(ini_path);
        QVERIFY(raw_ini.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString raw_text = QString::fromUtf8(raw_ini.readAll());
        QVERIFY(!raw_text.contains(QStringLiteral("%20")));
        QVERIFY(!raw_text.contains(QStringLiteral("\\\"")));

        QSettings check(ini_path, QSettings::IniFormat);
        QCOMPARE(
            check.value(QStringLiteral("Presets/BoJack")).toString(),
            QStringLiteral(
                "\u201CIn this terrifying world all we have are the "
                "connections that we make.\u201D\n- BoJack"));
        QCOMPARE(
            check.value(QStringLiteral("Presets/Albus_Dumbledore")).toString(),
            QStringLiteral(
                "\u201CHappiness can be found, even in the darkest of times, "
                "if one only remembers to turn on the light.\u201D\n- Albus "
                "Dumbledore"));
        QCOMPARE(
            check.value(QStringLiteral("Presets/Best_App")).toString(),
            QStringLiteral("Seer is the best app in this universe. \U0001F680"));
    }

    // 2. Populated [Presets]: empty/whitespace skipped, \\n unescaped, commas
    //    intact, oversized key elided with tooltip, content capped at
    //    kMaxPasteChars, long keys do not widen combobox, and Last used /
    //    user_text is preserved when selecting a custom preset.
    const QString long_key
        = QStringLiteral("VeryLongCustomPresetTitleExceedingLimit");
    QVERIFY(long_key.size() > 20);
    {
        QSettings seed(ini_path, QSettings::IniFormat);
        seed.setValue(QStringLiteral("user_text"),
                      QStringLiteral("kept custom text"));
        seed.beginGroup(QStringLiteral("Presets"));
        seed.remove(QStringLiteral(""));
        seed.setValue(QStringLiteral("custom_1"), QString());
        seed.setValue(QStringLiteral("blank"), QStringLiteral("   "));
        seed.setValue(QStringLiteral("CJK"), QStringLiteral("永\\n和"));
        seed.setValue(QStringLiteral("CommaLine"),
                      QStringLiteral("Alpha, Beta, Gamma"));
        seed.setValue(long_key, QStringLiteral("Long key test"));
        seed.setValue(
            QStringLiteral("TooLong"),
            QString(FontPreviewEdit::kMaxPasteChars + 123, QLatin1Char('x')));
        seed.endGroup();
        seed.sync();
    }

    int min_w_last_used_no_custom = 0;
    {
        QSettings empty_presets(dir.filePath(QStringLiteral("empty.ini")),
                                QSettings::IniFormat);
        TextPreviewTab probe;
        probe.loadPresets(empty_presets);
        probe.setCurrentText(QStringLiteral("kept custom text"));
        QComboBox *probe_cb = probe.findChild<QComboBox *>();
        QVERIFY(probe_cb != nullptr);
        min_w_last_used_no_custom = probe_cb->minimumWidth();
    }

    {
        ViewOptionsPrivate d;
        d.path = font_path;
        ViewOptions opts;
        opts.d_ptr = &d;

        TestFontViewer viewer(ini_path);
        viewer.load(nullptr, &opts);
        auto *fw       = viewer.findChild<FontWidget *>();
        auto *text_tab = fw ? fw->findChild<TextPreviewTab *>() : nullptr;
        QVERIFY(fw != nullptr && text_tab != nullptr);

        QComboBox *presets = find_preset_combo(text_tab);
        QVERIFY(presets != nullptr);

        // Long custom key must not widen combobox beyond built-in + Last used.
        QCOMPARE(presets->minimumWidth(), min_w_last_used_no_custom);
        QVERIFY(min_w_last_used_no_custom >= min_w_builtin);

        // Expected items: Last used (0), 3 built-in (1..3), separator (4),
        // 4 non-empty custom presets (5..8).
        QCOMPARE(presets->count(), 9);
        QCOMPARE(presets->itemData(0, Qt::UserRole + 1).toInt(), 1);
        QCOMPARE(presets->itemData(4, Qt::UserRole + 1).toInt(), 3);

        const int cjk_idx = presets->findText(QStringLiteral("CJK"));
        QVERIFY(cjk_idx >= 5);
        QCOMPARE(presets->itemData(cjk_idx, Qt::UserRole + 1).toInt(), 2);
        QCOMPARE(presets->itemData(cjk_idx, Qt::UserRole).toString(),
                 QStringLiteral("永\n和"));
        QCOMPARE(presets->itemData(cjk_idx, Qt::ToolTipRole).toString(),
                 QStringLiteral("CJK"));

        const int comma_idx = presets->findText(QStringLiteral("CommaLine"));
        QVERIFY(comma_idx >= 5);
        QCOMPARE(presets->itemData(comma_idx, Qt::UserRole).toString(),
                 QStringLiteral("Alpha, Beta, Gamma"));

        int long_idx = -1;
        for (int i = 5; i < presets->count(); ++i) {
            if (presets->itemData(i, Qt::ToolTipRole).toString() == long_key) {
                long_idx = i;
                break;
            }
        }
        QVERIFY(long_idx >= 5);
        const QString elided = presets->itemText(long_idx);
        QCOMPARE(elided.size(), 20);
        QVERIFY(elided.endsWith(QChar(0x2026)));
        QCOMPARE(presets->itemData(long_idx, Qt::UserRole).toString(),
                 QStringLiteral("Long key test"));

        const int too_long_idx = presets->findText(QStringLiteral("TooLong"));
        QVERIFY(too_long_idx >= 5);
        QCOMPARE(presets->itemData(too_long_idx, Qt::UserRole).toString().size(),
                 FontPreviewEdit::kMaxPasteChars);

        // Selecting CJK preset via popup QMenu updates preview without
        // overwriting user_text, preserves '&' without mnemonics, and
        // includes a real QMenu separator before custom items.
        presets->showPopup();
        auto *popup_menu = presets->findChild<QMenu *>();
        QVERIFY(popup_menu != nullptr);
        QVERIFY(popup_menu->isVisible());
        const QList<QAction *> acts = popup_menu->actions();
        QCOMPARE(acts.size(), 9);
        QVERIFY(acts[4]->isSeparator());
        QCOMPARE(acts[3]->text(), QStringLiteral("Digits && Symbols"));
        QVERIFY(acts[0]->isChecked());
        acts[cjk_idx]->trigger();
        presets->hidePopup();

        auto *edit = text_tab->findChild<FontPreviewEdit *>();
        QVERIFY(edit != nullptr);
        QCOMPARE(edit->toPlainText(), QStringLiteral("永\n和"));
        QCOMPARE(fw->getLastEditedText(), QStringLiteral("kept custom text"));

        // Re-loading [Presets] replaces custom items cleanly (no dup separator).
        QSettings reload_ini(dir.filePath(QStringLiteral("reload.ini")),
                             QSettings::IniFormat);
        reload_ini.setValue(QStringLiteral("Presets/OnlyOne"),
                            QStringLiteral("Single"));
        reload_ini.sync();
        text_tab->loadPresets(reload_ini);
        QCOMPARE(presets->count(), 6);
        QCOMPARE(presets->itemData(4, Qt::UserRole + 1).toInt(), 3);
        QCOMPARE(presets->itemText(5), QStringLiteral("OnlyOne"));
    }

    {
        QSettings check(ini_path, QSettings::IniFormat);
        QCOMPARE(check.value(QStringLiteral("user_text")).toString(),
                 QStringLiteral("kept custom text"));
    }
}

void ViewerTest::toolbarReflowsWhenResetTogglesAndAvoidsDanglingPresetSeparator()
{
    const QString font_path = fixtureFont();
    if (font_path.isEmpty()) {
        QSKIP("set FONTVIEWER_TEST_FONT to a font file to run viewer tests");
    }

    FontWidget view;
    QVERIFY(view.init(font_path));
    auto *text_tab = view.findChild<TextPreviewTab *>();
    QVERIFY(text_tab != nullptr);

    QPushButton *aa_btn = nullptr;
    QPushButton *reset_btn = text_tab->findChild<QPushButton *>(
        QStringLiteral("text_toolbar_reset_btn"));
    for (QPushButton *btn : text_tab->findChildren<QPushButton *>()) {
        if (btn->text() == QLatin1String("Aa")) {
            aa_btn = btn;
            break;
        }
    }
    QVERIFY(aa_btn != nullptr && reset_btn != nullptr);
    QWidget *toolbar = aa_btn->parentWidget();
    QVERIFY(toolbar != nullptr);

    // Pick a window width where the inline toolbar fits WITHOUT the Reset
    // button, but overflows once Reset becomes visible.
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QApplication::processEvents();

    const int inline_w_no_reset = toolbar->sizeHint().width();
    QVERIFY(!aa_btn->isVisible());
    QVERIFY(!reset_btn->isVisible());

    view.setInvertPreview(true);
    QApplication::processEvents();
    const int inline_w_with_reset = toolbar->sizeHint().width();
    QVERIFY(inline_w_with_reset > inline_w_no_reset);
    view.setInvertPreview(false);
    QApplication::processEvents();

    const int boundary_tab_w = (inline_w_no_reset + inline_w_with_reset) / 2;
    const int chrome_dx      = view.width() - text_tab->width();
    view.resize(boundary_tab_w + chrome_dx, 600);
    QApplication::processEvents();
    QVERIFY(!aa_btn->isVisible());
    QVERIFY(!reset_btn->isVisible());

    // Enabling a custom parameter makes Reset visible, so the toolbar must
    // collapse Height/Spacing into Aa instead of clipping children.
    view.setInvertPreview(true);
    QApplication::processEvents();
    QVERIFY(reset_btn->isVisible());
    QVERIFY(aa_btn->isVisible());
    for (QWidget *w : toolbar->findChildren<QWidget *>(
             QString(), Qt::FindDirectChildrenOnly)) {
        if (w->isVisible()) {
            QVERIFY(w->geometry().right() < toolbar->width());
        }
    }

    // Clicking Reset hides Reset and expands the toolbar back inline.
    reset_btn->click();
    QApplication::processEvents();
    QVERIFY(!reset_btn->isVisible());
    QVERIFY(!aa_btn->isVisible());

    // Pre-show custom state and DPR scaling must both feed current hints into
    // TextToolbar::sizeHint().
    {
        FontWidget hidden_view;
        QVERIFY(hidden_view.init(font_path));
        auto *hidden_tab = hidden_view.findChild<TextPreviewTab *>();
        QVERIFY(hidden_tab != nullptr);
        QPushButton *hidden_aa = nullptr;
        for (QPushButton *b : hidden_tab->findChildren<QPushButton *>()) {
            if (b->text() == QLatin1String("Aa")) {
                hidden_aa = b;
                break;
            }
        }
        QVERIFY(hidden_aa != nullptr);
        QWidget *hidden_tb = hidden_aa->parentWidget();
        QVERIFY(hidden_tb != nullptr);
        const int w_default = hidden_tb->sizeHint().width();
        hidden_view.setInvertPreview(true);
        const int w_with_reset = hidden_tb->sizeHint().width();
        QVERIFY(w_with_reset > w_default);
        hidden_view.updateDPR(2.0);
        QVERIFY(hidden_tb->sizeHint().width() > w_with_reset);
    }

    // Blank custom presets must not leave a trailing separator in the combo.
    QComboBox *presets = nullptr;
    for (QComboBox *cb : text_tab->findChildren<QComboBox *>()) {
        if (cb->findText(QStringLiteral("Paragraph")) >= 0) {
            presets = cb;
            break;
        }
    }
    QVERIFY(presets != nullptr);
    const int base_count = presets->count();
    text_tab->setCustomPresets({
        {QStringLiteral("   "), QStringLiteral("valid")},
        {QStringLiteral("Blank"), QStringLiteral("   \n  ")},
    });
    QCOMPARE(presets->count(), base_count);
    QVERIFY(presets->itemData(presets->count() - 1, Qt::UserRole + 1).toInt()
            != 3);
}

void ViewerTest::openTypeFeatureParserBoundsEmptyScriptLangAndOobOffsets()
{
    const QString base_path = fixturePath("Roboto-Regular.ttf");
    QVERIFY(!base_path.isEmpty());
    QFile f(base_path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray base_bytes = f.readAll();

    // Build a GSUB table with 4,000 staggered scripts having langSysCount = 0
    // and out-of-bounds defaultLangSys offsets, plus a valid script "latn".
    constexpr int kScripts = 4000;
    QByteArray feature_list;
    appendU16(feature_list, 1);
    feature_list.append("liga", 4);
    appendU16(feature_list, 8);
    appendU16(feature_list, 0);
    appendU16(feature_list, 0);

    QByteArray valid_script;
    appendU16(valid_script, 4);     // defaultLangSysOffset
    appendU16(valid_script, 0);     // langSysCount = 0
    appendU16(valid_script, 0);     // lookupOrderOffset
    appendU16(valid_script, 0);     // requiredFeatureIndex = 0 ("liga")
    appendU16(valid_script, 0);     // featureCount = 0

    constexpr quint16 kScriptListOff = 10;
    const int script_list_hdr        = 2 + kScripts * 6;
    const quint16 kValidScriptRel    = quint16(script_list_hdr);
    const quint16 kPoolRel
        = quint16(script_list_hdr + valid_script.size());

    QByteArray script_list;
    appendU16(script_list, kScripts);
    script_list.append("latn", 4);
    appendU16(script_list, kValidScriptRel);
    for (int s = 1; s < kScripts; ++s) {
        script_list.append("latn", 4);
        appendU16(script_list, quint16(kPoolRel + (s - 1) * 4));
    }
    script_list.append(valid_script);
    for (int s = 1; s < kScripts; ++s) {
        // Out-of-bounds defaultLangSysOffset (0xFFFE) and langSysCount = 0.
        appendU16(script_list, 0xFFFE);
        appendU16(script_list, 0);
    }

    const quint16 feature_list_off = quint16(kScriptListOff + script_list.size());
    const quint16 lookup_list_off
        = quint16(feature_list_off + feature_list.size());

    QByteArray gsub;
    appendU16(gsub, 1);
    appendU16(gsub, 0);
    appendU16(gsub, kScriptListOff);
    appendU16(gsub, feature_list_off);
    appendU16(gsub, lookup_list_off);
    gsub.append(script_list);
    gsub.append(feature_list);
    appendU16(gsub, 0);

    QByteArray empty_gpos;
    appendU16(empty_gpos, 1);
    appendU16(empty_gpos, 0);
    appendU16(empty_gpos, 10);
    appendU16(empty_gpos, 12);
    appendU16(empty_gpos, 14);
    appendU16(empty_gpos, 0);
    appendU16(empty_gpos, 0);
    appendU16(empty_gpos, 0);

    const QByteArray synth_ttf = buildSyntheticSfntWithTables(
        base_bytes, {{"GSUB", gsub}, {"GPOS", empty_gpos}});
    QVERIFY(!synth_ttf.isEmpty());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString synth_path = dir.filePath(QStringLiteral("oob_ot.ttf"));
    QFile out_file(synth_path);
    QVERIFY(out_file.open(QIODevice::WriteOnly));
    QCOMPARE(out_file.write(synth_ttf), qint64(synth_ttf.size()));
    out_file.close();

    FontInspectorEngine engine;
    QVERIFY(engine.openFile(synth_path));
    const auto &feats = engine.inspectFace(0).ot_features;
    QCOMPARE(feats.size(), 1);
    QCOMPARE(feats.first().tag, QByteArrayLiteral("liga"));
}

void ViewerTest::characterWidgetTooltipEscapesHtmlAndHidesOnEmptySpace()
{
    const QString font_path = fixturePath("Roboto-Regular.ttf");
    if (font_path.isEmpty()) {
        QSKIP("set FONTVIEWER_TEST_FONT to a font file to run viewer tests");
    }

    CharacterWidget cw;
    cw.init(font_path);
    cw.updateFont(QFont(QStringLiteral("Roboto"), 16));
    cw.resize(520, 420);
    cw.show();
    QVERIFY(QTest::qWaitForWindowExposed(&cw));
    QApplication::processEvents();

    // Find '<' (0x3C) in the adaptive grid by scanning rows.
    const int sq       = cw.cellSizePx();
    const int cols     = cw.columnCount();
    const int origin_x = qMax(0, (cw.width() - cols * sq) / 2);
    const int pad_y    = 4;
    QPoint lt_pt;
    for (int row = 0; row < 20 && lt_pt.isNull(); ++row) {
        for (int col = 0; col < cols; ++col) {
            const QPoint pt(origin_x + col * sq + sq / 2,
                            pad_y + row * sq + sq / 2);
            QSignalSpy spy(&cw, &CharacterWidget::selectionChangedCodePoint);
            QMouseEvent press(QEvent::MouseButtonPress, pt, cw.mapToGlobal(pt),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&cw, &press);
            if (!spy.isEmpty() && spy.at(0).at(0).toUInt() == '<') {
                lt_pt = pt;
                break;
            }
        }
    }
    QVERIFY(!lt_pt.isNull());

    QMouseEvent move_lt(QEvent::MouseMove, lt_pt, cw.mapToGlobal(lt_pt),
                        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&cw, &move_lt);
    QVERIFY(!QToolTip::text().contains(QStringLiteral("\"><</span>")));
    QVERIFY(!QToolTip::text().contains(QStringLiteral("font-size:26pt")));

    // Hovering margin outside grid columns must not show any 0x0/U+0000 tip.
    const QPoint empty_pt(cw.width() - 1, cw.height() - 1);
    QMouseEvent move_empty(QEvent::MouseMove, empty_pt,
                           cw.mapToGlobal(empty_pt), Qt::NoButton, Qt::NoButton,
                           Qt::NoModifier);
    QApplication::sendEvent(&cw, &move_empty);
    QVERIFY(!QToolTip::text().contains(QStringLiteral("U+0000")));
    QVERIFY(!QToolTip::text().contains(QStringLiteral("0x0")));
}

void ViewerTest::paletteChangeUpdatesToolbarIconsSeparatorAndEmptyLabel()
{
    const QString vf_path = fixturePath("Roboto-VariableFont_wdth,wght.ttf");
    if (vf_path.isEmpty()) {
        QSKIP("set FONTVIEWER_TEST_FONT to a font file to run viewer tests");
    }

    FontWidget view;
    QVERIFY(view.init(vf_path));
    QPalette light_pal = makeThemePalette(false);
    light_pal.setColor(QPalette::All, QPalette::Mid, QColor(0x70, 0x70, 0x70));
    view.setPalette(light_pal);
    view.updateTheme(0);
    view.resize(850, 650);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QApplication::processEvents();

    QPushButton *copy_btn = nullptr;
    for (QPushButton *b : view.findChildren<QPushButton *>()) {
        if (b->toolTip().startsWith(QStringLiteral("Copy as Image"))) {
            copy_btn = b;
            break;
        }
    }
    QVERIFY(copy_btn != nullptr);
    const QImage light_icon = copy_btn->icon().pixmap(14, 14).toImage();

    auto *gi = view.findChild<GlyphInspectorWidget *>();
    QVERIFY(gi != nullptr);
    QLabel *empty_lbl = nullptr;
    for (QLabel *lbl : gi->findChildren<QLabel *>()) {
        if (lbl->text().contains(QStringLiteral("Select a character"))) {
            empty_lbl = lbl;
            break;
        }
    }
    QVERIFY(empty_lbl != nullptr);
    const QColor light_lbl_color = empty_lbl->palette().color(QPalette::WindowText);

    QWidget *sep_widget = nullptr;
    for (QWidget *w : view.findChildren<QWidget *>()) {
        if (w->property("flow_separator").toBool()) {
            sep_widget = w;
            break;
        }
    }
    QVERIFY(sep_widget != nullptr);
    const QColor light_sep_color = sep_widget->palette().color(QPalette::WindowText);

    // Switch only the widget palette to dark without calling updateTheme(1)
    // explicitly: PaletteChange must update baked icons and custom-palette labels.
    QPalette dark_pal = makeThemePalette(true);
    dark_pal.setColor(QPalette::All, QPalette::Mid, QColor(0x88, 0x99, 0xAA));
    view.setPalette(dark_pal);
    QApplication::processEvents();

    const QImage dark_icon = copy_btn->icon().pixmap(14, 14).toImage();
    QVERIFY(dark_icon != light_icon);
    QVERIFY(empty_lbl->palette().color(QPalette::WindowText) != light_lbl_color);
    QCOMPARE(empty_lbl->palette().color(QPalette::WindowText),
             QColor(0x88, 0x99, 0xAA));
    QVERIFY(sep_widget->palette().color(QPalette::WindowText) != light_sep_color);
}

void ViewerTest::customPresetDoesNotWidenDropdownOrSplitSurrogateTitle()
{
    const QString font_path = fixtureFont();
    if (font_path.isEmpty()) {
        QSKIP("set FONTVIEWER_TEST_FONT to a font file to run viewer tests");
    }

    FontWidget view;
    QVERIFY(view.init(font_path));
    view.resize(850, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QApplication::processEvents();

    auto *text_tab = view.findChild<TextPreviewTab *>();
    QVERIFY(text_tab != nullptr);
    QComboBox *presets = nullptr;
    for (QComboBox *cb : text_tab->findChildren<QComboBox *>()) {
        if (cb->findText(QStringLiteral("Paragraph")) >= 0) {
            presets = cb;
            break;
        }
    }
    QVERIFY(presets != nullptr);
    QPushButton *aa_btn = nullptr;
    for (QPushButton *b : text_tab->findChildren<QPushButton *>()) {
        if (b->text() == QLatin1String("Aa")) {
            aa_btn = b;
            break;
        }
    }
    QVERIFY(aa_btn != nullptr);
    QWidget *toolbar = aa_btn->parentWidget();
    QVERIFY(toolbar != nullptr);

    const int cb_w_before  = presets->width();
    const int cb_sh_before = presets->sizeHint().width();
    const int tb_sh_before = toolbar->sizeHint().width();

    // A 20-char wide custom key and an overlong surrogate-containing key must
    // neither widen the dropdown/toolbar nor leave a lone high surrogate.
    QString surrogate_key(18, QLatin1Char('A'));
    surrogate_key += QChar(0xD83D);
    surrogate_key += QChar(0xDE00);
    surrogate_key += QStringLiteral("tail");

    text_tab->setCustomPresets({
        {QString(20, QLatin1Char('W')), QStringLiteral("Wide title")},
        {surrogate_key, QStringLiteral("Emoji title")},
    });
    QApplication::processEvents();

    QCOMPARE(presets->sizeHint().width(), cb_sh_before);
    QCOMPARE(presets->width(), cb_w_before);
    QCOMPARE(toolbar->sizeHint().width(), tb_sh_before);

    int emoji_idx = -1;
    for (int i = 0; i < presets->count(); ++i) {
        if (presets->itemData(i, Qt::ToolTipRole).toString() == surrogate_key) {
            emoji_idx = i;
            break;
        }
    }
    QVERIFY(emoji_idx >= 0);
    const QString elided = presets->itemText(emoji_idx);
    QVERIFY(elided.endsWith(QChar(0x2026)));
    const QString stem = elided.left(elided.size() - 1);
    QVERIFY(!stem.isEmpty());
    QVERIFY(!stem.at(stem.size() - 1).isHighSurrogate());
}

void ViewerTest::waterfallScrollsToNewlyInsertedLargeCustomSizeWhileVisible()
{
    const QString font_path = fixtureFont();
    if (font_path.isEmpty()) {
        QSKIP("set FONTVIEWER_TEST_FONT to a font file to run viewer tests");
    }

    FontWidget view;
    QVERIFY(view.init(font_path));
    view.setCurrentFontSize(10);
    view.setWaterfallEnabled(true);
    view.resize(600, 300);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QApplication::processEvents();

    auto *wf = view.findChild<WaterfallWidget *>();
    QVERIFY(wf != nullptr);
    auto *sa = qobject_cast<QScrollArea *>(wf->parentWidget()->parentWidget());
    QVERIFY(sa != nullptr);
    QCOMPARE(sa->verticalScrollBar()->value(), 0);

    // Selecting a custom size larger than 72 pt grows the waterfall while
    // already visible; the scroll bar must still settle on the new row.
    view.setCurrentFontSize(120);
    QTRY_VERIFY(wf->rowRect(120).isValid()
                && wf->rowRect(120).bottom()
                       <= sa->verticalScrollBar()->value()
                              + sa->viewport()->height() + 8);
}

void ViewerTest::customShortcutCannotBreakFixedZoomShortcuts()
{
    const QString font_path = fixtureFont();
    if (font_path.isEmpty()) {
        QSKIP("set FONTVIEWER_TEST_FONT to a font file to run viewer tests");
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini_path = dir.filePath(QStringLiteral("fontviewer.ini"));
    {
        QSettings seed(ini_path, QSettings::IniFormat);
        seed.setValue(QStringLiteral("Shortcuts/save_svg"),
                      QStringLiteral("Ctrl+="));
        seed.setValue(QStringLiteral("Shortcuts/toggle_invert"),
                      QStringLiteral("Ctrl+-"));
        seed.sync();
    }

    ViewOptionsPrivate d;
    d.path = font_path;
    ViewOptions opts;
    opts.d_ptr = &d;

    TestFontViewer viewer(ini_path);
    viewer.load(nullptr, &opts);
    viewer.resize(800, 600);
    viewer.show();
    QVERIFY(QTest::qWaitForWindowExposed(&viewer));
    auto *fw = viewer.findChild<FontWidget *>();
    QVERIFY(fw != nullptr);

    QCOMPARE(fw->shortcutSequence(FontShortcutAction::SaveSvg),
             FontShortcutMap::defaultSequence(FontShortcutAction::SaveSvg));
    QCOMPARE(fw->shortcutSequence(FontShortcutAction::ToggleInvert),
             FontShortcutMap::defaultSequence(FontShortcutAction::ToggleInvert));

    const int before_pt = fw->getCurrentFontSize();
    QTest::keyClick(&viewer, Qt::Key_Equal, Qt::ControlModifier);
    QVERIFY(fw->getCurrentFontSize() > before_pt);
    QTest::keyClick(&viewer, Qt::Key_Minus, Qt::ControlModifier);
    QCOMPARE(fw->getCurrentFontSize(), before_pt);
}

void ViewerTest::glyphGuidePillsToggleCanvasAndNarrowViewport()
{
    const QString font_path = fixtureFont();
    if (font_path.isEmpty()) {
        QSKIP("set FONTVIEWER_TEST_FONT to a font file to run viewer tests");
    }

    FontInspectorEngine engine;
    QVERIFY(engine.openFile(font_path));
    const FontDecorationMetrics deco = engine.decorationMetrics(0);
    QVERIFY(deco.has_underline && deco.has_strikeout);

    QScrollArea sa;
    sa.setWidgetResizable(true);
    sa.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    auto *gi = new GlyphInspectorWidget(&sa);
    sa.setWidget(gi);
    sa.resize(320, 560);
    sa.show();
    QVERIFY(QTest::qWaitForWindowExposed(&sa));

    QFont ft(QStringLiteral("Roboto"));
    ft.setPointSize(16);
    gi->updateGlyph(QChar('A'), ft, font_path, deco);
    QApplication::processEvents();
    QApplication::processEvents();
    QCOMPARE(sa.horizontalScrollBar()->maximum(), 0);

    auto *preview = gi->previewWidget();
    QVERIFY(preview != nullptr);
    sa.ensureWidgetVisible(preview);
    QApplication::processEvents();
    preview->repaint();

    const QStringList initial_labels = preview->lastLabelTexts();
    QVERIFY(initial_labels.contains(QStringLiteral("baseline")));
    QVERIFY(initial_labels.contains(QStringLiteral("ascent")));
    QVERIFY(initial_labels.contains(QStringLiteral("descent")));
    QVERIFY(initial_labels.contains(QStringLiteral("origin")));
    QVERIFY(initial_labels.contains(QStringLiteral("advance")));
    QVERIFY(!preview->lastUnderlineBand().isEmpty());
    QVERIFY(!preview->lastStrikeoutBand().isEmpty());

    auto find_pill = [gi](const QString &key) -> QPushButton * {
        return gi->findChild<QPushButton *>(
            QStringLiteral("guide_pill_%1").arg(key));
    };

    // Advance hides both origin and advance labels.
    QPushButton *adv_pill = find_pill(QStringLiteral("advance"));
    QVERIFY(adv_pill != nullptr);
    adv_pill->click();
    preview->repaint();
    QVERIFY(!preview->isGuideVisible(GlyphGuideId::Advance));
    QVERIFY(!preview->lastLabelTexts().contains(QStringLiteral("origin")));
    QVERIFY(!preview->lastLabelTexts().contains(QStringLiteral("advance")));
    adv_pill->click();
    preview->repaint();
    QVERIFY(preview->isGuideVisible(GlyphGuideId::Advance));
    QVERIFY(preview->lastLabelTexts().contains(QStringLiteral("origin")));
    QVERIFY(preview->lastLabelTexts().contains(QStringLiteral("advance")));

    // Underline & Strikeout hide their bands and labels.
    QPushButton *ul_pill = find_pill(QStringLiteral("underline"));
    QPushButton *st_pill = find_pill(QStringLiteral("strikeout"));
    QVERIFY(ul_pill != nullptr && st_pill != nullptr);
    ul_pill->click();
    st_pill->click();
    preview->repaint();
    QVERIFY(preview->lastUnderlineBand().isEmpty());
    QVERIFY(preview->lastStrikeoutBand().isEmpty());
    for (const QString &lbl : preview->lastLabelTexts()) {
        QVERIFY(!lbl.startsWith(QLatin1String("underline")));
        QVERIFY(!lbl.startsWith(QLatin1String("strikeout")));
    }
    ul_pill->click();
    st_pill->click();
    preview->repaint();
    QVERIFY(!preview->lastUnderlineBand().isEmpty());
    QVERIFY(!preview->lastStrikeoutBand().isEmpty());

    // Bearings hides LSB/RSB when present.
    QPushButton *bear_pill = find_pill(QStringLiteral("bearings"));
    QVERIFY(bear_pill != nullptr);
    bear_pill->click();
    preview->repaint();
    QVERIFY(!preview->lastLabelTexts().contains(QStringLiteral("LSB")));
    QVERIFY(!preview->lastLabelTexts().contains(QStringLiteral("RSB")));
    bear_pill->click();

    // Label-less guides (bbox, center) update preview visibility state.
    QPushButton *bbox_pill   = find_pill(QStringLiteral("bbox"));
    QPushButton *center_pill = find_pill(QStringLiteral("center"));
    QVERIFY(bbox_pill != nullptr && center_pill != nullptr);
    bbox_pill->click();
    center_pill->click();
    QVERIFY(!preview->isGuideVisible(GlyphGuideId::BBox));
    QVERIFY(!preview->isGuideVisible(GlyphGuideId::Center));

    // clearSelection() / preview->clear() must preserve hidden guide state.
    gi->clearSelection();
    QVERIFY(!preview->isGuideVisible(GlyphGuideId::BBox));
    QVERIFY(!preview->isGuideVisible(GlyphGuideId::Center));

    // Widening from narrow viewport must shrink multi-row guide bar height.
    gi->updateGlyph(QChar('A'), ft, font_path, deco);
    QApplication::processEvents();
    auto *toggle_all = gi->findChild<QPushButton *>(
        QStringLiteral("guide_toggle_all_btn"));
    QVERIFY(toggle_all != nullptr && toggle_all->parentWidget() != nullptr);
    const int narrow_bar_h = toggle_all->parentWidget()->height();
    sa.resize(850, 560);
    QApplication::processEvents();
    QVERIFY(toggle_all->parentWidget()->height() < narrow_bar_h);
}

void ViewerTest::glyphGuideHideAllShowAllStateMachine()
{
    const QString font_path = fixtureFont();
    if (font_path.isEmpty()) {
        QSKIP("set FONTVIEWER_TEST_FONT to a font file to run viewer tests");
    }

    FontInspectorEngine engine;
    QVERIFY(engine.openFile(font_path));
    const FontDecorationMetrics full_deco = engine.decorationMetrics(0);

    GlyphInspectorWidget gi;
    QFont ft(QStringLiteral("Roboto"));
    ft.setPointSize(16);
    gi.updateGlyph(QChar('A'), ft, font_path, full_deco);

    auto *toggle_all = gi.findChild<QPushButton *>(
        QStringLiteral("guide_toggle_all_btn"));
    auto *outline_btn = gi.findChild<QPushButton *>(
        QStringLiteral("guide_pill_outline"));
    auto *baseline_pill = gi.findChild<QPushButton *>(
        QStringLiteral("guide_pill_baseline"));
    QVERIFY(toggle_all != nullptr && outline_btn != nullptr
            && baseline_pill != nullptr);

    QCOMPARE(toggle_all->text(), QStringLiteral("Hide All"));
    QVERIFY(!gi.isShowOutlinePoints());

    // Hide All hides every visible guide without altering Outline.
    toggle_all->click();
    QCOMPARE(toggle_all->text(), QStringLiteral("Show All"));
    QVERIFY(!gi.isShowOutlinePoints());
    for (QPushButton *pill : gi.guidePillButtons()) {
        if (!pill->isHidden()) {
            QVERIFY(!pill->isChecked());
        }
    }

    // Re-enabling one guide keeps button as Show All until all are enabled.
    baseline_pill->click();
    QVERIFY(baseline_pill->isChecked());
    QCOMPARE(toggle_all->text(), QStringLiteral("Show All"));

    toggle_all->click();
    QCOMPARE(toggle_all->text(), QStringLiteral("Hide All"));
    for (QPushButton *pill : gi.guidePillButtons()) {
        if (!pill->isHidden()) {
            QVERIFY(pill->isChecked());
        }
    }

    // Hidden unavailable guides (underline when deco lacks underline) are
    // preserved across Hide All / Show All cycles.
    gi.setHiddenGuideIds({QStringLiteral("underline")});
    FontDecorationMetrics no_ul_deco = full_deco;
    no_ul_deco.has_underline         = false;
    gi.updateGlyph(QChar('A'), ft, font_path, no_ul_deco);
    auto *ul_pill = gi.findChild<QPushButton *>(
        QStringLiteral("guide_pill_underline"));
    QVERIFY(ul_pill != nullptr);
    QVERIFY(ul_pill->isHidden());
    // Visible guides are all ON -> button says Hide All.
    QCOMPARE(toggle_all->text(), QStringLiteral("Hide All"));
    toggle_all->click();
    QCOMPARE(toggle_all->text(), QStringLiteral("Show All"));
    toggle_all->click();
    QCOMPARE(toggle_all->text(), QStringLiteral("Hide All"));
    QVERIFY(gi.hiddenGuideIds().contains(QStringLiteral("underline")));
}

void ViewerTest::glyphGuideIniPersistenceAndLosslessPreservation()
{
    const QString font_path = fixtureFont();
    if (font_path.isEmpty()) {
        QSKIP("set FONTVIEWER_TEST_FONT to a font file to run viewer tests");
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini_path = dir.filePath(QStringLiteral("fontviewer.ini"));

    // Seed quoted comma list + unknown token + mixed case/whitespace.
    {
        QSettings seed(ini_path, QSettings::IniFormat);
        seed.setValue(QStringLiteral("hidden_guides"),
                      QStringLiteral(" CENTER , bogus_id , underline "));
        seed.sync();
    }

    // Open without ever selecting a glyph in Glyph Inspector, then close:
    // unavailable/unselected hidden items must survive losslessly.
    {
        ViewOptionsPrivate d;
        d.path = font_path;
        ViewOptions opts;
        opts.d_ptr = &d;

        TestFontViewer viewer(ini_path);
        viewer.load(nullptr, &opts);
        auto *fw = viewer.findChild<FontWidget *>();
        QVERIFY(fw != nullptr);
        const QStringList loaded = fw->hiddenGuideIds();
        QVERIFY(loaded.contains(QStringLiteral("underline")));
        QVERIFY(loaded.contains(QStringLiteral("center")));
        QVERIFY(!loaded.contains(QStringLiteral("bogus_id")));
    }

    {
        QSettings check(ini_path, QSettings::IniFormat);
        const QStringList raw = check.value(QStringLiteral("hidden_guides"))
                                    .toStringList();
        QVERIFY(raw.contains(QStringLiteral("underline")));
        QVERIFY(raw.contains(QStringLiteral("center")));
    }

    // Clearing all hidden guides removes the key (never writes @Invalid()).
    {
        ViewOptionsPrivate d;
        d.path = font_path;
        ViewOptions opts;
        opts.d_ptr = &d;

        TestFontViewer viewer(ini_path);
        viewer.load(nullptr, &opts);
        auto *fw = viewer.findChild<FontWidget *>();
        QVERIFY(fw != nullptr);
        fw->setHiddenGuideIds({});
    }

    {
        QSettings check(ini_path, QSettings::IniFormat);
        QVERIFY(!check.contains(QStringLiteral("hidden_guides")));
        QFile f(ini_path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(!f.readAll().contains("@Invalid()"));
    }
}

void ViewerTest::openTypeDrawerSampleIsolationAndFlowSeparators()
{
    const QString noto_vf = fixturePath("NotoSans-VariableFont_wdth,wght.ttf");
    if (noto_vf.isEmpty()) {
        QSKIP("NotoSans fixture missing");
    }

    FontWidget view;
    QVERIFY(view.init(noto_vf));
    view.resize(850, 720);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QApplication::processEvents();

    auto *text_tab = view.findChild<TextPreviewTab *>();
    auto *feat_bar = view.findChild<OpenTypeFeaturesBar *>();
    auto *feat_btn = view.findChild<QPushButton *>(
        QStringLiteral("text_features_btn"));
    auto *sample_lbl = view.findChild<QLabel *>(QStringLiteral("label_sample"));
    auto *tabs       = view.findChild<QTabWidget *>();
    QVERIFY(text_tab && feat_bar && feat_btn && sample_lbl && tabs);

    // Drawer is collapsed by default.
    QVERIFY(!feat_bar->isVisible());
    QCOMPARE(feat_btn->text(), QStringLiteral("Features"));

    // Toggling feature & letter spacing updates Text tab and badge, while
    // Sample tab remains completely isolated across tab switches & refreshes.
    view.setFeatureEnabled(QByteArrayLiteral("smcp"), true);
    view.setLetterSpacing(2.5);
    QApplication::processEvents();
    QCOMPARE(feat_btn->text(), QStringLiteral("Features · 1"));

    tabs->setCurrentIndex(1);  // Sample tab
    QApplication::processEvents();
    QVERIFY(sample_lbl->font().featureTags().isEmpty());
    QVERIFY(qFuzzyIsNull(sample_lbl->font().letterSpacing()));

    // Ctrl+Shift+R resets OpenType overrides even while drawer is collapsed.
    QAction *reset_ot = view.shortcutAction(FontShortcutAction::ResetOpenType);
    QVERIFY(reset_ot != nullptr);
    QVERIFY(reset_ot->isEnabled());
    reset_ot->trigger();
    QApplication::processEvents();
    QVERIFY(!view.isFeatureEnabled(QByteArrayLiteral("smcp")));
    QCOMPARE(feat_btn->text(), QStringLiteral("Features"));

    // Expand drawer and verify no dangling/consecutive separators across widths.
    tabs->setCurrentIndex(0);
    feat_btn->click();
    QApplication::processEvents();
    QVERIFY(feat_bar->isVisible());

    auto check_separators = [feat_bar]() {
        QList<QWidget *> seps;
        for (QWidget *w : feat_bar->findChildren<QWidget *>()) {
            if (w->property("flow_separator").toBool()) {
                seps.append(w);
            }
        }
        QVERIFY(!seps.isEmpty());
        for (QWidget *sep : seps) {
            if (sep->geometry().isEmpty()) {
                continue;
            }
            const QRect sr = sep->geometry();
            QWidget *host  = sep->parentWidget();
            QVERIFY(host != nullptr);
            bool has_left_pill  = false;
            bool has_right_pill = false;
            for (QPushButton *pill : feat_bar->pillButtons()) {
                const QRect pr = pill->geometry();
                if (pr.isEmpty() || qAbs(pr.center().y() - sr.center().y()) > 8) {
                    continue;
                }
                if (pr.right() < sr.left()) {
                    has_left_pill = true;
                }
                else if (pr.left() > sr.right()) {
                    has_right_pill = true;
                }
            }
            QVERIFY2(has_left_pill && has_right_pill,
                     "Separator must not appear at row start or row end");
        }
        const QVector<QPushButton *> pills = feat_bar->pillButtons();
        for (int i = 0; i + 1 < pills.size(); ++i) {
            const QRect r1 = pills[i]->geometry();
            const QRect r2 = pills[i + 1]->geometry();
            if (r1.isEmpty() || r2.isEmpty()
                || qAbs(r1.center().y() - r2.center().y()) > 8) {
                continue;
            }
            const QByteArray t1
                = pills[i]->text().section(QStringLiteral(" · "), 0, 0).toLatin1();
            const QByteArray t2
                = pills[i + 1]->text().section(QStringLiteral(" · "), 0, 0).toLatin1();
            if (FontInspectorEngine::featureGroupIndex(t1)
                == FontInspectorEngine::featureGroupIndex(t2)) {
                continue;
            }
            bool found_sep = false;
            for (QWidget *sep : seps) {
                const QRect sr = sep->geometry();
                if (!sr.isEmpty()
                    && qAbs(sr.center().y() - r1.center().y()) <= 8
                    && sr.left() > r1.right() && sr.right() < r2.left()) {
                    found_sep = true;
                    break;
                }
            }
            QVERIFY2(found_sep,
                     "Adjacent pills from different groups on the same row must have a separator");
        }
    };

    for (int w : {850, 510, 384}) {
        view.resize(w, 720);
        QApplication::processEvents();
        check_separators();
    }
}

void ViewerTest::openTypeMutexGroupsAndSingleSignal()
{
    OpenTypeFeaturesBar bar;
    bar.rebuild({
        {QByteArrayLiteral("lnum"), QStringLiteral("Lining Figures"), false},
        {QByteArrayLiteral("onum"), QStringLiteral("Oldstyle Figures"), false},
        {QByteArrayLiteral("pnum"), QStringLiteral("Proportional Figures"), false},
        {QByteArrayLiteral("tnum"), QStringLiteral("Tabular Figures"), false},
        {QByteArrayLiteral("frac"), QStringLiteral("Fractions"), false},
        {QByteArrayLiteral("afrc"), QStringLiteral("Alternative Fractions"), false},
        {QByteArrayLiteral("sups"), QStringLiteral("Superscript"), false},
        {QByteArrayLiteral("subs"), QStringLiteral("Subscript"), false},
        {QByteArrayLiteral("sinf"), QStringLiteral("Scientific Inferiors"), false},
    });
    bar.show();
    bar.resize(700, 120);
    QApplication::processEvents();

    QSignalSpy spy(&bar, &OpenTypeFeaturesBar::sigFeaturesChanged);
    auto find_pill = [&bar](const char *tag) -> QPushButton * {
        for (QPushButton *b : bar.pillButtons()) {
            if (b->text().startsWith(QString::fromLatin1(tag))) {
                return b;
            }
        }
        return nullptr;
    };

    // Group A: lnum <-> onum
    QPushButton *lnum = find_pill("lnum");
    QPushButton *onum = find_pill("onum");
    QVERIFY(lnum && onum);
    lnum->click();
    QCOMPARE(spy.count(), 1);
    QVERIFY(bar.isFeatureEnabled("lnum"));
    onum->click();
    QCOMPARE(spy.count(), 2);
    QVERIFY(!bar.isFeatureEnabled("lnum"));
    QVERIFY(bar.isFeatureEnabled("onum"));
    QVERIFY(!lnum->isChecked());
    // Click active item again -> turns off (radio-or-off).
    onum->click();
    QCOMPARE(spy.count(), 3);
    QVERIFY(!bar.isFeatureEnabled("onum"));

    // Group B: pnum <-> tnum via setFeatureEnabled (no-op keeps signal silent)
    bar.setFeatureEnabled("pnum", true);
    QCOMPARE(spy.count(), 4);
    bar.setFeatureEnabled("pnum", true);
    QCOMPARE(spy.count(), 4);
    bar.setFeatureEnabled("tnum", true);
    QCOMPARE(spy.count(), 5);
    bar.setFeatureEnabled("pnum", false);
    QCOMPARE(spy.count(), 5);
    QVERIFY(!bar.isFeatureEnabled("pnum"));
    QVERIFY(bar.isFeatureEnabled("tnum"));

    // Group C: frac <-> afrc
    bar.setFeatureEnabled("frac", true);
    bar.setFeatureEnabled("afrc", true);
    QVERIFY(!bar.isFeatureEnabled("frac"));
    QVERIFY(bar.isFeatureEnabled("afrc"));

    // Group D: sups <-> subs <-> sinf
    bar.setFeatureEnabled("sups", true);
    bar.setFeatureEnabled("subs", true);
    QVERIFY(!bar.isFeatureEnabled("sups"));
    QVERIFY(bar.isFeatureEnabled("subs"));
    bar.setFeatureEnabled("sinf", true);
    QVERIFY(!bar.isFeatureEnabled("subs"));
    QVERIFY(bar.isFeatureEnabled("sinf"));

    // Reset clears all overrides.
    bar.resetOverrides();
    QVERIFY(!bar.hasOverrides());
    for (QPushButton *b : bar.pillButtons()) {
        QVERIFY(!b->isChecked());
    }
}

void ViewerTest::flowLayoutWrapsNextGroupWhenSeparatorOverflowsBoundary()
{
    QWidget host;
    auto *flow = new fontui::FlowLayout(&host, 5, 4);
    auto *a    = new QPushButton(&host);
    auto *sep  = new fontui::FlowSeparator(1.0, &host);
    auto *b    = new QPushButton(&host);
    a->setFixedSize(60, 20);
    b->setFixedSize(60, 20);
    flow->addWidget(a);
    flow->addWidget(sep);
    flow->addWidget(b);

    // At 125px, A (60) + gap (5) + B (60) = 125 fits, while A + gap + sep (1) +
    // gap + B = 131 overflows. Hiding sep must wrap B onto the second row so
    // two groups never sit adjacent on one row without a separator.
    host.resize(125, 100);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    QApplication::processEvents();

    QVERIFY(sep->geometry().isEmpty());
    QVERIFY(b->geometry().top() > a->geometry().bottom());
    QCOMPARE(host.layout()->heightForWidth(125), 20 + 4 + 20);
}

void ViewerTest::sliderBadgesStayCloserToOwnSliderAndDrawerUsesClear()
{
    const QString noto_vf = fixturePath("NotoSans-VariableFont_wdth,wght.ttf");
    if (noto_vf.isEmpty()) {
        QSKIP("NotoSans fixture missing");
    }

    FontWidget view;
    QVERIFY(view.init(noto_vf));
    view.resize(900, 720);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QApplication::processEvents();

    auto *line_val = view.findChild<QLabel *>(
        QStringLiteral("text_line_value_label"));
    auto *space_val = view.findChild<QLabel *>(
        QStringLiteral("text_space_value_label"));
    QVERIFY(line_val != nullptr && space_val != nullptr);

    QWidget *line_host  = line_val->parentWidget();
    QWidget *space_host = space_val->parentWidget();
    QVERIFY(line_host != nullptr && space_host != nullptr);
    auto *line_slider = line_host->findChild<QSlider *>();
    QVERIFY(line_slider != nullptr);

    // Height value badge sits adjacent to its own slider and visibly further
    // from the Spacing group (Gestalt proximity).
    const int gap_to_own_slider
        = line_val->geometry().left() - line_slider->geometry().right() - 1;
    const int gap_to_next_group
        = space_host->geometry().left() - line_host->geometry().right() - 1;
    QVERIFY(gap_to_own_slider >= 0 && gap_to_own_slider <= 6);
    QVERIFY(gap_to_next_group > gap_to_own_slider);

    // Modified badge weight must not shrink horizontal padding or shift size.
    const int badge_w_before = space_val->width();
    view.setLetterSpacing(-20.0);
    QApplication::processEvents();
    QCOMPARE(space_val->width(), badge_w_before);
    QFont med_font = space_val->font();
    med_font.setWeight(QFont::Medium);
    QVERIFY(space_val->width()
            >= QFontMetrics(med_font).horizontalAdvance(QStringLiteral("-20.0"))
                   + 8);
    view.setLetterSpacing(0.0);
    QApplication::processEvents();

    // OpenType drawer omits redundant title label, keeps spacing note hidden
    // by default, and reveals "Clear" in its reserved slot without jumping.
    auto *feat_bar = view.findChild<OpenTypeFeaturesBar *>();
    auto *feat_btn = view.findChild<QPushButton *>(
        QStringLiteral("text_features_btn"));
    auto *ot_note = view.findChild<QLabel *>(
        QStringLiteral("opentype_note"));
    auto *ot_clear = view.findChild<QPushButton *>(
        QStringLiteral("opentype_reset_btn"));
    auto *tb_reset = view.findChild<QPushButton *>(
        QStringLiteral("text_toolbar_reset_btn"));
    QVERIFY(feat_bar != nullptr && feat_btn != nullptr && ot_note != nullptr
            && ot_clear != nullptr && tb_reset != nullptr);
    for (QLabel *lbl : feat_bar->findChildren<QLabel *>()) {
        QVERIFY(!lbl->text().contains(QStringLiteral("OpenType Features")));
    }
    feat_btn->click();
    QApplication::processEvents();
    QVERIFY(feat_bar->isVisible());
    QVERIFY(!ot_note->isVisible());
    QVERIFY(!ot_clear->isVisible());
    const int h_before = feat_bar->height();
    view.setFeatureEnabled(QByteArrayLiteral("smcp"), true);
    QApplication::processEvents();
    QVERIFY(ot_clear->isVisible());
    QCOMPARE(feat_bar->height(), h_before);
    QCOMPARE(ot_clear->text(), QStringLiteral("Clear"));
    QCOMPARE(tb_reset->text(), QStringLiteral("Reset"));

    // Inspector preview canvas keeps generous minimum height and descriptive
    // guide tooltips.
    auto *gi_preview = view.findChild<GlyphPreviewWidget *>();
    QVERIFY(gi_preview != nullptr);
    QVERIFY(gi_preview->minimumHeight() >= 380);
    auto *gi = view.findChild<GlyphInspectorWidget *>();
    QVERIFY(gi != nullptr);
    for (QPushButton *pill : gi->guidePillButtons()) {
        QVERIFY(pill->toolTip().contains(QStringLiteral(" — ")));
    }
    auto *outline_pill = gi->findChild<QPushButton *>(
        QStringLiteral("guide_pill_outline"));
    QVERIFY(outline_pill != nullptr);
    QVERIFY(outline_pill->toolTip().contains(QStringLiteral(" — ")));

    QCOMPARE(QString::fromLatin1(FONTVIEWER_VERSION_STR),
             QStringLiteral("1.5.0"));
    QVERIFY(!QString::fromLatin1(FONTVIEWER_GIT_HASH).isEmpty());
    QCOMPARE(QString::fromLatin1(FONTVIEWER_PRODUCT_VERSION_STRING),
             QStringLiteral("%1+%2").arg(
                 QString::fromLatin1(FONTVIEWER_VERSION_STR),
                 QString::fromLatin1(FONTVIEWER_GIT_HASH)));
}

void ViewerTest::charactersTabAdaptiveColumnsAndScrollReachability()
{
    const QString roboto_path = fixturePath("Roboto-Regular.ttf");
    QVERIFY(!roboto_path.isEmpty());

    FontWidget view;
    QVERIFY(view.init(roboto_path));
    auto *tabs      = view.findChild<QTabWidget *>();
    auto *chars_tab = view.findChild<CharactersTab *>();
    auto *cw        = view.findChild<CharacterWidget *>();
    QVERIFY(tabs != nullptr && chars_tab != nullptr && cw != nullptr);
    auto *sa = chars_tab->findChild<QScrollArea *>(
        QStringLiteral("chars_scroll_area"));
    QVERIFY(sa != nullptr);

    tabs->setCurrentWidget(chars_tab);
    view.resize(380, 520);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QApplication::processEvents();

    const int cols_narrow = cw->columnCount();
    QCOMPARE(sa->horizontalScrollBar()->maximum(), 0);
    QVERIFY(sa->verticalScrollBar()->maximum() > 0);

    view.resize(900, 640);
    QApplication::processEvents();
    const int cols_wide = cw->columnCount();
    QVERIFY(cols_wide > cols_narrow);
    QCOMPARE(sa->horizontalScrollBar()->maximum(), 0);
    QVERIFY(sa->verticalScrollBar()->maximum() > 0);

    // Scroll to bottom and verify the last glyph's click hit lands on the last
    // code point inside the visible viewport.
    sa->verticalScrollBar()->setValue(sa->verticalScrollBar()->maximum());
    QApplication::processEvents();
    const int total    = cw->getCharCount();
    const int sq       = cw->cellSizePx();
    const int origin_x = qMax(0, (cw->width() - cols_wide * sq) / 2);
    const int last_row = (total - 1) / cols_wide;
    const int last_col = (total - 1) % cols_wide;
    const QPoint last_cell_pt(origin_x + last_col * sq + sq / 2,
                              4 + last_row * sq + sq / 2);
    const QPoint vp_pt = cw->mapTo(sa->viewport(), last_cell_pt);
    QVERIFY(sa->viewport()->rect().contains(vp_pt));
}

static QByteArray appendFormat12CmapToSfnt(const QByteArray &sfnt_bytes,
                                           quint32 code_point,
                                           quint32 glyph_id)
{
    const int cmap_dir_off = findSfntTableDirOffset(sfnt_bytes, "cmap");
    if (cmap_dir_off < 0) {
        return {};
    }

    // Minimal valid cmap: 1 encoding record (Platform 3, Encoding 10) -> Format
    // 12 subtable covering both Basic Latin ('A') and the target non-BMP cp.
    QByteArray new_cmap;
    appendU16(new_cmap, 0);   // cmap version
    appendU16(new_cmap, 1);   // numTables
    appendU16(new_cmap, 3);   // platformID = Windows
    appendU16(new_cmap, 10);  // encodingID = UCS-4
    appendU32(new_cmap, 12);  // offset to subtable

    // Format 12 subtable (16-byte header + 2 * 12-byte groups = 40 bytes)
    appendU16(new_cmap, 12);  // format
    appendU16(new_cmap, 0);   // reserved
    appendU32(new_cmap, 40);  // length
    appendU32(new_cmap, 0);   // language
    appendU32(new_cmap, 2);   // numGroups
    appendU32(new_cmap, 'A');
    appendU32(new_cmap, 'A');
    appendU32(new_cmap, glyph_id);
    appendU32(new_cmap, code_point);
    appendU32(new_cmap, code_point);
    appendU32(new_cmap, glyph_id);

    QByteArray out = sfnt_bytes;
    while (out.size() % 4 != 0) {
        out.append('\0');
    }
    const quint32 new_off = quint32(out.size());
    out.append(new_cmap);
    writeU32Be(out, cmap_dir_off + 8, new_off);
    writeU32Be(out, cmap_dir_off + 12, quint32(new_cmap.size()));
    const quint32 old_csum = readU32Be(out, cmap_dir_off + 4);
    Q_UNUSED(old_csum);
    return out;
}

void ViewerTest::charactersNonBmpFullPipelineAndOverflowCount()
{
    const QString roboto_path = fixturePath("Roboto-Regular.ttf");
    QVERIFY(!roboto_path.isEmpty());

    // 1) Verify > 65,535 code-point count no longer truncates via ushort.
    CharacterWidget big_cw;
    QVector<quint32> many;
    many.reserve(70000);
    for (quint32 cp = 0x20000u; cp < 0x20000u + 70000u; ++cp) {
        many.append(cp);
    }
    big_cw.setCodePoints(many);
    QCOMPARE(big_cw.getCharCount(), 70000);

    // 2) Verify non-BMP U+1F680 end-to-end (engine -> widget -> Inspector -> SVG).
    const QRawFont raw_roboto(roboto_path, 16);
    const QVector<quint32> a_gids = raw_roboto.glyphIndexesForString(
        QStringLiteral("A"));
    QVERIFY(!a_gids.isEmpty() && a_gids[0] != 0);

    QFile f(roboto_path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray patched = appendFormat12CmapToSfnt(f.readAll(), 0x1F680u,
                                                         a_gids[0]);
    QVERIFY(!patched.isEmpty());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString nonbmp_path = dir.filePath(QStringLiteral("nonbmp.ttf"));
    QFile out_f(nonbmp_path);
    QVERIFY(out_f.open(QIODevice::WriteOnly));
    QCOMPARE(out_f.write(patched), qint64(patched.size()));
    out_f.close();

    FontInspectorEngine eng;
    QVERIFY(eng.openFile(nonbmp_path));
    const auto &cps = eng.faceCodePoints(0);
    QVERIFY(cps.contains(0x1F680u));
    const GlyphOutlineData ol = eng.glyphOutline(0, quint32(0x1F680u));
    QVERIFY(ol.valid);

    FontWidget fw;
    QVERIFY(fw.init(nonbmp_path));
    auto *tabs      = fw.findChild<QTabWidget *>();
    auto *chars_tab = fw.findChild<CharactersTab *>();
    auto *cw        = fw.findChild<CharacterWidget *>();
    auto *gi        = fw.findChild<GlyphInspectorWidget *>();
    QVERIFY(tabs && chars_tab && cw && gi);

    QSignalSpy sel_spy(cw, &CharacterWidget::selectionChanged);
    emit cw->selectionChangedCodePoint(0x1F680u);
    QApplication::processEvents();
    QCOMPARE(sel_spy.count(), 0);  // BMP-only signal stays quiet for SMP
    QCOMPARE(gi->currentCodePoint(), 0x1F680u);
    QVERIFY(gi->currentChar().isNull());
    QCOMPARE(tabs->tabText(tabs->count() - 1),
             QStringLiteral("Glyph Inspector - U+1F680"));
    QCOMPARE(unicode_blocks::blockName(0x1F680u),
             QStringLiteral("Transport and Map Symbols"));

    cw->setSelectedCodePoint(0x1F680u);
    QVERIFY(!cw->getSelectedCharPix().isNull());
    const QString svg_path = dir.filePath(QStringLiteral("nonbmp.svg"));
    QString err;
    tabs->setCurrentWidget(chars_tab);
    QVERIFY2(fw.saveAsSvgTo(svg_path, &err), qPrintable(err));
    QVERIFY(QFileInfo(svg_path).size() > 0);
}

void ViewerTest::charactersFilterPrecedenceBlockAndFocusYield()
{
    const QString roboto_path = fixturePath("Roboto-Regular.ttf");
    QVERIFY(!roboto_path.isEmpty());

    FontWidget view;
    QVERIFY(view.init(roboto_path));
    auto *tabs      = view.findChild<QTabWidget *>();
    auto *chars_tab = view.findChild<CharactersTab *>();
    auto *cw        = view.findChild<CharacterWidget *>();
    auto *combo = view.findChild<QComboBox *>(QStringLiteral("chars_block_combo"));
    auto *filter = view.findChild<QLineEdit *>(QStringLiteral("chars_filter_edit"));
    auto *empty_overlay = view.findChild<QWidget *>(
        QStringLiteral("chars_empty_overlay"));
    QVERIFY(tabs && chars_tab && cw && combo && filter && empty_overlay);

    tabs->setCurrentWidget(chars_tab);
    view.resize(820, 620);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QApplication::processEvents();

    const int all_count = cw->getCharCount();
    QVERIFY(all_count > 100);

    // Block combo, filter input, and Cell slider group must never overlap.
    QVERIFY(combo->geometry().right() < filter->geometry().left());
    auto *cell_slider = chars_tab->findChild<QSlider *>(
        QStringLiteral("chars_cell_slider"));
    QVERIFY(cell_slider != nullptr && cell_slider->parentWidget() != nullptr);
    QVERIFY(filter->geometry().right()
            < cell_slider->parentWidget()->geometry().left());

    // Block + range filter form AND intersection.
    filter->setText(QStringLiteral("U+0041-U+0043"));
    QTest::qWait(120);
    QApplication::processEvents();
    QCOMPARE(cw->getCharCount(), 3);
    QVERIFY(!empty_overlay->isVisible());

    // Switching block away from Basic Latin intersects to 0 and reveals overlay.
    QVERIFY(combo->count() > 2);
    combo->setCurrentIndex(2);
    QApplication::processEvents();
    QCOMPARE(cw->getCharCount(), 0);
    QVERIFY(empty_overlay->isVisible());

    combo->setCurrentIndex(0);
    // Pure hex "ab" must not match "Arabic" block substring.
    filter->setText(QStringLiteral("ab"));
    QTest::qWait(120);
    QApplication::processEvents();
    QVERIFY(cw->getCharCount() < 50);

    // Filter click takes ClickFocus, Esc clears filter and yields focus back.
    filter->setFocus(Qt::MouseFocusReason);
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget *>(filter));
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(filter, &esc);
    QApplication::processEvents();
    QVERIFY(filter->text().isEmpty());
    QCOMPARE(cw->getCharCount(), all_count);
    QWidget *focus_after_esc = QApplication::focusWidget();
    QVERIFY(!focus_after_esc
            || (focus_after_esc != chars_tab
                && !chars_tab->isAncestorOf(focus_after_esc)));

    // Clicking a cell when filter holds focus also clears focus.
    filter->setFocus(Qt::MouseFocusReason);
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget *>(filter));
    const QPoint click_pt(qMax(10, (cw->width() - cw->columnCount() * cw->cellSizePx()) / 2 + 8),
                          10);
    QMouseEvent press(QEvent::MouseButtonPress, click_pt,
                      cw->mapToGlobal(click_pt), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(cw, &press);
    QApplication::processEvents();
    QWidget *focus_after_cell = QApplication::focusWidget();
    QVERIFY(!focus_after_cell
            || (focus_after_cell != chars_tab
                && !chars_tab->isAncestorOf(focus_after_cell)));
}

void ViewerTest::charactersSelectionStripAndCellSizeIniPersistence()
{
    const QString roboto_path = fixturePath("Roboto-Regular.ttf");
    QVERIFY(!roboto_path.isEmpty());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini_path = dir.filePath(QStringLiteral("fontviewer.ini"));
    {
        QSettings seed(ini_path, QSettings::IniFormat);
        seed.setValue(QStringLiteral("char_cell_size"), 999);
        seed.sync();
    }

    ViewOptionsPrivate d;
    d.path = roboto_path;
    ViewOptions opts;
    opts.d_ptr = &d;

    {
        TestFontViewer viewer(ini_path);
        viewer.load(nullptr, &opts);
        auto *fw = viewer.findChild<FontWidget *>();
        QVERIFY(fw != nullptr);
        // Out-of-range value falls back cleanly without overwriting INI on close.
        QCOMPARE(fw->charCellSizeDp(), CharacterWidget::kDefCellSizeDp);
    }
    {
        QSettings check(ini_path, QSettings::IniFormat);
        QCOMPARE(check.value(QStringLiteral("char_cell_size")).toInt(), 999);
    }

    {
        TestFontViewer viewer(ini_path);
        viewer.load(nullptr, &opts);
        auto *tabs      = viewer.findChild<QTabWidget *>();
        auto *fw        = viewer.findChild<FontWidget *>();
        auto *chars_tab = viewer.findChild<CharactersTab *>();
        auto *cw        = viewer.findChild<CharacterWidget *>();
        auto *gi        = viewer.findChild<GlyphInspectorWidget *>();
        auto *edit      = viewer.findChild<FontPreviewEdit *>();
        QVERIFY(tabs && fw && chars_tab && cw && gi && edit);

        tabs->setCurrentWidget(chars_tab);
        viewer.resize(820, 620);
        viewer.show();
        QVERIFY(QTest::qWaitForWindowExposed(&viewer));

        // Ctrl+= ZoomIn on Characters tab scales Cell Size instead of Point size.
        const int pt_before = fw->getCurrentFontSize();
        QAction *zoom_in    = fw->shortcutAction(FontShortcutAction::ZoomIn);
        QVERIFY(zoom_in != nullptr);
        zoom_in->trigger();
        QApplication::processEvents();
        QCOMPARE(fw->getCurrentFontSize(), pt_before);
        QCOMPARE(fw->charCellSizeDp(), CharacterWidget::kDefCellSizeDp + 4);

        // Select 'A' in grid, verify Copy Char / Send to Text / Inspect.
        const int sq       = cw->cellSizePx();
        const int cols     = cw->columnCount();
        const int origin_x = qMax(0, (cw->width() - cols * sq) / 2);
        QPoint a_pt;
        for (int row = 0; row < 10 && a_pt.isNull(); ++row) {
            for (int col = 0; col < cols; ++col) {
                const QPoint pt(origin_x + col * sq + sq / 2,
                                4 + row * sq + sq / 2);
                QMouseEvent press(QEvent::MouseButtonPress, pt,
                                  cw->mapToGlobal(pt), Qt::LeftButton,
                                  Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(cw, &press);
                if (cw->selectedCodePoint() == quint32('A')) {
                    a_pt = pt;
                    break;
                }
            }
        }
        QVERIFY(!a_pt.isNull());

        auto *copy_btn = chars_tab->findChild<QPushButton *>(
            QStringLiteral("chars_copy_text_btn"));
        auto *send_btn = chars_tab->findChild<QPushButton *>(
            QStringLiteral("chars_insert_text_btn"));
        auto *insp_btn = chars_tab->findChild<QPushButton *>(
            QStringLiteral("chars_inspect_btn"));
        QVERIFY(copy_btn && send_btn && insp_btn);
        QCOMPARE(send_btn->text(), QStringLiteral("Send to Text →"));
        QCOMPARE(insp_btn->text(), QStringLiteral("Inspect →"));
        QVERIFY(copy_btn->isVisible() && send_btn->isVisible()
                && insp_btn->isVisible());

        copy_btn->click();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("A"));

        const QString before_text = edit->toPlainText();
        send_btn->click();
        QApplication::processEvents();
        QCOMPARE(tabs->currentIndex(), 0);
        QCOMPARE(edit->toPlainText(), before_text + QStringLiteral("A"));

        auto *gi_sa = qobject_cast<QScrollArea *>(tabs->widget(3));
        QVERIFY(gi_sa != nullptr);

        tabs->setCurrentWidget(chars_tab);
        insp_btn->click();
        QApplication::processEvents();
        QCOMPARE(tabs->currentIndex(), 3);
        QCOMPARE(gi->currentCodePoint(), quint32('A'));
        QCOMPARE(gi_sa->verticalScrollBar()->value(), 0);

        tabs->setCurrentWidget(chars_tab);
        QMouseEvent dbl(QEvent::MouseButtonDblClick, a_pt,
                        cw->mapToGlobal(a_pt), Qt::LeftButton, Qt::LeftButton,
                        Qt::NoModifier);
        QApplication::sendEvent(cw, &dbl);
        QApplication::processEvents();
        QCOMPARE(tabs->currentIndex(), 3);
        QCOMPARE(gi->currentCodePoint(), quint32('A'));
        QVERIFY(gi_sa->verticalScrollBar()->maximum() > 0);
        QCOMPARE(gi_sa->verticalScrollBar()->value(),
                 gi_sa->verticalScrollBar()->maximum());
    }

    {
        QSettings saved(ini_path, QSettings::IniFormat);
        QCOMPARE(saved.value(QStringLiteral("char_cell_size")).toInt(),
                 CharacterWidget::kDefCellSizeDp + 4);
    }
}

void ViewerTest::charactersMultiFaceTtcEnumeration()
{
    const QString ttc_path = systemTtcFont();
    if (ttc_path.isEmpty()) {
        QSKIP("System TTC font not available");
    }

    FontInspectorEngine engine;
    QVERIFY(engine.openFile(ttc_path));
    QVERIFY(engine.faceCount() > 1);
    const auto &cps0 = engine.faceCodePoints(0);
    const auto &cps1 = engine.faceCodePoints(1);
    QVERIFY(!cps0.isEmpty());
    QVERIFY(!cps1.isEmpty());
}

void ViewerTest::charactersOptimizationsAndStripElide()
{
    CharacterGridModel model;
    model.setAllCodePoints({0x0041u, 0x0042u, 0x0043u, 0x03B1u, 0x1F600u});

    QVERIFY(model.setSearchQuery(QStringLiteral("latin")));
    QCOMPARE(model.filteredCodePoints(),
             QVector<quint32>({0x0041u, 0x0042u, 0x0043u}));

    QVERIFY(model.setSearchQuery(QStringLiteral("41")));
    QCOMPARE(model.filteredCodePoints(), QVector<quint32>({0x0041u}));

    QVERIFY(model.setSearchQuery(QString()));

    CharactersTab tab;
    tab.resize(620, 420);
    tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tab));

    tab.setCodePoints({0x0041u, 0x0042u, 0x0043u, 0xE88Au, 0x1F300u, 0x1F600u},
                      {10u, 11u, 12u, 412u, 900u, 901u},
                      {QStringLiteral("A"), QStringLiteral("B"),
                       QStringLiteral("C"), QStringLiteral("home"),
                       QStringLiteral("cyclone"), QStringLiteral("grinning")});
    tab.setCellSizeDp(64, true);
    QCOMPARE(tab.cellSizeDp(), 64);

    QSignalSpy cell_spy(&tab, &CharactersTab::sigCharCellSizeChanged);
    auto *badge = tab.findChild<QLabel *>(QStringLiteral("chars_cell_badge"));
    auto *slider = tab.findChild<QSlider *>(QStringLiteral("chars_cell_slider"));
    QVERIFY(badge && slider);

    QMouseEvent dbl_badge(QEvent::MouseButtonDblClick, badge->rect().center(),
                          badge->mapToGlobal(badge->rect().center()),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(badge, &dbl_badge);
    QCOMPARE(tab.cellSizeDp(), CharacterWidget::kDefCellSizeDp);
    QCOMPARE(cell_spy.count(), 1);
    QCOMPARE(cell_spy.takeFirst().at(0).toInt(),
             CharacterWidget::kDefCellSizeDp);

    tab.setCellSizeDp(72, true);
    cell_spy.clear();
    QMouseEvent dbl_slider(QEvent::MouseButtonDblClick, slider->rect().center(),
                           slider->mapToGlobal(slider->rect().center()),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(slider, &dbl_slider);
    QCOMPARE(tab.cellSizeDp(), CharacterWidget::kDefCellSizeDp);
    QCOMPARE(cell_spy.count(), 1);

    auto *cw       = tab.characterWidget();
    auto *sel_bar  = tab.findChild<QWidget *>(QStringLiteral("chars_selection_bar"));
    auto *sel_meta = tab.findChild<QLabel *>(QStringLiteral("chars_sel_meta"));
    auto *code_chip
        = tab.findChild<QPushButton *>(QStringLiteral("chars_meta_code_btn"));
    auto *dec_chip
        = tab.findChild<QPushButton *>(QStringLiteral("chars_meta_dec_btn"));
    auto *gid_chip
        = tab.findChild<QPushButton *>(QStringLiteral("chars_meta_gid_btn"));
    auto *name_chip
        = tab.findChild<QPushButton *>(QStringLiteral("chars_meta_name_btn"));
    auto *copy_btn
        = tab.findChild<QPushButton *>(QStringLiteral("chars_copy_text_btn"));
    auto *send_btn
        = tab.findChild<QPushButton *>(QStringLiteral("chars_insert_text_btn"));
    auto *insp_btn
        = tab.findChild<QPushButton *>(QStringLiteral("chars_inspect_btn"));
    auto *actions_btn
        = tab.findChild<QPushButton *>(QStringLiteral("chars_actions_menu_btn"));
    auto *actions_menu
        = tab.findChild<QMenu *>(QStringLiteral("chars_actions_menu"));
    QVERIFY(cw && sel_bar && sel_meta && code_chip && dec_chip && gid_chip
            && name_chip && copy_btn && send_btn && insp_btn && actions_btn
            && actions_menu);
    for (QPushButton *b :
         {code_chip, dec_chip, gid_chip, name_chip, copy_btn, send_btn,
          insp_btn, actions_btn}) {
        QCOMPARE(b->focusPolicy(), Qt::NoFocus);
    }

    // Custom PUA icon glyph shows all copyable metadata chips at standard width.
    tab.resize(740, 420);
    emit cw->selectionChangedCodePoint(0xE88Au);
    QApplication::processEvents();
    QVERIFY(code_chip->isVisible() && dec_chip->isVisible()
            && gid_chip->isVisible() && name_chip->isVisible());
    QCOMPARE(code_chip->text(), QStringLiteral("U+E88A"));
    QCOMPARE(dec_chip->text(), QStringLiteral("Dec 59530"));
    QCOMPARE(gid_chip->text(), QStringLiteral("#412"));
    QCOMPARE(name_chip->text(), QStringLiteral("home"));
    QVERIFY(copy_btn->isVisible() && send_btn->isVisible()
            && insp_btn->isVisible());
    QVERIFY(!actions_btn->isVisible());

    QSignalSpy toast_spy(&tab, &CharactersTab::sigCopyToastRequested);
    QSignalSpy insert_spy(&tab, &CharactersTab::sigInsertTextRequested);
    QSignalSpy inspect_spy(&tab, &CharactersTab::inspectRequested);

    for (QPushButton *chip : {code_chip, dec_chip, gid_chip, name_chip}) {
        const QString expected = chip->text();
        chip->click();
        QCOMPARE(QApplication::clipboard()->text(), expected);
        QVERIFY(toast_spy.count() >= 1);
        QVERIFY(toast_spy.takeLast().at(0).toString().contains(expected));
    }

    // Narrow pane preserves primary chips (#GID, glyph name) and folds actions
    // into the popup Actions menu cleanly without clipping.
    tab.resize(360, 420);
    QApplication::processEvents();
    QVERIFY(!copy_btn->isVisible() && !send_btn->isVisible()
            && !insp_btn->isVisible());
    QVERIFY(actions_btn->isVisible());
    QVERIFY(actions_btn->geometry().right() <= sel_bar->rect().right() + 1);
    QVERIFY(code_chip->isVisible() && gid_chip->isVisible()
            && name_chip->isVisible());
    QCOMPARE(actions_menu->actions().size(), 3);

    QTimer::singleShot(0, actions_menu, [actions_menu]() {
        actions_menu->actions().at(0)->trigger();
        actions_menu->close();
    });
    actions_btn->click();
    QCOMPARE(QApplication::clipboard()->text(), QString::fromUcs4(U"\uE88A", 1));
    QVERIFY(toast_spy.count() >= 1);
    actions_menu->actions().at(1)->trigger();
    QCOMPARE(insert_spy.count(), 1);
    QCOMPARE(insert_spy.takeFirst().at(0).toString(),
             QString::fromUcs4(U"\uE88A", 1));
    actions_menu->actions().at(2)->trigger();
    QCOMPARE(inspect_spy.count(), 1);
    QCOMPARE(inspect_spy.takeFirst().at(0).toUInt(), 0xE88Au);

    // Long block name elides trailing block label cleanly with tooltip, and
    // squeezes down further to 360px without clipping actions or chips.
    tab.resize(440, 420);
    emit cw->selectionChangedCodePoint(0x1F300u);
    QApplication::processEvents();
    QVERIFY(actions_btn->isVisible());
    QVERIFY(actions_btn->geometry().right() <= sel_bar->rect().right() + 1);
    QVERIFY(code_chip->isVisible() && gid_chip->isVisible()
            && name_chip->isVisible());
    QVERIFY(sel_meta->isVisible());
    QVERIFY(!sel_meta->toolTip().isEmpty());
    QVERIFY(sel_meta->text().endsWith(QChar(0x2026)));

    tab.resize(360, 420);
    QApplication::processEvents();
    QVERIFY(actions_btn->isVisible());
    QVERIFY(actions_btn->geometry().right() <= sel_bar->rect().right() + 1);
    QVERIFY(code_chip->isVisible() && gid_chip->isVisible());
    QVERIFY(!sel_meta->toolTip().isEmpty());

    // Resizing narrow never alters copied content (full exact token is copied).
    code_chip->click();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("U+1F300"));
    gid_chip->click();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("#900"));
    if (name_chip->isVisible()) {
        name_chip->click();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("cyclone"));
    }

    // DPR 2.0 + Dark palette keep strip actions inside bar bounds.
    QPalette dark;
    dark.setColor(QPalette::Window, QColor(30, 30, 30));
    dark.setColor(QPalette::WindowText, QColor(232, 232, 232));
    tab.setPalette(dark);
    tab.setDpr(2.0);
    QApplication::processEvents();
    QVERIFY(actions_btn->isVisible());
    QVERIFY(actions_btn->geometry().right() <= sel_bar->rect().right() + 1);
}

void ViewerTest::glyphGuideToggleAnimationAndImmediateLogicalCache()
{
    const QString font_path = fixtureFont();
    if (font_path.isEmpty()) {
        QSKIP("set FONTVIEWER_TEST_FONT to a font file to run viewer tests");
    }

    FontInspectorEngine engine;
    QVERIFY(engine.openFile(font_path));
    const FontDecorationMetrics deco = engine.decorationMetrics(0);
    const GlyphOutlineData outline   = engine.glyphOutline(0, quint32('A'));
    QVERIFY(outline.valid && !outline.points.isEmpty());

    GlyphInspectorWidget gi;
    QFont ft(QStringLiteral("Roboto"));
    ft.setPointSize(16);
    gi.resize(520, 680);
    gi.show();
    QVERIFY(QTest::qWaitForWindowExposed(&gi));
    gi.updateGlyph(quint32('A'), ft, font_path, deco, outline);
    QApplication::processEvents();

    auto *preview = gi.previewWidget();
    auto *baseline_pill
        = gi.findChild<QPushButton *>(QStringLiteral("guide_pill_baseline"));
    auto *ascent_pill
        = gi.findChild<QPushButton *>(QStringLiteral("guide_pill_ascent"));
    auto *outline_btn
        = gi.findChild<QPushButton *>(QStringLiteral("guide_pill_outline"));
    auto *toggle_all
        = gi.findChild<QPushButton *>(QStringLiteral("guide_toggle_all_btn"));
    QVERIFY(preview && baseline_pill && ascent_pill && outline_btn
            && toggle_all);

    QVERIFY(qFuzzyCompare(preview->guideOpacity(GlyphGuideId::Baseline), 1.0));
    QVERIFY(qFuzzyCompare(preview->outlineOpacity(), 0.0));
    QVERIFY(!preview->isGuideAnimating());

    // Clicking a guide pill animates opacity while logical caches update at once.
    baseline_pill->click();
    preview->repaint();
    QVERIFY(preview->isGuideAnimating());
    QVERIFY(!preview->isGuideVisible(GlyphGuideId::Baseline));
    QVERIFY(!preview->lastLabelTexts().contains(QStringLiteral("baseline")));

    QTRY_VERIFY(!preview->isGuideAnimating());
    QCOMPARE(preview->guideOpacity(GlyphGuideId::Baseline), 0.0);

    // Outline pill click starts fade-in and populates screen points synchronously.
    outline_btn->click();
    preview->repaint();
    QVERIFY(preview->isGuideAnimating());
    QVERIFY(preview->isShowOutlinePoints());
    QVERIFY(!preview->lastOutlineScreenPoints().isEmpty());

    QTRY_VERIFY(!preview->isGuideAnimating());
    QCOMPARE(preview->outlineOpacity(), 1.0);

    // Baseline is hidden, so Toggle All starts as Show All -> restores Baseline,
    // then Hide All -> hides all guides, then Show All -> restores all guides.
    QCOMPARE(toggle_all->text(), QStringLiteral("Show All"));
    toggle_all->click();
    QVERIFY(preview->isGuideAnimating());
    QTRY_VERIFY(!preview->isGuideAnimating());
    QCOMPARE(preview->guideOpacity(GlyphGuideId::Baseline), 1.0);
    QCOMPARE(toggle_all->text(), QStringLiteral("Hide All"));

    toggle_all->click();
    QVERIFY(preview->isGuideAnimating());
    QTRY_VERIFY(!preview->isGuideAnimating());
    QCOMPARE(preview->guideOpacity(GlyphGuideId::Ascent), 0.0);

    toggle_all->click();
    QVERIFY(preview->isGuideAnimating());
    QTRY_VERIFY(!preview->isGuideAnimating());
    QCOMPARE(preview->guideOpacity(GlyphGuideId::Ascent), 1.0);
}

void ViewerTest::charactersGlyphNameLookupAndKeyboardNavigation()
{
    const QString font_path = fixtureFont();
    if (font_path.isEmpty()) {
        QSKIP("set FONTVIEWER_TEST_FONT to a font file to run viewer tests");
    }

    FontInspectorEngine engine;
    QVERIFY(engine.openFile(font_path));
    const auto &cps   = engine.faceCodePoints(0);
    const auto &gids  = engine.faceCodePointGlyphIds(0);
    const auto &names = engine.faceGlyphNames(0);
    QVERIFY(!cps.isEmpty());
    QCOMPARE(gids.size(), cps.size());
    if (!names.isEmpty()) {
        QCOMPARE(names.size(), cps.size());
    }
    const GlyphIdentity id_a = engine.glyphIdentity(0, quint32('A'));
    QVERIFY(id_a.glyph_index > 0);

    // Glyph name search matches non-hex glyph name directly, even when unsorted.
    CharacterGridModel named_model;
    named_model.setAllCodePoints({0x2192u, 0x0041u, 0x0026u}, {12u, 10u, 11u},
                                 {QStringLiteral("arrowright"),
                                  QStringLiteral("A"),
                                  QStringLiteral("ampersand")});
    QVERIFY(named_model.setSearchQuery(QStringLiteral("arrow")));
    QCOMPARE(named_model.filteredCodePoints(), QVector<quint32>({0x2192u}));
    QCOMPARE(named_model.glyphIdForCodePoint(0x2192u), 12u);
    QCOMPARE(named_model.glyphNameForCodePoint(0x2192u),
             QStringLiteral("arrowright"));
    QVERIFY(named_model.containsCodePoint(0x2192u));

    FontWidget view;
    QVERIFY(view.init(font_path));
    view.resize(760, 560);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto *tabs      = view.findChild<QTabWidget *>();
    auto *chars_tab = view.findChild<CharactersTab *>();
    auto *cw        = view.findChild<CharacterWidget *>();
    auto *combo     = view.findChild<QComboBox *>(
        QStringLiteral("chars_block_combo"));
    auto *filter    = view.findChild<QLineEdit *>(
        QStringLiteral("chars_filter_edit"));
    auto *sel_meta = view.findChild<QLabel *>(
        QStringLiteral("chars_sel_meta"));
    QVERIFY(tabs && chars_tab && cw && combo && filter && sel_meta);

    tabs->setCurrentWidget(chars_tab);
    QApplication::processEvents();
    QVERIFY(cw->columnCount() >= 2);
    QCOMPARE(filter->height(), combo->height());

    view.updateDPR(2.0);
    QApplication::processEvents();
    QCOMPARE(filter->height(), combo->height());
    view.updateDPR(1.0);
    QApplication::processEvents();

    // Arrow keys navigate the grid while leaving CharacterWidget NoFocus.
    QTest::keyClick(chars_tab, Qt::Key_Right);
    QApplication::processEvents();
    const quint32 first_cp = cps.first();
    QCOMPARE(cw->selectedCodePoint(), first_cp);
    auto *gid_chip
        = chars_tab->findChild<QPushButton *>(QStringLiteral("chars_meta_gid_btn"));
    QVERIFY(gid_chip != nullptr);
    QVERIFY(gid_chip->isVisible());
    QVERIFY(gid_chip->text().startsWith(QLatin1Char('#')));

    QTest::keyClick(chars_tab, Qt::Key_Right);
    QApplication::processEvents();
    QCOMPARE(cw->selectedCodePoint(), cps.at(1));

    QTest::keyClick(chars_tab, Qt::Key_Down);
    QApplication::processEvents();
    const int expected_down_idx = 1 + cw->columnCount();
    if (expected_down_idx < cps.size()) {
        QCOMPARE(cw->selectedCodePoint(), cps.at(expected_down_idx));
        // PageUp from row 1 must jump to row 0 (same col), not get stuck.
        QTest::keyClick(chars_tab, Qt::Key_PageUp);
        QApplication::processEvents();
        QCOMPARE(cw->selectedCodePoint(), cps.at(1));
    }

    QTest::keyClick(chars_tab, Qt::Key_Home);
    QApplication::processEvents();
    QCOMPARE(cw->selectedCodePoint(), first_cp);

    // Focus must never remain inside Characters grid controls after key nav.
    QWidget *fw = QApplication::focusWidget();
    QVERIFY(!fw || (fw != &view && !view.isAncestorOf(fw)));

    // Focusing filter edit yields arrow keys cleanly back to text editing.
    filter->setFocus(Qt::MouseFocusReason);
    QApplication::processEvents();
    const quint32 before_filter_key = cw->selectedCodePoint();
    QTest::keyClicks(filter, QStringLiteral("0041"));
    QTest::keyClick(filter, Qt::Key_Left);
    QApplication::processEvents();
    QCOMPARE(cw->selectedCodePoint(), before_filter_key);

    // Pressing Enter in filter applies filter, clears focus, selects result.
    QTest::keyClick(filter, Qt::Key_Return);
    QApplication::processEvents();
    QVERIFY(!filter->hasFocus());
    QCOMPARE(cw->selectedCodePoint(), quint32(0x0041u));

    // Pressing Return on Characters navigates to Glyph Inspector.
    QTest::keyClick(chars_tab, Qt::Key_Return);
    QApplication::processEvents();
    QCOMPARE(tabs->currentIndex(), 3);
    auto *gi = view.findChild<GlyphInspectorWidget *>();
    QVERIFY(gi != nullptr);
    QCOMPARE(gi->currentCodePoint(), quint32(0x0041u));
}

void ViewerTest::sampleTabScriptSectionsCopyAndIsolation()
{
    const QString noto_vf = fixturePath("NotoSans-VariableFont_wdth,wght.ttf");
    if (noto_vf.isEmpty()) {
        QSKIP("NotoSans fixture missing");
    }

    FontWidget view;
    QVERIFY(view.init(noto_vf));
    view.resize(820, 680);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto *tabs       = view.findChild<QTabWidget *>();
    auto *sample_tab = view.findChild<SamplePreviewTab *>();
    QVERIFY(tabs && sample_tab);

    tabs->setCurrentWidget(sample_tab);
    QApplication::processEvents();

    auto *sample_lbl = view.findChild<QLabel *>(QStringLiteral("label_sample"));
    auto *summary    = view.findChild<QLabel *>(
        QStringLiteral("sample_summary_label"));
    auto *copy_btn
        = view.findChild<QPushButton *>(QStringLiteral("sample_copy_btn"));
    auto *copy_all = view.findChild<QPushButton *>(
        QStringLiteral("sample_copy_all_btn"));
    QVERIFY(sample_lbl && summary && copy_btn && copy_all);
    QCOMPARE(view.findChild<QPushButton *>(
                 QStringLiteral("sample_follow_ot_btn")),
             nullptr);
    QVERIFY(summary->text().contains(QStringLiteral("Writing System")));

    QSignalSpy toast_spy(&view, &FontWidget::sigToast);
    copy_btn->click();
    QCOMPARE(QApplication::clipboard()->text(), sample_lbl->text());
    QVERIFY(toast_spy.count() >= 1);

    toast_spy.clear();
    copy_all->click();
    QVERIFY(!QApplication::clipboard()->text().isEmpty());
    QVERIFY(toast_spy.count() >= 1);

    // Text tab OpenType and letter-spacing overrides remain isolated from Sample.
    view.setFeatureEnabled(QByteArrayLiteral("smcp"), true);
    view.setLetterSpacing(3.0);
    QApplication::processEvents();
    QVERIFY(sample_lbl->font().featureTags().isEmpty());
    QVERIFY(qFuzzyIsNull(sample_lbl->font().letterSpacing()));

    // Dark palette + DPR updates must repaint Sample labels and spacing cleanly.
    QPalette dark = view.palette();
    dark.setColor(QPalette::Window, QColor(28, 28, 32));
    dark.setColor(QPalette::WindowText, QColor(238, 238, 242));
    view.setPalette(dark);
    view.updateDPR(2.0);
    QApplication::processEvents();
    QCOMPARE(sample_lbl->palette().color(QPalette::WindowText),
             QColor(238, 238, 242));
    QVERIFY(summary->palette().color(QPalette::WindowText) != QColor(28, 28, 32));

    // Ctrl+Wheel over empty viewport whitespace still zooms cleanly.
    QSignalSpy zoom_spy(sample_tab, &SamplePreviewTab::sigZoomRequested);
    auto *scroll = sample_tab->findChild<QScrollArea *>(
        QStringLiteral("scrollArea_sample"));
    QVERIFY(scroll && scroll->viewport());
    sendWheel(scroll->viewport(), 120, Qt::ControlModifier);
    QCOMPARE(zoom_spy.count(), 1);
    QCOMPARE(zoom_spy.at(0).at(0).toInt(), 1);
}

void ViewerTest::charactersHostContractToolbarWrapAndSelectionBounds()
{
    // 1) Host Esc QAction override + IME preedit preservation in filter edit.
    QWidget host;
    auto *host_lay = new QVBoxLayout(&host);
    host_lay->setContentsMargins(0, 0, 0, 0);
    auto *tab = new CharactersTab(&host);
    host_lay->addWidget(tab);
    host.resize(620, 440);

    int host_esc_count = 0;
    auto *esc_action   = new QAction(&host);
    esc_action->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(esc_action, &QAction::triggered, &host,
            [&host_esc_count]() { ++host_esc_count; });
    host.addAction(esc_action);

    tab->setCodePoints({0x0041u, 0x0042u, 0x0043u, 0x0044u, 0xE88Au},
                       {10u, 11u, 12u, 13u, 412u},
                       {QStringLiteral("A"), QStringLiteral("B"),
                        QStringLiteral("C"), QStringLiteral("D"),
                        QStringLiteral("very_long_unbroken_postscript_glyph_"
                                       "name_0123456789abcdef_0123456789abcdef_"
                                       "0123456789abcdef_0123456789abcdef")});
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

    auto *filter = tab->findChild<QLineEdit *>(
        QStringLiteral("chars_filter_edit"));
    QVERIFY(filter != nullptr);
    filter->setFocus(Qt::MouseFocusReason);
    filter->setText(QStringLiteral("Latin"));
    QApplication::processEvents();
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget *>(filter));

    QInputMethodEvent preedit(QStringLiteral("zw"), {});
    QApplication::sendEvent(filter, &preedit);
    QTest::keyClick(filter, Qt::Key_Escape);
    QApplication::processEvents();
    QCOMPARE(host_esc_count, 0);
    QCOMPARE(filter->text(), QStringLiteral("Latin"));
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget *>(filter));

    QInputMethodEvent end_preedit;
    QApplication::sendEvent(filter, &end_preedit);
    QTest::keyClick(filter, Qt::Key_Escape);
    QCOMPARE(host_esc_count, 0);
    QVERIFY(filter->text().isEmpty());
    // Deferred queued clearFocus matches FontPreviewEdit host race guard.
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget *>(filter));
    QTRY_VERIFY(QApplication::focusWidget() != filter);

    QTest::keyClick(&host, Qt::Key_Escape);
    QCOMPARE(host_esc_count, 1);

    // 2) Toolbar super-narrow & DPR 2.0 wrap never clips Block, Filter, or Cell.
    auto *combo = tab->findChild<QComboBox *>(
        QStringLiteral("chars_block_combo"));
    auto *slider = tab->findChild<QSlider *>(
        QStringLiteral("chars_cell_slider"));
    QVERIFY(combo && slider);
    QWidget *toolbar = combo->parentWidget();
    QVERIFY(toolbar != nullptr);

    for (qreal dpr : {1.0, 2.0}) {
        tab->setDpr(dpr);
        host.resize(240, 440);
        QApplication::processEvents();
        const QRect tb_rect = toolbar->rect();
        QVERIFY(combo->geometry().left() >= tb_rect.left());
        QVERIFY(combo->geometry().right() <= tb_rect.right());
        QVERIFY(filter->geometry().left() >= tb_rect.left());
        QVERIFY(filter->geometry().right() <= tb_rect.right());
        QVERIFY(filter->width() > 20);
        QVERIFY(!combo->geometry().intersects(filter->geometry()));
    }
    tab->setDpr(1.0);

    // 3) Selection strip with extra-long glyph name never pushes Actions out of bounds.
    auto *cw      = tab->characterWidget();
    auto *sel_bar = tab->findChild<QWidget *>(
        QStringLiteral("chars_selection_bar"));
    auto *code_chip = tab->findChild<QPushButton *>(
        QStringLiteral("chars_meta_code_btn"));
    auto *actions_btn = tab->findChild<QPushButton *>(
        QStringLiteral("chars_actions_menu_btn"));
    QVERIFY(cw && sel_bar && code_chip && actions_btn);

    emit cw->selectionChangedCodePoint(0xE88Au);
    for (int w : {360, 280, 210}) {
        for (qreal dpr : {1.0, 2.0}) {
            tab->setDpr(dpr);
            host.resize(w, 440);
            QApplication::processEvents();
            QVERIFY(actions_btn->isVisible());
            QVERIFY(actions_btn->geometry().right() <= sel_bar->rect().right());
            QVERIFY(code_chip->isVisible());
        }
    }
    tab->setDpr(1.0);
    code_chip->click();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("U+E88A"));

    // 4) Down key on the final partial row stays in-place symmetrically with Up.
    host.resize(320, 440);
    tab->setCellSizeDp(64, false);
    QApplication::processEvents();
    const int cols = cw->columnCount();
    QVERIFY(cols >= 2 && cols <= 4);
    QTest::keyClick(tab, Qt::Key_End);
    QApplication::processEvents();
    QCOMPARE(cw->selectedCodePoint(), quint32(0xE88Au));
    QTest::keyClick(tab, Qt::Key_Down);
    QApplication::processEvents();
    QCOMPARE(cw->selectedCodePoint(), quint32(0xE88Au));
}

void ViewerTest::characterGridModelHexWordLookupAndUnassigned()
{
    CharacterGridModel model;
    model.setAllCodePoints({0x0041u, 0x2FE5u, 0xE145u, 0xE87Cu, 0xE880u, 0xFACEu},
                           {10u, 15u, 11u, 12u, 13u, 14u},
                           {QStringLiteral("A"), QStringLiteral("unassigned"),
                            QStringLiteral("add"), QStringLiteral("face"),
                            QStringLiteral("faced_smile"),
                            QStringLiteral("uniFACE")});

    // Bare hex words like "face" and "add" match exact glyph names plus hex.
    QVERIFY(model.setSearchQuery(QStringLiteral("face")));
    QCOMPARE(model.filteredCodePoints(), QVector<quint32>({0xE87Cu, 0xFACEu}));

    QVERIFY(model.setSearchQuery(QStringLiteral("add")));
    QCOMPARE(model.filteredCodePoints(), QVector<quint32>({0xE145u}));

    // Explicit U+ hex skips glyph name collision.
    QVERIFY(model.setSearchQuery(QStringLiteral("U+FACE")));
    QCOMPARE(model.filteredCodePoints(), QVector<quint32>({0xFACEu}));

    // Unassigned block filter matches only unassigned code points linearly.
    QVERIFY(model.setSearchQuery(QString()));
    QVERIFY(model.setBlockFilter(CharacterGridModel::kUnassignedBlock));
    QCOMPARE(model.filteredCodePoints(), QVector<quint32>({0x2FE5u}));
}

void ViewerTest::glyphInspectorLongNameOverflowFontClearAndEngineEdgeCases()
{
    const QString roboto_path = fixturePath("Roboto-Regular.ttf");
    QVERIFY(!roboto_path.isEmpty());

    // 1) Long glyph name in narrow 320px QScrollArea elides cleanly without horizontal scrollbar.
    const QString long_name(150, QLatin1Char('x'));
    QScrollArea scroll;
    scroll.setWidgetResizable(true);
    auto *gi = new GlyphInspectorWidget(&scroll);
    scroll.setWidget(gi);
    scroll.resize(320, 480);
    scroll.show();
    QVERIFY(QTest::qWaitForWindowExposed(&scroll));

    QFont f(QStringLiteral("Arial"), 16);
    gi->updateGlyph(quint32('A'), f, roboto_path, {}, {}, long_name);
    QApplication::processEvents();

    auto *id_lbl = gi->findChild<QLabel *>(QStringLiteral("gi_glyph_id_label"));
    QVERIFY(id_lbl != nullptr);
    QVERIFY(id_lbl->text().contains(QChar(0x2026)));
    QVERIFY(id_lbl->toolTip().contains(long_name));
    QCOMPARE(scroll.horizontalScrollBar()->maximum(), 0);

    // Explicit empty updateFont clears stale glyph name, whereas default null preserves it.
    gi->updateFont(f, {}, {}, QStringLiteral("kept_name"));
    QVERIFY(id_lbl->toolTip().contains(QStringLiteral("kept_name"))
            || id_lbl->text().contains(QStringLiteral("kept_name")));
    gi->updateFont(f, {}, {});
    QVERIFY(id_lbl->toolTip().contains(QStringLiteral("kept_name"))
            || id_lbl->text().contains(QStringLiteral("kept_name")));
    gi->updateFont(f, {}, {}, QStringLiteral(""));
    QVERIFY(!id_lbl->text().contains(QStringLiteral("kept_name")));
    QVERIFY(!id_lbl->toolTip().contains(QStringLiteral("kept_name")));

    // Hiding preview stops an active guide animation cleanly and snaps opacity.
    auto *preview = gi->findChild<GlyphPreviewWidget *>();
    QVERIFY(preview != nullptr);
    preview->setGuideVisible(GlyphGuideId::Baseline, false, true);
    QVERIFY(preview->isGuideAnimating());
    scroll.hide();
    QApplication::processEvents();
    QVERIFY(!preview->isGuideAnimating());
    QCOMPARE(preview->guideOpacity(GlyphGuideId::Baseline), 0.0);

    // 2) Engine: >128 byte post 2.0 glyph name + SMP-only probeSampleForFace.
    QFile src_file(roboto_path);
    QVERIFY(src_file.open(QIODevice::ReadOnly));
    const QByteArray roboto_bytes = src_file.readAll();

    const QRawFont raw_roboto(roboto_path, 16);
    const QVector<quint32> a_gids
        = raw_roboto.glyphIndexesForString(QStringLiteral("A"));
    QVERIFY(!a_gids.isEmpty() && a_gids[0] != 0);
    const quint32 gid_a = a_gids[0];

    // Patch Format 12 cmap containing ONLY non-BMP U+1F600 mapped to gid_a.
    QByteArray smp_only = appendFormat12CmapToSfnt(roboto_bytes, 0x1F600u, gid_a);
    QVERIFY(!smp_only.isEmpty());
    const int cmap_dir_off = findSfntTableDirOffset(smp_only, "cmap");
    const int post_dir_off = findSfntTableDirOffset(smp_only, "post");
    const int maxp_dir_off = findSfntTableDirOffset(smp_only, "maxp");
    QVERIFY(cmap_dir_off >= 0 && post_dir_off >= 0 && maxp_dir_off >= 0);

    // Replace the appended cmap table with a single-group Format 12 cmap (0x1F600).
    QByteArray one_smp_cmap;
    appendU16(one_smp_cmap, 0);
    appendU16(one_smp_cmap, 1);
    appendU16(one_smp_cmap, 3);
    appendU16(one_smp_cmap, 10);
    appendU32(one_smp_cmap, 12);
    appendU16(one_smp_cmap, 12);
    appendU16(one_smp_cmap, 0);
    appendU32(one_smp_cmap, 28);
    appendU32(one_smp_cmap, 0);
    appendU32(one_smp_cmap, 1);
    appendU32(one_smp_cmap, 0x1F600u);
    appendU32(one_smp_cmap, 0x1F600u);
    appendU32(one_smp_cmap, gid_a);
    while (smp_only.size() % 4 != 0) {
        smp_only.append('\0');
    }
    const quint32 smp_cmap_off = quint32(smp_only.size());
    smp_only.append(one_smp_cmap);
    writeU32Be(smp_only, cmap_dir_off + 8, smp_cmap_off);
    writeU32Be(smp_only, cmap_dir_off + 12, quint32(one_smp_cmap.size()));

    // Patch post 2.0 table with a 180-byte glyph name on gid_a.
    const quint32 maxp_off = readU32Be(smp_only, maxp_dir_off + 8);
    const quint16 num_glyphs = readU16Be(smp_only, int(maxp_off) + 4);
    QVERIFY(num_glyphs > gid_a);
    const QByteArray long_ps_name(180, 'g');
    QByteArray new_post(32, '\0');
    new_post[0] = 0x00;
    new_post[1] = 0x02;  // version 2.0
    appendU16(new_post, num_glyphs);
    for (quint16 g = 0; g < num_glyphs; ++g) {
        appendU16(new_post, g == gid_a ? 258 : 0);
    }
    new_post.append(char(long_ps_name.size()));
    new_post.append(long_ps_name);
    while (smp_only.size() % 4 != 0) {
        smp_only.append('\0');
    }
    const quint32 new_post_off = quint32(smp_only.size());
    smp_only.append(new_post);
    writeU32Be(smp_only, post_dir_off + 8, new_post_off);
    writeU32Be(smp_only, post_dir_off + 12, quint32(new_post.size()));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString edge_path = dir.filePath(QStringLiteral("smp_longname.ttf"));
    QFile edge_file(edge_path);
    QVERIFY(edge_file.open(QIODevice::WriteOnly));
    QCOMPARE(edge_file.write(smp_only), qint64(smp_only.size()));
    edge_file.close();

    FontInspectorEngine edge_eng;
    QVERIFY(edge_eng.openFile(edge_path));
    const QString smp_sample = edge_eng.probeSampleForFace(0);
    QCOMPARE(smp_sample, QString::fromUcs4(U"\U0001F600", 1));
    const GlyphIdentity smp_id = edge_eng.glyphIdentity(0, 0x1F600u);
    QCOMPARE(smp_id.glyph_index, gid_a);
    QCOMPARE(smp_id.glyph_name, QString::fromLatin1(long_ps_name));
}

void ViewerTest::reviewFixesSampleOffTabPuaAndHiDpiScaling()
{
    const QString roboto_path = fixturePath("Roboto-Regular.ttf");
    QVERIFY(!roboto_path.isEmpty());

    QFile src_file(roboto_path);
    QVERIFY(src_file.open(QIODevice::ReadOnly));
    QByteArray pua_sfnt = src_file.readAll();

    const QRawFont raw_roboto(roboto_path, 16);
    const QVector<quint32> a_gids
        = raw_roboto.glyphIndexesForString(QStringLiteral("A"));
    QVERIFY(!a_gids.isEmpty() && a_gids[0] != 0);
    const quint32 gid_a = a_gids[0];

    const int cmap_dir_off = findSfntTableDirOffset(pua_sfnt, "cmap");
    const int os2_dir_off  = findSfntTableDirOffset(pua_sfnt, "OS/2");
    QVERIFY(cmap_dir_off >= 0 && os2_dir_off >= 0);

    // Clear OS/2 unicode range bits so Qt reports no Latin writing system
    // while mapping only PUA U+E88A..U+E88D.
    const quint32 os2_off = readU32Be(pua_sfnt, os2_dir_off + 8);
    const quint32 os2_len = readU32Be(pua_sfnt, os2_dir_off + 12);
    QVERIFY(os2_off + os2_len <= quint32(pua_sfnt.size()));
    QVERIFY(os2_len >= 58);
    for (int i = 42; i < 58; ++i) {
        pua_sfnt[int(os2_off) + i] = '\0';
    }

    QByteArray pua_cmap;
    appendU16(pua_cmap, 0);
    appendU16(pua_cmap, 1);
    appendU16(pua_cmap, 3);
    appendU16(pua_cmap, 10);
    appendU32(pua_cmap, 12);
    appendU16(pua_cmap, 12);
    appendU16(pua_cmap, 0);
    appendU32(pua_cmap, 28);
    appendU32(pua_cmap, 0);
    appendU32(pua_cmap, 1);
    appendU32(pua_cmap, 0xE88Au);
    appendU32(pua_cmap, 0xE88Du);
    appendU32(pua_cmap, gid_a);
    while (pua_sfnt.size() % 4 != 0) {
        pua_sfnt.append('\0');
    }
    const quint32 pua_cmap_off = quint32(pua_sfnt.size());
    pua_sfnt.append(pua_cmap);
    writeU32Be(pua_sfnt, cmap_dir_off + 8, pua_cmap_off);
    writeU32Be(pua_sfnt, cmap_dir_off + 12, quint32(pua_cmap.size()));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString pua_path = dir.filePath(QStringLiteral("pua_only.ttf"));
    QFile pua_file(pua_path);
    QVERIFY(pua_file.open(QIODevice::WriteOnly));
    QCOMPARE(pua_file.write(pua_sfnt), qint64(pua_sfnt.size()));
    pua_file.close();

    // 1) probeSampleForFace returns PUA characters when no standard printable
    // scalars exist.
    FontInspectorEngine pua_eng;
    QVERIFY(pua_eng.openFile(pua_path));
    const QString pua_probe = pua_eng.probeSampleForFace(0);
    QCOMPARE(pua_probe, QString::fromUcs4(U"\uE88A\uE88B\uE88C\uE88D", 4));

    FontWidget fw;
    QVERIFY(fw.init(pua_path));
    fw.resize(760, 560);
    fw.show();
    QVERIFY(QTest::qWaitForWindowExposed(&fw));

    auto *tabs       = fw.findChild<QTabWidget *>();
    auto *sample_tab = fw.findChild<SamplePreviewTab *>();
    QVERIFY(tabs && sample_tab);
    auto *sample_lbl = sample_tab->findChild<QLabel *>(
        QStringLiteral("label_sample"));
    QVERIFY(sample_lbl != nullptr);
    QCOMPARE(sample_lbl->text(), pua_probe);

    // 2) Off-tab renderPixmap & prepareSvgExport must not permanently hide
    // per-script Copy buttons, and draw_out safely no-ops after tab delete.
    QVERIFY(tabs->currentWidget() != sample_tab);
    sample_tab->updateSample(QStringLiteral("OffTabFamily"), fw.font(),
                             QStringLiteral("Sample"));
    QApplication::processEvents();
    QVERIFY(!sample_tab->renderPixmap(1.0).isNull());
    QSize svg_sz;
    QColor svg_bg;
    std::function<void(QPainter &)> svg_draw;
    QVERIFY(sample_tab->prepareSvgExport(svg_sz, svg_bg, svg_draw));
    QImage img(svg_sz, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&img);
    svg_draw(painter);
    painter.end();

    tabs->setCurrentWidget(sample_tab);
    QApplication::processEvents();
    const auto copy_btns
        = sample_tab->findChildren<QPushButton *>(QStringLiteral("sample_copy_btn"));
    QVERIFY(!copy_btns.isEmpty());
    for (QPushButton *b : copy_btns) {
        QVERIFY(!b->isHidden());
        QVERIFY(b->isVisible());
    }

    auto *ephemeral_sample = new SamplePreviewTab();
    ephemeral_sample->resize(400, 300);
    ephemeral_sample->updateSample(QStringLiteral("Eph"), fw.font(),
                                   QStringLiteral("ABC"));
    ephemeral_sample->show();
    QApplication::processEvents();
    QSize eph_sz;
    QColor eph_bg;
    std::function<void(QPainter &)> eph_draw;
    QVERIFY(ephemeral_sample->prepareSvgExport(eph_sz, eph_bg, eph_draw));
    delete ephemeral_sample;
    QImage safe_img(eph_sz, QImage::Format_ARGB32_Premultiplied);
    QPainter safe_p(&safe_img);
    eph_draw(safe_p);
    safe_p.end();

    // 3) Seer Host pixelSize scaling & manual setDpr scaling apply cleanly to
    // ValueBadge and SamplePreviewTab caption/heading labels.
    QWidget px_host;
    QFont px_font = px_host.font();
    px_font.setPixelSize(24);
    px_host.setFont(px_font);
    auto *badge = new fontui::ValueBadge(2.0, QStringLiteral("-20.0"), &px_host);
    QVERIFY(badge->font().pixelSize() >= 18);
    QCOMPARE(badge->font().pointSizeF(), -1.0);

    auto *hosted_sample = new SamplePreviewTab(&px_host);
    hosted_sample->setDpr(2.0);
    hosted_sample->updateSample(QStringLiteral("Hosted"), fw.font(),
                                QStringLiteral("Hello"));
    auto *sum_lbl = hosted_sample->findChild<QLabel *>(
        QStringLiteral("sample_summary_label"));
    QVERIFY(sum_lbl != nullptr);
    QVERIFY(sum_lbl->font().pixelSize() >= 18);
    QCOMPARE(sum_lbl->font().pointSizeF(), -1.0);
}

void ViewerTest::reviewFixesCharactersMenuLifetimeSliderFocusAndLazyNames()
{
    // 1) Destroying CharactersTab during openActionsMenu() modal exec() must
    // not crash or UAF.
    QPointer<CharactersTab> tab_guard;
    auto *heap_tab = new CharactersTab();
    tab_guard      = heap_tab;
    heap_tab->setCodePoints({0x0041u, 0x0042u}, {10u, 11u},
                            {QStringLiteral("A"), QStringLiteral("B")});
    heap_tab->resize(260, 420);
    heap_tab->show();
    QVERIFY(QTest::qWaitForWindowExposed(heap_tab));

    auto *cw          = heap_tab->characterWidget();
    auto *actions_btn = heap_tab->findChild<QPushButton *>(
        QStringLiteral("chars_actions_menu_btn"));
    QVERIFY(cw && actions_btn);
    emit cw->selectionChangedCodePoint(0x0041u);
    QApplication::processEvents();
    QVERIFY(actions_btn->isVisible());

    QTimer::singleShot(0, heap_tab, [heap_tab]() { delete heap_tab; });
    actions_btn->click();
    QVERIFY(tab_guard.isNull());

    // 2) Filter clearing selection closes an open actions menu, and
    // QSlider/QTabBar focus blocks grid navigation shortcuts.
    QWidget host;
    auto *host_lay = new QVBoxLayout(&host);
    auto *tab_bar  = new QTabBar(&host);
    tab_bar->addTab(QStringLiteral("One"));
    tab_bar->addTab(QStringLiteral("Two"));
    tab_bar->setFocusPolicy(Qt::StrongFocus);
    auto *slider = new QSlider(Qt::Horizontal, &host);
    slider->setRange(0, 100);
    slider->setFocusPolicy(Qt::StrongFocus);
    auto *tab = new CharactersTab(&host);
    host_lay->addWidget(tab_bar);
    host_lay->addWidget(slider);
    host_lay->addWidget(tab, 1);

    tab->setCodePoints({0x0041u, 0x0042u, 0x2FE5u}, {10u, 11u, 12u},
                       {QStringLiteral("A"), QStringLiteral("B"),
                        QStringLiteral("gap_glyph")});
    host.resize(300, 460);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

    auto *cw2          = tab->characterWidget();
    auto *filter2      = tab->findChild<QLineEdit *>(QStringLiteral("chars_filter_edit"));
    auto *actions_btn2 = tab->findChild<QPushButton *>(
        QStringLiteral("chars_actions_menu_btn"));
    auto *actions_menu2
        = tab->findChild<QMenu *>(QStringLiteral("chars_actions_menu"));
    QVERIFY(cw2 && filter2 && actions_btn2 && actions_menu2);

    emit cw2->selectionChangedCodePoint(0x0041u);
    QApplication::processEvents();
    QVERIFY(actions_btn2->isVisible());
    bool closed_by_filter = false;
    QTimer::singleShot(0, tab, [filter2, actions_menu2, &closed_by_filter]() {
        filter2->setText(QStringLiteral("U+FFFF"));
        QTest::qWait(120);
        QApplication::processEvents();
        closed_by_filter = !actions_menu2->isVisible();
        if (actions_menu2->isVisible()) {
            actions_menu2->close();
        }
    });
    actions_btn2->click();
    QVERIFY(closed_by_filter);
    QVERIFY(!actions_menu2->isVisible());
    QCOMPARE(cw2->selectedCodePoint(), quint32(0));

    filter2->clear();
    QTest::qWait(120);
    if (QWidget *fw_init = QApplication::focusWidget()) {
        fw_init->clearFocus();
    }
    QApplication::processEvents();
    QTest::keyClick(tab, Qt::Key_Home);
    QApplication::processEvents();
    QCOMPARE(cw2->selectedCodePoint(), quint32(0x0041u));

    slider->setFocus(Qt::TabFocusReason);
    QApplication::processEvents();
    QTest::keyClick(slider, Qt::Key_Right);
    QApplication::processEvents();
    QCOMPARE(cw2->selectedCodePoint(), quint32(0x0041u));

    tab_bar->setFocus(Qt::TabFocusReason);
    QApplication::processEvents();
    QTest::keyClick(tab_bar, Qt::Key_Right);
    QApplication::processEvents();
    QCOMPARE(cw2->selectedCodePoint(), quint32(0x0041u));

    // 3) Unicode unassigned naming consistency & deferred glyph-name loading.
    QCOMPARE(unicode_blocks::blockName(0x2FE5u),
             QStringLiteral("Unassigned / Other"));
    QCOMPARE(unicode_blocks::blockName(0x110000u),
             QStringLiteral("Unknown Block"));

    CharacterGridModel model;
    model.setAllCodePoints({0x2FE5u, 0x0041u}, {15u, 10u});
    model.setGlyphNames({QStringLiteral("gap"), QStringLiteral("latinA")});
    QVERIFY(model.setSearchQuery(QStringLiteral("unassigned")));
    QCOMPARE(model.filteredCodePoints(), QVector<quint32>({0x2FE5u}));

    const QString roboto_path = fixturePath("Roboto-Regular.ttf");
    QVERIFY(!roboto_path.isEmpty());
    QFile rob_f(roboto_path);
    QVERIFY(rob_f.open(QIODevice::ReadOnly));
    QByteArray named_sfnt = rob_f.readAll();

    const QRawFont raw_rob(roboto_path, 16);
    const QVector<quint32> a_gids
        = raw_rob.glyphIndexesForString(QStringLiteral("A"));
    QVERIFY(!a_gids.isEmpty() && a_gids[0] != 0);
    const quint32 gid_a = a_gids[0];

    const int post_dir_off = findSfntTableDirOffset(named_sfnt, "post");
    const int maxp_dir_off = findSfntTableDirOffset(named_sfnt, "maxp");
    QVERIFY(post_dir_off >= 0 && maxp_dir_off >= 0);
    const quint32 maxp_off   = readU32Be(named_sfnt, maxp_dir_off + 8);
    const quint16 num_glyphs = readU16Be(named_sfnt, int(maxp_off) + 4);
    QVERIFY(num_glyphs > gid_a);

    QByteArray post2(32, '\0');
    post2[0] = 0x00;
    post2[1] = 0x02;
    appendU16(post2, num_glyphs);
    for (quint16 g = 0; g < num_glyphs; ++g) {
        appendU16(post2, g == gid_a ? 258 : 0);
    }
    const QByteArray a_name("LatinCapA");
    post2.append(char(a_name.size()));
    post2.append(a_name);
    while (named_sfnt.size() % 4 != 0) {
        named_sfnt.append('\0');
    }
    const quint32 post2_off = quint32(named_sfnt.size());
    named_sfnt.append(post2);
    writeU32Be(named_sfnt, post_dir_off + 8, post2_off);
    writeU32Be(named_sfnt, post_dir_off + 12, quint32(post2.size()));

    QTemporaryDir named_dir;
    QVERIFY(named_dir.isValid());
    const QString named_path = named_dir.filePath(QStringLiteral("named_post2.ttf"));
    QFile named_file(named_path);
    QVERIFY(named_file.open(QIODevice::WriteOnly));
    QCOMPARE(named_file.write(named_sfnt), qint64(named_sfnt.size()));
    named_file.close();

    FontWidget fw;
    QVERIFY(fw.init(named_path));
    fw.resize(760, 560);
    fw.show();
    QVERIFY(QTest::qWaitForWindowExposed(&fw));
    auto *fw_tabs  = fw.findChild<QTabWidget *>();
    auto *fw_chars = fw.findChild<CharactersTab *>();
    auto *fw_cw    = fw.findChild<CharacterWidget *>();
    auto *fw_name  = fw_chars->findChild<QPushButton *>(
        QStringLiteral("chars_meta_name_btn"));
    QVERIFY(fw_tabs && fw_chars && fw_cw && fw_name);

    fw_tabs->setCurrentWidget(fw_chars);
    QApplication::processEvents();
    emit fw_cw->selectionChangedCodePoint(quint32('A'));
    QApplication::processEvents();
    QCOMPARE(fw_name->text(), QStringLiteral("LatinCapA"));
}

void ViewerTest::reviewFixesHostPixelFontsNarrowBoundsAndSvgExportLifetime()
{
    // 1) Host pixelSize scaling & dynamic FontChange at constant m_dpr=1.0
    // propagate across FeaturePillButton, GuidePillButton, empty-state label,
    // ValueBadge, VariableAxesBar, MetaCopyChip, m_sel_meta, and action spacer.
    QWidget host;
    QFont px_font_1x = host.font();
    px_font_1x.setPixelSize(12);
    host.setFont(px_font_1x);

    auto *vbox = new QVBoxLayout(&host);
    auto *feat_bar = new OpenTypeFeaturesBar(&host);
    feat_bar->rebuild({{QByteArrayLiteral("liga"), QStringLiteral("Standard Ligatures"), true}});

    auto *gi = new GlyphInspectorWidget(&host);
    auto *chars_tab = new CharactersTab(&host);
    chars_tab->setCodePoints({0x0041u, 0xE88Au}, {10u, 11u},
                             {QStringLiteral("A"), QStringLiteral("very_long_glyph_name_for_elision_check")});
    auto *text_tab = new TextPreviewTab(&host);

    FontInspectorEngine engine;
    const QString vf_path = fixturePath("NotoSans-VariableFont_wdth,wght.ttf");
    QString vf_family;
    int vf_id = -1;
    if (!vf_path.isEmpty() && engine.openFile(vf_path)) {
        vf_id = QFontDatabase::addApplicationFont(vf_path);
        const QStringList fams = vf_id >= 0 ? QFontDatabase::applicationFontFamilies(vf_id) : QStringList{};
        if (!fams.isEmpty()) {
            vf_family = fams.first();
        }
    }
    auto *axes_bar = new VariableAxesBar(&host);
    if (!vf_family.isEmpty()) {
        axes_bar->rebuild(engine, 0, vf_family, true);
    }

    vbox->addWidget(feat_bar);
    vbox->addWidget(gi);
    vbox->addWidget(chars_tab, 1);
    vbox->addWidget(text_tab, 1);
    vbox->addWidget(axes_bar);

    host.resize(640, 720);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

    QLabel *axes_val_lbl = nullptr;
    int axes_val_min_w_1x = 0;
    for (QLabel *lbl : axes_bar->findChildren<QLabel *>()) {
        if ((lbl->alignment() & Qt::AlignRight) && lbl->minimumWidth() > 0) {
            axes_val_lbl      = lbl;
            axes_val_min_w_1x = lbl->minimumWidth();
            break;
        }
    }

    // Apply a 24px host font while m_dpr remains 1.0 -> QEvent::FontChange alone
    // must refresh widgets that previously called setFont() (WA_SetFont).
    QFont px_font_2x = host.font();
    px_font_2x.setPixelSize(24);
    host.setFont(px_font_2x);
    QApplication::processEvents();

    auto *cw = chars_tab->characterWidget();
    QVERIFY(cw != nullptr);
    emit cw->selectionChangedCodePoint(0xE88Au);
    QApplication::processEvents();

    for (QPushButton *b : feat_bar->findChildren<QPushButton *>()) {
        if (b->isCheckable()) {
            QVERIFY2(b->font().pixelSize() >= 18,
                     qPrintable(QString::number(b->font().pixelSize())));
            QCOMPARE(b->font().pointSizeF(), -1.0);
        }
    }
    for (QPushButton *pill : gi->guidePillButtons()) {
        QVERIFY(pill->font().pixelSize() >= 18);
        QCOMPARE(pill->font().pointSizeF(), -1.0);
    }
    QLabel *gi_empty_lbl = nullptr;
    for (QLabel *lbl : gi->findChildren<QLabel *>()) {
        if (lbl->text().contains(QStringLiteral("Select a character"))) {
            gi_empty_lbl = lbl;
            break;
        }
    }
    QVERIFY(gi_empty_lbl != nullptr);
    QVERIFY(gi_empty_lbl->font().pixelSize() >= 28);
    QCOMPARE(gi_empty_lbl->font().pointSizeF(), -1.0);
    QVERIFY(gi_empty_lbl->font().italic());

    int chars_badges = 0;
    for (QLabel *lbl : chars_tab->findChildren<QLabel *>()) {
        if (auto *badge = dynamic_cast<fontui::ValueBadge *>(lbl)) {
            QVERIFY(badge->font().pixelSize() >= 18);
            QCOMPARE(badge->font().pointSizeF(), -1.0);
            ++chars_badges;
        }
    }
    QVERIFY(chars_badges > 0);
    int text_badges = 0;
    for (QLabel *lbl : text_tab->findChildren<QLabel *>()) {
        if (auto *badge = dynamic_cast<fontui::ValueBadge *>(lbl)) {
            QVERIFY(badge->font().pixelSize() >= 18);
            QCOMPARE(badge->font().pointSizeF(), -1.0);
            ++text_badges;
        }
    }
    QVERIFY(text_badges >= 2);
    auto *code_chip = chars_tab->findChild<QPushButton *>(QStringLiteral("chars_meta_code_btn"));
    auto *sel_meta  = chars_tab->findChild<QLabel *>(QStringLiteral("chars_sel_meta"));
    QVERIFY(code_chip && sel_meta);
    QCOMPARE(code_chip->font().pixelSize(), 24);
    QCOMPARE(sel_meta->font().pixelSize(), 24);
    if (axes_val_lbl) {
        QVERIFY(axes_val_lbl->minimumWidth() > axes_val_min_w_1x);
    }

    // Also verify DPR=2.0 scaling for GuideInspector & ultra-narrow CharactersTab
    // under the 24px host font (Actions button stays strictly inside sel_bar).
    gi->setDpr(2.0);
    chars_tab->setDpr(2.0);
    auto *sel_bar     = chars_tab->findChild<QWidget *>(QStringLiteral("chars_selection_bar"));
    auto *actions_btn = chars_tab->findChild<QPushButton *>(QStringLiteral("chars_actions_menu_btn"));
    QVERIFY(sel_bar && actions_btn);
    for (int w : {240, 210}) {
        host.resize(w, 720);
        QApplication::processEvents();
        QVERIFY(actions_btn->isVisible());
        QVERIFY(sel_bar->width() <= chars_tab->width() + 1);
        QVERIFY(actions_btn->geometry().right() <= sel_bar->rect().right());
    }
    chars_tab->setDpr(1.0);

    // 2) QTextEdit viewport focus blocks CharactersTab WindowShortcut arrows,
    // and Escape inside non-empty filter clears without restarting debounce timer.
    auto *note_edit = new QTextEdit(&host);
    vbox->addWidget(note_edit);
    host.resize(640, 760);
    QApplication::processEvents();
    cw->setSelectedCodePoint(0x0041u);
    emit cw->selectionChangedCodePoint(0x0041u);
    QApplication::processEvents();
    note_edit->setFocus(Qt::MouseFocusReason);
    QApplication::processEvents();
    QTest::keyClick(chars_tab, Qt::Key_Right);
    QApplication::processEvents();
    QCOMPARE(cw->selectedCodePoint(), quint32(0x0041u));
    auto *inner = new QWidget(note_edit->viewport());
    inner->setFocusPolicy(Qt::ClickFocus);
    inner->resize(16, 16);
    inner->show();
    inner->setFocus(Qt::MouseFocusReason);
    QApplication::processEvents();
    QCOMPARE(QApplication::focusWidget(), inner);
    QTest::keyClick(inner, Qt::Key_Right);
    QApplication::processEvents();
    QCOMPARE(cw->selectedCodePoint(), quint32(0x0041u));
    QTest::keyClick(chars_tab, Qt::Key_Right);
    QApplication::processEvents();
    QCOMPARE(cw->selectedCodePoint(), quint32(0x0041u));
    inner->clearFocus();
    note_edit->clearFocus();

    auto *filter = chars_tab->findChild<QLineEdit *>(QStringLiteral("chars_filter_edit"));
    QVERIFY(filter != nullptr);
    filter->setText(QStringLiteral("0041"));
    QTest::qWait(200);
    QCOMPARE(cw->getCharCount(), 1);
    filter->setFocus(Qt::MouseFocusReason);
    QTest::keyClick(filter, Qt::Key_Escape);
    QVERIFY(filter->text().isEmpty());
    QCOMPARE(cw->getCharCount(), 2);
    for (QTimer *t : chars_tab->findChildren<QTimer *>()) {
        QVERIFY(!t->isActive());
    }

    // 3) SamplePreviewTab style key change triggers rebuild rather than stale
    // family-only cache, and TextPreviewTab::prepareSvgExport draw_out safely
    // no-ops if TextPreviewTab is destroyed before invocation.
    SamplePreviewTab sample_tab;
    sample_tab.resize(400, 300);
    QFont f_reg(QStringLiteral("NonExistentCacheProbeFamily"));
    f_reg.setWeight(QFont::Normal);
    sample_tab.updateSample(QStringLiteral("NonExistentCacheProbeFamily"), f_reg,
                            QStringLiteral("AAA"));
    auto *first_lbl = sample_tab.findChild<QLabel *>(QStringLiteral("label_sample"));
    QPointer<QLabel> first_guard(first_lbl);
    QVERIFY(!first_guard.isNull());

    QFont f_bold = f_reg;
    f_bold.setWeight(QFont::Bold);
    sample_tab.updateSample(QStringLiteral("NonExistentCacheProbeFamily"), f_bold,
                            QStringLiteral("AAA"));
    QVERIFY(first_guard.isNull());

    for (bool waterfall : {false, true}) {
        auto *ephemeral_text = new TextPreviewTab();
        ephemeral_text->setWaterfallEnabled(waterfall);
        ephemeral_text->resize(480, 320);
        ephemeral_text->show();
        QApplication::processEvents();
        QSize svg_sz;
        QColor svg_bg;
        std::function<void(QPainter &)> svg_draw;
        QVERIFY(ephemeral_text->prepareSvgExport(svg_sz, svg_bg, svg_draw));
        delete ephemeral_text;
        QImage out_img(qMax(1, svg_sz.width()), qMax(1, svg_sz.height()),
                       QImage::Format_ARGB32_Premultiplied);
        QPainter p(&out_img);
        svg_draw(p);
        p.end();
    }

    if (vf_id >= 0) {
        QFontDatabase::removeApplicationFont(vf_id);
    }
}

void ViewerTest::p1OffTabClearSelectionStripAndP2GlyphIdReset()
{
    // P1: Updating or clearing selection while CharactersTab is hidden must still
    // hide chips, separators, and action buttons before the tab is shown.
    QTabWidget host;
    auto *other_tab = new QWidget(&host);
    auto *chars_tab = new CharactersTab(&host);
    host.addTab(other_tab, QStringLiteral("Other"));
    host.addTab(chars_tab, QStringLiteral("Characters"));
    host.resize(760, 480);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    QCOMPARE(host.currentWidget(), other_tab);

    chars_tab->setCodePoints({0x0041u, 0x0042u}, {10u, 11u},
                             {QStringLiteral("A"), QStringLiteral("B")});
    auto *cw       = chars_tab->characterWidget();
    auto *sel_meta = chars_tab->findChild<QLabel *>(QStringLiteral("chars_sel_meta"));
    auto *code_btn
        = chars_tab->findChild<QPushButton *>(QStringLiteral("chars_meta_code_btn"));
    auto *copy_btn
        = chars_tab->findChild<QPushButton *>(QStringLiteral("chars_copy_text_btn"));
    auto *send_btn
        = chars_tab->findChild<QPushButton *>(QStringLiteral("chars_insert_text_btn"));
    auto *insp_btn
        = chars_tab->findChild<QPushButton *>(QStringLiteral("chars_inspect_btn"));
    auto *act_btn
        = chars_tab->findChild<QPushButton *>(QStringLiteral("chars_actions_menu_btn"));
    QVERIFY(cw && sel_meta && code_btn && copy_btn && send_btn && insp_btn
            && act_btn);

    for (QWidget *w :
         {static_cast<QWidget *>(code_btn), static_cast<QWidget *>(copy_btn),
          static_cast<QWidget *>(send_btn), static_cast<QWidget *>(insp_btn),
          static_cast<QWidget *>(act_btn)}) {
        QVERIFY(w->isHidden());
    }

    // Switch to Characters, select a glyph, then switch back off-tab and clear.
    host.setCurrentWidget(chars_tab);
    QApplication::processEvents();
    emit cw->selectionChangedCodePoint(0x0041u);
    QApplication::processEvents();
    QVERIFY(code_btn->isVisible() && copy_btn->isVisible()
            && send_btn->isVisible() && insp_btn->isVisible());

    host.setCurrentWidget(other_tab);
    QApplication::processEvents();
    chars_tab->setCodePoints({0x0043u, 0x0044u}, {12u, 13u});
    QApplication::processEvents();

    host.setCurrentWidget(chars_tab);
    QApplication::processEvents();
    for (QWidget *w :
         {static_cast<QWidget *>(code_btn), static_cast<QWidget *>(copy_btn),
          static_cast<QWidget *>(send_btn), static_cast<QWidget *>(insp_btn),
          static_cast<QWidget *>(act_btn)}) {
        QVERIFY(!w->isVisible());
    }
    QVERIFY(sel_meta->isVisible());
    QVERIFY(sel_meta->text().contains(QStringLiteral("Click or use arrow keys")));

    // Verify block combobox keeps untruncated full text in its tooltip even when
    // the displayed item text is elided.
    chars_tab->setCodePoints({0x0041u, 0x1F300u});
    auto *combo = chars_tab->findChild<QComboBox *>(
        QStringLiteral("chars_block_combo"));
    QVERIFY(combo != nullptr);
    QCOMPARE(combo->toolTip(), QStringLiteral("All Characters (2)"));
    int long_blk_idx = -1;
    for (int i = 0; i < combo->count(); ++i) {
        if (combo->itemData(i, Qt::ToolTipRole)
                .toString()
                .startsWith(QStringLiteral("Miscellaneous Symbols and Pictographs"))) {
            long_blk_idx = i;
            break;
        }
    }
    QVERIFY(long_blk_idx >= 0);
    combo->setCurrentIndex(long_blk_idx);
    QApplication::processEvents();
    QVERIFY(combo->currentText().endsWith(QChar(0x2026)));
    QCOMPARE(combo->toolTip(),
             QStringLiteral("Miscellaneous Symbols and Pictographs (1)"));

    // P2: Falling back to invalid QRawFont resets stale glyph_id from previous inspect.
    const QString roboto_path = fixturePath("Roboto-Regular.ttf");
    QVERIFY(!roboto_path.isEmpty());
    const int rob_id = QFontDatabase::addApplicationFont(roboto_path);
    QVERIFY(rob_id >= 0);
    const QStringList fams = QFontDatabase::applicationFontFamilies(rob_id);
    QVERIFY(!fams.isEmpty());

    GlyphInspectorWidget gi;
    QFont valid_f(fams.first());
    valid_f.setPointSize(18);
    gi.updateGlyph(quint32('A'), valid_f, roboto_path, {}, {});
    auto *id_lbl
        = gi.findChild<QLabel *>(QStringLiteral("gi_glyph_id_label"));
    QVERIFY(id_lbl != nullptr);
    QVERIFY(id_lbl->text().startsWith(QLatin1Char('#')));

    QFont missing_f(QStringLiteral("__NonExistentGlyphFallbackFamily__"));
    missing_f.setStyleStrategy(QFont::NoFontMerging);
    if (!QRawFont::fromFont(missing_f).isValid()) {
        gi.updateGlyph(quint32('A'), missing_f, QString(), {}, {});
        QCOMPARE(id_lbl->text(), QStringLiteral("N/A"));
    }

    // P2: Hidden glyph preview box updated via setDpr(2.0) while unselected must
    // still contribute full minimumHeight() to selection bar height once selected.
    CharactersTab dpr_tab;
    dpr_tab.resize(760, 420);
    dpr_tab.setCodePoints({0x0041u}, {10u}, {QStringLiteral("A")});
    dpr_tab.setDpr(2.0);
    dpr_tab.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dpr_tab));
    auto *dpr_sel_bar
        = dpr_tab.findChild<QWidget *>(QStringLiteral("chars_selection_bar"));
    auto *dpr_cw = dpr_tab.characterWidget();
    QVERIFY(dpr_sel_bar && dpr_cw);
    emit dpr_cw->selectionChangedCodePoint(0x0041u);
    QApplication::processEvents();
    const int expected_min_h = fontui::dp(24, 2.0) + fontui::dp(12, 2.0);
    QVERIFY(dpr_sel_bar->height() >= expected_min_h);
    const QLabel *glyph_lbl = nullptr;
    for (const QLabel *lbl : dpr_sel_bar->findChildren<QLabel *>()) {
        if (lbl->objectName().isEmpty() && lbl->text() == QStringLiteral("A")) {
            glyph_lbl = lbl;
            break;
        }
    }
    QVERIFY(glyph_lbl != nullptr);
    QCOMPARE(glyph_lbl->minimumHeight(), fontui::dp(24, 2.0));
    QCOMPARE(glyph_lbl->height(), fontui::dp(24, 2.0));

    // P2: Full Unicode 16.0 block table & invalid sentinel 0 behavior.
    QCOMPARE(unicode_blocks::findBlockIndex(0u), -1);
    QCOMPARE(unicode_blocks::blockName(0u), QStringLiteral("Unknown Block"));
    QCOMPARE(unicode_blocks::blockName(0x10780u),
             QStringLiteral("Latin Extended-F"));
    QCOMPARE(unicode_blocks::blockName(0x17000u), QStringLiteral("Tangut"));
    QCOMPARE(unicode_blocks::blockName(0x1CC00u),
             QStringLiteral("Symbols for Legacy Computing Supplement"));
    QCOMPARE(unicode_blocks::blockName(0x1DF00u),
             QStringLiteral("Latin Extended-G"));
    QCOMPARE(unicode_blocks::blockName(0x1E900u), QStringLiteral("Adlam"));

    QFontDatabase::removeApplicationFont(rob_id);
}

void ViewerTest::tabHeightForWidthDoesNotInflateMinimumHeight()
{
    const QString roboto_path = fixturePath("Roboto-Regular.ttf");
    QVERIFY(!roboto_path.isEmpty());

    QWidget frame;
    auto *outer = new QGridLayout(&frame);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *center_host = new QWidget(&frame);
    auto *center_lay  = new QGridLayout(center_host);
    center_lay->setContentsMargins(0, 0, 0, 0);
    center_lay->setSpacing(0);

    auto *container = new QWidget(center_host);
    auto *vlay      = new QVBoxLayout(container);
    vlay->setContentsMargins(0, 0, 0, 0);
    vlay->setSpacing(0);

    auto *top_spacer = new QWidget(container);
    top_spacer->setFixedHeight(40);
    auto *fw = new FontWidget(container);
    auto *btm_spacer = new QWidget(container);
    btm_spacer->setFixedHeight(44);
    vlay->addWidget(top_spacer);
    vlay->addWidget(fw);
    vlay->addWidget(btm_spacer);
    center_lay->addWidget(container);
    outer->addWidget(center_host, 1, 0);

    QVERIFY(fw->init(roboto_path));
    fw->setCollapsed(true);
    fw->setCurrentFontSize(48);

    frame.resize(1024, 430);
    frame.show();
    QVERIFY(QTest::qWaitForWindowExposed(&frame));
    QApplication::processEvents();

    auto *tabs     = fw->findChild<QTabWidget *>();
    auto *text_tab = fw->findChild<TextPreviewTab *>();
    auto *char_tab = fw->findChild<CharactersTab *>();
    QVERIFY(tabs && text_tab && char_tab);
    QCOMPARE(tabs->count(), 4);

    for (int i = 0; i < tabs->count(); ++i) {
        tabs->setCurrentIndex(i);
        QApplication::processEvents();
        QVERIFY(fw->layout()->minimumHeightForWidth(1024) <= 320);
        QCOMPARE(center_host->height(), frame.height());
    }

    QVERIFY(text_tab->heightForWidth(340) > text_tab->heightForWidth(1024));
    QVERIFY(char_tab->heightForWidth(340) > char_tab->heightForWidth(1024));
    QVERIFY(fw->layout()->minimumHeightForWidth(340) <= 346);

    text_tab->setWaterfallEnabled(true);
    tabs->setCurrentWidget(text_tab);
    frame.resize(340, 430);
    QApplication::processEvents();
    QVERIFY(fw->layout()->minimumHeightForWidth(340) <= 346);
    QCOMPARE(center_host->height(), frame.height());

    // Direct QBoxLayout / QGridLayout hosting of TextPreviewTab or CharactersTab
    // (outside QTabWidget) must also keep heightForWidth() at minimum height,
    // and respect widget contentsMargins().
    for (bool test_chars : {false, true}) {
        QWidget direct_frame;
        auto *d_outer = new QGridLayout(&direct_frame);
        d_outer->setContentsMargins(0, 0, 0, 0);

        auto *d_center_host = new QWidget(&direct_frame);
        auto *d_center_lay  = new QGridLayout(d_center_host);
        d_center_lay->setContentsMargins(0, 0, 0, 0);
        d_center_lay->setSpacing(0);

        auto *d_container = new QWidget(d_center_host);
        auto *d_vlay      = new QVBoxLayout(d_container);
        d_vlay->setContentsMargins(0, 0, 0, 0);
        d_vlay->setSpacing(0);

        auto *d_top = new QWidget(d_container);
        d_top->setFixedHeight(40);
        QWidget *tab = nullptr;
        if (test_chars) {
            auto *ct = new CharactersTab(d_container);
            ct->setCodePoints({0x0041u, 0x0042u}, {10u, 11u});
            tab = ct;
        }
        else {
            auto *tt = new TextPreviewTab(d_container);
            tt->setWaterfallEnabled(true);
            tab = tt;
        }
        auto *d_btm = new QWidget(d_container);
        d_btm->setFixedHeight(44);
        d_vlay->addWidget(d_top);
        d_vlay->addWidget(tab);
        d_vlay->addWidget(d_btm);
        d_center_lay->addWidget(d_container);
        d_outer->addWidget(d_center_host, 1, 0);

        direct_frame.resize(1024, 430);
        direct_frame.show();
        QVERIFY(QTest::qWaitForWindowExposed(&direct_frame));
        QApplication::processEvents();

        QVERIFY(tab->heightForWidth(1024) <= 220);
        QCOMPARE(tab->layout()->heightForWidth(1024),
                 tab->layout()->minimumHeightForWidth(1024));
        QCOMPARE(d_center_host->height(), direct_frame.height());

        direct_frame.resize(340, 430);
        QApplication::processEvents();
        QVERIFY(tab->heightForWidth(340) > tab->heightForWidth(1024));
        QCOMPARE(d_center_host->height(), direct_frame.height());

        const int base_hfw = tab->heightForWidth(600);
        tab->setContentsMargins(4, 6, 4, 8);
        QVERIFY(tab->heightForWidth(600) >= base_hfw + 14);
    }
}

QTEST_MAIN(ViewerTest)
#include "viewer_tests.moc"
