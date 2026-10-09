#include "ui/UiStyle.h"

#include <QApplication>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>

namespace femgui::UiStyle {

static Theme s_currentTheme = Theme::LiquidGlassDark;

static const char kSettingsGroup[] = "ui";
static const char kSettingsThemeKey[] = "theme";

QString themeName(Theme theme)
{
    switch (theme) {
    case Theme::LiquidGlassDark:
        return QStringLiteral("Liquid Glass Dark");
    case Theme::LiquidGlassLight:
        return QStringLiteral("Liquid Glass Light");
    case Theme::ClassicSystem:
        return QStringLiteral("Classic System");
    }
    return QStringLiteral("Liquid Glass Dark");
}

static QString themeToString(Theme theme)
{
    switch (theme) {
    case Theme::LiquidGlassDark:
        return QStringLiteral("liquid_glass_dark");
    case Theme::LiquidGlassLight:
        return QStringLiteral("liquid_glass_light");
    case Theme::ClassicSystem:
        return QStringLiteral("classic_system");
    }
    return QStringLiteral("liquid_glass_dark");
}

static Theme stringToTheme(const QString &str)
{
    if (str == QLatin1String("liquid_glass_light")) {
        return Theme::LiquidGlassLight;
    } else if (str == QLatin1String("classic_system")) {
        return Theme::ClassicSystem;
    }
    return Theme::LiquidGlassDark;
}

QPalette paletteForTheme(Theme theme)
{
    QPalette pal;
    switch (theme) {
    case Theme::LiquidGlassDark: {
        // Deep obsidian / midnight canvas with vibrant iOS accents
        const QColor window(14, 20, 32);           // #0E1420
        const QColor windowText(242, 245, 249);     // #F2F5F9
        const QColor base(19, 27, 43);              // #131B2B
        const QColor alternateBase(25, 35, 55);     // #192337
        const QColor text(240, 244, 248);           // #F0F4F8
        const QColor button(27, 38, 59);            // #1B263B
        const QColor buttonText(255, 255, 255);
        const QColor highlight(10, 132, 255);       // #0A84FF (Apple iOS Electric Blue)
        const QColor highlightedText(255, 255, 255);
        const QColor mid(42, 55, 79);               // #2A374F (Specular border base)
        const QColor midlight(51, 67, 96);
        const QColor dark(7, 10, 16);
        const QColor shadow(4, 6, 10);
        const QColor placeholder(142, 156, 174);    // #8E9CAE

        pal.setColor(QPalette::Window, window);
        pal.setColor(QPalette::WindowText, windowText);
        pal.setColor(QPalette::Base, base);
        pal.setColor(QPalette::AlternateBase, alternateBase);
        pal.setColor(QPalette::Text, text);
        pal.setColor(QPalette::BrightText, Qt::white);
        pal.setColor(QPalette::Button, button);
        pal.setColor(QPalette::ButtonText, buttonText);
        pal.setColor(QPalette::Highlight, highlight);
        pal.setColor(QPalette::HighlightedText, highlightedText);
        pal.setColor(QPalette::Mid, mid);
        pal.setColor(QPalette::Midlight, midlight);
        pal.setColor(QPalette::Dark, dark);
        pal.setColor(QPalette::Shadow, shadow);
        pal.setColor(QPalette::PlaceholderText, placeholder);
        pal.setColor(QPalette::ToolTipBase, QColor(26, 36, 54));
        pal.setColor(QPalette::ToolTipText, Qt::white);
        break;
    }
    case Theme::LiquidGlassLight: {
        // Alpine frost / translucent crystalline ice with Apple system blue
        const QColor window(237, 243, 249);         // #EDF3F9
        const QColor windowText(28, 28, 30);        // #1C1C1E
        const QColor base(255, 255, 255);
        const QColor alternateBase(242, 246, 251);  // #F2F6FB
        const QColor text(28, 28, 30);
        const QColor button(229, 238, 247);         // #E5EEF7
        const QColor buttonText(28, 28, 30);
        const QColor highlight(0, 122, 255);        // #007AFF (Apple System Blue)
        const QColor highlightedText(255, 255, 255);
        const QColor mid(209, 220, 231);            // #D1DCE7
        const QColor midlight(228, 236, 244);
        const QColor dark(176, 191, 207);
        const QColor shadow(142, 156, 174);
        const QColor placeholder(142, 142, 147);    // #8E8E93

        pal.setColor(QPalette::Window, window);
        pal.setColor(QPalette::WindowText, windowText);
        pal.setColor(QPalette::Base, base);
        pal.setColor(QPalette::AlternateBase, alternateBase);
        pal.setColor(QPalette::Text, text);
        pal.setColor(QPalette::BrightText, Qt::black);
        pal.setColor(QPalette::Button, button);
        pal.setColor(QPalette::ButtonText, buttonText);
        pal.setColor(QPalette::Highlight, highlight);
        pal.setColor(QPalette::HighlightedText, highlightedText);
        pal.setColor(QPalette::Mid, mid);
        pal.setColor(QPalette::Midlight, midlight);
        pal.setColor(QPalette::Dark, dark);
        pal.setColor(QPalette::Shadow, shadow);
        pal.setColor(QPalette::PlaceholderText, placeholder);
        pal.setColor(QPalette::ToolTipBase, Qt::white);
        pal.setColor(QPalette::ToolTipText, windowText);
        break;
    }
    case Theme::ClassicSystem:
        pal = QApplication::style()->standardPalette();
        break;
    }
    return pal;
}

static QString stylesheetForTheme(Theme theme)
{
    switch (theme) {
    case Theme::LiquidGlassDark:
        return QStringLiteral(R"(
            /* Global Application Typography and Canvas */
            QWidget {
                font-family: "Segoe UI Variable Text", "SF Pro Text", "Segoe UI", -apple-system, sans-serif;
                font-size: 13px;
                color: #F0F4F8;
            }

            QMainWindow, QDialog {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 #0A0E17, stop:0.45 #0E1523, stop:1 #131B2B);
            }

            /* Specular Rim Glass Floating ToolBar */
            QToolBar {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 rgba(255, 255, 255, 0.08),
                    stop:0.15 rgba(255, 255, 255, 0.04),
                    stop:1 rgba(255, 255, 255, 0.02));
                border: 1px solid rgba(255, 255, 255, 0.12);
                border-top: 1px solid rgba(255, 255, 255, 0.22);
                border-radius: 8px;
                padding: 4px;
                spacing: 4px;
                margin: 3px 6px;
            }

            /* Menu and Menu Bar */
            QMenuBar {
                background: transparent;
                padding: 2px 6px;
                color: #E2E8F0;
            }
            QMenuBar::item {
                background: transparent;
                padding: 4px 10px;
                border-radius: 6px;
            }
            QMenuBar::item:selected {
                background: rgba(255, 255, 255, 0.10);
                color: #FFFFFF;
            }
            QMenu {
                background-color: rgba(16, 23, 37, 0.95);
                border: 1px solid rgba(255, 255, 255, 0.15);
                border-top: 1px solid rgba(255, 255, 255, 0.28);
                border-radius: 8px;
                padding: 6px;
            }
            QMenu::item {
                padding: 6px 24px 6px 12px;
                border-radius: 6px;
                color: #F0F4F8;
            }
            QMenu::item:selected {
                background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                    stop:0 #0A84FF, stop:1 #0066D6);
                color: #FFFFFF;
            }
            QMenu::separator {
                height: 1px;
                background: rgba(255, 255, 255, 0.10);
                margin: 4px 8px;
            }

            /* Liquid Glass Buttons & Controls */
            QPushButton, QToolBar QToolButton {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 rgba(255, 255, 255, 0.10),
                    stop:0.4 rgba(255, 255, 255, 0.05),
                    stop:1 rgba(255, 255, 255, 0.02));
                border: 1px solid rgba(255, 255, 255, 0.13);
                border-top: 1px solid rgba(255, 255, 255, 0.28);
                border-radius: 8px;
                padding: 6px 16px;
                min-height: 28px;
                color: #F0F4F8;
                font-weight: 500;
            }
            QPushButton:hover, QToolBar QToolButton:hover {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 rgba(255, 255, 255, 0.18),
                    stop:0.4 rgba(255, 255, 255, 0.09),
                    stop:1 rgba(255, 255, 255, 0.04));
                border-color: rgba(255, 255, 255, 0.28);
            }
            QPushButton:pressed, QToolBar QToolButton:pressed {
                background: rgba(0, 0, 0, 0.30);
                border-color: rgba(255, 255, 255, 0.08);
            }
            QPushButton:disabled, QToolBar QToolButton:disabled {
                background: rgba(255, 255, 255, 0.02);
                border-color: rgba(255, 255, 255, 0.05);
                color: rgba(255, 255, 255, 0.25);
            }

