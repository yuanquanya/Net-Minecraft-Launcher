#include <QCoreApplication>
#include <QtGui/QDesktopServices>
#include <QUrl>
#include <QDir>
#include <iostream>
#include "LauncherCore.h"
#include "HttpServer.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    std::cout << "Starting Net Minecraft Launcher Server..." << std::endl;

    LauncherCore launcher;
    launcher.init(QDir::current().filePath(".minecraft").toStdString());

    HttpServer server;
    server.setLauncher(&launcher);

    if (!server.listen(QHostAddress::Any, 8080)) {
        std::cerr << "Failed to start server on port 8080" << std::endl;
        return 1;
    }

    std::cout << "Server running at http://localhost:8080" << std::endl;

    // Open browser automatically
    // Note: QDesktopServices::openUrl requires a GUI application instance on some platforms/versions.
    // Since we use QCoreApplication (console), we fallback to system command if needed.
    if (!QDesktopServices::openUrl(QUrl("http://localhost:8080"))) {
        std::cout << "Failed to open browser via Qt, trying system command..." << std::endl;
#ifdef Q_OS_WIN
        system("start http://localhost:8080");
#elif defined(Q_OS_MAC)
        system("open http://localhost:8080");
#else
        system("xdg-open http://localhost:8080");
#endif
    }

    return app.exec();
}
