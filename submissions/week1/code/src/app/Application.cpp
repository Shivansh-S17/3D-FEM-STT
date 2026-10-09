#include "app/Application.h"

#include <QApplication>

namespace femgui {

void configureApplication(QApplication &app)
{
    QApplication::setOrganizationName("FemGuiProject");
    QApplication::setApplicationName("FEM Analysis GUI");
    QApplication::setApplicationVersion("0.1.0-prototype");
    Q_UNUSED(app);
}

} // namespace femgui
