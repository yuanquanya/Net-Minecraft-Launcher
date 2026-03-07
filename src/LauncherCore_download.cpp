// LauncherCore_download.cpp
// ═══════════════════════════════════════════════════════════════════════════
//  New feature implementations:
//    • getRemoteVersionList()   – fetch+cache Mojang version_manifest_v2.json
//    • getInstalledVersions()   – scan workDir/versions/ for local installs
//    • setVersionIsolation()    – toggle per-version game directory isolation
//    • downloadMinecraftVersion()– async 3-phase MC version download pipeline
//    • getDownloadStatus()      – thread-safe snapshot of download state
// ═══════════════════════════════════════════════════════════════════════════

#include "LauncherCore.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>
#include <QtConcurrent>
#include <iostream>

// ── Constants ────────────────────────────────────────────────────────────────

static constexpr const char* MANIFEST_URL =
    "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
static constexpr const char* MANIFEST_URL_MIRROR =
    "https://bmclapi2.bangbang93.com/mc/game/version_manifest_v2.json";

static constexpr int REMOTE_CACHE_SECS = 300; // 5-minute cache

// ── Helpers ──────────────────────────────────────────────────────────────────

static int64_t dirSizeBytes(const QString& path) {
    int64_t total = 0;
    QDirIterator it(path, QDir::Files | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

// ── getRemoteVersionList ──────────────────────────────────────────────────────

std::vector<MinecraftVersion> LauncherCore::getRemoteVersionList() {
    {
        QMutexLocker lk(&m_remoteVersionsLock);
        if (!m_remoteVersionsCache.empty() &&
            m_remoteVersionsCachedAt.secsTo(QDateTime::currentDateTime()) < REMOTE_CACHE_SECS)
        {
            return m_remoteVersionsCache;
        }
    }

    // Try primary URL, fall back to BMCL mirror
    bool ok = false;
    QByteArray data = httpGet(MANIFEST_URL, &ok);
    if (!ok || data.isEmpty())
        data = httpGet(MANIFEST_URL_MIRROR, &ok);

    if (!ok || data.isEmpty()) {
        std::cerr << "[Remote] Failed to fetch version manifest" << std::endl;
        QMutexLocker lk(&m_remoteVersionsLock);
        return m_remoteVersionsCache; // return stale cache if any
    }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray arr    = doc.object()["versions"].toArray();

    std::vector<MinecraftVersion> list;
    list.reserve(static_cast<size_t>(arr.size()));

    for (const QJsonValue& v : arr) {
        QJsonObject o = v.toObject();
        MinecraftVersion mv;
        mv.id   = o["id"].toString().toStdString();
        mv.type = o["type"].toString().toStdString();
        mv.url  = o["url"].toString().toStdString();
        list.push_back(mv);
    }

    QMutexLocker lk(&m_remoteVersionsLock);
    m_remoteVersionsCache    = list;
    m_remoteVersionsCachedAt = QDateTime::currentDateTime();
    return list;
}

// ── getInstalledVersions ──────────────────────────────────────────────────────

std::vector<InstalledVersion> LauncherCore::getInstalledVersions() const {
    const QString versionsRoot = QString::fromStdString(workDir) + "/versions";
    const QString isolationCfg = QString::fromStdString(workDir) + "/isolation.ini";

    QSettings iso(isolationCfg, QSettings::IniFormat);
    iso.beginGroup("isolation");

    std::vector<InstalledVersion> result;

    QDir dir(versionsRoot);
    if (!dir.exists()) return result;

    for (const QString& entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString jsonPath = versionsRoot + "/" + entry + "/" + entry + ".json";
        if (!QFile::exists(jsonPath)) continue;

        InstalledVersion iv;
        iv.id = entry.toStdString();

        // Parse type from version JSON
        QFile f(jsonPath);
        if (f.open(QIODevice::ReadOnly)) {
            QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
            iv.type = obj["type"].toString("release").toStdString();
            f.close();
        }

        // Isolation
        iv.isolated = iso.value(entry, false).toBool();
        if (iv.isolated) {
            iv.gameDir = (QString::fromStdString(workDir) +
                          "/isolated/" + entry).toStdString();
        }

        // Disk usage
        iv.diskBytes = dirSizeBytes(versionsRoot + "/" + entry);

        result.push_back(iv);
    }

    return result;
}

// ── setVersionIsolation ───────────────────────────────────────────────────────

bool LauncherCore::setVersionIsolation(const std::string& versionId, bool isolated) {
    const QString isolationCfg = QString::fromStdString(workDir) + "/isolation.ini";
    QSettings iso(isolationCfg, QSettings::IniFormat);
    iso.beginGroup("isolation");
    iso.setValue(QString::fromStdString(versionId), isolated);
    iso.sync();

    if (isolated) {
        // Ensure the isolated directory exists
        const QString isoDir = QString::fromStdString(workDir) + "/isolated/" +
                               QString::fromStdString(versionId);
        QDir().mkpath(isoDir);
    }
    return true;
}

// ── getDownloadStatus ─────────────────────────────────────────────────────────

McDownloadStatus LauncherCore::getDownloadStatus() const {
    QMutexLocker lk(&m_dlStatusLock);
    return m_dlStatus;
}

// ── downloadMinecraftVersion ──────────────────────────────────────────────────
//
//  Phase 1 – Resolve version manifest URL from all.json
//  Phase 2 – Download client.jar + libraries + assets index
//  Phase 3 – Verify and emit mcDownloadFinished
//

void LauncherCore::downloadMinecraftVersion(const std::string& versionId) {
    {
        QMutexLocker lk(&m_dlStatusLock);
        if (m_dlStatus.active) {
            std::cerr << "[DL] Another download is in progress" << std::endl;
            return;
        }
        m_dlStatus = { true, versionId, 0, "初始化...", false, "" };
    }

    QtConcurrent::run([this, versionId]() {

        auto setProgress = [&](int pct, const std::string& msg) {
            {
                QMutexLocker lk(&m_dlStatusLock);
                m_dlStatus.progress  = pct;
                m_dlStatus.statusMsg = msg;
            }
            emit mcDownloadProgress(pct, QString::fromStdString(msg));
        };

        auto finish = [&](bool ok, const std::string& err = {}) {
            {
                QMutexLocker lk(&m_dlStatusLock);
                m_dlStatus.active    = false;
                m_dlStatus.success   = ok;
                m_dlStatus.error     = err;
                m_dlStatus.progress  = ok ? 100 : m_dlStatus.progress;
                m_dlStatus.statusMsg = ok ? "完成" : ("失败: " + err);
            }
            emit mcDownloadFinished(ok,
                                    QString::fromStdString(versionId),
                                    QString::fromStdString(err));
        };

        // ── Phase 1: Resolve version manifest URL ──────────────────────────
        setProgress(2, "解析版本清单...");

        const QString vid   = QString::fromStdString(versionId);
        const QString vDir  = QString::fromStdString(workDir) + "/versions/" + vid;
        QDir().mkpath(vDir);

        // Look up URL from remote manifest
        std::string manifestUrl;
        {
            auto list = getRemoteVersionList();
            for (const auto& mv : list) {
                if (mv.id == versionId) { manifestUrl = mv.url; break; }
            }
        }

        if (manifestUrl.empty()) {
            finish(false, "未找到版本 " + versionId);
            return;
        }

        // ── Phase 2: Download version JSON ────────────────────────────────
        setProgress(5, "下载版本 JSON...");

        const QString jsonPath = vDir + "/" + vid + ".json";
        if (!downloadFile(manifestUrl, jsonPath.toStdString())) {
            finish(false, "版本 JSON 下载失败");
            return;
        }

        QFile f(jsonPath);
        if (!f.open(QIODevice::ReadOnly)) { finish(false, "无法读取版本 JSON"); return; }
        QJsonObject vObj = QJsonDocument::fromJson(f.readAll()).object();
        f.close();

        // ── Phase 3: Download client.jar ──────────────────────────────────
        setProgress(10, "下载客户端 JAR...");

        QJsonObject clientDl = vObj["downloads"].toObject()["client"].toObject();
        QString clientUrl    = clientDl["url"].toString();
        QString clientSha    = clientDl["sha1"].toString();
        int     clientSize   = clientDl["size"].toInt(-1);

        const QString clientJar = vDir + "/" + vid + ".jar";
        if (!downloadFile(clientUrl.toStdString(), clientJar.toStdString(),
                          clientSize, clientSha.toStdString()))
        {
            finish(false, "客户端 JAR 下载失败");
            return;
        }

        // ── Phase 4: Build library download list ──────────────────────────
        setProgress(25, "解析依赖库清单...");

        std::vector<DownloadTask> libTasks;
        const QString libRoot = QString::fromStdString(workDir) + "/libraries";

        for (const QJsonValue& lv : vObj["libraries"].toArray()) {
            QJsonObject lib     = lv.toObject();
            // Evaluate rules
            if (lib.contains("rules") && !evaluateRules(lib["rules"].toArray()))
                continue;

            QJsonObject artifact = lib["downloads"].toObject()["artifact"].toObject();
            if (artifact.isEmpty()) continue;

            DownloadTask t;
            t.url  = artifact["url"].toString().toStdString();
            t.path = (libRoot + "/" + artifact["path"].toString()).toStdString();
            t.size = artifact["size"].toInt(-1);
            t.sha1 = artifact["sha1"].toString().toStdString();
            libTasks.push_back(t);
        }

        // ── Phase 5: Download libraries ───────────────────────────────────
        setProgress(30, "下载依赖库 (0/" + std::to_string(libTasks.size()) + ")...");

        int libsDone = 0;
        int libsTotal = static_cast<int>(libTasks.size());

        bool libOk = batchDownload(libTasks, 16,
            [&](int done, int total) {
                libsDone = done;
                int pct  = 30 + (done * 40 / std::max(total, 1));
                setProgress(pct, "下载依赖库 (" + std::to_string(done) +
                            "/" + std::to_string(total) + ")...");
            });

        if (!libOk) {
            finish(false, "部分依赖库下载失败，请重试");
            return;
        }

        // ── Phase 6: Download asset index ─────────────────────────────────
        setProgress(72, "下载资源索引...");

        QJsonObject assetIdx  = vObj["assetIndex"].toObject();
        QString assetIndexId  = assetIdx["id"].toString();
        QString assetIndexUrl = assetIdx["url"].toString();
        int     assetIdxSize  = assetIdx["size"].toInt(-1);
        QString assetIdxSha   = assetIdx["sha1"].toString();

        const QString assetsRoot = QString::fromStdString(workDir) + "/assets";
        const QString assetIdxDir = assetsRoot + "/indexes";
        QDir().mkpath(assetIdxDir);

        const QString assetIdxPath = assetIdxDir + "/" + assetIndexId + ".json";
        if (!downloadFile(assetIndexUrl.toStdString(), assetIdxPath.toStdString(),
                          assetIdxSize, assetIdxSha.toStdString()))
        {
            finish(false, "资源索引下载失败");
            return;
        }

        // ── Phase 7: Download assets ──────────────────────────────────────
        setProgress(75, "解析资源列表...");

        QFile af(assetIdxPath);
        std::vector<DownloadTask> assetTasks;
        if (af.open(QIODevice::ReadOnly)) {
            QJsonObject objects = QJsonDocument::fromJson(af.readAll())
                                      .object()["objects"].toObject();
            af.close();
            const QString objRoot = assetsRoot + "/objects";
            for (const QString& key : objects.keys()) {
                QJsonObject obj = objects[key].toObject();
                QString hash    = obj["hash"].toString();
                int     size    = obj["size"].toInt(-1);
                QString prefix  = hash.left(2);
                QString destDir = objRoot + "/" + prefix;
                QDir().mkpath(destDir);

                DownloadTask t;
                t.url  = ("https://resources.download.minecraft.net/" +
                           prefix + "/" + hash).toStdString();
                t.path = (destDir + "/" + hash).toStdString();
                t.size = size;
                t.sha1 = hash.toStdString();
                assetTasks.push_back(t);
            }
        }

        setProgress(78, "下载游戏资源 (0/" + std::to_string(assetTasks.size()) + ")...");

        bool assetsOk = batchDownload(assetTasks, 32,
            [&](int done, int total) {
                int pct = 78 + (done * 20 / std::max(total, 1));
                setProgress(pct, "下载游戏资源 (" + std::to_string(done) +
                            "/" + std::to_string(total) + ")...");
            });

        if (!assetsOk) {
            finish(false, "部分资源文件下载失败，请重试");
            return;
        }

        setProgress(100, "完成");
        finish(true);
    });
}
