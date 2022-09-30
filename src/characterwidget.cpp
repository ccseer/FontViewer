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

#include <QFontDatabase>
#include <QMouseEvent>
#include <QPainter>
#include <QRawFont>
#include <QToolTip>

CharacterWidget::CharacterWidget(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
}

void CharacterWidget::init(const QString &path)
{
    const ushort max = 0xffff;
    const QRawFont rf(path, 12);
    // cost less than 10ms when contains 30k chars
    for (auto i = 0; i < max; ++i) {
        if (rf.supportsCharacter(i)) {
            m_char_indexes << i;
        }
    }
}

void CharacterWidget::updateFont(const QFont &font)
{
    m_ft = font;
    calculateSquareSize();
    adjustSize();
    update();
}

void CharacterWidget::calculateSquareSize()
{
    m_squareSize = qMax(16, 4 + QFontMetrics(m_ft, this).height());
}

QSize CharacterWidget::sizeHint() const
{
    return QSize(m_columns * m_squareSize, (65536 / m_columns) * m_squareSize);
}

void CharacterWidget::mouseMoveEvent(QMouseEvent *event)
{
    QPoint widgetPosition = mapFromGlobal(event->globalPos());
    uint key              = (widgetPosition.y() / m_squareSize) * m_columns
               + widgetPosition.x() / m_squareSize;

    QString text
        = QString::fromLatin1(
              "<p>Character: <span style=\"font-size: 24pt; font-family: %1\">")
              .arg(m_ft.family())
          + QChar(key) + QString::fromLatin1("</span><p>Value: 0x")
          + QString::number(key, 16);
    QToolTip::showText(event->globalPos(), text, this);
}

void CharacterWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_lastKey = (event->y() / m_squareSize) * m_columns
                    + event->x() / m_squareSize;
        if (QChar(m_lastKey).category() != QChar::Other_NotAssigned)
            emit characterSelected(QString(QChar(m_lastKey)));
        update();
    }
    else
        QWidget::mousePressEvent(event);
}

void CharacterWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.fillRect(event->rect(), QBrush(Qt::white));
    painter.setFont(m_ft);

    QRect redrawRect = event->rect();
    int beginRow     = redrawRect.top() / m_squareSize;
    int endRow       = redrawRect.bottom() / m_squareSize;
    int beginColumn  = redrawRect.left() / m_squareSize;
    int endColumn    = redrawRect.right() / m_squareSize;

    //    for (int row = beginRow; row <= endRow; ++row) {
    //        for (int column = beginColumn; column <= endColumn; ++column) {
    //        }
    //    }

    // TODO: need some modifications to make it work
    QFontMetrics fontMetrics(m_ft);
    const QRawFont rawft = QRawFont::fromFont(m_ft);
    for (int row = beginRow; row <= endRow; ++row) {
        for (int column = beginColumn; column <= endColumn; ++column) {
            int key = row * m_columns + column;
            if (!rawft.supportsCharacter(key)) {
                continue;
            }

            painter.setPen(QPen(Qt::gray));
            painter.drawRect(column * m_squareSize, row * m_squareSize,
                             m_squareSize, m_squareSize);

            painter.setPen(QPen(Qt::black));
            painter.setClipRect(column * m_squareSize, row * m_squareSize,
                                m_squareSize, m_squareSize);

            if (key == m_lastKey)
                painter.fillRect(column * m_squareSize + 1,
                                 row * m_squareSize + 1, m_squareSize,
                                 m_squareSize, QBrush(Qt::red));

            painter.drawText(
                column * m_squareSize + (m_squareSize / 2)
                    - fontMetrics.horizontalAdvance(QChar(key)) / 2,
                row * m_squareSize + 4 + fontMetrics.ascent(),
                QString(QChar(key)));
        }
    }
}
