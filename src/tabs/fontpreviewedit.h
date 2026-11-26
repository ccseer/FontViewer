#pragma once

#include <QTextEdit>

class FontPreviewEdit : public QTextEdit {
    Q_OBJECT

public:
    explicit FontPreviewEdit(QWidget *parent = nullptr);

    static constexpr int kMaxPasteChars = 10000;

    void setDpr(qreal dpr);
    void setPreviewText(const QString &text);
    void setLineHeight(qreal height);
    qreal lineHeight() const
    {
        return m_line_height;
    }

    Q_SIGNAL void sigToast(const QString &msg);
    Q_SIGNAL void sigUserEdited();
    Q_SIGNAL void sigZoomRequested(int steps);
    Q_SIGNAL void sigFocusChanged(bool focused);

protected:
    bool event(QEvent *event) override;
    void insertFromMimeData(const QMimeData *source) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void applyLineHeightToAll();

    qreal m_dpr            = 1.0;
    qreal m_line_height    = 1.4;
    bool m_applying_format = false;
    bool m_setting_text    = false;
    bool m_ime_composing   = false;
};
