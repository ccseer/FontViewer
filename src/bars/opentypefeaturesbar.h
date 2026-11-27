#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>
#include <QWidget>

#include "engine/fontinspectorengine.h"

namespace fontui {
class FlowSeparator;
}

class QLabel;
class QPushButton;
class QScrollArea;
class QHBoxLayout;
class QVBoxLayout;

class OpenTypeFeaturesBar : public QWidget {
    Q_OBJECT

public:
    explicit OpenTypeFeaturesBar(QWidget *parent = nullptr);

    static bool isLigatureFeatureTag(const QByteArray &tag);

    void setDpr(qreal dpr);
    void rebuild(const QVector<FontFeatureInfo> &features);
    void setLetterSpacingActive(bool active);
    void setFeatureEnabled(const QByteArray &tag, bool enabled);
    bool isFeatureEnabled(const QByteArray &tag) const;
    void resetOverrides();
    void applyPalette();
    void setResetShortcutHint(const QString &hint);

    bool hasFeatures() const
    {
        return !m_controls.isEmpty();
    }
    bool hasOverrides() const
    {
        return !m_overrides.isEmpty();
    }
    int overrideCount() const
    {
        return m_overrides.size();
    }
    const QHash<QByteArray, bool> &overrides() const
    {
        return m_overrides;
    }
    QVector<QPushButton *> pillButtons() const;

    Q_SIGNAL void sigFeaturesChanged();
    Q_SIGNAL void sigInteractionClearFocus();

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    struct FeaturePillControl {
        QByteArray tag;
        bool default_on  = false;
        bool is_ligature = false;
        QString tooltip;
        QPushButton *button = nullptr;
    };

    void applyToggleWithMutex(const QByteArray &tag, bool enabled);
    void updatePillsState();
    void updateLayoutMetrics();
    void updateScrollAreaHeight();

    QHash<QByteArray, bool> m_overrides;
    QVector<FeaturePillControl> m_controls;
    QVector<fontui::FlowSeparator *> m_separators;
    QLabel *m_note                  = nullptr;
    QPushButton *m_reset            = nullptr;
    QScrollArea *m_scroll_area      = nullptr;
    QWidget *m_pills_host           = nullptr;
    QVBoxLayout *m_root_layout      = nullptr;
    QHBoxLayout *m_body_layout      = nullptr;
    qreal m_dpr                     = 1.0;
    QString m_reset_hint;
    bool m_spacing_active           = false;
    bool m_updating_scroll_height   = false;
};
