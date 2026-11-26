#include "fontshortcuts.h"

#include <QDebug>
#include <QKeyCombination>
#include <QSettings>

#define qprintt qDebug() << "[FontViewer]"

namespace {
constexpr auto g_shortcuts_group = "Shortcuts";

struct ShortcutEntry {
    FontShortcutAction action;
    const char *ini_key;
    const char *title;
    const char *default_key;
    bool customizable;
};

// Order must match FontShortcutAction: the table index is the action index.
constexpr ShortcutEntry g_entries[kFontShortcutActionCount] = {
    {FontShortcutAction::ToggleCollapse, "toggle_collapse",
     "Collapse / Expand Header", "Ctrl+B", true},
    {FontShortcutAction::CopyImage, "copy_image", "Copy as Image",
     "Ctrl+Shift+C", true},
    {FontShortcutAction::SaveSvg, "save_svg", "Save as SVG", "Ctrl+S", true},
    {FontShortcutAction::ToggleInvert, "toggle_invert", "Invert Preview",
     "Ctrl+Shift+I", true},
    {FontShortcutAction::ToggleWaterfall, "toggle_waterfall",
     "Toggle Waterfall", "Ctrl+Shift+W", true},
    {FontShortcutAction::ToggleOutlinePoints, "toggle_outline_points",
     "Show Outline Points", "Ctrl+Shift+O", true},
    {FontShortcutAction::ResetOpenType, "reset_opentype",
     "Reset OpenType Features", "Ctrl+Shift+R", true},
    {FontShortcutAction::ZoomIn, "zoom_in", "Zoom In", "Ctrl+=", false},
    {FontShortcutAction::ZoomOut, "zoom_out", "Zoom Out", "Ctrl+-", false},
    {FontShortcutAction::TabText, "tab_text", "Text Tab", "Ctrl+1", true},
    {FontShortcutAction::TabSample, "tab_sample", "Sample Tab", "Ctrl+2", true},
    {FontShortcutAction::TabCharacters, "tab_characters", "Characters Tab",
     "Ctrl+3", true},
    {FontShortcutAction::TabGlyphInspector, "tab_glyph_inspector",
     "Glyph Inspector Tab", "Ctrl+4", true},
};

bool isValidAction(FontShortcutAction action)
{
    const int idx = static_cast<int>(action);
    return idx >= 0 && idx < kFontShortcutActionCount;
}

const ShortcutEntry &entryAt(FontShortcutAction action)
{
    return g_entries[static_cast<int>(action)];
}

bool isDisallowedKey(Qt::Key key)
{
    switch (key) {
    case 0:
    case Qt::Key_unknown:
    case Qt::Key_Control:
    case Qt::Key_Shift:
    case Qt::Key_Alt:
    case Qt::Key_Meta:
    case Qt::Key_AltGr:
    case Qt::Key_Space:
    case Qt::Key_Escape:
        return true;
    default:
        return false;
    }
}

bool isValidSequence(const QKeySequence &seq)
{
    if (seq.isEmpty()) {
        return false;
    }
    for (int i = 0; i < seq.count(); ++i) {
        if (isDisallowedKey(seq[i].key())) {
            return false;
        }
    }
    return true;
}
}  // namespace

FontShortcutMap::FontShortcutMap()
{
    for (int i = 0; i < kFontShortcutActionCount; ++i) {
        m_sequences[i] = defaultSequence(g_entries[i].action);
    }
}

void FontShortcutMap::load(QSettings &ini)
{
    bool modified = false;
    ini.beginGroup(QLatin1String(g_shortcuts_group));

    for (int i = 0; i < kFontShortcutActionCount; ++i) {
        const ShortcutEntry &entry = g_entries[i];
        const QString key          = QLatin1String(entry.ini_key);

        if (!entry.customizable) {
            if (ini.contains(key)) {
                ini.remove(key);
                modified = true;
            }
            m_sequences[i] = defaultSequence(entry.action);
            continue;
        }

        const QVariant raw_var     = ini.value(key);
        const QString raw
            = (raw_var.metaType().id() == QMetaType::QStringList
                   ? raw_var.toStringList().join(QLatin1Char(','))
                   : raw_var.toString())
                  .trimmed();

        if (!ini.contains(key) || raw.isEmpty()) {
            ini.setValue(key, QLatin1String(entry.default_key));
            m_sequences[i] = defaultSequence(entry.action);
            modified       = true;
            continue;
        }

        if (raw.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0) {
            m_sequences[i] = QKeySequence();
            continue;
        }

        QKeySequence parsed
            = QKeySequence::fromString(raw, QKeySequence::PortableText);
        if (!isValidSequence(parsed)) {
            parsed = QKeySequence::fromString(raw, QKeySequence::NativeText);
        }
        if (isValidSequence(parsed)) {
            bool collides_with_fixed = false;
            for (const ShortcutEntry &fixed : g_entries) {
                if (!fixed.customizable
                    && parsed == defaultSequence(fixed.action)) {
                    collides_with_fixed = true;
                    break;
                }
            }
            if (collides_with_fixed) {
                m_sequences[i] = defaultSequence(entry.action);
                qprintt << "Shortcut"
                        << parsed.toString(QKeySequence::PortableText)
                        << "for" << entry.ini_key
                        << "conflicts with built-in zoom, falling back to"
                        << m_sequences[i].toString(QKeySequence::PortableText);
            }
            else {
                m_sequences[i] = parsed;
            }
        }
        else {
            m_sequences[i] = defaultSequence(entry.action);
            qprintt << "Invalid shortcut for" << entry.ini_key << "=" << raw
                    << "falling back to"
                    << m_sequences[i].toString(QKeySequence::PortableText);
        }
    }

    ini.endGroup();

    if (modified) {
        ini.sync();
    }

    for (int i = 0; i < kFontShortcutActionCount; ++i) {
        if (m_sequences[i].isEmpty()) {
            continue;
        }
        for (int j = i + 1; j < kFontShortcutActionCount; ++j) {
            if (!m_sequences[j].isEmpty() && m_sequences[i] == m_sequences[j]) {
                qprintt << "Duplicate shortcut"
                        << m_sequences[i].toString(QKeySequence::PortableText)
                        << "for" << g_entries[i].ini_key << "and"
                        << g_entries[j].ini_key;
            }
        }
    }
}

QKeySequence FontShortcutMap::sequence(FontShortcutAction action) const
{
    if (!isValidAction(action)) {
        return {};
    }
    return m_sequences[static_cast<int>(action)];
}

QString FontShortcutMap::text(FontShortcutAction action) const
{
    if (!isValidAction(action)) {
        return {};
    }
    return m_sequences[static_cast<int>(action)].toString(
        QKeySequence::NativeText);
}

QString FontShortcutMap::iniKey(FontShortcutAction action)
{
    if (!isCustomizable(action)) {
        return {};
    }
    return QStringLiteral("%1/%2").arg(QLatin1String(g_shortcuts_group),
                                       QLatin1String(entryAt(action).ini_key));
}

bool FontShortcutMap::isCustomizable(FontShortcutAction action)
{
    return isValidAction(action) && entryAt(action).customizable;
}

QString FontShortcutMap::title(FontShortcutAction action)
{
    if (!isValidAction(action)) {
        return {};
    }
    return QLatin1String(entryAt(action).title);
}

QKeySequence FontShortcutMap::defaultSequence(FontShortcutAction action)
{
    if (!isValidAction(action)) {
        return {};
    }
    return QKeySequence(QLatin1String(entryAt(action).default_key),
                        QKeySequence::PortableText);
}
