#include "HttpServer.h"
#include "WebContent.h"
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextStream>
#include <iostream>
#include <utility> // for std::as_const

HttpServer::HttpServer(QObject *parent) 
    : QTcpServer(parent), launcher(nullptr) 
{
#ifdef NMCL_USE_WEBSOCKETS
    wsServer = new QWebSocketServer(QStringLiteral("NMCL WebSocket"), QWebSocketServer::NonSecureMode, this);
    if (wsServer->listen(QHostAddress::Any, 8081)) {
        std::cout << "WebSocket Server running at ws://localhost:8081" << std::endl;
        connect(wsServer, &QWebSocketServer::newConnection, this, &HttpServer::onNewWebSocketConnection);
    } else {
        std::cerr << "Failed to start WebSocket Server on port 8081" << std::endl;
    }
#endif
}

HttpServer::~HttpServer() {
#ifdef NMCL_USE_WEBSOCKETS
    if (wsServer) wsServer->close();
    qDeleteAll(clients.begin(), clients.end());
    clients.clear();
#endif
}

void HttpServer::setLauncher(LauncherCore* core) {
    launcher = core;
    if (launcher) {
        // Connect signals
        connect(launcher, &LauncherCore::javaProgress,        this, &HttpServer::broadcastJavaProgress);
        connect(launcher, &LauncherCore::javaFinished,        this, &HttpServer::broadcastJavaFinished);
        connect(launcher, &LauncherCore::mcDownloadProgress,  this, &HttpServer::broadcastMcDownloadProgress);
        connect(launcher, &LauncherCore::mcDownloadFinished,  this, &HttpServer::broadcastMcDownloadFinished);
    }
}

#ifdef NMCL_USE_WEBSOCKETS
void HttpServer::onNewWebSocketConnection() {
    QWebSocket *pSocket = wsServer->nextPendingConnection();
    connect(pSocket, &QWebSocket::disconnected, this, &HttpServer::onWebSocketDisconnected);
    clients << pSocket;
    std::cout << "[WS] Client connected" << std::endl;
}

void HttpServer::onWebSocketDisconnected() {
    QWebSocket *pClient = qobject_cast<QWebSocket *>(sender());
    if (pClient) {
        clients.removeAll(pClient);
        pClient->deleteLater();
        std::cout << "[WS] Client disconnected" << std::endl;
    }
}
#endif

void HttpServer::broadcastJavaProgress(int percent, QString message) {
#ifdef NMCL_USE_WEBSOCKETS
    QJsonObject obj;
    obj["type"] = "java_progress";
    obj["percent"] = percent;
    obj["message"] = message;
    
    QString text = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    for (QWebSocket *pClient : std::as_const(clients)) {
        pClient->sendTextMessage(text);
    }
#endif
}

void HttpServer::broadcastJavaFinished(bool success, QString error) {
#ifdef NMCL_USE_WEBSOCKETS
    QJsonObject obj;
    obj["type"] = "java_finished";
    obj["success"] = success;
    obj["error"] = error;
    
    QString text = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    for (QWebSocket *pClient : std::as_const(clients)) {
        pClient->sendTextMessage(text);
    }
#endif
}

void HttpServer::broadcastMcDownloadProgress(int percent, QString message) {
#ifdef NMCL_USE_WEBSOCKETS
    QJsonObject obj;
    obj["type"]    = "mc_download_progress";
    obj["percent"] = percent;
    obj["message"] = message;
    QString text = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    for (QWebSocket *pClient : std::as_const(clients))
        pClient->sendTextMessage(text);
#endif
}

void HttpServer::broadcastMcDownloadFinished(bool success, QString versionId, QString error) {
#ifdef NMCL_USE_WEBSOCKETS
    QJsonObject obj;
    obj["type"]      = "mc_download_finished";
    obj["success"]   = success;
    obj["versionId"] = versionId;
    obj["error"]     = error;
    QString text = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    for (QWebSocket *pClient : std::as_const(clients))
        pClient->sendTextMessage(text);
#endif
}