            /* Primary Action Button - iOS Vibrant Electric Blue */
            QPushButton[primary="true"], QToolButton[primary="true"] {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 #0A84FF, stop:1 #0066D6);
                border: 1px solid #0A84FF;
                border-top: 1px solid rgba(255, 255, 255, 0.45);
                color: #FFFFFF;
                font-weight: 700;
                font-size: 13px;
                border-radius: 8px;
                min-height: 32px;
                padding: 7px 20px;
            }
            QPushButton[primary="true"]:hover, QToolButton[primary="true"]:hover {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 #409CFF, stop:1 #0A84FF);
                border-color: #5AC8FA;
            }
            QPushButton[primary="true"]:pressed, QToolButton[primary="true"]:pressed {
                background: #0051B3;
                border-color: #00408F;
            }

            /* Destructive Action Button - iOS Coral Red */
            QPushButton[destructive="true"] {
                background: rgba(255, 69, 58, 0.18);
                border: 1px solid rgba(255, 69, 58, 0.45);
                border-top: 1px solid rgba(255, 120, 110, 0.60);
                color: #FF6961;
                font-weight: 600;
                border-radius: 8px;
                min-height: 28px;
                padding: 6px 16px;
            }
            QPushButton[destructive="true"]:hover {
                background: rgba(255, 69, 58, 0.28);
                color: #FF8079;
            }
            QPushButton[destructive="true"]:pressed {
                background: rgba(255, 69, 58, 0.40);
            }

