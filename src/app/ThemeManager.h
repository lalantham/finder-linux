#pragma once

#include "AppSettings.h"
#include <QObject>
#include <QColor>

class ThemeManager : public QObject {
    Q_OBJECT

public:
    static ThemeManager& instance();

    void applyTheme(AppTheme theme);
    [[nodiscard]] bool isDark() const noexcept { return m_isDark; }
    [[nodiscard]] QColor accentColor() const noexcept { return m_accentColor; }
    [[nodiscard]] QString accentColorHex() const noexcept { return m_accentColorHex; }

signals:
    void themeChanged(bool isDark);

private:
    ThemeManager();
    static QColor detectSystemAccentColor();

    bool m_isDark{false};
    QColor m_accentColor;
    QString m_accentColorHex;
};
