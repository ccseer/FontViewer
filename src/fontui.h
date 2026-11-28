#pragma once

#include <QColor>
#include <QGridLayout>
#include <QIcon>
#include <QLabel>
#include <QLayout>
#include <QOperatingSystemVersion>
#include <QPalette>
#include <QPushButton>
#include <QVector>
#include <QWidget>
#include <QtGlobal>

class QComboBox;
class QEvent;
class QVBoxLayout;

namespace fontui {

extern const char kSvgCopyImage[];
extern const char kSvgSaveSvg[];
extern const char kSvgChevronUp[];
extern const char kSvgChevronDown[];

QIcon svgIcon(const char *svg_data, const QColor &color, int icon_sz, qreal dpr);
QPushButton *makeIconButton(const char *tooltip, QWidget *parent);
void configureComboBox(QComboBox *cb);
void fitComboBoxToContents(QComboBox *cb, qreal dpr);

// Compact inline text-action button that matches label row height and retains
// its layout space while hidden to prevent toolbar/header jitter.
class CompactActionButton : public QPushButton {
public:
    explicit CompactActionButton(const QString &text,
                                 qreal dpr,
                                 QWidget *parent    = nullptr,
                                 bool retain_hidden = true);

    void setDpr(qreal dpr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    qreal dpr() const
    {
        return m_dpr;
    }
    void changeEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    qreal m_dpr = 1.0;
};

// Vertical divider for FlowLayout that carries the "flow_separator" property
// so FlowLayout automatically hides leading, trailing, consecutive, or wrapped
// separators during layout without mutating widget visibility.
class FlowSeparator : public QWidget {
public:
    explicit FlowSeparator(qreal dpr, QWidget *parent = nullptr);

    void setDpr(qreal dpr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    qreal m_dpr = 1.0;
};

class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget *parent = nullptr,
                        int h_spacing   = 5,
                        int v_spacing   = 4);
    ~FlowLayout() override;

    void setSpacingPair(int h_spacing, int v_spacing);
    void addItem(QLayoutItem *item) override;
    int count() const override;
    QLayoutItem *itemAt(int index) const override;
    QLayoutItem *takeAt(int index) override;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    void setGeometry(const QRect &rect) override;
    QSize sizeHint() const override;
    QSize minimumSize() const override;

private:
    int doLayout(const QRect &rect, bool test_only) const;

    QVector<QLayoutItem *> m_items;
    int m_h_spacing = 5;
    int m_v_spacing = 4;
};

// Grid layout whose heightForWidth() reports minimumHeightForWidth() so parent
// layouts (both QStackedLayout inside QTabWidget and QBoxLayout/QGridLayout
// via QWidgetItem) wrap toolbars without turning scrollable viewport sizeHints
// into minimum-height floors.
class MinHfwGridLayout : public QGridLayout {
public:
    using QGridLayout::QGridLayout;

    int heightForWidth(int width) const override;
};

// Host-aligned rounded surface card behind preview/grid viewports.
class SurfaceFrame : public QWidget {
public:
    explicit SurfaceFrame(QWidget *parent = nullptr);

    void addContent(QWidget *w);
    void setDpr(qreal dpr);
    void setInverted(bool inverted);
    void setFocused(bool focused);
    void setWaterfallActive(bool active);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void updateMetrics();

    QVBoxLayout *m_layout = nullptr;
    qreal m_dpr           = 1.0;
    bool m_inverted       = false;
    bool m_focused        = false;
    bool m_waterfall      = false;
};

// Compact pill badge displaying tabular values (height/spacing/cell size).
class ValueBadge final : public QLabel {
public:
    explicit ValueBadge(qreal dpr,
                        const QString &sample_width_text = QStringLiteral("-20.0"),
                        QWidget *parent = nullptr);

    void setDpr(qreal dpr);
    void refreshFont();
    void setModified(bool modified);
    bool isModified() const
    {
        return m_modified;
    }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void changeEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void syncFont();
    void updateMetrics();

    qreal m_dpr = 1.0;
    QString m_sample_width_text;
    bool m_modified     = false;
    bool m_syncing_font = false;
};

QFont scaledCaptionFont(const QWidget *widget,
                        qreal dpr,
                        qreal delta_pt,
                        qreal min_pt,
                        qreal pixel_ratio);

inline qreal clampDpr(qreal dpr)
{
    return dpr > 0.0 ? dpr : 1.0;
}

inline int dp(int px, qreal dpr)
{
    if (px == 0) {
        return 0;
    }
    const int sign = px < 0 ? -1 : 1;
    return sign * qMax(1, qRound(qAbs(px) * clampDpr(dpr)));
}

inline qreal dpf(qreal px, qreal dpr)
{
    return px * clampDpr(dpr);
}

inline bool isWin11OrLater()
{
    return QOperatingSystemVersionBase::current()
           >= QOperatingSystemVersion::Windows11;
}

// Mirrors Seer Host MaterialStyle: square on Windows 10, 4 px controls and
// 8 px surfaces/containers scaled by DPR on Windows 11+.
inline qreal surfaceRadius(qreal dpr, bool container = false)
{
    if (!isWin11OrLater()) {
        return 0.0;
    }
    return (container ? 8.0 : 4.0) * clampDpr(dpr);
}

inline bool isDarkPalette(const QPalette &pal, int theme_hint = -1)
{
    const QColor win = pal.color(QPalette::Window);
    if (win.isValid()) {
        return win.lightness() < 128;
    }
    return theme_hint == 1;
}

inline QColor accentColor(const QPalette &pal)
{
    const QColor hl = pal.color(QPalette::Highlight);
    if (hl.isValid() && hl.alpha() > 0) {
        return hl;
    }
    return pal.color(QPalette::Accent);
}

inline QColor blendColors(const QColor &a, const QColor &b, qreal t)
{
    const qreal k = qBound(0.0, t, 1.0);
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * k,
                            a.greenF() + (b.greenF() - a.greenF()) * k,
                            a.blueF() + (b.blueF() - a.blueF()) * k,
                            a.alphaF() + (b.alphaF() - a.alphaF()) * k);
}

inline QColor mutedTextColor(const QPalette &pal)
{
    const QColor win = pal.color(QPalette::Window);
    const QColor mid = pal.color(QPalette::Mid);
    const QColor fg  = pal.color(QPalette::WindowText);
    if (mid.isValid() && qAbs(mid.lightness() - win.lightness()) >= 60) {
        return mid;
    }
    return blendColors(fg, win, 0.42);
}

inline QColor frameBorderColor(const QPalette &pal)
{
    const bool dark = isDarkPalette(pal);
    QColor c        = pal.color(QPalette::WindowText);
    c.setAlpha(dark ? 36 : 30);
    return c;
}

}  // namespace fontui
