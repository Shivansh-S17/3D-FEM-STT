#pragma once

class QApplication;

namespace femgui {

// One-time process-wide setup: organization/app identity for QSettings,
// high-DPI behavior, and the default widget style.
void configureApplication(QApplication &app);

} // namespace femgui
