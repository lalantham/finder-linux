#pragma once

#include "AppSettings.h"
#include <QObject>
#include <QColor>

struct ThemeColors {
    bool isDark{false};
    QColor window;
    QColor windowAlt;
    QColor view;
    QColor viewAlt;
    QColor text;
    QColor subText;
    QColor button;
    QColor buttonText;
    QColor border;
    QColor accent;
    QColor selection;
    QColor selectionText;
};

class ThemeManager : public QObject {
    Q_OBJECT

public:
    static ThemeManager& instance();

    void applyTheme(AppTheme theme);
    [[nodiscard]] bool isDark() const noexcept { return m_colors.isDark; }
    [[nodiscard]] QColor accentColor() const noexcept { return m_colors.accent; }
    [[nodiscard]] QString accentColorHex() const noexcept { return m_colors.accent.name(); }
    [[nodiscard]] QColor windowColor() const noexcept { return m_colors.window; }
    [[nodiscard]] QColor viewColor() const noexcept { return m_colors.view; }
    [[nodiscard]] QColor textColor() const noexcept { return m_colors.text; }
    [[nodiscard]] QColor subTextColor() const noexcept { return m_colors.subText; }
    [[nodiscard]] const ThemeColors& colors() const noexcept { return m_colors; }

signals:
    void themeChanged(bool isDark);

private:
    ThemeManager();
    ThemeColors detectColors(AppTheme theme);
    static bool parseKdeGlobals(ThemeColors& outColors);

    ThemeColors m_colors;
};
