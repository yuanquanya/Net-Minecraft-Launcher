#pragma once

#include <string>
#include <vector>
#include <QObject>
#include <QNetworkAccessManager>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>

// 简单的版本信息结构体
struct MinecraftVersion {
    std::string id;
    std::string type;
    std::string url;
};

class LauncherCore : public QObject {
    Q_OBJECT
public:
    explicit LauncherCore(QObject *parent = nullptr);
    ~LauncherCore();

    // 初始化（如设置工作目录）
    void init(const std::string& workDir);

    // 获取版本列表 (同步阻塞方式，方便演示)
    std::vector<MinecraftVersion> getVersionList();

    // 启动游戏
    // versionId: 版本号 (如 "1.16.5")
    // username: 玩家名
    // maxMemory: 最大内存 (MB)
    // Returns: 0 on success, 1 on error, 2 if Java is missing
    int launchGame(const std::string& versionId, const std::string& username, int maxMemory);
    
    // Java Installation
    struct JavaInstallStatus {
        bool installing = false;
        int progress = 0; // 0-100
        std::string statusMsg;
        bool success = false;
        std::string error;
    };
    
    JavaInstallStatus getJavaStatus() { return javaStatus; }
    void installJava(int majorVersion);
    int getRecommendedJavaVersion(const std::string& versionId);

private:
    std::string workDir;
    QNetworkAccessManager *networkManager;
    JavaInstallStatus javaStatus;
    
    // Helper to find best java path
    QString findJavaPath(int requiredVersion);

    // 网络请求辅助函数 (同步封装)
    QByteArray httpGet(const std::string& url);
    // Modified: support sha1 and size check
    bool downloadFile(const std::string& url, const std::string& filepath, int size = -1, const std::string& sha1 = "");
    bool extractNative(const std::string& archivePath, const std::string& targetDir);
    
    bool validateFile(const std::string& filepath, int size, const std::string& sha1);
    std::string calculateFileSha1(const std::string& filepath);

    // 核心逻辑辅助函数
    QJsonObject getVersionManifest(const std::string& versionId);
    std::string buildClassPath(const QJsonObject& versionJson);
    std::vector<std::string> buildArguments(const QJsonObject& versionJson, const std::string& username, int maxMemory, const std::string& classPath, const std::string& nativesPath);
};
