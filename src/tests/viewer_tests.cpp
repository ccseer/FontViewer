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
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
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
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtNumeric>

#include <limits>

#include "characterwidget.h"
#include "fontinspectorengine.h"
#include "fontpreviewedit.h"
#include "fontviewer.h"
#include "fontwidget.h"
#include "glyphinspectorwidget.h"
#include "waterfallwidget.h"

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
        if (btn->toolTip() == QLatin1String("Collapse")) {
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
    for (QWidget *child : view->findChildren<QWidget *>()) {
        if (child == edit || child->window() != view->window()) {
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

    view.resize(850, 600);
    QApplication::processEvents();
    QVERIFY(!aa_btn->isVisible());
    QVERIFY(!aa_panel->isVisible());
    QVERIFY(!aa_panel->isAncestorOf(line_slider));
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
    emit char_wgt->selectionChanged(QChar('A'));
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
    viewer.loadFileInfo();
    viewer.loadFileInfo();
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

        QPushButton *reset_btn = nullptr;
        for (QPushButton *b : static_view.findChildren<QPushButton *>()) {
            if (b->text() == QLatin1String("Reset")) {
                reset_btn = b;
                break;
            }
        }
        QVERIFY(reset_btn == nullptr);
    }

    // Variable font shows axis sliders + Reset buttons, keeps them NoFocus,
    // swallows hover wheel, and resets cleanly.
    FontWidget view;
    QVERIFY(view.init(vf_path));
    view.resize(850, 650);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QVector<QPushButton *> resets;
    for (QPushButton *b : view.findChildren<QPushButton *>()) {
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
             (QStringList{"liga", "kern", "smcp", "ss01", "ss02", "ss03",
                          "cv01", "ccmp", "clig", "salt"}));

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
        emit char_wgt->selectionChanged(QChar('A'));
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

        // Clicking a pill or Reset Features must clear FontPreviewEdit focus.
        QPushButton *reset_btn = nullptr;
        for (QPushButton *b : view->findChildren<QPushButton *>()) {
            if (b->text() == QStringLiteral("Reset Features")) {
                reset_btn = b;
                break;
            }
        }
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

        // Note label palette follows runtime theme/palette Mid color changes.
        QLabel *note_lbl = nullptr;
        for (QLabel *lbl : view->findChildren<QLabel *>()) {
            if (lbl->text().startsWith(QStringLiteral("Text preview only"))) {
                note_lbl = lbl;
                break;
            }
        }
        QVERIFY(note_lbl != nullptr);
        // A non-wrapped hint would force the whole viewer layout wider.
        QVERIFY(note_lbl->wordWrap());
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

    // Many-feature fonts (Noto Sans VF has 40+ features) must stay focus-free
    // and keep the OpenType bar bounded so tabs stay usable.
    {
        const QString noto_vf = fixturePath("NotoSans-VariableFont_wdth,wght.ttf");
        QVERIFY(!noto_vf.isEmpty());
        FontWidget noto_view;
        QVERIFY(noto_view.init(noto_vf));
        noto_view.resize(480, 620);
        noto_view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&noto_view));
        QApplication::processEvents();
        QVERIFY(noto_view.featurePillButtons().size() >= 30);
        auto *noto_tabs = noto_view.findChild<QTabWidget *>();
        QVERIFY(noto_tabs != nullptr);
        QVERIFY(noto_tabs->height() >= 250);
        QScrollArea *feat_scroll = nullptr;
        for (QScrollArea *sa : noto_view.findChildren<QScrollArea *>()) {
            if (!sa->findChildren<QPushButton *>().isEmpty()) {
                feat_scroll = sa;
                break;
            }
        }
        QVERIFY(feat_scroll != nullptr);
        QVERIFY(feat_scroll->verticalScrollBar()->maximum() > 0);
        QVERIFY(feat_scroll->height() <= 120);
        for (QWidget *child : noto_view.findChildren<QWidget *>()) {
            if (qobject_cast<FontPreviewEdit *>(child)
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
            emit char_wgt->selectionChanged(QChar('H'));
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

        QCheckBox *chk = nullptr;
        for (QCheckBox *c : gi->findChildren<QCheckBox *>()) {
            if (c->text() == QLatin1String("Show Outline Points")) {
                chk = c;
                break;
            }
        }
        QVERIFY(chk != nullptr);
        QCOMPARE(chk->focusPolicy(), Qt::NoFocus);
        QVERIFY(!chk->isChecked());

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

        chk->setChecked(true);
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

    const int expected_sq = qMax(16, 4 + QFontMetrics(large_ft, &cw).height());
    QVERIFY(expected_sq > 255);

    const QWidget &cw_widget = cw;
    QCOMPARE(cw_widget.sizeHint().width(), 16 * expected_sq);

    // Resizing wider than 16 columns must neither draw ghost columns past col 15
    // nor select characters outside the 16-column grid.
    cw.resize(16 * expected_sq + 64, expected_sq);
    cw.show();
    QVERIFY(QTest::qWaitForWindowExposed(&cw));
    const QImage rendered = cw.grab().toImage();
    const QColor base_col = cw.palette().base().color();
    for (int y = 0; y < rendered.height(); ++y) {
        for (int x = 16 * expected_sq + 1; x < rendered.width(); ++x) {
            QCOMPARE(rendered.pixelColor(x, y), base_col);
        }
    }

    QSignalSpy spy(&cw, &CharacterWidget::selectionChanged);
    const QPoint out_pt(16 * expected_sq + 8, 8);
    QMouseEvent press(QEvent::MouseButtonPress, out_pt, cw.mapToGlobal(out_pt),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&cw, &press);
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(0).toChar().isNull());
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

QTEST_MAIN(ViewerTest)
#include "viewer_tests.moc"
