/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the examples of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:BSD$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** BSD License Usage
** Alternatively, you may use this file under the terms of the BSD license
** as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of The Qt Company Ltd nor the names of its
**     contributors may be used to endorse or promote products derived
**     from this software without specific prior written permission.
**
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "characterwidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QRawFont>
#include <QToolTip>
#include <QtMath>

CharacterWidget::CharacterWidget(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
}

void CharacterWidget::init(const QString &path)
{
    constexpr ushort max = 0xffff;
    const QRawFont rf(path, 12);
    m_char_indexes.reserve(max);
    // cost less than 10ms when contains 30k chars
    for (auto i = 0; i < max; ++i) {
        if (rf.supportsCharacter(i)) {
            m_char_indexes << i;
        }
    }
}

QPixmap CharacterWidget::getSelectedCharPix()
{
    if (m_last_key == 0) {
        return {};
    }
    const auto &pal = palette();

    const QRect rt(0, 0, m_sz_square, m_sz_square);
    QPixmap pix(rt.size());
    pix.fill(pal.base().color());
    QPainter p(&pix);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    p.setFont(m_ft);
    p.setPen(QPen(pal.text().color()));
    p.drawText(rt, Qt::AlignCenter, QChar(m_last_key));
    return pix;
}

void CharacterWidget::updateFont(const QFont &font)
{
    m_ft        = font;
    m_sz_square = qMax(16, 4 + QFontMetrics(m_ft, this).height());
    adjustSize();
    update();
}

QSize CharacterWidget::sizeHint() const
{
    if (auto c = getCharCount()) {
        return QSize(m_columns * m_sz_square,
                     qCeil(c * 1. / m_columns) * m_sz_square);
    }
    return QSize(m_columns * m_sz_square, (65536 / m_columns) * m_sz_square);
}

ushort CharacterWidget::getCharIndexAtPos(const QPoint &pt)
{
    uint index = (pt.y() / m_sz_square) * m_columns + pt.x() / m_sz_square;
    if (index < getCharCount()) {
        return m_char_indexes[index];
    }
    return 0;
}

void CharacterWidget::mouseMoveEvent(QMouseEvent *event)
{
    ushort c     = getCharIndexAtPos(event->pos());
    QString text = QString(
                       "<p>Character: <span style=\"font-size: 28pt; "
                       "font-family: %1\">%2</span><p>Value: 0x%3")
                       .arg(m_ft.family(), QChar(c), QString::number(c, 16));
    QToolTip::showText(event->globalPosition().toPoint(), text, this);
}

void CharacterWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    m_last_key = getCharIndexAtPos(event->pos());
    update();
}

void CharacterWidget::paintEvent(QPaintEvent *event)
{
    if (m_char_indexes.empty()) {
        return;
    }
    QPainter painter(this);
    const auto &p = palette();
    painter.fillRect(event->rect(), p.base());
    painter.setFont(m_ft);

    const QRect rt_redraw = event->rect();
    const int row_end     = rt_redraw.bottom() / m_sz_square;
    const int col_begin   = rt_redraw.left() / m_sz_square;
    const int col_end     = rt_redraw.right() / m_sz_square;
    const ushort char_t   = getCharCount();
    const QFontMetrics fm(m_ft);
    for (int row = rt_redraw.top() / m_sz_square; row <= row_end; ++row) {
        const int first_index_row = row * m_columns;
        const int y               = row * m_sz_square;
        for (int col = col_begin; col <= col_end; ++col) {
            const auto index = first_index_row + col;
            if (index >= char_t) {
                return;
            }
            const int x      = col * m_sz_square;
            const ushort key = m_char_indexes[index];

            painter.setClipRect(x, y, m_sz_square, m_sz_square);

            if (key == m_last_key) {
                painter.fillRect(x + 1, y + 1, m_sz_square, m_sz_square,
                                 p.accent());
            }

            painter.drawText(
                x + (m_sz_square / 2) - fm.horizontalAdvance(QChar(key)) / 2,
                y + 4 + fm.ascent(), QChar(key));
        }
    }
}