            /* Fresnel Refraction Glass Cards */
            QWidget[role="card"], QLabel[role="card"] {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 rgba(255, 255, 255, 0.08),
                    stop:0.18 rgba(255, 255, 255, 0.04),
                    stop:1 rgba(255, 255, 255, 0.015));
                border: 1px solid rgba(255, 255, 255, 0.12);
                border-top: 1px solid rgba(255, 255, 255, 0.24);
                border-radius: 8px;
                padding: 10px 14px;
            }

            /* Typography Hierarchy */
            QLabel[role="pageTitle"] {
                font-size: 16px;
                font-weight: 700;
                letter-spacing: -0.2px;
                color: #FFFFFF;
            }
            QLabel[role="pageIntro"], QLabel[role="subtle"] {
                color: #8E9CAE;
                font-size: 12px;
            }

            /* iOS Translucent Status Badges */
            QLabel[state="success"] {
                background: rgba(48, 209, 88, 0.18);
                border: 1px solid rgba(48, 209, 88, 0.40);
                border-radius: 8px;
                padding: 6px 14px;
                min-height: 20px;
                color: #30D158;
                font-weight: 600;
            }
            QLabel[state="warning"] {
                background: rgba(255, 159, 10, 0.18);
                border: 1px solid rgba(255, 159, 10, 0.40);
                border-radius: 8px;
                padding: 6px 14px;
                min-height: 20px;
                color: #FF9F0A;
                font-weight: 600;
            }
            QLabel[state="error"] {
                background: rgba(255, 69, 58, 0.18);
                border: 1px solid rgba(255, 69, 58, 0.40);
                border-radius: 8px;
                padding: 6px 14px;
                min-height: 20px;
                color: #FF453A;
                font-weight: 600;
            }
            QLabel[validation="true"] {
                background: rgba(255, 69, 58, 0.12);
                border: 1px solid rgba(255, 69, 58, 0.25);
                border-left: 4px solid #FF453A;
                border-radius: 8px;
                padding: 8px 14px;
                color: #FFB3AE;
            }
        )") + QStringLiteral(R"(
            /* Viewport Glass HUD Overlay */
            QLabel[role="hud"] {
                background: rgba(14, 20, 32, 0.82);
                border: 1px solid rgba(255, 255, 255, 0.16);
                border-top: 1px solid rgba(255, 255, 255, 0.30);
                border-radius: 8px;
                padding: 4px 12px;
                color: #F0F4F8;
            }

            /* Workflow Step Tree */
            QTreeWidget#workflowTree {
                border: 0;
                background: transparent;
                outline: 0;
                padding: 4px;
            }
            QTreeWidget#workflowTree::item {
                min-height: 30px;
                padding: 4px 6px;
                border: none;
                background: transparent;
                color: #A0B0C4;
            }
            QTreeWidget#workflowTree::item:hover {
                background: transparent;
                color: #FFFFFF;
            }
            QTreeWidget#workflowTree::item:selected {
                background: transparent;
                border: none;
                color: #FFFFFF;
            }
            QTreeWidget#workflowTree::branch {
                background: transparent;
            }
            QTreeWidget#workflowTree::branch:selected {
                background: transparent;
            }
            QTreeWidget#workflowTree::branch:hover {
                background: transparent;
            }

            /* Segmented Capsule Tabs */
            QTabWidget::pane {
                border: 1px solid rgba(255, 255, 255, 0.10);
                border-radius: 8px;
                background: transparent;
            }
            QTabBar::tab {
                background: rgba(255, 255, 255, 0.04);
                border: 1px solid rgba(255, 255, 255, 0.08);
                border-radius: 8px;
                padding: 6px 16px;
                margin: 2px 4px;
                color: #8E9CAE;
                font-weight: 500;
            }
            QTabBar::tab:hover {
                background: rgba(255, 255, 255, 0.09);
                color: #E2E8F0;
            }
            QTabBar::tab:selected {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 rgba(255, 255, 255, 0.18),
                    stop:1 rgba(255, 255, 255, 0.07));
                border: 1px solid rgba(255, 255, 255, 0.28);
                color: #FFFFFF;
                font-weight: 600;
            }

            /* Group Boxes */
            QGroupBox {
                font-weight: 600;
                margin-top: 14px;
                padding-top: 10px;
                border: 1px solid rgba(255, 255, 255, 0.10);
                border-radius: 8px;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 12px;
                padding: 0 6px;
                color: #8E9CAE;
            }

            /* Recessed Glass Wells for Inputs */
            QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
                background: rgba(8, 12, 20, 0.55);
                border: 1px solid rgba(255, 255, 255, 0.12);
                border-radius: 8px;
                padding: 5px 8px;
                color: #F0F4F8;
                min-height: 22px;
            }
            QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
                border: 1.5px solid #0A84FF;
                background: rgba(12, 18, 30, 0.75);
            }
            QLineEdit[readOnly="true"] {
                background: rgba(255, 255, 255, 0.03);
                border-color: rgba(255, 255, 255, 0.06);
                color: #8E9CAE;
            }
            QComboBox::drop-down {
                subcontrol-origin: padding;
                subcontrol-position: top right;
                width: 22px;
                border: 0;
            }
            QComboBox QAbstractItemView {
                background-color: rgba(16, 23, 37, 0.96);
                border: 1px solid rgba(255, 255, 255, 0.15);
                border-radius: 8px;
                selection-background-color: rgba(10, 132, 255, 0.40);
                color: #F0F4F8;
                padding: 4px;
            }

            /* Glass Data Tables */
            QTableWidget {
                gridline-color: rgba(255, 255, 255, 0.08);
                border: 1px solid rgba(255, 255, 255, 0.10);
                border-radius: 8px;
                background: rgba(12, 18, 29, 0.50);
                alternate-background-color: rgba(255, 255, 255, 0.02);
            }
            QHeaderView::section {
                padding: 6px 8px;
                font-weight: 600;
                color: #8E9CAE;
                border: 0;
                border-bottom: 1px solid rgba(255, 255, 255, 0.12);
                background: rgba(16, 23, 37, 0.70);
            }

            /* Sleek Apple Minimalist ScrollBars */
            QScrollBar:vertical {
                width: 8px;
                background: transparent;
                margin: 0;
            }
            QScrollBar::handle:vertical {
                background: rgba(255, 255, 255, 0.18);
                min-height: 24px;
                border-radius: 4px;
            }
            QScrollBar::handle:vertical:hover {
                background: rgba(255, 255, 255, 0.32);
            }
            QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,
            QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
                background: transparent;
                height: 0;
            }
            QScrollBar:horizontal {
                height: 8px;
                background: transparent;
                margin: 0;
            }
            QScrollBar::handle:horizontal {
                background: rgba(255, 255, 255, 0.18);
                min-width: 24px;
                border-radius: 4px;
            }
            QScrollBar::handle:horizontal:hover {
                background: rgba(255, 255, 255, 0.32);
            }
            QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal,
            QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
                background: transparent;
                width: 0;
            }

            /* Progress Bar */
            QProgressBar {
                border: 1px solid rgba(255, 255, 255, 0.12);
                border-radius: 6px;
                text-align: center;
                background: rgba(8, 12, 20, 0.50);
                min-height: 12px;
                max-height: 14px;
            }
            QProgressBar::chunk {
                background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                    stop:0 #0A84FF, stop:1 #5AC8FA);
                border-radius: 5px;
            }

            /* Status Bar */
            QStatusBar {
                background: transparent;
                border-top: 1px solid rgba(255, 255, 255, 0.08);
                padding: 2px 6px;
                color: #8E9CAE;
            }
            QSplitter::handle {
                background: rgba(255, 255, 255, 0.06);
            }
            QSplitter::handle:hover {
                background: #0A84FF;
            }

            /* Advanced VS Code Style Dock Islands */
            QDockWidget {
                font-weight: 600;
                color: #D6E2F0;
            }
            QDockWidget::title {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 rgba(255, 255, 255, 0.07), stop:1 rgba(255, 255, 255, 0.02));
                border: 1px solid rgba(255, 255, 255, 0.08);
                border-bottom: 1px solid rgba(255, 255, 255, 0.12);
                border-radius: 8px 8px 0 0;
                padding: 6px 12px;
                font-size: 12px;
                font-weight: 600;
                color: #C0D0E4;
                text-align: left;
            }
            QDockWidget::close-button, QDockWidget::float-button {
                border: 1px solid transparent;
                border-radius: 6px;
                background: transparent;
                padding: 2px;
                subcontrol-position: top right;
            }
            QDockWidget::close-button:hover, QDockWidget::float-button:hover {
                background: rgba(255, 255, 255, 0.12);
                border: 1px solid rgba(255, 255, 255, 0.20);
            }
            QMainWindow::separator {
                background: rgba(255, 255, 255, 0.06);
                width: 5px;
                height: 5px;
            }
            QMainWindow::separator:hover {
                background: #0A84FF;
            }

            /* Floating Translucent Viewport HUD Island */
            QWidget#viewportHud, QWidget#viewportHudSub {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 rgba(22, 30, 44, 0.92),
                    stop:1 rgba(14, 20, 32, 0.85));
                border: 1px solid rgba(255, 255, 255, 0.14);
                border-top: 1px solid rgba(255, 255, 255, 0.28);
                border-radius: 8px;
                margin: 4px 6px;
                padding: 2px 8px;
            }
            QWidget#viewportHud QToolButton, QWidget#viewportHudSub QToolButton {
                border-radius: 8px;
                padding: 4px 10px;
                min-height: 22px;
                font-size: 12px;
                font-weight: 600;
            }
            QWidget#viewportHud QComboBox, QWidget#viewportHudSub QComboBox {
                border-radius: 8px;
                padding: 2px 8px;
                min-height: 22px;
                font-size: 12px;
            }

            /* Viewport HUD Camera Segmented Control */
            QWidget#viewportHud QWidget#cameraSegmentedControl {
                background: rgba(8, 12, 20, 0.65);
                border: 1px solid rgba(255, 255, 255, 0.12);
                border-radius: 8px;
                padding: 1px;
            }
            QWidget#viewportHud QWidget#cameraSegmentedControl QToolButton {
                background: transparent;
                border: 0;
                border-radius: 6px;
                padding: 4px 12px;
                min-height: 20px;
                font-size: 12px;
                font-weight: 500;
                color: #A0B0C4;
            }
            QWidget#viewportHud QWidget#cameraSegmentedControl QToolButton:hover {
                background: rgba(255, 255, 255, 0.08);
                color: #FFFFFF;
            }
            QWidget#viewportHud QWidget#cameraSegmentedControl QToolButton:checked {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 rgba(10, 132, 255, 0.95), stop:1 rgba(0, 102, 214, 0.95));
                color: #FFFFFF;
                font-weight: 600;
            }
            QWidget#viewportHud QToolButton#fitButton {
                background: rgba(255, 255, 255, 0.08);
                border: 1px solid rgba(255, 255, 255, 0.14);
                border-radius: 8px;
                padding: 4px 12px;
                min-height: 22px;
                font-size: 12px;
                font-weight: 600;
                color: #F0F4F8;
            }
            QWidget#viewportHud QToolButton#fitButton:hover {
                background: rgba(255, 255, 255, 0.16);
                border-color: rgba(255, 255, 255, 0.25);
            }
            QWidget#viewportHud QToolButton#fitButton:pressed {
                background: rgba(0, 0, 0, 0.35);
            }
        )");

    case Theme::LiquidGlassLight:
        return QStringLiteral(R"(
            /* Global Application Typography and Canvas */
            QWidget {
                font-family: "Segoe UI Variable Text", "SF Pro Text", "Segoe UI", -apple-system, sans-serif;
                font-size: 13px;
                color: #1C1C1E;
            }

            QMainWindow, QDialog {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 #F6F9FD, stop:0.45 #EEF4FA, stop:1 #E5EFF8);
            }

            /* Specular Rim Glass Floating ToolBar */
            QToolBar {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 rgba(255, 255, 255, 0.95),
                    stop:0.2 rgba(255, 255, 255, 0.85),
                    stop:1 rgba(255, 255, 255, 0.70));
                border: 1px solid rgba(255, 255, 255, 0.95);
                border-bottom: 1px solid rgba(0, 0, 0, 0.08);
                border-radius: 8px;
                padding: 4px;
                spacing: 4px;
                margin: 3px 6px;
            }

            /* Menu and Menu Bar */
            QMenuBar {
                background: transparent;
                padding: 2px 6px;
                color: #1C1C1E;
            }
            QMenuBar::item {
                background: transparent;
                padding: 4px 10px;
                border-radius: 6px;
            }
            QMenuBar::item:selected {
                background: rgba(0, 0, 0, 0.06);
                color: #000000;
            }
            QMenu {
                background-color: rgba(255, 255, 255, 0.95);
                border: 1px solid rgba(0, 0, 0, 0.10);
                border-radius: 8px;
                padding: 6px;
            }
            QMenu::item {
                padding: 6px 24px 6px 12px;
                border-radius: 6px;
                color: #1C1C1E;
            }
            QMenu::item:selected {
                background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                    stop:0 #007AFF, stop:1 #0056B3);
                color: #FFFFFF;
            }
            QMenu::separator {
                height: 1px;
                background: rgba(0, 0, 0, 0.08);
                margin: 4px 8px;
            }

            /* Liquid Glass Buttons & Controls */
            QPushButton, QToolBar QToolButton {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 rgba(255, 255, 255, 0.95),
                    stop:0.4 rgba(255, 255, 255, 0.80),
                    stop:1 rgba(240, 246, 252, 0.70));
                border: 1px solid rgba(0, 0, 0, 0.10);
                border-top: 1px solid rgba(255, 255, 255, 0.98);
                border-radius: 8px;
                padding: 6px 16px;
                min-height: 28px;
                color: #1C1C1E;
                font-weight: 500;
            }
            QPushButton:hover, QToolBar QToolButton:hover {
                background: #FFFFFF;
                border-color: rgba(0, 0, 0, 0.18);
            }
            QPushButton:pressed, QToolBar QToolButton:pressed {
                background: rgba(0, 0, 0, 0.08);
                border-color: rgba(0, 0, 0, 0.12);
            }
            QPushButton:disabled, QToolBar QToolButton:disabled {
                background: rgba(255, 255, 255, 0.40);
                border-color: rgba(0, 0, 0, 0.05);
                color: rgba(0, 0, 0, 0.25);
            }

            /* Primary Action Button - Apple System Blue */
            QPushButton[primary="true"], QToolButton[primary="true"] {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 #007AFF, stop:1 #0056B3);
                border: 1px solid #0056B3;
                border-top: 1px solid rgba(255, 255, 255, 0.45);
                color: #FFFFFF;
                font-weight: 700;
                font-size: 13px;
                border-radius: 8px;
                min-height: 32px;
                padding: 7px 20px;
            }
            QPushButton[primary="true"]:hover, QToolButton[primary="true"]:hover {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 #3395FF, stop:1 #007AFF);
                border-color: #007AFF;
            }
            QPushButton[primary="true"]:pressed, QToolButton[primary="true"]:pressed {
                background: #004999;
            }

            /* Destructive Action Button */
            QPushButton[destructive="true"] {
                background: rgba(255, 59, 48, 0.12);
                border: 1px solid rgba(255, 59, 48, 0.35);
                color: #D70015;
                font-weight: 600;
                border-radius: 8px;
                min-height: 28px;
                padding: 6px 16px;
            }
            QPushButton[destructive="true"]:hover {
                background: rgba(255, 59, 48, 0.22);
                color: #C00010;
            }
            QPushButton[destructive="true"]:pressed {
                background: rgba(255, 59, 48, 0.35);
            }

            /* Fresnel Refraction Glass Cards */
            QWidget[role="card"], QLabel[role="card"] {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 rgba(255, 255, 255, 0.95),
                    stop:0.25 rgba(255, 255, 255, 0.85),
                    stop:1 rgba(255, 255, 255, 0.72));
                border: 1px solid rgba(255, 255, 255, 0.98);
                border-bottom: 1px solid rgba(0, 0, 0, 0.08);
                border-radius: 8px;
                padding: 10px 14px;
            }

            /* Typography Hierarchy */
            QLabel[role="pageTitle"] {
                font-size: 16px;
                font-weight: 700;
                letter-spacing: -0.2px;
                color: #1C1C1E;
            }
            QLabel[role="pageIntro"], QLabel[role="subtle"] {
                color: #8E8E93;
                font-size: 12px;
            }

            /* iOS Translucent Status Badges */
            QLabel[state="success"] {
                background: rgba(52, 199, 89, 0.15);
                border: 1px solid rgba(52, 199, 89, 0.35);
                border-radius: 8px;
                padding: 6px 14px;
                min-height: 20px;
                color: #248A3D;
                font-weight: 600;
            }
            QLabel[state="warning"] {
                background: rgba(255, 149, 0, 0.15);
                border: 1px solid rgba(255, 149, 0, 0.35);
                border-radius: 8px;
                padding: 6px 14px;
                min-height: 20px;
                color: #C97200;
                font-weight: 600;
            }
            QLabel[state="error"] {
                background: rgba(255, 59, 48, 0.15);
                border: 1px solid rgba(255, 59, 48, 0.35);
                border-radius: 8px;
                padding: 6px 14px;
                min-height: 20px;
                color: #D70015;
                font-weight: 600;
            }
            QLabel[validation="true"] {
                background: rgba(255, 59, 48, 0.08);
                border: 1px solid rgba(255, 59, 48, 0.20);
                border-left: 4px solid #D70015;
                border-radius: 8px;
                padding: 8px 14px;
                color: #A00010;
            }
        )") + QStringLiteral(R"(
            /* Viewport Glass HUD Overlay */
            QLabel[role="hud"] {
                background: rgba(255, 255, 255, 0.88);
                border: 1px solid rgba(255, 255, 255, 0.95);
                border-bottom: 1px solid rgba(0, 0, 0, 0.10);
                border-radius: 8px;
                padding: 4px 12px;
                color: #1C1C1E;
            }

            /* Workflow Step Tree */
            QTreeWidget#workflowTree {
                border: 0;
                background: transparent;
                outline: 0;
                padding: 4px;
            }
            QTreeWidget#workflowTree::item {
                min-height: 30px;
                padding: 4px 6px;
                border: none;
                background: transparent;
                color: #48484A;
            }
            QTreeWidget#workflowTree::item:hover {
                background: transparent;
                color: #1C1C1E;
            }
            QTreeWidget#workflowTree::item:selected {
                background: transparent;
                border: none;
                color: #007AFF;
            }
            QTreeWidget#workflowTree::branch {
                background: transparent;
            }
            QTreeWidget#workflowTree::branch:selected {
                background: transparent;
            }
            QTreeWidget#workflowTree::branch:hover {
                background: transparent;
            }

            /* Segmented Capsule Tabs */
            QTabWidget::pane {
                border: 1px solid rgba(0, 0, 0, 0.08);
                border-radius: 8px;
                background: transparent;
            }
            QTabBar::tab {
                background: rgba(0, 0, 0, 0.04);
                border: 1px solid rgba(0, 0, 0, 0.06);
                border-radius: 8px;
                padding: 6px 16px;
                margin: 2px 4px;
                color: #636366;
                font-weight: 500;
            }
            QTabBar::tab:hover {
                background: rgba(0, 0, 0, 0.08);
                color: #1C1C1E;
            }
            QTabBar::tab:selected {
                background: #FFFFFF;
                border: 1px solid rgba(0, 0, 0, 0.12);
                color: #1C1C1E;
                font-weight: 600;
            }

            /* Group Boxes */
            QGroupBox {
                font-weight: 600;
                margin-top: 14px;
                padding-top: 10px;
                border: 1px solid rgba(0, 0, 0, 0.10);
                border-radius: 8px;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 12px;
                padding: 0 6px;
                color: #636366;
            }

            /* Recessed Glass Wells for Inputs */
            QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
                background: rgba(255, 255, 255, 0.85);
                border: 1px solid rgba(0, 0, 0, 0.14);
                border-radius: 8px;
                padding: 5px 8px;
                color: #1C1C1E;
                min-height: 22px;
            }
            QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
                border: 1.5px solid #007AFF;
                background: #FFFFFF;
            }
            QLineEdit[readOnly="true"] {
                background: rgba(0, 0, 0, 0.03);
                border-color: rgba(0, 0, 0, 0.08);
                color: #8E8E93;
            }
            QComboBox::drop-down {
                subcontrol-origin: padding;
                subcontrol-position: top right;
                width: 22px;
                border: 0;
            }
            QComboBox QAbstractItemView {
                background-color: #FFFFFF;
                border: 1px solid rgba(0, 0, 0, 0.14);
                border-radius: 8px;
                selection-background-color: rgba(0, 122, 255, 0.20);
                color: #1C1C1E;
                padding: 4px;
            }

            /* Glass Data Tables */
            QTableWidget {
                gridline-color: rgba(0, 0, 0, 0.06);
                border: 1px solid rgba(0, 0, 0, 0.10);
                border-radius: 8px;
                background: rgba(255, 255, 255, 0.70);
                alternate-background-color: rgba(0, 0, 0, 0.02);
            }
            QHeaderView::section {
                padding: 6px 8px;
                font-weight: 600;
                color: #636366;
                border: 0;
                border-bottom: 1px solid rgba(0, 0, 0, 0.10);
                background: rgba(240, 245, 250, 0.80);
            }

            /* Sleek Apple Minimalist ScrollBars */
            QScrollBar:vertical {
                width: 8px;
                background: transparent;
                margin: 0;
            }
            QScrollBar::handle:vertical {
                background: rgba(0, 0, 0, 0.18);
                min-height: 24px;
                border-radius: 4px;
            }
            QScrollBar::handle:vertical:hover {
                background: rgba(0, 0, 0, 0.32);
            }
            QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,
            QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
                background: transparent;
                height: 0;
            }
            QScrollBar:horizontal {
                height: 8px;
                background: transparent;
                margin: 0;
            }
            QScrollBar::handle:horizontal {
                background: rgba(0, 0, 0, 0.18);
                min-width: 24px;
                border-radius: 4px;
            }
            QScrollBar::handle:horizontal:hover {
                background: rgba(0, 0, 0, 0.32);
            }
            QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal,
            QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
                background: transparent;
                width: 0;
            }

            /* Progress Bar */
            QProgressBar {
                border: 1px solid rgba(0, 0, 0, 0.12);
                border-radius: 6px;
                text-align: center;
                background: rgba(0, 0, 0, 0.05);
                min-height: 12px;
                max-height: 14px;
            }
            QProgressBar::chunk {
                background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                    stop:0 #007AFF, stop:1 #5AC8FA);
                border-radius: 5px;
            }

            /* Status Bar */
            QStatusBar {
                background: transparent;
                border-top: 1px solid rgba(0, 0, 0, 0.08);
                padding: 2px 6px;
                color: #8E8E93;
            }
            QSplitter::handle {
                background: rgba(0, 0, 0, 0.06);
            }
            QSplitter::handle:hover {
                background: #007AFF;
            }

            /* Advanced VS Code Style Dock Islands */
            QDockWidget {
                font-weight: 600;
                color: #1C1C1E;
            }
            QDockWidget::title {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 rgba(255, 255, 255, 0.95), stop:1 rgba(240, 246, 252, 0.85));
                border: 1px solid rgba(0, 0, 0, 0.08);
                border-bottom: 1px solid rgba(0, 0, 0, 0.12);
                border-radius: 8px 8px 0 0;
                padding: 6px 12px;
                font-size: 12px;
                font-weight: 600;
                color: #3A3A3C;
                text-align: left;
            }
            QDockWidget::close-button, QDockWidget::float-button {
                border: 1px solid transparent;
                border-radius: 6px;
                background: transparent;
                padding: 2px;
                subcontrol-position: top right;
            }
            QDockWidget::close-button:hover, QDockWidget::float-button:hover {
                background: rgba(0, 0, 0, 0.08);
                border: 1px solid rgba(0, 0, 0, 0.14);
            }
            QMainWindow::separator {
                background: rgba(0, 0, 0, 0.06);
                width: 5px;
                height: 5px;
            }
            QMainWindow::separator:hover {
                background: #007AFF;
            }

            /* Floating Translucent Viewport HUD Island */
            QWidget#viewportHud, QWidget#viewportHudSub {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                    stop:0 rgba(255, 255, 255, 0.95),
                    stop:1 rgba(240, 246, 252, 0.88));
                border: 1px solid rgba(0, 0, 0, 0.12);
                border-top: 1px solid rgba(255, 255, 255, 0.98);
                border-radius: 8px;
                margin: 4px 6px;
                padding: 2px 8px;
            }
            QWidget#viewportHud QToolButton, QWidget#viewportHudSub QToolButton {
                border-radius: 8px;
                padding: 4px 10px;
                min-height: 22px;
                font-size: 12px;
                font-weight: 600;
            }
            QWidget#viewportHud QComboBox, QWidget#viewportHudSub QComboBox {
                border-radius: 8px;
                padding: 2px 8px;
                min-height: 22px;
                font-size: 12px;
            }

            /* Viewport HUD Camera Segmented Control */
            QWidget#viewportHud QWidget#cameraSegmentedControl {
                background: rgba(0, 0, 0, 0.06);
                border: 1px solid rgba(0, 0, 0, 0.08);
                border-radius: 8px;
                padding: 1px;
            }
            QWidget#viewportHud QWidget#cameraSegmentedControl QToolButton {
                background: transparent;
                border: 0;
                border-radius: 6px;
                padding: 4px 12px;
                min-height: 20px;
                font-size: 12px;
                font-weight: 500;
                color: #636366;
            }
            QWidget#viewportHud QWidget#cameraSegmentedControl QToolButton:hover {
                background: rgba(0, 0, 0, 0.06);
                color: #1C1C1E;
            }
            QWidget#viewportHud QWidget#cameraSegmentedControl QToolButton:checked {
                background: #FFFFFF;
                border: 1px solid rgba(0, 0, 0, 0.10);
                color: #007AFF;
                font-weight: 600;
            }
            QWidget#viewportHud QToolButton#fitButton {
                background: rgba(0, 0, 0, 0.05);
                border: 1px solid rgba(0, 0, 0, 0.10);
                border-radius: 8px;
                padding: 4px 12px;
                min-height: 22px;
                font-size: 12px;
                font-weight: 600;
                color: #1C1C1E;
            }
            QWidget#viewportHud QToolButton#fitButton:hover {
                background: rgba(0, 0, 0, 0.09);
                border-color: rgba(0, 0, 0, 0.16);
            }
            QWidget#viewportHud QToolButton#fitButton:pressed {
                background: rgba(0, 0, 0, 0.15);
            }
        )");

    case Theme::ClassicSystem:
        return QStringLiteral(R"(
            QMainWindow, QDialog { background: palette(window); }
            QToolBar {
                border: 0;
                border-bottom: 1px solid palette(mid);
                padding: 5px;
                spacing: 4px;
            }
            QToolBar QToolButton, QPushButton {
                min-height: 24px;
                padding: 4px 10px;
            }
            QPushButton[primary="true"], QToolButton[primary="true"] {
                background: palette(highlight);
                color: palette(highlighted-text);
                border: 1px solid palette(highlight);
                border-radius: 4px;
                font-weight: 600;
            }
            QPushButton[destructive="true"] { color: #c94c4c; }
            QWidget[role="card"] {
                background: palette(base);
                border: 1px solid palette(mid);
                border-radius: 6px;
            }
            QLabel[role="pageTitle"] { font-size: 16px; font-weight: 600; }
            QLabel[role="pageIntro"], QLabel[role="subtle"] { color: palette(placeholder-text); }
            QLabel[state="success"] { color: #2e9b57; font-weight: 600; }
            QLabel[state="warning"] { color: #c48a19; font-weight: 600; }
            QLabel[state="error"] { color: #c94c4c; font-weight: 600; }
            QLabel[validation="true"] {
                background: palette(base);
                border-left: 3px solid #c94c4c;
                padding: 5px 7px;
            }
            QLabel[role="hud"] {
                background: palette(base);
                border: 1px solid palette(mid);
                border-radius: 4px;
                padding: 4px 8px;
            }
            QTreeWidget#workflowTree {
                border: 0;
                background: palette(window);
                outline: 0;
            }
            QTreeWidget#workflowTree::item { min-height: 27px; padding: 2px 4px; }
            QTreeWidget#workflowTree::item:selected {
                background: palette(highlight);
                color: palette(highlighted-text);
                border-radius: 3px;
            }
            QTabWidget::pane { border: 1px solid palette(mid); }
            QGroupBox { font-weight: 600; margin-top: 8px; }
            QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }
            QLineEdit[readOnly="true"] { background: palette(alternate-base); color: palette(text); }
            QTableWidget { gridline-color: palette(mid); alternate-background-color: palette(alternate-base); }
            QHeaderView::section { padding: 5px; border: 0; border-bottom: 1px solid palette(mid); }
        )");
    }
    return QString();
}

