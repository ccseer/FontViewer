#include "fontpreviewedit.h"

#include <QFocusEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QWheelEvent>

#include "fontui.h"

FontPreviewEdit::FontPreviewEdit(QWidget *parent) : QTextEdit(parent)
{
    setAcceptRichText(false);
    setFrameShape(QFrame::NoFrame);
    verticalScrollBar()->setProperty("is_readonly", true);
    horizontalScrollBar()->setProperty("is_readonly", true);
    // Only a mouse click may move focus into the editor; the viewer keeps every
    // other control out of the focus chain so the host keeps Space / arrows.
    setFocusPolicy(Qt::ClickFocus);
    document()->setDocumentMargin(fontui::dpf(8.0, m_dpr));

    connect(this, &QTextEdit::textChanged, this, [this]() {
        if (m_setting_text || m_applying_format) {
            return;
        }
        emit sigUserEdited();
    });
}

void FontPreviewEdit::setDpr(qreal dpr)
{
    const qreal clamped = fontui::clampDpr(dpr);
    if (qFuzzyCompare(m_dpr, clamped)) {
        return;
    }
    m_dpr = clamped;
    document()->setDocumentMargin(fontui::dpf(8.0, m_dpr));
}

void FontPreviewEdit::setPreviewText(const QString &text)
{
    m_setting_text = true;
    setPlainText(text);
    m_setting_text = false;
    applyLineHeightToAll();
    if (QTextDocument *doc = document()) {
        doc->clearUndoRedoStacks();
    }
}

void FontPreviewEdit::setLineHeight(qreal height)
{
    m_line_height = qBound(1.0, height, 2.5);
    applyLineHeightToAll();
}

void FontPreviewEdit::applyLineHeightToAll()
{
    if (m_applying_format) {
        return;
    }

    QTextDocument *doc = document();
    if (!doc) {
        return;
    }

    QTextBlockFormat format;
    format.setLineHeight(m_line_height * 100.0,
                         QTextBlockFormat::ProportionalHeight);

    m_applying_format = true;

    // Every block is rewritten, so the whole pass is wrapped in a single edit
    // block: rewriting a block format emits contentsChanged again, and the
    // re-entry flag above keeps that from recursing. Merging instead of
    // replacing keeps alignment / indent / margins set elsewhere untouched.
    QTextCursor edit_cursor(doc);
    edit_cursor.beginEditBlock();
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        QTextCursor block_cursor(block);
        block_cursor.mergeBlockFormat(format);
    }
    edit_cursor.endEditBlock();

    m_applying_format = false;
}

void FontPreviewEdit::insertFromMimeData(const QMimeData *source)
{
    if (!source) {
        return;
    }

    QString text = source->text();
    if (text.isEmpty()) {
        return;
    }
    if (text.size() > kMaxPasteChars) {
        int cut = kMaxPasteChars;
        // Never cut between a surrogate pair: the resulting lone high surrogate
        // cannot be shaped and would show up as a replacement glyph.
        if (cut > 0 && text.at(cut - 1).isHighSurrogate()) {
            --cut;
        }
        text.truncate(cut);
        emit sigToast(
            tr("Pasted text was truncated to %1 characters").arg(kMaxPasteChars));
    }

    // Keep the paste and line-height refresh as a single undo step.
    QTextCursor edit_cursor = textCursor();
    edit_cursor.beginEditBlock();
    insertPlainText(text);
    applyLineHeightToAll();
    edit_cursor.endEditBlock();
}

bool FontPreviewEdit::event(QEvent *event)
{
    if (event->type() == QEvent::Paint) {
        // PreviewSurfaceFrame paints the single rounded border; suppress
        // QTextEdit's own PE_Frame pass so Seer Host's MaterialStyle does not
        // draw an inner frame under the scrollbar.
        return true;
    }
    if (event->type() == QEvent::ShortcutOverride) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape
            && !(ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
            // Claim Escape before a host window QAction("Esc") fires so first
            // Esc cancels IME or queues focus release instead of closing Seer.
            event->accept();
            return true;
        }
    }
    return QTextEdit::event(event);
}

void FontPreviewEdit::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape
        && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
        if (m_ime_composing) {
            // The candidate window owns this key; only forward it so the input
            // method can cancel the composition. Focus must stay here, and an
            // ignored Escape must not bubble up to ancestor handlers.
            QTextEdit::keyPressEvent(event);
            event->accept();
            return;
        }
        event->accept();
        // The host resolves its Esc action from a queued call that reads
        // QApplication::focusWidget() when it runs. Releasing the focus from the
        // same key event would race that call, so defer it to the event queue.
        QMetaObject::invokeMethod(
            this, [this]() { clearFocus(); }, Qt::QueuedConnection);
        return;
    }
    QTextEdit::keyPressEvent(event);
}

void FontPreviewEdit::inputMethodEvent(QInputMethodEvent *event)
{
    m_ime_composing = !event->preeditString().isEmpty();
    QTextEdit::inputMethodEvent(event);
}

void FontPreviewEdit::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const int delta = event->angleDelta().y();
        if (delta != 0) {
            emit sigZoomRequested(delta > 0 ? 1 : -1);
        }
        event->accept();
        return;
    }
    // A bare wheel scrolls the document; swallowing it would force the user to
    // reach for the scroll bar on a long paste.
    QTextEdit::wheelEvent(event);
}

void FontPreviewEdit::focusInEvent(QFocusEvent *event)
{
    QTextEdit::focusInEvent(event);
    emit sigFocusChanged(true);
}

void FontPreviewEdit::focusOutEvent(QFocusEvent *event)
{
    // Losing focus while the input method is composing leaves no candidate
    // window to cancel, so drop the flag instead of swallowing the next Esc.
    m_ime_composing = false;
    QTextEdit::focusOutEvent(event);
    emit sigFocusChanged(false);
}