void HttpServer::incomingConnection(qintptr socketDescriptor) {
    QTcpSocket *socket = new QTcpSocket(this);
    if (!socket->setSocketDescriptor(socketDescriptor)) {
        socket->deleteLater();
        return;
    }

    connect(socket, &QTcpSocket::readyRead, [this, socket]() {
        QByteArray requestData = socket->readAll();
        QString requestStr = QString::fromUtf8(requestData);
        QTextStream in(&requestStr);
        
        // Simple HTTP Parser
        QString method, url, protocol;
        in >> method >> url >> protocol;

        if (method.isEmpty()) return;

        std::cout << "[Request] " << method.toStdString() << " " << url.toStdString() << std::endl;

        QByteArray responseBody;
        QString contentType = "text/plain";
        int statusCode = 200;

        // Routing
        if (method == "GET" && (url == "/" || url == "/index.html")) {
            responseBody = INDEX_HTML;
            contentType = "text/html";
        } 
        else if (method == "GET" && url == "/api/versions") {
            contentType = "application/json";
            if (launcher) {
                auto versions = launcher->getVersionList();
                QJsonArray arr;
                for (const auto& v : versions) {
                    QJsonObject obj;
                    obj["id"] = QString::fromStdString(v.id);
                    obj["type"] = QString::fromStdString(v.type);
                    arr.append(obj);
                }
                responseBody = QJsonDocument(arr).toJson();
            } else {
                responseBody = "[]";
            }
        }
        else if (method == "POST" && url == "/api/launch") {
            contentType = "application/json";
            
            // Extract body (very naive)
            QStringList parts = requestStr.split("\r\n\r\n");
            QString body = parts.size() > 1 ? parts.last() : "";
            
            // If body is empty, try single \n
            if (body.isEmpty()) {
                 parts = requestStr.split("\n\n");
                 body = parts.size() > 1 ? parts.last() : "";
            }

            QJsonDocument doc = QJsonDocument::fromJson(body.toUtf8());
            QJsonObject root = doc.object();
            
            QString ver = root["version"].toString();
            QString user = root["username"].toString();
            int mem = root["memory"].toInt();

            QJsonObject resp;
            if (launcher && !ver.isEmpty()) {
                int res = launcher->launchGame(ver.toStdString(), user.toStdString(), mem);
                if (res == 0) {
                    resp["success"] = true;
                    resp["message"] = "Launched";
                } else if (res == 2) {
                    resp["success"] = false;
                    resp["error"] = "no_java";
                    resp["message"] = "Java environment missing";
                    resp["requiredVersion"] = launcher->getRecommendedJavaVersion(ver.toStdString());
                } else {
                    resp["success"] = false;
                    resp["message"] = "Launch failed (Unknown error)";
                }
            } else {
                 resp["success"] = false;
                 resp["message"] = "Invalid parameters";
            }

            responseBody = QJsonDocument(resp).toJson();
        }
        else if (method == "GET" && url == "/api/java/status") {
             contentType = "application/json";
             if (launcher) {
                 auto status = launcher->getJavaStatus();
                 QJsonObject obj;
                 obj["installing"] = status.installing;
                 obj["progress"] = status.progress;
                 obj["message"] = QString::fromStdString(status.statusMsg);
                 obj["success"] = status.success;
                 obj["error"] = QString::fromStdString(status.error);
                 responseBody = QJsonDocument(obj).toJson();
             } else {
                 responseBody = "{}";
             }
        }
        else if (method == "POST" && url == "/api/java/install") {
             contentType = "application/json";
             
             // Extract body
             QStringList parts = requestStr.split("\r\n\r\n");
             QString body = parts.size() > 1 ? parts.last() : "";
             if (body.isEmpty()) {
                  parts = requestStr.split("\n\n");
                  body = parts.size() > 1 ? parts.last() : "";
             }
             QJsonDocument doc = QJsonDocument::fromJson(body.toUtf8());
             int ver = doc.object()["version"].toInt();
             
             if (launcher && ver > 0) {
                 // Async call not supported in this simple server structure, 
                 // but installJava is currently blocking in our implementation (for demo).
                 // In real app, this should just trigger a thread and return immediately.
                 // For now, let's just trigger it. It will block the server thread!
                 // TODO: Move to thread.
                 
                 // Since we are single threaded here (mostly), blocking is bad.
                 // But for this MVP, we will accept it or use QTimer::singleShot
                 QMetaObject::invokeMethod(launcher, [launcher=this->launcher, ver](){
                     launcher->installJava(ver);
                 }, Qt::QueuedConnection);
                 
                 QJsonObject resp;
                 resp["success"] = true;
                 resp["message"] = "Installation started";
                 responseBody = QJsonDocument(resp).toJson();
             } else {
                 QJsonObject resp;
                 resp["success"] = false;
                 resp["message"] = "Invalid version";
                 responseBody = QJsonDocument(resp).toJson();
             }
        }
        else if (method == "GET" && url == "/api/versions/remote") {
            // Return all versions from Mojang manifest (cached 5 min)
            contentType = "application/json";
            if (launcher) {
                auto versions = launcher->getRemoteVersionList();
                QJsonArray arr;
                for (const auto& v : versions) {
                    QJsonObject obj;
                    obj["id"]   = QString::fromStdString(v.id);
                    obj["type"] = QString::fromStdString(v.type);
                    arr.append(obj);
                }
                responseBody = QJsonDocument(arr).toJson();
            } else {
                responseBody = "[]";
            }
        }
        else if (method == "GET" && url == "/api/versions/installed") {
            // Return locally installed versions with isolation info
            contentType = "application/json";
            if (launcher) {
                auto versions = launcher->getInstalledVersions();
                QJsonArray arr;
                for (const auto& v : versions) {
                    QJsonObject obj;
                    obj["id"]        = QString::fromStdString(v.id);
                    obj["type"]      = QString::fromStdString(v.type);
                    obj["isolated"]  = v.isolated;
                    obj["gameDir"]   = QString::fromStdString(v.gameDir);
                    obj["diskBytes"] = static_cast<double>(v.diskBytes);
                    arr.append(obj);
                }
                responseBody = QJsonDocument(arr).toJson();
            } else {
                responseBody = "[]";
            }
        }
        else if (method == "POST" && url == "/api/download/minecraft") {
            contentType = "application/json";
            QStringList parts = requestStr.split("\r\n\r\n");
            QString body = parts.size() > 1 ? parts.last() : "";
            if (body.isEmpty()) { parts = requestStr.split("\n\n"); body = parts.size() > 1 ? parts.last() : ""; }

            QJsonDocument doc = QJsonDocument::fromJson(body.toUtf8());
            QString verId = doc.object()["versionId"].toString();

            if (launcher && !verId.isEmpty()) {
                QMetaObject::invokeMethod(launcher, [launcher=this->launcher, verId](){
                    launcher->downloadMinecraftVersion(verId.toStdString());
                }, Qt::QueuedConnection);

                QJsonObject resp;
                resp["success"] = true;
                resp["message"] = "下载已开始";
                responseBody = QJsonDocument(resp).toJson();
            } else {
                QJsonObject resp;
                resp["success"] = false;
                resp["message"] = "无效的版本 ID";
                responseBody = QJsonDocument(resp).toJson();
            }
        }
        else if (method == "GET" && url == "/api/download/status") {
            contentType = "application/json";
            if (launcher) {
                auto s = launcher->getDownloadStatus();
                QJsonObject obj;
                obj["active"]     = s.active;
                obj["versionId"]  = QString::fromStdString(s.versionId);
                obj["progress"]   = s.progress;
                obj["message"]    = QString::fromStdString(s.statusMsg);
                obj["success"]    = s.success;
                obj["error"]      = QString::fromStdString(s.error);
                responseBody = QJsonDocument(obj).toJson();
            } else {
                responseBody = "{}";
            }
        }
        else if (method == "POST" && url == "/api/versions/isolation") {
            contentType = "application/json";
            QStringList parts = requestStr.split("\r\n\r\n");
            QString body = parts.size() > 1 ? parts.last() : "";
            if (body.isEmpty()) { parts = requestStr.split("\n\n"); body = parts.size() > 1 ? parts.last() : ""; }

            QJsonDocument doc = QJsonDocument::fromJson(body.toUtf8());
            QString verId    = doc.object()["versionId"].toString();
            bool    isolated = doc.object()["isolated"].toBool();

            QJsonObject resp;
            if (launcher && !verId.isEmpty()) {
                bool ok = launcher->setVersionIsolation(verId.toStdString(), isolated);
                resp["success"] = ok;
                resp["message"] = ok ? "隔离设置已保存" : "设置失败";
            } else {
                resp["success"] = false;
                resp["message"] = "无效参数";
            }
            responseBody = QJsonDocument(resp).toJson();
        }
        else {
            statusCode = 404;
            responseBody = "Not Found";
        }

        // Send Response
        QString statusMsg = (statusCode == 200) ? "OK" : "Not Found";
        QByteArray response;
        response.append(QString("HTTP/1.1 %1 %2\r\n").arg(statusCode).arg(statusMsg).toUtf8());
        response.append(QString("Content-Type: %1\r\n").arg(contentType).toUtf8());
        response.append(QString("Content-Length: %1\r\n").arg(responseBody.size()).toUtf8());
        response.append("Connection: close\r\n");
        response.append("Access-Control-Allow-Origin: *\r\n"); // CORS
        response.append("\r\n");
        response.append(responseBody);

        socket->write(response);
        socket->flush();
        socket->waitForBytesWritten(3000);
        socket->close();
        socket->deleteLater();
    });
}