void setTheme(QApplication &app, Theme theme)
{
    s_currentTheme = theme;

    QSettings settings(QStringLiteral("FemGuiProject"), QStringLiteral("FEM Analysis GUI"));
    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    settings.setValue(QString::fromLatin1(kSettingsThemeKey), themeToString(theme));
    settings.endGroup();

    app.setPalette(paletteForTheme(theme));
    app.setStyleSheet(stylesheetForTheme(theme));
}

Theme currentTheme()
{
    return s_currentTheme;
}

void apply(QApplication &app)
{
    QSettings settings(QStringLiteral("FemGuiProject"), QStringLiteral("FEM Analysis GUI"));
    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    const QString savedThemeStr = settings.value(QString::fromLatin1(kSettingsThemeKey), QStringLiteral("liquid_glass_dark")).toString();
    settings.endGroup();

    Theme initialTheme = stringToTheme(savedThemeStr);
    setTheme(app, initialTheme);
}

bool isDark(const QPalette &palette)
{
    return palette.color(QPalette::Window).lightness() < 128;
}

QColor viewportBackground(const QPalette &palette)
{
    return isDark(palette) ? QColor(11, 14, 20) : QColor(237, 243, 249);
}

QColor viewportBackgroundAccent(const QPalette &palette)
{
    return isDark(palette) ? QColor(24, 35, 53) : QColor(215, 227, 238);
}

QColor meshColor(const QPalette &palette)
{
    return isDark(palette) ? QColor(76, 132, 184) : QColor(72, 116, 158);
}

QColor edgeColor(const QPalette &palette)
{
    return isDark(palette) ? QColor(218, 224, 232) : QColor(37, 44, 52);
}

QColor loadColor(const QPalette &palette)
{
    return isDark(palette) ? QColor(255, 132, 101) : QColor(205, 71, 42);
}

QColor selectionColor(const QPalette &palette)
{
    return isDark(palette) ? QColor(255, 213, 79) : QColor(214, 145, 0);
}

// Convenience accessors based on active theme
QColor viewportBackground()
{
    return viewportBackground(paletteForTheme(s_currentTheme));
}

QColor viewportBackgroundAccent()
{
    return viewportBackgroundAccent(paletteForTheme(s_currentTheme));
}

QColor meshColor()
{
    return meshColor(paletteForTheme(s_currentTheme));
}

QColor edgeColor()
{
    return edgeColor(paletteForTheme(s_currentTheme));
}

QColor loadColor()
{
    return loadColor(paletteForTheme(s_currentTheme));
}

QColor selectionColor()
{
    return selectionColor(paletteForTheme(s_currentTheme));
}

} // namespace femgui::UiStyle
