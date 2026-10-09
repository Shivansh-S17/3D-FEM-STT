#include <QApplication>
#include <QSurfaceFormat>
#include <QVTKOpenGLNativeWidget.h>

#include "app/Application.h"
#include "ui/MainWindow.h"

int main(int argc, char *argv[])
{
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());
    QApplication app(argc, argv);
    femgui::configureApplication(app);

    femgui::MainWindow window;
    window.show();

    return app.exec();
}
