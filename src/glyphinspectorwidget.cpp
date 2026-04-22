#include "glyphinspectorwidget.h"

#include <QFontMetrics>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPainter>
#include <QRawFont>
#include <QVBoxLayout>

#define qprintt qDebug() << "[GlyphInspector]"

// GlyphPreviewWidget implementation
GlyphPreviewWidget::GlyphPreviewWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(300, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void GlyphPreviewWidget::setGlyphData(QChar ch,
                                      const QFont &font,
                                      const QRectF &bbox)
{
    m_char     = ch;
    m_font     = font;
    m_bbox     = bbox;
    m_has_data = true;
    update();
}

void GlyphPreviewWidget::clear()
{
    m_has_data = false;
    update();
}

QSize GlyphPreviewWidget::sizeHint() const
{
    return QSize(400, 400);
}

void GlyphPreviewWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    const auto &pal  = palette();
    const QRect rect = this->rect();

    // Fill background
    painter.fillRect(rect, pal.base());

    if (!m_has_data) {
        return;
    }

    // Calculate center
    int center_x = rect.width() / 2;
    int center_y = rect.height() / 2;

    // Setup display font
    QFont display_font = m_font;
    display_font.setPointSize(qMax(72, m_font.pointSize() * 4));
    QFontMetrics fm(display_font);

    // Calculate text position (center the glyph in the widget)
    // drawText uses baseline as Y coordinate
    // To center vertically: baseline = center_y + (ascent - descent) / 2
    // Calculate text position (center the glyph in the widget)
    // drawText uses baseline as Y coordinate
    // To center vertically: baseline = center_y + (ascent - descent) / 2
    int advance_width = fm.horizontalAdvance(m_char);
    int text_x        = center_x - advance_width / 2;
    int text_y        = center_y + (fm.ascent() - fm.descent()) / 2;

    // Draw advance width rectangle (subtle fill)
    painter.fillRect(QRect(text_x, 20, advance_width, rect.height() - 40),
                     QColor(200, 100, 255, 10));

    // Draw baseline (dashed line) - horizontal through text baseline
    QPen baseline_pen(pal.mid().color());
    baseline_pen.setStyle(Qt::DashLine);
    baseline_pen.setWidth(1);
    painter.setPen(baseline_pen);
    painter.drawLine(10, text_y, rect.width() - 10, text_y);

    // Draw vertical center line (dotted)
    QPen center_pen(pal.mid().color());
    center_pen.setStyle(Qt::DotLine);
    center_pen.setWidth(1);
    painter.setPen(center_pen);
    painter.drawLine(center_x, 10, center_x, rect.height() - 10);

    // Draw advance width vertical lines
    QPen advance_pen(QColor(200, 100, 255, 120));
    advance_pen.setStyle(Qt::DashLine);
    painter.setPen(advance_pen);
    painter.drawLine(text_x, 20, text_x, rect.height() - 20);
    painter.drawLine(text_x + advance_width, 20, text_x + advance_width,
                     rect.height() - 20);

    // Draw ascent line (above baseline)
    QPen ascent_pen(QColor(100, 150, 255, 120));
    ascent_pen.setStyle(Qt::DotLine);
    painter.setPen(ascent_pen);
    int ascent_y = text_y - fm.ascent();
    painter.drawLine(10, ascent_y, rect.width() - 10, ascent_y);

    // Draw descent line (below baseline)
    QPen descent_pen(QColor(255, 150, 100, 120));
    descent_pen.setStyle(Qt::DotLine);
    painter.setPen(descent_pen);
    int descent_y = text_y + fm.descent();
    painter.drawLine(10, descent_y, rect.width() - 10, descent_y);

    // Draw cap height line
    int cap_y = text_y - fm.capHeight();
    if (fm.capHeight() > 0 && cap_y != ascent_y) {
        QPen cap_pen(QColor(255, 100, 200, 120));
        cap_pen.setStyle(Qt::DotLine);
        painter.setPen(cap_pen);
        painter.drawLine(10, cap_y, rect.width() - 10, cap_y);
    }

    // Draw x-height line
    int x_y = text_y - fm.xHeight();
    if (fm.xHeight() > 0) {
        QPen x_pen(QColor(100, 200, 150, 120));
        x_pen.setStyle(Qt::DotLine);
        painter.setPen(x_pen);
        painter.drawLine(10, x_y, rect.width() - 10, x_y);
    }

    // Draw bounding box (green) - actual glyph bounds
    QPen bbox_pen(QColor(100, 255, 100, 100));
    bbox_pen.setStyle(Qt::DashDotLine);
    bbox_pen.setWidth(1);
    painter.setPen(bbox_pen);

    // Get tight bounding rect for the character
    QRect tight_bbox = fm.tightBoundingRect(m_char);
    // tightBoundingRect returns rect relative to baseline at (0,0)
    // Translate to actual drawing position
    tight_bbox.translate(text_x, text_y);
    painter.drawRect(tight_bbox);

    // Draw bearing markers
    QPen bearing_pen(QColor(150, 150, 150, 80));
    bearing_pen.setStyle(Qt::DotLine);
    painter.setPen(bearing_pen);
    painter.drawLine(tight_bbox.left(), 30, tight_bbox.left(),
                     rect.height() - 30);
    painter.drawLine(tight_bbox.right(), 30, tight_bbox.right(),
                     rect.height() - 30);

    // Draw the glyph
    painter.setFont(display_font);
    painter.setPen(pal.text().color());
    painter.drawText(text_x, text_y, QString(m_char));

    // Draw labels with better safety margins and alignment
    QFont label_font = painter.font();
    label_font.setPointSize(8);
    painter.setFont(label_font);

    auto drawMetricLabel = [&](int y, const QString &text, QColor color,
                               bool isAbove) {
        painter.setPen(color);
        int w = 100;
        int h = 25;
        int x = rect.width() - w - 5;
        QRect r(x, isAbove ? y - h : y, w, h);
        painter.drawText(
            r, Qt::AlignRight | (isAbove ? Qt::AlignBottom : Qt::AlignTop),
            text);
    };

    // Right-side horizontal metrics
    drawMetricLabel(ascent_y, "ascent", QColor(100, 150, 255), true);
    if (fm.capHeight() > 0 && cap_y != ascent_y) {
        drawMetricLabel(cap_y, "cap height", QColor(255, 100, 200), true);
    }
    if (fm.xHeight() > 0) {
        drawMetricLabel(x_y, "x-height", QColor(100, 200, 150), true);
    }
    drawMetricLabel(text_y, "baseline", pal.mid().color(), false);
    drawMetricLabel(descent_y, "descent", QColor(255, 150, 100), false);

    // Vertical metrics (Origin, Advance, LSB, RSB)
    auto drawVerticalLabel = [&](int x, const QString &text, QColor color,
                                 bool isBottom, bool isLeftOfLine) {
        painter.setPen(color);
        int w       = 60;
        int h       = 20;
        int label_x = isLeftOfLine ? x - w - 2 : x + 2;

        // Boundary check: if outside is clipped by widget, move to inside
        if (label_x < 5)
            label_x = x + 2;
        if (label_x + w > rect.width() - 5)
            label_x = x - w - 2;

        int label_y = isBottom ? rect.height() - h - 5 : 5;
        QRect r(label_x, label_y, w, h);
        painter.drawText(
            r,
            (label_x < x ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter,
            text);
    };

    drawVerticalLabel(text_x, "origin", QColor(200, 100, 255), true, true);
    drawVerticalLabel(text_x + advance_width, "advance", QColor(200, 100, 255),
                      true, false);

    if (tight_bbox.left() != text_x) {
        drawVerticalLabel(tight_bbox.left(), "LSB", QColor(150, 150, 150),
                          false, true);
    }
    if (tight_bbox.right() != text_x + advance_width) {
        drawVerticalLabel(tight_bbox.right(), "RSB", QColor(150, 150, 150),
                          false, false);
    }
}

// GlyphInspectorWidget implementation
GlyphInspectorWidget::GlyphInspectorWidget(QWidget *parent) : QWidget(parent)
{
    createUI();
}

void GlyphInspectorWidget::createUI()
{
    auto main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(10, 10, 10, 10);
    main_layout->setSpacing(10);

    // No selection label
    m_label_no_selection
        = new QLabel("Select a character from the Characters tab", this);
    m_label_no_selection->setAlignment(Qt::AlignCenter);
    m_label_no_selection->setWordWrap(true);
    QFont font = m_label_no_selection->font();
    font.setPointSize(14);
    font.setItalic(true);
    m_label_no_selection->setFont(font);
    QPalette pal = m_label_no_selection->palette();
    pal.setColor(QPalette::WindowText, pal.color(QPalette::Mid));
    m_label_no_selection->setPalette(pal);
    main_layout->addWidget(m_label_no_selection, 1);  // stretch to fill space

    // Unicode Information group
    m_group_unicode         = new QGroupBox("Unicode Information", this);
    auto unicode_layout     = new QFormLayout(m_group_unicode);
    m_label_unicode_value   = new QLabel(this);
    m_label_unicode_decimal = new QLabel(this);
    m_label_unicode_block   = new QLabel(this);
    unicode_layout->addRow("Code Point:", m_label_unicode_value);
    unicode_layout->addRow("Decimal:", m_label_unicode_decimal);
    unicode_layout->addRow("Block:", m_label_unicode_block);
    main_layout->addWidget(m_group_unicode);

    // Glyph Metrics group
    m_group_glyph         = new QGroupBox("Glyph Metrics", this);
    auto glyph_layout     = new QFormLayout(m_group_glyph);
    m_label_advance_width = new QLabel(this);
    m_label_bbox          = new QLabel(this);
    m_label_bearings      = new QLabel(this);
    glyph_layout->addRow("Advance Width:", m_label_advance_width);
    glyph_layout->addRow("Bounding Box:", m_label_bbox);
    glyph_layout->addRow("Bearings:", m_label_bearings);
    main_layout->addWidget(m_group_glyph);

    // Font Metrics group
    m_group_font         = new QGroupBox("Font Metrics", this);
    auto font_layout     = new QFormLayout(m_group_font);
    m_label_ascent       = new QLabel(this);
    m_label_descent      = new QLabel(this);
    m_label_line_gap     = new QLabel(this);
    m_label_units_per_em = new QLabel(this);
    font_layout->addRow("Ascent:", m_label_ascent);
    font_layout->addRow("Descent:", m_label_descent);
    font_layout->addRow("Line Gap:", m_label_line_gap);
    font_layout->addRow("Units per Em:", m_label_units_per_em);
    main_layout->addWidget(m_group_font);

    // Visual Preview group
    m_group_preview     = new QGroupBox("Visual Preview", this);
    auto preview_layout = new QVBoxLayout(m_group_preview);
    m_preview_widget    = new GlyphPreviewWidget(this);
    preview_layout->addWidget(m_preview_widget,
                              1);  // stretch factor 1 to fill space
    main_layout->addWidget(m_group_preview,
                           1);  // stretch factor 1 to take remaining space

    // Initially hide all groups
    m_group_unicode->hide();
    m_group_glyph->hide();
    m_group_font->hide();
    m_group_preview->hide();
}

void GlyphInspectorWidget::updateGlyph(QChar ch,
                                       const QFont &font,
                                       const QString &font_path)
{
    m_char          = ch;
    m_font          = font;
    m_font_path     = font_path;
    m_has_selection = !ch.isNull();

    if (m_has_selection) {
        m_label_no_selection->hide();
        m_group_unicode->show();
        m_group_glyph->show();
        m_group_font->show();
        m_group_preview->show();

        updateMetrics();
        updateLabels();
        m_preview_widget->setGlyphData(m_char, m_font, m_metrics.bounding_box);
    }
    else {
        clearSelection();
    }
}

void GlyphInspectorWidget::clearSelection()
{
    m_has_selection = false;
    m_label_no_selection->show();
    m_group_unicode->hide();
    m_group_glyph->hide();
    m_group_font->hide();
    m_group_preview->hide();
    m_preview_widget->clear();
}

QPixmap GlyphInspectorWidget::getPreviewPixmap() const
{
    if (!m_has_selection || !m_preview_widget) {
        return {};
    }
    return m_preview_widget->grab();
}

void GlyphInspectorWidget::updateFont(const QFont &font)
{
    if (m_has_selection) {
        m_font = font;
        updateMetrics();
        updateLabels();
        m_preview_widget->setGlyphData(m_char, m_font, m_metrics.bounding_box);
    }
}

void GlyphInspectorWidget::updateTheme(int theme)
{
    m_theme = theme;
    if (m_preview_widget) {
        m_preview_widget->update();
    }
}

void GlyphInspectorWidget::updateMetrics()
{
    // Try QRawFont first for accurate metrics
    QRawFont raw_font = QRawFont::fromFont(m_font);
    if (!raw_font.isValid()) {
        qprintt << "QRawFont invalid, falling back to QFontMetrics";
        // Fallback to QFontMetrics
        QFontMetrics fm(m_font);
        m_metrics.advance_width = fm.horizontalAdvance(m_char);
        m_metrics.bounding_box  = fm.boundingRect(m_char);
        m_metrics.ascent        = fm.ascent();
        m_metrics.descent       = fm.descent();
        m_metrics.line_gap      = fm.lineSpacing() - fm.height();
        m_metrics.units_per_em  = 0;
        m_metrics.left_bearing  = 0;
        m_metrics.right_bearing = 0;
        return;
    }

    QString str(m_char);
    QVector<quint32> glyph_indexes = raw_font.glyphIndexesForString(str);
    if (glyph_indexes.isEmpty()) {
        qprintt << "No glyph index for character" << m_char;
        return;
    }

    quint32 glyph_index       = glyph_indexes[0];
    QVector<QPointF> advances = raw_font.advancesForGlyphIndexes({glyph_index});

    m_metrics.advance_width = advances.isEmpty() ? 0 : advances[0].x();
    m_metrics.bounding_box  = raw_font.boundingRect(glyph_index);
    m_metrics.ascent        = raw_font.ascent();
    m_metrics.descent       = raw_font.descent();
    m_metrics.line_gap      = raw_font.leading();
    m_metrics.units_per_em  = raw_font.unitsPerEm();

    // Calculate bearings
    m_metrics.left_bearing = m_metrics.bounding_box.left();
    m_metrics.right_bearing
        = m_metrics.advance_width - m_metrics.bounding_box.right();
}

void GlyphInspectorWidget::updateLabels()
{
    // Unicode info
    ushort code_point = m_char.unicode();
    m_label_unicode_value->setText(
        QString("U+%1").arg(code_point, 4, 16, QChar('0')).toUpper());
    m_label_unicode_decimal->setText(QString::number(code_point));
    m_label_unicode_block->setText(getUnicodeBlockName(code_point));

    // Glyph metrics
    m_label_advance_width->setText(
        QString("%1 px").arg(m_metrics.advance_width, 0, 'f', 2));
    m_label_bbox->setText(QString("(%1, %2, %3, %4)")
                              .arg(m_metrics.bounding_box.x(), 0, 'f', 1)
                              .arg(m_metrics.bounding_box.y(), 0, 'f', 1)
                              .arg(m_metrics.bounding_box.width(), 0, 'f', 1)
                              .arg(m_metrics.bounding_box.height(), 0, 'f', 1));
    m_label_bearings->setText(QString("Left %1, Right %2")
                                  .arg(m_metrics.left_bearing, 0, 'f', 2)
                                  .arg(m_metrics.right_bearing, 0, 'f', 2));

    // Font metrics
    m_label_ascent->setText(QString("%1").arg(m_metrics.ascent, 0, 'f', 2));
    m_label_descent->setText(QString("%1").arg(m_metrics.descent, 0, 'f', 2));
    m_label_line_gap->setText(QString("%1").arg(m_metrics.line_gap, 0, 'f', 2));
    m_label_units_per_em->setText(m_metrics.units_per_em > 0
                                      ? QString::number(m_metrics.units_per_em)
                                      : "N/A");
}

QString GlyphInspectorWidget::getUnicodeBlockName(ushort code_point)
{
    // Basic Unicode block detection
    if (code_point <= 0x007F)
        return "Basic Latin";
    if (code_point <= 0x00FF)
        return "Latin-1 Supplement";
    if (code_point <= 0x017F)
        return "Latin Extended-A";
    if (code_point <= 0x024F)
        return "Latin Extended-B";
    if (code_point <= 0x02AF)
        return "IPA Extensions";
    if (code_point <= 0x036F)
        return "Combining Diacritical Marks";
    if (code_point <= 0x03FF)
        return "Greek and Coptic";
    if (code_point <= 0x04FF)
        return "Cyrillic";
    if (code_point <= 0x052F)
        return "Cyrillic Supplement";
    if (code_point <= 0x058F)
        return "Armenian";
    if (code_point <= 0x05FF)
        return "Hebrew";
    if (code_point <= 0x06FF)
        return "Arabic";
    if (code_point <= 0x09FF)
        return "Devanagari / Bengali / Gurmukhi / Gujarati / Oriya / Tamil / "
               "Telugu / Kannada / Malayalam";
    if (code_point <= 0x0FFF)
        return "Thai / Lao / Tibetan / Myanmar";
    if (code_point <= 0x109F)
        return "Georgian / Hangul Jamo";
    if (code_point <= 0x11FF)
        return "Ethiopic / Cherokee";
    if (code_point <= 0x1FFF)
        return "Unified Canadian Aboriginal Syllabics / Ogham / Runic / "
               "Tagalog / Hanunoo / Buhid / Tagbanwa / Khmer / Mongolian";
    if (code_point <= 0x20CF)
        return "General Punctuation / Superscripts and Subscripts / Currency "
               "Symbols";
    if (code_point <= 0x214F)
        return "Letterlike Symbols / Number Forms";
    if (code_point <= 0x218F)
        return "Arrows";
    if (code_point <= 0x24FF)
        return "Mathematical Operators / Miscellaneous Technical / Control "
               "Pictures / OCR / Enclosed Alphanumerics / Box Drawing / Block "
               "Elements / Geometric Shapes / Miscellaneous Symbols";
    if (code_point <= 0x27BF)
        return "Dingbats / Miscellaneous Mathematical Symbols-A / Supplemental "
               "Arrows-A";
    if (code_point <= 0x2FFF)
        return "Braille Patterns / Supplemental Arrows-B / Miscellaneous "
               "Mathematical Symbols-B / Supplemental Mathematical Operators / "
               "Miscellaneous Symbols and Arrows";
    if (code_point <= 0x4DBF)
        return "CJK Radicals Supplement / Kangxi Radicals / Ideographic "
               "Description Characters / CJK Symbols and Punctuation / "
               "Hiragana / Katakana / Bopomofo / Hangul Compatibility Jamo / "
               "Kanbun / Bopomofo Extended / CJK Strokes / Katakana Phonetic "
               "Extensions / Enclosed CJK Letters and Months / CJK "
               "Compatibility / CJK Unified Ideographs Extension A";
    if (code_point <= 0x9FFF)
        return "CJK Unified Ideographs";
    if (code_point <= 0xD7FF)
        return "Hangul Syllables";
    if (code_point <= 0xF8FF)
        return "Private Use Area";
    if (code_point <= 0xFAFF)
        return "CJK Compatibility Ideographs";
    if (code_point <= 0xFB4F)
        return "Alphabetic Presentation Forms";
    if (code_point <= 0xFDFF)
        return "Arabic Presentation Forms-A";
    if (code_point <= 0xFE6F)
        return "Combining Half Marks / CJK Compatibility Forms / Small Form "
               "Variants";
    if (code_point <= 0xFEFF)
        return "Arabic Presentation Forms-B / Halfwidth and Fullwidth Forms / "
               "Specials";
    return "Unknown Block";
}
