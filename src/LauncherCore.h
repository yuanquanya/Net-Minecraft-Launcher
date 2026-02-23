#ifndef LAUNCHERCORE_H
#define LAUNCHERCORE_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QProcess>
#include <QThreadPool>
#include <QtConcurrent>
#include <QPointer>
#include <QMutex>
#include <QReadWriteLock>
#include <vector>
#include <string>

// ════════════════════════════════════════════════════════════════════════════
// Launch Context – carries all state through the 8-step launch pipeline
// ════════════════════════════════════════════════════════════════════════════

enum class ProcessPriority {
    Normal,
    High,
    Low
};

struct LaunchContext {
    std::string versionId;
    std::string username;
    std::string uuid;
    std::string accessToken;
    int         maxMemory = 2048;

    QJsonObject versionManifest;
    QString     javaPath;
    QString     nativesDir;
    QString     classPath;

    std::vector<std::string> jvmArgs;   // JVM-only args (pre-mainClass)
    std::vector<std::string> gameArgs;  // Full flattened list

    QString            customPreLaunchCommand;
    ProcessPriority    processPriority = ProcessPriority::Normal;
    QPointer<QProcess> process;
    qint64             pid = 0;
};

struct MinecraftVersion {
    std::string id;
    std::string type;
    std::string url;
};

// ════════════════════════════════════════════════════════════════════════════
// JavaEntry – result of one JavaSearchLoader probe
// ════════════════════════════════════════════════════════════════════════════

struct JavaEntry {
    QString path;           // Absolute path to java(w).exe / java
    int     majorVersion = 0;
    QString arch;           // "x64" | "x86" | "arm64"
    QString vendor;         // e.g. "OpenJDK", "Eclipse Temurin"
    bool    isLauncher = false; // true  = managed by us (inside runtime/)
    bool    isValid    = false; // false = -version probe failed

    bool operator==(const JavaEntry& o) const { return path == o.path; }
};

struct JavaStatus {
    bool installing = false;
    int progress = 0;
    std::string statusMsg;
    bool success = false;
    std::string error;
};

// ════════════════════════════════════════════════════════════════════════════
// LauncherCore
// ════════════════════════════════════════════════════════════════════════════

class LauncherCore : public QObject {
    Q_OBJECT
public:
    explicit LauncherCore(QObject* parent = nullptr);
    ~LauncherCore();

    void init(const std::string& workDir);

    // ── Version list ─────────────────────────────────────────────────────────
    std::vector<MinecraftVersion> getVersionList();
    int getRecommendedJavaVersion(const std::string& versionId);

    // ── Game launch ──────────────────────────────────────────────────────────
    // Returns: 0 = OK, 1 = generic error, 2 = Java missing
    int launchGame(const std::string& versionId,
                   const std::string& username,
                   int maxMemory,
                   const QString& customPreLaunchCommand = {},
                   ProcessPriority priority = ProcessPriority::Normal);

    // ════════════════════════════════════════════════════════════════════════
    // Java management  (mirrors PCL2 ModJava.vb)
    // ════════════════════════════════════════════════════════════════════════

    JavaStatus getJavaStatus() const;

    // ── JavaSearchLoader ─────────────────────────────────────────────────────
    // Scans all well-known directories (including our own runtime/), probes
    // each candidate with `java -version`, and rebuilds the internal list.
    // Async variant – emits javaListReady() when done.
    void refreshJavaList();
    // Blocking variant – returns the list directly (call from worker threads).
    QVector<JavaEntry> refreshJavaListSync();
    // Thread-safe snapshot of the last completed scan.
    QVector<JavaEntry> getJavaList() const;
    // Best entry for a required major version; isValid=false if not found.
    JavaEntry findBestJava(int majorVersion) const;

    // ── GetJavaDownloadLoader ─────────────────────────────────────────────────
    // Async, 3-phase pipeline that mirrors PCL2's GetJavaDownloadLoader:
    //
    //   Phase 1 – JavaFileList:
    //     • Fetch piston-meta all.json (or BMCLAPI mirror).
    //     • Select component for the requested major version and OS arch.
    //     • Download and parse the component manifest → DownloadTask list.
    //
    //   Phase 2 – JavaDownloadLoader:
    //     • Batch-download all files with 32 parallel threads.
    //     • Mirror: piston-data.mojang.com → bmclapi2.bangbang93.com.
    //     • Per-file SHA1 + size validation.
    //     • On any failure → delete target dir (prevent corrupt install).
    //
    //   Phase 3 – JavaSearchLoader:
    //     • Call refreshJavaListSync() to detect the newly installed runtime.
    //     • Emit javaListReady() so the UI can refresh its Java picker.
    //
    // Signals emitted: javaPhaseChanged, javaProgress, javaFinished, javaListReady.
    void installJava(int majorVersion);

