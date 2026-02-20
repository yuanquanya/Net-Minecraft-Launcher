#include "LauncherCore.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QDir>
#include <QCoreApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QSettings>
#include <QStringList>

namespace fs = std::filesystem;

LauncherCore::LauncherCore(QObject *parent) : QObject(parent) {
    networkManager = new QNetworkAccessManager(this);
}

LauncherCore::~LauncherCore() {
}

void LauncherCore::init(const std::string& dir) {
    workDir = dir;
    fs::create_directories(fs::path(workDir) / "versions");
    fs::create_directories(fs::path(workDir) / "libraries");
    fs::create_directories(fs::path(workDir) / "assets");
}

static QString resolveJavaInDir(const QString& baseDir) {
    QDir dir(baseDir);
    QStringList exeNames;
    exeNames << "javaw.exe" << "java.exe";
    for (const auto& exe : exeNames) {
        QString candidate = dir.filePath("bin/" + exe);
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    // Also check root for portable versions
    for (const auto& exe : exeNames) {
         QString candidate = dir.filePath(exe);
         if (QFileInfo::exists(candidate)) {
             return candidate;
         }
    }
    return QString();
}

static QString findJavaHomeFromRegistry() {
    QStringList keys;
    keys << "HKEY_LOCAL_MACHINE\\SOFTWARE\\JavaSoft\\Java Runtime Environment";
    keys << "HKEY_LOCAL_MACHINE\\SOFTWARE\\JavaSoft\\JRE";
    keys << "HKEY_LOCAL_MACHINE\\SOFTWARE\\JavaSoft\\JDK";
    keys << "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\JavaSoft\\Java Runtime Environment";
    keys << "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\JavaSoft\\JRE";
    keys << "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\JavaSoft\\JDK";

    for (const auto& keyPath : keys) {
        QSettings baseKey(keyPath, QSettings::NativeFormat);
        QString currentVersion = baseKey.value("CurrentVersion").toString();
        if (currentVersion.isEmpty()) {
            continue;
        }
        QSettings versionKey(keyPath + "\\" + currentVersion, QSettings::NativeFormat);
        QString javaHome = versionKey.value("JavaHome").toString();
        if (!javaHome.isEmpty()) {
            return javaHome;
        }
    }
    return QString();
}

// 查找 Java 路径的实现
QString LauncherCore::findJavaPath(int requiredVersion) {
    // 1. 优先检查本地私有运行时 (runtime/<ver>)
    QString localRuntime = QString::fromStdString(workDir) + "/runtime/java-runtime-" + QString::number(requiredVersion);
    QString localJava = resolveJavaInDir(localRuntime);
    if (!localJava.isEmpty()) return localJava;
    
    // 2. 检查系统 Java (PATH, JAVA_HOME, Registry)
    // Note: In real world, we should check 'java -version' to ensure it matches requiredVersion
    QString javaPath = QStandardPaths::findExecutable("java");
    if (javaPath.isEmpty()) javaPath = QStandardPaths::findExecutable("javaw");
    
    if (javaPath.isEmpty()) {
        QString javaHome = qgetenv("JAVA_HOME");
        if (!javaHome.isEmpty()) javaPath = resolveJavaInDir(javaHome);
    }
    
    if (javaPath.isEmpty()) {
        QString regHome = findJavaHomeFromRegistry();
        if (!regHome.isEmpty()) javaPath = resolveJavaInDir(regHome);
    }

    return javaPath;
}

int LauncherCore::getRecommendedJavaVersion(const std::string& versionId) {
    QJsonObject vJson = getVersionManifest(versionId);
    if (vJson.contains("javaVersion")) {
        return vJson["javaVersion"].toObject()["majorVersion"].toInt();
    }
    return 8; // Default to 8
}

void LauncherCore::installJava(int majorVersion) {
    javaStatus.installing = true;
    javaStatus.progress = 0;
    javaStatus.success = false;
    javaStatus.error = "";
    javaStatus.statusMsg = "Starting download...";

    // 简化的下载链接 (示例用，生产环境应使用 API 获取)
    std::string url;
    if (majorVersion == 8) {
            // Java 8 (Temurin via BMCLAPI GitHub Proxy)
            url = "https://bmclapi2.bangbang93.com/github/adoptium/temurin8-binaries/releases/download/jdk8u402-b06/OpenJDK8U-jre_x64_windows_hotspot_8u402b06.zip";
        } else {
            // Java 17 (Temurin via BMCLAPI GitHub Proxy)
            url = "https://bmclapi2.bangbang93.com/github/adoptium/temurin17-binaries/releases/download/jdk-17.0.10%2B7/OpenJDK17U-jre_x64_windows_hotspot_17.0.10_7.zip";
        }

    std::string zipPath = (fs::path(workDir) / "runtime" / ("java_" + std::to_string(majorVersion) + ".zip")).string();
    std::string targetDir = (fs::path(workDir) / "runtime" / ("java-runtime-" + std::to_string(majorVersion))).string();

    fs::create_directories(fs::path(workDir) / "runtime");
    
    javaStatus.statusMsg = "Downloading JRE " + std::to_string(majorVersion) + "...";
    
    // Manual download with progress
    QNetworkRequest request(QUrl(QString::fromStdString(url)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QSslConfiguration conf = request.sslConfiguration();
    conf.setPeerVerifyMode(QSslSocket::VerifyNone);
    request.setSslConfiguration(conf);

    QNetworkReply *reply = networkManager->get(request);
    
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::downloadProgress, [&](qint64 received, qint64 total) {
        if (total > 0) {
            javaStatus.progress = (int)((received * 100) / total);
        }
    });
    
    loop.exec();
    
    if (reply->error() != QNetworkReply::NoError) {
        javaStatus.error = "Download failed: " + reply->errorString().toStdString();
        javaStatus.installing = false;
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    std::ofstream outfile(zipPath, std::ios::binary);
    outfile.write(data.constData(), data.size());
    outfile.close();

    javaStatus.statusMsg = "Extracting JRE...";
    javaStatus.progress = 100;

    // Extract
    fs::create_directories(targetDir);
    extractNative(zipPath, targetDir);
    
    // Cleanup zip
    fs::remove(zipPath);

    // Verify
    QString javaExe = resolveJavaInDir(QString::fromStdString(targetDir));
    // Handle nested folder structure common in JDK zips
    if (javaExe.isEmpty()) {
        for (const auto& entry : fs::directory_iterator(targetDir)) {
            if (entry.is_directory()) {
                javaExe = resolveJavaInDir(QString::fromStdString(entry.path().string()));
                if (!javaExe.isEmpty()) {
                    break;
                }
            }
        }
    }

    if (!javaExe.isEmpty()) {
        javaStatus.success = true;
        javaStatus.statusMsg = "Installed successfully!";
    } else {
        javaStatus.error = "Extraction failed or java.exe not found";
    }
    
    javaStatus.installing = false;
}

QByteArray LauncherCore::httpGet(const std::string& url) {
    QNetworkRequest request(QUrl(QString::fromStdString(url)));
    QSslConfiguration conf = request.sslConfiguration();
    conf.setPeerVerifyMode(QSslSocket::VerifyNone);
    request.setSslConfiguration(conf);

    QNetworkReply *reply = networkManager->get(request);

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    QByteArray data;
    if (reply->error() == QNetworkReply::NoError) {
        data = reply->readAll();
    } else {
        std::cerr << "Network Error: " << reply->errorString().toStdString() << std::endl;
    }
    reply->deleteLater();
    return data;
}

std::vector<MinecraftVersion> LauncherCore::getVersionList() {
    std::vector<MinecraftVersion> versions;
    // 使用 BMCLAPI 镜像源替代官方源
    QByteArray response = httpGet("https://bmclapi2.bangbang93.com/mc/game/version_manifest.json");
    
    if (response.isEmpty()) return versions;

    QJsonDocument doc = QJsonDocument::fromJson(response);
    if (doc.isNull() || !doc.isObject()) return versions;

    QJsonObject root = doc.object();
    QJsonArray versionArray = root["versions"].toArray();

    for (const auto& v : versionArray) {
        QJsonObject vObj = v.toObject();
        std::string officialUrl = vObj["url"].toString().toStdString();
        versions.push_back({
            vObj["id"].toString().toStdString(),
            vObj["type"].toString().toStdString(),
            officialUrl 
        });
    }
    
    return versions;
}

std::string LauncherCore::calculateFileSha1(const std::string& filepath) {
    QFile file(QString::fromStdString(filepath));
    if (file.open(QIODevice::ReadOnly)) {
        QCryptographicHash hash(QCryptographicHash::Sha1);
        if (hash.addData(&file)) {
            return hash.result().toHex().toStdString();
        }
    }
    return "";
}

bool LauncherCore::validateFile(const std::string& filepath, int size, const std::string& sha1) {
    if (!fs::exists(filepath)) return false;
    
    if (size != -1 && fs::file_size(filepath) != size) {
        std::cout << "[Validate] Size mismatch: " << filepath << std::endl;
        return false;
    }

    if (!sha1.empty()) {
        std::string localSha1 = calculateFileSha1(filepath);
        if (localSha1 != sha1) {
             std::cout << "[Validate] SHA1 mismatch: " << filepath << std::endl;
             return false;
        }
    }
    
    return true;
}

bool LauncherCore::downloadFile(const std::string& url, const std::string& filepath, int size, const std::string& sha1) {
    fs::create_directories(fs::path(filepath).parent_path());
    
    // Check if file exists and is valid
    if (validateFile(filepath, size, sha1)) {
        std::cout << "[Skipped] " << filepath << " (Valid)" << std::endl;
        return true;
    }

    // 自动替换为 BMCLAPI 镜像源
    std::string finalUrl = url;
    QString qUrl = QString::fromStdString(url);
    if (qUrl.contains("launchermeta.mojang.com")) {
        qUrl.replace("launchermeta.mojang.com", "bmclapi2.bangbang93.com");
    } else if (qUrl.contains("launcher.mojang.com")) {
        qUrl.replace("launcher.mojang.com", "bmclapi2.bangbang93.com");
    } else if (qUrl.contains("resources.download.minecraft.net")) {
        qUrl.replace("resources.download.minecraft.net", "bmclapi2.bangbang93.com/assets");
    } else if (qUrl.contains("libraries.minecraft.net")) {
        qUrl.replace("libraries.minecraft.net", "bmclapi2.bangbang93.com/maven");
    } else if (qUrl.contains("piston-meta.mojang.com")) {
        qUrl.replace("piston-meta.mojang.com", "bmclapi2.bangbang93.com");
    }
    finalUrl = qUrl.toStdString();

    std::cout << "[Downloading] " << finalUrl << std::endl;
    QByteArray data = httpGet(finalUrl);
    if (!data.isEmpty()) {
        std::ofstream outfile(filepath, std::ios::binary);
        outfile.write(data.constData(), data.size());
        outfile.close();
        
        // Validate after download
        if (validateFile(filepath, size, sha1)) {
            return true;
        } else {
             std::cerr << "[Error] Downloaded file corrupted: " << filepath << std::endl;
             fs::remove(filepath); // Delete corrupted file
             return false;
        }
    }
    return false;
}

bool LauncherCore::extractNative(const std::string& archivePath, const std::string& targetDir) {
    // Use system tar (available on Windows 10+) to extract
    QProcess unzip;
    QStringList args;
    args << "-xf" << QString::fromStdString(archivePath) << "-C" << QString::fromStdString(targetDir);
    
    std::cout << "[Extracting] " << archivePath << std::endl;
    unzip.start("tar", args);
    unzip.waitForFinished();
    
    return unzip.exitCode() == 0;
}

QJsonObject LauncherCore::getVersionManifest(const std::string& versionId) {
    auto list = getVersionList();
    std::string url;
    for (const auto& v : list) {
        if (v.id == versionId) {
            url = v.url;
            break;
        }
    }
    
    if (url.empty()) return QJsonObject();

    QByteArray jsonBytes = httpGet(url);
    if (jsonBytes.isEmpty()) return QJsonObject();

    std::string localJsonPath = (fs::path(workDir) / "versions" / versionId / (versionId + ".json")).string();
    fs::create_directories(fs::path(localJsonPath).parent_path());
    std::ofstream out(localJsonPath);
    out << jsonBytes.toStdString();
    out.close();

    QJsonDocument doc = QJsonDocument::fromJson(jsonBytes);
    return doc.object();
}

std::string LauncherCore::buildClassPath(const QJsonObject& versionJson) {
    std::string cp;
    std::string sep = ";"; 
    std::string nativesDir = (fs::path(workDir) / "versions" / versionJson["id"].toString().toStdString() / "natives").string();
    fs::create_directories(nativesDir);

    if (versionJson.contains("libraries")) {
        QJsonArray libs = versionJson["libraries"].toArray();
        for (const auto& libVal : libs) {
            QJsonObject lib = libVal.toObject();
            QJsonObject downloads = lib["downloads"].toObject();

            // 1. Regular Artifact
            if (downloads.contains("artifact")) {
                QJsonObject artifact = downloads["artifact"].toObject();
                std::string path = artifact["path"].toString().toStdString();
                std::string fullPath = (fs::path(workDir) / "libraries" / path).string();
                std::string url = artifact["url"].toString().toStdString();
                int size = artifact.contains("size") ? artifact["size"].toInt() : -1;
                std::string sha1 = artifact.contains("sha1") ? artifact["sha1"].toString().toStdString() : "";
                
                downloadFile(url, fullPath, size, sha1);
                cp += fullPath + sep;
            }

            // 2. Natives (Classifiers)
            if (downloads.contains("classifiers")) {
                QJsonObject classifiers = downloads["classifiers"].toObject();
                if (classifiers.contains("natives-windows")) {
                    QJsonObject nativeArtifact = classifiers["natives-windows"].toObject();
                    std::string path = nativeArtifact["path"].toString().toStdString();
                    std::string fullPath = (fs::path(workDir) / "libraries" / path).string();
                    std::string url = nativeArtifact["url"].toString().toStdString();
                    int size = nativeArtifact.contains("size") ? nativeArtifact["size"].toInt() : -1;
                    std::string sha1 = nativeArtifact.contains("sha1") ? nativeArtifact["sha1"].toString().toStdString() : "";

                    if (downloadFile(url, fullPath, size, sha1)) {
                        extractNative(fullPath, nativesDir);
                    }
                }
            }
        }
    }

    // Client Jar
    std::string versionId = versionJson["id"].toString().toStdString();
    std::string clientJarPath = (fs::path(workDir) / "versions" / versionId / (versionId + ".jar")).string();
    
    if (versionJson.contains("downloads") && versionJson["downloads"].toObject().contains("client")) {
        QJsonObject clientObj = versionJson["downloads"].toObject()["client"].toObject();
        std::string clientUrl = clientObj["url"].toString().toStdString();
        int size = clientObj.contains("size") ? clientObj["size"].toInt() : -1;
        std::string sha1 = clientObj.contains("sha1") ? clientObj["sha1"].toString().toStdString() : "";
        downloadFile(clientUrl, clientJarPath, size, sha1);
    }
    
    cp += clientJarPath;
    
    return cp;
}

std::vector<std::string> LauncherCore::buildArguments(const QJsonObject& versionJson, const std::string& username, int maxMemory, const std::string& classPath, const std::string& nativesPath) {
    std::vector<std::string> args;

    args.push_back("-Xmx" + std::to_string(maxMemory) + "M");
    args.push_back("-Djava.library.path=" + nativesPath);
    args.push_back("-cp");
    args.push_back(classPath);

    std::string mainClass = versionJson["mainClass"].toString().toStdString();
    args.push_back(mainClass);

    if (versionJson.contains("arguments") && versionJson["arguments"].toObject().contains("game")) {
        QJsonArray gameArgs = versionJson["arguments"].toObject()["game"].toArray();
        for (const auto& argVal : gameArgs) {
            if (argVal.isString()) {
                std::string s = argVal.toString().toStdString();
                if (s == "${auth_player_name}") s = username;
                else if (s == "${version_name}") s = versionJson["id"].toString().toStdString();
                else if (s == "${game_directory}") s = workDir;
                else if (s == "${assets_root}") s = (fs::path(workDir) / "assets").string();
                else if (s == "${assets_index_name}") s = versionJson.contains("assets") ? versionJson["assets"].toString().toStdString() : "legacy";
                else if (s == "${auth_uuid}") s = "00000000-0000-0000-0000-000000000000"; 
                else if (s == "${auth_access_token}") s = "0"; 
                else if (s == "${user_type}") s = "mojang";
                else if (s == "${version_type}") s = versionJson["type"].toString().toStdString();
                
                args.push_back(s);
            }
        }
    } else if (versionJson.contains("minecraftArguments")) {
        std::string rawArgs = versionJson["minecraftArguments"].toString().toStdString();
        // Naive split, should implement tokenizer for real use
        QString qRawArgs = QString::fromStdString(rawArgs);
        QStringList parts = qRawArgs.split(' ');
        for(const auto& part : parts) {
             std::string s = part.toStdString();
             if (s == "${auth_player_name}") s = username;
             else if (s == "${version_name}") s = versionJson["id"].toString().toStdString();
             else if (s == "${game_directory}") s = workDir;
             else if (s == "${assets_root}") s = (fs::path(workDir) / "assets").string();
             else if (s == "${assets_index_name}") s = versionJson.contains("assets") ? versionJson["assets"].toString().toStdString() : "legacy";
             else if (s == "${auth_uuid}") s = "00000000-0000-0000-0000-000000000000";
             else if (s == "${auth_access_token}") s = "0";
             else if (s == "${user_type}") s = "mojang";
             else if (s == "${version_type}") s = versionJson["type"].toString().toStdString();
             args.push_back(s);
        }
    }

    return args;
}

int LauncherCore::launchGame(const std::string& versionId, const std::string& username, int maxMemory) {
    std::cout << "准备启动版本: " << versionId << std::endl;

    QJsonObject vJson = getVersionManifest(versionId);
    if (vJson.isEmpty()) {
        std::cerr << "无法获取版本信息" << std::endl;
        return 1;
    }
    
    // Check Java Requirement
    int requiredJava = 8;
    if (vJson.contains("javaVersion")) {
        requiredJava = vJson["javaVersion"].toObject()["majorVersion"].toInt();
    }
    
    QString javaPath = findJavaPath(requiredJava);
    
    if (javaPath.isEmpty()) {
        std::cerr << "Java missing. Required: " << requiredJava << std::endl;
        return 2; // Return code 2 indicates missing Java
    }

    std::string nativesDir = (fs::path(workDir) / "versions" / versionId / "natives").string();
    fs::create_directories(nativesDir);
    
    std::string cp = buildClassPath(vJson);
    std::vector<std::string> args = buildArguments(vJson, username, maxMemory, cp, nativesDir);

    QStringList arguments;
    for (const auto& arg : args) {
        arguments << QString::fromStdString(arg);
    }

    std::cout << "Using Java at: " << javaPath.toStdString() << std::endl;
    std::cout << "Starting Java Process..." << std::endl;
    
    bool started = QProcess::startDetached(javaPath, arguments);
    
    if (started) {
        std::cout << "Game process started successfully!" << std::endl;
        return 0;
    } else {
        std::cerr << "Failed to start java process." << std::endl;
        return 1;
    }
}
