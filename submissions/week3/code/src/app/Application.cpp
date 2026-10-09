#include "app/Application.h"
#include "ui/UiStyle.h"

#include <QApplication>

namespace femgui {

void configureApplication(QApplication &app)
{
    QApplication::setOrganizationName("FemGuiProject");
    QApplication::setApplicationName("FEM Analysis GUI");
    QApplication::setApplicationVersion("0.2.0");
    UiStyle::apply(app);
}

} // namespace femgui
