#pragma once

#include <QTcpServer>
#include <QObject>
#include "LauncherCore.h"

class HttpServer : public QTcpServer {
    Q_OBJECT
public:
    explicit HttpServer(QObject *parent = nullptr);
    void setLauncher(LauncherCore* core);

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private:
    LauncherCore* launcher;
};
