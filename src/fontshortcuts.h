#pragma once

#include <QKeySequence>
#include <QString>

class QSettings;

enum class FontShortcutAction {
    ToggleCollapse = 0,
    CopyImage,
    SaveSvg,
    ToggleInvert,
    ToggleWaterfall,
    ToggleOutlinePoints,
    ResetOpenType,
    ZoomIn,
    ZoomOut,
    TabText,
    TabSample,
    TabCharacters,
    TabGlyphInspector,
    Count,
};

constexpr int kFontShortcutActionCount = static_cast<int>(FontShortcutAction::Count);

class FontShortcutMap {
public:
    FontShortcutMap();

    // Reads every action under [Shortcuts], writing defaults when a key is
    // missing or empty so fontviewer.ini documents every editable shortcut.
    // The literal "none" unbinds an action.
    void load(QSettings &ini);

    QKeySequence sequence(FontShortcutAction action) const;
    QString text(FontShortcutAction action) const;

    static QString iniKey(FontShortcutAction action);
    static bool isCustomizable(FontShortcutAction action);
    static QString title(FontShortcutAction action);
    static QKeySequence defaultSequence(FontShortcutAction action);

private:
    QKeySequence m_sequences[kFontShortcutActionCount];
};
