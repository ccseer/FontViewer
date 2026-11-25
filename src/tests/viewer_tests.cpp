// Offscreen regression suite for the Text tab: editable preview, type-scale
// waterfall, focus contract and preference persistence. Tests that need a real
// font take its path from FONTVIEWER_TEST_FONT and skip when the variable is
// unset, so the suite never depends on a network download or a fixed path.

#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QInputMethodEvent>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
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
    void inspectorEngineResolvesTtcAndLoadFileInfoLatches();
    void variableAxesBarControlsFocusWheelAndReset();
    void namedInstanceSyncsOrPreservesAxisCoords();
    void variableAxisDragDefersHiddenCharactersRebuild();
    void glyphInspectorBandsNoOverlapAndNoHorizontalScroll();
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

    // Glyph role on CharacterWidget never inherits letter spacing.
    QVERIFY(qFuzzyIsNull(char_wgt->currentFont().letterSpacing()));
    QVERIFY(!qFuzzyIsNull(edit->font().letterSpacing()));

    const float initial_char_wght
        = char_wgt->currentFont().variableAxisValue("wght");

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

    QCOMPARE(sa.horizontalScrollBar()->maximum(), 0);
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
}

QTEST_MAIN(ViewerTest)
#include "viewer_tests.moc"
