#include <QApplication>

#include "app/Application.h"
#include "ui/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    femgui::configureApplication(app);

    femgui::MainWindow window;
    window.show();

    return app.exec();
}
