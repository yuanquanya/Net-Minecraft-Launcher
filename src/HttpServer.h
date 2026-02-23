#pragma once

#include <QTcpServer>
#include <QObject>
#ifdef NMCL_USE_WEBSOCKETS
#include <QWebSocketServer>
#include <QWebSocket>
#endif
#include <QList>
#include "LauncherCore.h"

class HttpServer : public QTcpServer {
    Q_OBJECT
public:
    explicit HttpServer(QObject *parent = nullptr);
    ~HttpServer();
    void setLauncher(LauncherCore* core);

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private slots:
#ifdef NMCL_USE_WEBSOCKETS
    void onNewWebSocketConnection();
    void onWebSocketDisconnected();
#endif
    
    // Broadcast slots
    void broadcastJavaProgress(int percent, QString message);
    void broadcastJavaFinished(bool success, QString error);

private:
    LauncherCore* launcher;
#ifdef NMCL_USE_WEBSOCKETS
    QWebSocketServer *wsServer;
    QList<QWebSocket*> clients;
#endif
};
