#pragma once

#include <QColor>
#include <QIcon>
#include <QOperatingSystemVersion>
#include <QPalette>
#include <QPushButton>
#include <QtGlobal>

class QComboBox;
class QWidget;

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
    void paintEvent(QPaintEvent *event) override;

private:
    qreal m_dpr = 1.0;
};

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