    // ── Download infrastructure ───────────────────────────────────────────────
    struct DownloadTask {
        std::string url;
        std::string path;
        int         size   = -1;
        std::string sha1;
        bool        extract       = false;
        std::string extractTarget;
    };

    bool batchDownload(const std::vector<DownloadTask>& tasks,
                       int maxThreads = 32,
                       std::function<void(int /*done*/, int /*total*/)> progressCallback = nullptr);

signals:
    // ── Java install signals ─────────────────────────────────────────────────
    // phase: 1=FileList, 2=Download, 3=Search
    void javaPhaseChanged(int phase, QString phaseName);
    void javaProgress(int percent, QString message);
    void javaFinished(bool success, QString error);

    // ── Java search signal ────────────────────────────────────────────────────
    void javaListReady(QVector<JavaEntry> entries);

    // ── Launch signals ────────────────────────────────────────────────────────
    void launchLog(QString message);
    void gameStarted();
    void gameWindowReady();
    void gameExited(int exitCode);

private:
    std::string            workDir;
    QNetworkAccessManager* networkManager;

    // Protected by javaListLock (many readers, one writer)
    mutable QReadWriteLock javaListLock;
    QVector<JavaEntry>     javaList;

    JavaStatus m_javaStatus;
    mutable QMutex m_javaStatusLock;

    // ── Java install manifest entry ───────────────────────────────────────────
    struct JavaManifestFile {
        QString path;   // Relative destination path within the runtime dir
        QString url;
        QString sha1;
        int     size = -1;
    };

    // ── JavaSearchLoader internals ────────────────────────────────────────────
    // Probe one java(w).exe; returns an entry with isValid=false on failure.
    JavaEntry probeJavaEntry(const QString& execPath, bool isLauncher) const;
    // Walk a directory tree and collect valid JavaEntry objects.
    void scanDirForJava(const QString& baseDir, bool isLauncher,
                        QVector<JavaEntry>& out) const;

    // ── JavaFileList internals ────────────────────────────────────────────────
    QString majorVersionToComponent(int majorVersion) const;
    QString getCurrentJavaPlatform() const;
    // Fetches all.json, navigates to the component entry, returns manifest URL.
    QString fetchManifestUrl(const QString& component, QNetworkAccessManager* nam);
    // Parses the component manifest JSON into a file list.
    QVector<JavaManifestFile> parseManifestFiles(const QByteArray& data);

    // ── JavaDownloadLoader internals ─────────────────────────────────────────
    // Build mirror-prioritised URL list and apply mirror substitution.
    QStringList buildMirrorUrls(const QString& originalUrl) const;

    // Resolve the cached java path for the launch pipeline.
    QString findJavaPath(int majorVersion) const;

    // ── Manifest / Version ────────────────────────────────────────────────────
    QJsonObject getVersionManifest(const std::string& versionId);
    bool evaluateRules(const QJsonArray& rules) const;

    // ── File / Network ────────────────────────────────────────────────────────
    std::string calculateFileSha1(const std::string& filepath);
    bool validateFile(const std::string& filepath, int size, const std::string& sha1);
    bool extractNative(const std::string& archivePath, const std::string& targetDir);

    QByteArray httpGet(const std::string& url,
                       bool* success = nullptr,
                       QNetworkAccessManager* nam = nullptr);

    bool downloadFile(const std::string& url,
                      const std::string& filepath,
                      int size = -1,
                      const std::string& sha1 = "",
                      QNetworkAccessManager* nam = nullptr);

    // ── Launch pipeline steps ─────────────────────────────────────────────────
    bool stepCheckJava(LaunchContext& ctx);
    bool stepFixFiles(LaunchContext& ctx);
    bool stepExtractNatives(LaunchContext& ctx);
    bool stepConstructArguments(LaunchContext& ctx);
    bool stepPreRun(LaunchContext& ctx);
    bool stepCustomCommands(LaunchContext& ctx);
    bool stepLaunch(LaunchContext& ctx);
    void stepWait(LaunchContext& ctx);
};

#endif // LAUNCHERCORE_H
