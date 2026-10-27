#pragma once

#include "AppSettings.h"
#include <QObject>

class ThemeManager : public QObject {
    Q_OBJECT

public:
    static ThemeManager& instance();

    void applyTheme(AppTheme theme);
    [[nodiscard]] bool isDark() const noexcept { return m_isDark; }

signals:
    void themeChanged(bool isDark);

private:
    ThemeManager() = default;
    bool m_isDark{false};
};
