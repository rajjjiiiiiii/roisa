// main.cpp — ROISA entry point

#include <QApplication>
#include <QStyleFactory>
#include <QSurfaceFormat>
#include "gui/MainWindow.h"

#ifdef ROISA_USE_VTK
#  include <QVTKOpenGLNativeWidget.h>
#endif

int main(int argc, char* argv[])
{
    // VTK requires a specific OpenGL surface format set BEFORE QApplication.
#ifdef ROISA_USE_VTK
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());
#endif

    QApplication app(argc, argv);
    app.setApplicationName("ROISA");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("ImageTools");

    // Use Fusion style for a clean, dark-theme-compatible look
    app.setStyle(QStyleFactory::create("Fusion"));

    // Dark palette
    QPalette dark;
    dark.setColor(QPalette::Window,          QColor(30,  30,  30));
    dark.setColor(QPalette::WindowText,      QColor(220, 220, 220));
    dark.setColor(QPalette::Base,            QColor(42,  42,  42));
    dark.setColor(QPalette::AlternateBase,   QColor(50,  50,  50));
    dark.setColor(QPalette::ToolTipBase,     Qt::black);
    dark.setColor(QPalette::ToolTipText,     Qt::white);
    dark.setColor(QPalette::Text,            QColor(220, 220, 220));
    dark.setColor(QPalette::Button,          QColor(50,  50,  50));
    dark.setColor(QPalette::ButtonText,      QColor(220, 220, 220));
    dark.setColor(QPalette::BrightText,      Qt::red);
    dark.setColor(QPalette::Link,            QColor(42, 130, 218));
    dark.setColor(QPalette::Highlight,       QColor(42, 130, 218));
    dark.setColor(QPalette::HighlightedText, Qt::black);
    app.setPalette(dark);

    MainWindow window;

    // If a path is provided on the command line, load it immediately
    const auto args = app.arguments();
    if (args.size() >= 2)
        window.loadPath(args[1]);

    window.show();
    return app.exec();
}
