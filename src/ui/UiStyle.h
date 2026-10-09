#pragma once

#include <QColor>
#include <QString>

class QApplication;
class QPalette;

namespace femgui::UiStyle {

enum class Theme {
    LiquidGlassDark,
    LiquidGlassLight,
    ClassicSystem
};

void apply(QApplication &app);
void setTheme(QApplication &app, Theme theme);
Theme currentTheme();
QString themeName(Theme theme);
QPalette paletteForTheme(Theme theme);

bool isDark(const QPalette &palette);
QColor viewportBackground(const QPalette &palette);
QColor viewportBackgroundAccent(const QPalette &palette);
QColor meshColor(const QPalette &palette);
QColor edgeColor(const QPalette &palette);
QColor loadColor(const QPalette &palette);
QColor selectionColor(const QPalette &palette);

// Convenience accessors based on active theme
QColor viewportBackground();
QColor viewportBackgroundAccent();
QColor meshColor();
QColor edgeColor();
QColor loadColor();
QColor selectionColor();

} // namespace femgui::UiStyle
