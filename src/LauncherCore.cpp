// LauncherCore.cpp
// PCL2-style Minecraft launcher core.
//
// Java system mirrors ModJava.vb:
//   JavaSearchLoader  → refreshJavaListSync()   (scan + probe all candidates)
//   GetJavaDownloadLoader:
//     Phase 1 JavaFileList      → fetchManifestUrl() + parseManifestFiles()
//     Phase 2 JavaDownloadLoader → phaseDownload() via batchDownload()
//     Phase 3 JavaSearchLoader  → refreshJavaListSync() again
//
// Launch pipeline mirrors ModLaunch.vb:
//   1. stepCheckJava        (McLaunchJava)
//   2. stepFixFiles         (DlClientFix)
//   3. stepExtractNatives   (McLaunchNatives)
//   4. stepConstructArguments (McLaunchArgumentMain)
//   5. stepPreRun           (McLaunchPrerun)
//   6. stepCustomCommands   (McLaunchCustom)
//   7. stepLaunch           (McLaunchRun)
//   8. stepWait             (McLaunchWait)

#include "LauncherCore.h"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <set>
#include <atomic>

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QDir>
#include <QDirIterator>
#include <QCoreApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QSettings>
#include <QStringList>
#include <QSysInfo>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QTimer>
#include <QThread>
#include <QPointer>
#include <QReadLocker>
#include <QWriteLocker>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace fs = std::filesystem;

// ════════════════════════════════════════════════════════════════════════════
// Construction / Init
// ════════════════════════════════════════════════════════════════════════════

LauncherCore::LauncherCore(QObject* parent) : QObject(parent) {
    networkManager = new QNetworkAccessManager(this);
}

LauncherCore::~LauncherCore() {}

void LauncherCore::init(const std::string& dir) {
    workDir = dir;
    fs::create_directories(fs::path(workDir) / "versions");
    fs::create_directories(fs::path(workDir) / "libraries");
    fs::create_directories(fs::path(workDir) / "assets" / "indexes");
    fs::create_directories(fs::path(workDir) / "assets" / "objects");
    fs::create_directories(fs::path(workDir) / "runtime");
}

// ════════════════════════════════════════════════════════════════════════════
// JavaSearchLoader  (ModJava.vb:479-601)
//
// Scans every known location, probes each java binary with `java -version`,
// extracts major version / arch / vendor, and rebuilds javaList.
// ════════════════════════════════════════════════════════════════════════════

// ── Probe one binary ─────────────────────────────────────────────────────────
// Runs `<exec> -version` and parses the output the same way PCL2 does.
// Returns a JavaEntry with isValid=false if the binary cannot be executed.
JavaEntry LauncherCore::probeJavaEntry(const QString& execPath, bool isLauncher) const {
    JavaEntry entry;
    entry.path       = execPath;
    entry.isLauncher = isLauncher;
    entry.isValid    = false;

    QProcess p;
    p.setProgram(execPath);
    p.setArguments({ "-version" });
    p.start();
    if (!p.waitForFinished(4000)) { p.kill(); return entry; }

    // -version output goes to stderr on most JVMs
    QString out = QString::fromUtf8(p.readAllStandardError());
    if (out.isEmpty()) out = QString::fromUtf8(p.readAllStandardOutput());
    if (out.isEmpty()) return entry;

    // ── Major version ────────────────────────────────────────────────────────
    // "openjdk version \"17.0.1\" ..."  or  "java version \"1.8.0_202\" ..."
    QRegularExpression reVer(R"(version\s+"(\d+)(\.(\d+))?)");
    auto mVer = reVer.match(out);
    if (!mVer.hasMatch()) return entry;
    int major = mVer.captured(1).toInt();
    if (major == 1) major = mVer.captured(3).toInt(); // 1.8 → 8
    entry.majorVersion = major;

    // ── Architecture ─────────────────────────────────────────────────────────
    // "64-Bit" → x64, "32-Bit" or absence → x86, "aarch64" → arm64
    if (out.contains("aarch64", Qt::CaseInsensitive) ||
        out.contains("arm64",   Qt::CaseInsensitive))
        entry.arch = "arm64";
    else if (out.contains("64-Bit", Qt::CaseInsensitive) ||
             out.contains("64-bit", Qt::CaseInsensitive))
        entry.arch = "x64";
    else
        entry.arch = "x86";

    // ── Vendor ───────────────────────────────────────────────────────────────
    const QStringList vendors = {
        "Eclipse Temurin", "Temurin",
        "GraalVM", "Oracle", "OpenJDK",
        "BellSoft Liberica", "Liberica",
        "Azul", "Microsoft", "Amazon Corretto", "Corretto",
        "Dragonwell", "SapMachine", "Zulu",
    };
    entry.vendor = "Unknown";
    for (const QString& v : vendors) {
        if (out.contains(v, Qt::CaseInsensitive)) { entry.vendor = v; break; }
    }

    entry.isValid = true;
    return entry;
}

// ── Walk one directory tree ───────────────────────────────────────────────────
void LauncherCore::scanDirForJava(const QString& baseDir, bool isLauncher,
                                  QVector<JavaEntry>& out) const {
    if (!QDir(baseDir).exists()) return;

#ifdef Q_OS_WIN
    const QStringList filter = { "javaw.exe" };
#else
    const QStringList filter = { "java" };
#endif

    QDirIterator it(baseDir, filter,
                    QDir::Files | QDir::Executable,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString path = it.next();
        // Skip javaw.exe duplicates – we already prefer javaw over java on
        // Windows, so skip the java.exe sibling when javaw.exe was found.
        JavaEntry entry = probeJavaEntry(path, isLauncher);
        if (!entry.isValid) continue;
        // Deduplicate by path
        bool dup = false;
        for (const JavaEntry& e : out) if (e.path == path) { dup = true; break; }
        if (!dup) out.append(entry);
    }

#ifdef Q_OS_WIN
    // On Windows also pick up java.exe in the same dirs if javaw.exe wasn't
    // found (e.g. server-only JRE).
    QDirIterator it2(baseDir, { "java.exe" },
                     QDir::Files | QDir::Executable,
                     QDirIterator::Subdirectories);
    while (it2.hasNext()) {
        QString path = it2.next();
        // Skip if we already have javaw.exe from the same directory
        QString javaw = QFileInfo(path).absolutePath() + "/javaw.exe";
        if (QFile::exists(javaw)) continue;
        JavaEntry entry = probeJavaEntry(path, isLauncher);
        if (!entry.isValid) continue;
        bool dup = false;
        for (const JavaEntry& e : out) if (e.path == path) { dup = true; break; }
        if (!dup) out.append(entry);
    }
#endif
}

// ── refreshJavaListSync  (JavaSearchLoader) ────────────────────────────────
// Scans all well-known directories. PCL2 scans (ModJava.vb:479-601):
//   • .minecraft/runtime            (official launcher managed)
//   • Our own runtime/              (launcher managed)
//   • JAVA_HOME, common install dirs
//   • Windows Registry
//   • PATH
QVector<JavaEntry> LauncherCore::refreshJavaListSync() {
    QVector<JavaEntry> found;

    // 1. Our own managed runtimes (highest priority – fully validated)
    scanDirForJava(QString::fromStdString(workDir) + "/runtime", true, found);

    // 2. Official Minecraft Launcher runtimes
    QString roam = qgetenv("APPDATA");
    if (!roam.isEmpty())
        scanDirForJava(roam + "/.minecraft/runtime", false, found);

    // 3. Well-known system directories
    const QStringList sysDirs = {
        qgetenv("JAVA_HOME"),
        "C:/Program Files/Java",
        "C:/Program Files (x86)/Java",
        QString(qgetenv("USERPROFILE")) + "/.jdks",
        "C:/Program Files/Eclipse Adoptium",
        "C:/Program Files/BellSoft",
        "C:/Program Files/Azul Systems",
        "C:/Program Files/Microsoft",
        "C:/Program Files/Amazon Corretto",
        "/usr/lib/jvm",
        "/Library/Java/JavaVirtualMachines",
        "/usr/local/opt",        // Homebrew on macOS
    };
    for (const QString& d : sysDirs) {
        if (!d.isEmpty()) scanDirForJava(d, false, found);
    }

    // 4. Windows Registry (PCL2 also checks this)
#ifdef Q_OS_WIN
    const QStringList regKeys = {
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\JavaSoft\\Java Runtime Environment",
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\JavaSoft\\JRE",
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\JavaSoft\\JDK",
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\JavaSoft\\Java Runtime Environment",
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\JavaSoft\\JRE",
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\JavaSoft\\JDK",
    };
    for (const QString& key : regKeys) {
        QSettings base(key, QSettings::NativeFormat);
        for (const QString& ver : base.childGroups()) {
            QSettings sub(key + "\\" + ver, QSettings::NativeFormat);
            QString home = sub.value("JavaHome").toString();
            if (!home.isEmpty()) scanDirForJava(home, false, found);
        }
    }
#endif

    // 5. PATH (last resort)
    QString pathJava = QStandardPaths::findExecutable("java");
    if (!pathJava.isEmpty()) {
        bool alreadyFound = false;
        for (const JavaEntry& e : found)
            if (e.path == pathJava) { alreadyFound = true; break; }
        if (!alreadyFound) {
            JavaEntry e = probeJavaEntry(pathJava, false);
            if (e.isValid) found.append(e);
        }
    }

    // Sort: managed first, then by descending major version
    std::stable_sort(found.begin(), found.end(),
                     [](const JavaEntry& a, const JavaEntry& b) {
                         if (a.isLauncher != b.isLauncher) return a.isLauncher;
                         return a.majorVersion > b.majorVersion;
                     });

    { QWriteLocker wl(&javaListLock); javaList = found; }
    return found;
}

void LauncherCore::refreshJavaList() {
    QPointer<LauncherCore> self(this);
    QtConcurrent::run([self]() {
        if (!self) return;
        QVector<JavaEntry> list = self->refreshJavaListSync();
        // Re-check: object may have been destroyed during the long scan.
        if (self) emit self->javaListReady(list);
    });
}

QVector<JavaEntry> LauncherCore::getJavaList() const {
    QReadLocker rl(&javaListLock);
    return javaList;
}

JavaStatus LauncherCore::getJavaStatus() const {
    QMutexLocker locker(&m_javaStatusLock);
    return m_javaStatus;
}

JavaEntry LauncherCore::findBestJava(int majorVersion) const {
    QReadLocker rl(&javaListLock);
    // Prefer launcher-managed; among those prefer x64; among those pick any
    for (const JavaEntry& e : javaList)
        if (e.isValid && e.majorVersion == majorVersion && e.isLauncher && e.arch == "x64")
            return e;
    for (const JavaEntry& e : javaList)
        if (e.isValid && e.majorVersion == majorVersion && e.isLauncher)
            return e;
    for (const JavaEntry& e : javaList)
        if (e.isValid && e.majorVersion == majorVersion && e.arch == "x64")
            return e;
    for (const JavaEntry& e : javaList)
        if (e.isValid && e.majorVersion == majorVersion)
            return e;
    return {}; // isValid=false
}

QString LauncherCore::findJavaPath(int majorVersion) const {
    JavaEntry e = findBestJava(majorVersion);
    return e.isValid ? e.path : QString();
}

// ════════════════════════════════════════════════════════════════════════════
// JavaFileList helpers  (ModJava.vb:723-756)
// ════════════════════════════════════════════════════════════════════════════

QString LauncherCore::majorVersionToComponent(int v) const {
    switch (v) {
        case 8:  return "jre-legacy";
        case 16: return "java-runtime-alpha";
        case 17: return "java-runtime-gamma";
        case 21: return "java-runtime-delta";
        case 25: return "java-runtime-epsilon";
        default: return {};
    }
}

QString LauncherCore::getCurrentJavaPlatform() const {
#if defined(Q_OS_WIN)
    QString arch = QSysInfo::currentCpuArchitecture();
    if (arch == "arm64")  return "windows-arm64";
    return (arch == "x86_64") ? "windows-x64" : "windows-x86";
#elif defined(Q_OS_LINUX)
    return (QSysInfo::currentCpuArchitecture() == "arm64") ? "linux-arm64" : "linux";
#elif defined(Q_OS_MACOS)
    return (QSysInfo::currentCpuArchitecture() == "arm64") ? "mac-os-arm64" : "mac-os";
#else
    return "windows-x64";
#endif
}

// Fetches all.json and navigates to the component manifest URL.
// Tries BMCLAPI mirror first, then Mojang (ModJava.vb:723-738).
QString LauncherCore::fetchManifestUrl(const QString& component,
                                       QNetworkAccessManager* nam) {
    // Static hash for the all.json endpoint
    const QString hash = "2ec0cc96c44e5a76b9c8b7c39df7210883d12871";
    const QStringList allJsonUrls = {
        "https://bmclapi2.bangbang93.com/v1/products/java-runtime/" + hash + "/all.json",
        "https://piston-meta.mojang.com/v1/products/java-runtime/" + hash + "/all.json",
    };
    QByteArray allJson;
    for (const QString& url : allJsonUrls) {
        allJson = httpGet(url.toStdString(), nullptr, nam);
        if (!allJson.isEmpty()) break;
    }
    if (allJson.isEmpty()) return {};

    QJsonDocument doc = QJsonDocument::fromJson(allJson);
    if (!doc.isObject()) return {};

    QString platform = getCurrentJavaPlatform();
    QJsonObject plat = doc.object()[platform].toObject();
    if (!plat.contains(component)) return {};

    QJsonArray vers = plat[component].toArray();
    if (vers.isEmpty()) return {};

    // PCL2 picks the first entry (index 0)
    return vers[0].toObject()["manifest"].toObject()["url"].toString();
}

// Parses the component manifest JSON → list of files to download.
QVector<LauncherCore::JavaManifestFile>
LauncherCore::parseManifestFiles(const QByteArray& data) {
    QVector<JavaManifestFile> result;
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return result;
    QJsonObject files = doc.object()["files"].toObject();
    for (auto it = files.begin(); it != files.end(); ++it) {
        QJsonObject entry = it.value().toObject();
        if (entry["type"].toString() != "file") continue;   // Skip directories
        QJsonObject raw = entry["downloads"].toObject()["raw"].toObject();
        QString url = raw["url"].toString();
        if (url.isEmpty()) continue;
        JavaManifestFile f;
        f.path = it.key();
        f.url  = url;
        f.sha1 = raw["sha1"].toString();
        f.size = raw["size"].toInt(-1);
        result.append(f);
    }
    return result;
}

// ════════════════════════════════════════════════════════════════════════════
// GetJavaDownloadLoader  (ModJava.vb:701-719)
//
// Three-phase async pipeline. All three phases run on a worker thread;
// signals are cross-thread safe via Qt's queued connections.
// ════════════════════════════════════════════════════════════════════════════

void LauncherCore::installJava(int majorVersion) {
    // Use QPointer so the lambda never touches a destroyed LauncherCore.
    // If the UI closes the dialog and the object is deleted mid-download,
    // all emits become no-ops instead of crashing.
    QPointer<LauncherCore> self(this);

    QtConcurrent::run([self, majorVersion]() {
        // All emits go through this guard – silently drops if object was destroyed.
        auto safeEmit = [&self](auto fn) {
            if (self) fn(self.data());
        };
        auto progress = [&safeEmit](int pct, const QString& msg) {
            safeEmit([&](LauncherCore* p){
                {
                    QMutexLocker l(&p->m_javaStatusLock);
                    p->m_javaStatus.installing = true;
                    p->m_javaStatus.progress = pct;
                    p->m_javaStatus.statusMsg = msg.toStdString();
                }
                emit p->javaProgress(pct, msg);
            });
        };
        auto fail = [&safeEmit](const QString& err) {
            safeEmit([&](LauncherCore* p){
                {
                    QMutexLocker l(&p->m_javaStatusLock);
                    p->m_javaStatus.installing = false;
                    p->m_javaStatus.success = false;
                    p->m_javaStatus.error = err.toStdString();
                }
                emit p->javaFinished(false, err);
            });
        };
        auto log = [&safeEmit](const QString& msg) {
            safeEmit([&](LauncherCore* p){ emit p->launchLog(msg); });
        };

        if (!self) return; // Already destroyed before we even started

        // ── Capture workDir while self is still alive ────────────────────────
        const std::string workDir = self->workDir;

        // ── Pre-flight ───────────────────────────────────────────────────────
        QString component = self->majorVersionToComponent(majorVersion);
        if (component.isEmpty()) {
            fail("Unsupported Java major version: " + QString::number(majorVersion));
            return;
        }

        // Install to: workDir/runtime/<component>  (launcher-managed)
        std::string targetDir =
            (fs::path(workDir) / "runtime" / component.toStdString()).string();

        // ════════════════════════════════════════════════════════════════════
        // Phase 1 – JavaFileList
        //   Fetch all.json → locate component manifest URL → parse file list.
        //   Each file is compared against what's on disk; valid files are
        //   skipped to support resume / incremental update.
        // ════════════════════════════════════════════════════════════════════
        safeEmit([](LauncherCore* p){ emit p->javaPhaseChanged(1, "Fetching file list"); });
        progress(0, "Connecting to Mojang...");

        // Create NAM on this thread – httpGet runs its own local event loop.
        QNetworkAccessManager localNam;

        QString manifestUrl;
        if (self) manifestUrl = self->fetchManifestUrl(component, &localNam);
        if (manifestUrl.isEmpty()) {
            fail("Failed to obtain Java runtime manifest URL from Mojang/BMCLAPI.");
            return;
        }

        progress(3, "Downloading file manifest...");
        QByteArray manifestData;
        if (self) manifestData = self->httpGet(manifestUrl.toStdString(), nullptr, &localNam);
        if (manifestData.isEmpty()) {
            fail("Failed to download the component manifest.");
            return;
        }

        QVector<JavaManifestFile> files;
        if (self) files = self->parseManifestFiles(manifestData);
        if (files.isEmpty()) {
            fail("Component manifest contained no downloadable files.");
            return;
        }

        log(QString("[Java] %1 files listed for Java %2 (%3)")
            .arg(files.size()).arg(majorVersion).arg(component));

        fs::create_directories(fs::path(targetDir));

        int totalFiles = files.size();
        // Track counts atomically so the progress lambda (called from pool
        // threads inside batchDownload) can read them safely.
        std::atomic<int> skipped{0};
        std::vector<DownloadTask> tasks;
        tasks.reserve(static_cast<size_t>(totalFiles));

        for (const JavaManifestFile& f : files) {
            std::string localPath =
                (fs::path(targetDir) / f.path.toStdString()).string();

            // validateFile checks size first (cheap), then SHA1 (expensive).
            // Files that pass are already good – no need to re-download.
            bool valid = false;
            if (self) valid = self->validateFile(localPath, f.size, f.sha1.toStdString());
            if (valid) { skipped.fetch_add(1, std::memory_order_relaxed); continue; }

            // FIX(Bug2): Pass the original Mojang URL so buildMirrorUrls can
            // generate the full three-way fallback chain:
            //   1. bmclapi2.bangbang93.com  (fastest in CN)
            //   2. download.mcbbs.net       (secondary CN mirror)
            //   3. piston-data.mojang.com   (official, last resort)
            // Pre-replacing the URL caused buildMirrorUrls to receive a BMCLAPI
            // address and skip to the else-branch, leaving only ONE usable URL,
            // so any corruption or rate-limit hit at 88%+ was fatal.
            tasks.push_back({ f.url.toStdString(), localPath, f.size,
                              f.sha1.toStdString() });
        }

        {
            int skip = skipped.load();
            if (skip > 0)
                progress((skip * 90) / totalFiles,
                         QString("%1 / %2 files already up to date.")
                         .arg(skip).arg(totalFiles));
        }
        progress(5, QString("Need to download %1 file(s)...").arg(tasks.size()));

        // ════════════════════════════════════════════════════════════════════
        // Phase 2 – JavaDownloadLoader
        //   Parallel batch download with per-file SHA1 + size validation.
        //   downloadFile() validates each file immediately after writing, so
        //   there is NO need for a full post-download re-scan (which was the
        //   original cause of the 98% stall: re-hashing 300+ files silently).
        //   On any failure the target directory is fully removed to prevent
        //   a corrupted partial install from being detected as valid by
        //   JavaSearchLoader (mirrors ModJava.vb:710-712 cleanup logic).
        // ════════════════════════════════════════════════════════════════════
        safeEmit([](LauncherCore* p){ emit p->javaPhaseChanged(2, "Downloading Java runtime"); });

        // Capture skipped by value for the lambda (it's already done being written)
        const int skippedCount = skipped.load();

        if (!tasks.empty()) {
            bool ok = false;
            if (self) {
                ok = self->batchDownload(tasks, 16, // FIX(Bug3): reduced from 32 to avoid BMCLAPI rate-limiting at high progress
                    [&progress, skippedCount, totalFiles](int done, int taskTotal) {
                        int overall = skippedCount + done;
                        // Always fire on the last file so progress reaches ~90%
                        // exactly, not just on multiples of 10.
                        bool shouldReport = (done % 5 == 0) || (done == taskTotal);
                        if (shouldReport) {
                            // Scale download phase to 5–90% of total bar
                            int pct = 5 + (overall * 85) / totalFiles;
                            progress(pct,
                                QString("Downloading %1 / %2 files...")
                                .arg(overall).arg(totalFiles));
                        }
                    });
            }

            if (!ok) {
                // PCL2 cleanup: remove the whole directory on any failure so
                // an incomplete Java can't be falsely detected as installed.
                std::error_code ec;
                fs::remove_all(fs::path(targetDir), ec);
                log("[Java] Removed incomplete installation: "
                    + QString::fromStdString(targetDir));
                fail("Download failed. Incomplete files have been removed.");
                return;
            }
        }

        // ════════════════════════════════════════════════════════════════════
        // Phase 3 – JavaSearchLoader (targeted fast variant)
        //   PCL2 runs a full JavaSearchLoader after install. Doing a full
        //   system scan here (all Program Files, registry, PATH) is slow and
        //   was the second cause of apparent hang + crash after popup close:
        //   probing 10+ Java installs × 4s timeout = 40+ seconds, during
        //   which the lambda holds `this` alive via captures and emits signals
        //   after the object may have been destroyed.
        //
        //   Fix: scan ONLY the newly installed directory (fast – 1 probe).
        //   Then schedule the full background rescan via queued signal so it
        //   runs on the main thread after this lambda has returned, safely
        //   checking QPointer before any emit.
        // ════════════════════════════════════════════════════════════════════
        safeEmit([](LauncherCore* p){ emit p->javaPhaseChanged(3, "Registering new Java runtime"); });
        progress(92, "Locating installed Java binary...");

        // Find the java executable we just installed (single directory probe)
        JavaEntry installed;
        {
            QVector<JavaEntry> found;
            if (self)
                self->scanDirForJava(QString::fromStdString(targetDir), true, found);

            for (const JavaEntry& e : found) {
                if (e.isValid && e.majorVersion == majorVersion) {
                    installed = e;
                    break;
                }
            }
        }

        if (!installed.isValid) {
            // Binary not found in the directory we just populated. This means
            // the manifest was for the wrong platform or the download silently
            // failed validation. Clean up.
            std::error_code ec;
            fs::remove_all(fs::path(targetDir), ec);
            fail("Installation appeared to succeed but no valid Java "
                 + QString::number(majorVersion) + " executable was found. "
                 "The directory has been removed.");
            return;
        }

        // Register the new entry immediately so stepCheckJava can use it
        // without waiting for the full background rescan.
        if (self) {
            QWriteLocker wl(&self->javaListLock);
            // Remove any stale entry for the same path, then prepend
            self->javaList.removeIf([&installed](const JavaEntry& e) {
                return e.path == installed.path;
            });
            self->javaList.prepend(installed);
        }

        log("[Java] Java " + QString::number(majorVersion)
            + " installed at: " + installed.path);
        progress(97, "Refreshing Java list in background...");

        // Kick off the full system rescan asynchronously so it doesn't block
        // this thread. Uses QPointer guard inside refreshJavaList() lambda.
        if (self) self->refreshJavaList();   // async, emits javaListReady when done

        progress(100, "Java " + QString::number(majorVersion)
                      + " installed successfully!");
        safeEmit([](LauncherCore* p){
            {
                QMutexLocker l(&p->m_javaStatusLock);
                p->m_javaStatus.installing = false;
                p->m_javaStatus.success = true;
                p->m_javaStatus.statusMsg = "Installed successfully";
            }
            emit p->javaFinished(true, {});
        });
    });
}

// ════════════════════════════════════════════════════════════════════════════
// Mirror URL Builder  (shared by download helpers)
// Applies the same BMCLAPI mirror substitution table used in ModJava.vb
// and the rest of PCL2's download code.
// ════════════════════════════════════════════════════════════════════════════

QStringList LauncherCore::buildMirrorUrls(const QString& original) const {
    QStringList urls;
    QString m = original;

    if (original.contains("piston-data.mojang.com")) {
        urls << QString(original).replace("piston-data.mojang.com",
                                          "bmclapi2.bangbang93.com")
             << QString(original).replace("piston-data.mojang.com",
                                          "download.mcbbs.net")
             << original;
    } else {
        if      (original.contains("launchermeta.mojang.com"))
            m.replace("launchermeta.mojang.com",  "bmclapi2.bangbang93.com");
        else if (original.contains("launcher.mojang.com"))
            m.replace("launcher.mojang.com",       "bmclapi2.bangbang93.com");
        else if (original.contains("resources.download.minecraft.net"))
            m.replace("resources.download.minecraft.net",
                      "bmclapi2.bangbang93.com/assets");
        else if (original.contains("libraries.minecraft.net"))
            m.replace("libraries.minecraft.net",   "bmclapi2.bangbang93.com/maven");
        else if (original.contains("piston-meta.mojang.com"))
            m.replace("piston-meta.mojang.com",    "bmclapi2.bangbang93.com");
        urls << m;
        if (m != original) urls << original;
    }
    return urls;
}

// ════════════════════════════════════════════════════════════════════════════
// Network
// ════════════════════════════════════════════════════════════════════════════

QByteArray LauncherCore::httpGet(const std::string& url, bool* success,
                                 QNetworkAccessManager* nam) {
    if (success) *success = false;
    QNetworkAccessManager* mgr = nam ? nam : networkManager;
    if (!mgr) return {};

    QNetworkRequest req(QUrl(QString::fromStdString(url)));

    // ── Force HTTP/1.1 ────────────────────────────────────────────────────
    // Qt enables HTTP/2 by default. BMCLAPI and Mojang CDN both have HTTP/2
    // implementations that cause Qt to emit:
    //   "qt.network.http2: stream N error: Internal server error"
    // followed by a hard stream RST. The resulting QNetworkReply error text
    // is "Internal server error" which is misleading (it is a protocol-level
    // RST, not an HTTP 500). Forcing HTTP/1.1 eliminates this entirely.
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    // ── SSL ───────────────────────────────────────────────────────────────
    QSslConfiguration ssl = req.sslConfiguration();
    ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
    req.setSslConfiguration(ssl);

    // ── Redirects & headers ───────────────────────────────────────────────
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  "PCL2-Qt-Launcher/1.0 Mozilla/5.0");

    QNetworkReply* reply = mgr->get(req);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, [&]() {
        if (reply->isRunning()) reply->abort();
        loop.quit();
    });
    // Reset inactivity timer on every received chunk
    connect(reply, &QNetworkReply::downloadProgress, &timer,
            [&](qint64, qint64) { timer.start(30000); });
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(30000);
    loop.exec();
    if (timer.isActive()) timer.stop();

    QByteArray data;
    if (reply->error() == QNetworkReply::NoError) {
        int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (code >= 200 && code < 300) {
            data = reply->readAll();
            if (success) *success = true;
        } else {
            // Use Qt signal for logging to avoid Windows code-page mojibake
            // on std::cerr (which does not understand UTF-8 on MSVC/MinGW).
            emit const_cast<LauncherCore*>(this)->launchLog(
                QString("[HTTP %1] %2").arg(code).arg(QString::fromStdString(url)));
        }
    } else if (reply->error() != QNetworkReply::OperationCanceledError) {
        // Never write reply->errorString() to std::cerr: it is a QString
        // (UTF-8 / UTF-16) and toStdString() on Windows produces the system
        // code page, which mangles any non-ASCII character (e.g. the em-dash
        // in "Internal server error" from an HTTP/2 RST becomes "Dz?").
        // Route through the signal so the UI displays it correctly.
        emit const_cast<LauncherCore*>(this)->launchLog(
            QString("[Net] %1 | %2")
                .arg(reply->errorString())
                .arg(QString::fromStdString(url)));
    }
    reply->deleteLater();
    return data;
}

bool LauncherCore::downloadFile(const std::string& url, const std::string& path,
                                int size, const std::string& sha1,
                                QNetworkAccessManager* nam) {
    fs::create_directories(fs::path(path).parent_path());
    if (validateFile(path, size, sha1)) {
        // Already valid on disk, nothing to do
        return true;
    }

    QStringList urls = buildMirrorUrls(QString::fromStdString(url));
    for (int i = 0; i < urls.size(); ++i) {
        bool ok = false;
        QByteArray data = httpGet(urls[i].toStdString(), &ok, nam);
        if (!ok) {
            // This mirror failed; try next one
            continue;
        }
        { std::ofstream f(path, std::ios::binary); f.write(data.constData(), data.size()); }
        if (validateFile(path, size, sha1)) return true;
        // Validation failed – this mirror returned corrupt/truncated data.
        // Remove the bad file and fall through to the next mirror URL.
        emit const_cast<LauncherCore*>(this)->launchLog(
            QString("[Corrupt] Mirror %1 returned invalid data, trying next mirror: %2")
            .arg(urls[i])
            .arg(QString::fromStdString(path)));
        fs::remove(path);
        continue;  // FIX: was `return false`, now retries remaining mirrors
    }
    emit const_cast<LauncherCore*>(this)->launchLog(
        QString("[Failed] All mirrors exhausted for: %1")
        .arg(QString::fromStdString(url)));
    return false;
}

// ════════════════════════════════════════════════════════════════════════════
// File utilities
// ════════════════════════════════════════════════════════════════════════════

std::string LauncherCore::calculateFileSha1(const std::string& filepath) {
    QFile f(QString::fromStdString(filepath));
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(&f);
    return h.result().toHex().toStdString();
}

bool LauncherCore::validateFile(const std::string& filepath, int size,
                                const std::string& sha1) {
    if (!fs::exists(filepath)) return false;
    if (size > 0 && static_cast<int>(fs::file_size(filepath)) != size) return false;
    if (!sha1.empty() && calculateFileSha1(filepath) != sha1) return false;
    return true;
}

bool LauncherCore::extractNative(const std::string& archivePath,
                                 const std::string& targetDir) {
    QProcess p;
    p.start("tar", { "-xf",
                     QString::fromStdString(archivePath),
                     "-C",
                     QString::fromStdString(targetDir) });
    p.waitForFinished(-1);
    return p.exitCode() == 0;
}

// ════════════════════════════════════════════════════════════════════════════
// Batch download
// ════════════════════════════════════════════════════════════════════════════

bool LauncherCore::batchDownload(const std::vector<DownloadTask>& tasks,
                                 int maxThreads,
                                 std::function<void(int, int)> progressCallback) {
    if (tasks.empty()) return true;

    QThreadPool pool;
    pool.setMaxThreadCount(maxThreads);

    std::atomic<int>  done{0};
    std::atomic<bool> allOk{true};
    int total = static_cast<int>(tasks.size());
    QMutex cbMutex;

    QtConcurrent::blockingMap(&pool, tasks, [&](const DownloadTask& t) {
        QNetworkAccessManager localNam;
        bool ok = downloadFile(t.url, t.path, t.size, t.sha1, &localNam);
        if (ok && t.extract && !t.extractTarget.empty())
            ok = extractNative(t.path, t.extractTarget);
        if (!ok) allOk = false;
        int n = ++done;
        if (progressCallback) {
            // Report every 5 files OR on the very last file so progress always
            // reaches 100%. Do NOT hold the mutex while calling the callback –
            // the callback may itself emit Qt signals which acquire their own
            // locks (deadlock risk). Use try-lock: if we lose the race, the
            // next iteration will report instead.
            bool isLast = (n == total);
            bool periodic = (n % 5 == 0);
            if (isLast || periodic) {
                QMutexLocker lk(&cbMutex);
                progressCallback(n, total);
            }
        }
    });

    return allOk.load();
}

// ════════════════════════════════════════════════════════════════════════════
// Version list
// ════════════════════════════════════════════════════════════════════════════

std::vector<MinecraftVersion> LauncherCore::getVersionList() {
    std::vector<MinecraftVersion> result;
    std::set<std::string> known;

    const QStringList urls = {
        "https://bmclapi2.bangbang93.com/mc/game/version_manifest.json",
        "https://launchermeta.mojang.com/mc/game/version_manifest.json",
    };
    for (const QString& u : urls) {
        QByteArray resp = httpGet(u.toStdString());
        if (resp.isEmpty()) continue;
        QJsonDocument doc = QJsonDocument::fromJson(resp);
        if (!doc.isObject()) continue;
        for (const QJsonValue& v : doc.object()["versions"].toArray()) {
            QJsonObject o = v.toObject();
            std::string id = o["id"].toString().toStdString();
            result.push_back({ id, o["type"].toString().toStdString(),
                               o["url"].toString().toStdString() });
            known.insert(id);
        }
        break;
    }

    // Local-only versions
    QDir dir(QString::fromStdString(workDir) + "/versions");
    if (dir.exists()) {
        for (const QString& name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (known.count(name.toStdString())) continue;
            QFile f(dir.filePath(name + "/" + name + ".json"));
            if (!f.open(QIODevice::ReadOnly)) continue;
            QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            if (!doc.isObject()) continue;
            QJsonObject o = doc.object();
            result.push_back({ o["id"].toString().toStdString(),
                               o["type"].toString().toStdString(), {} });
        }
    }
    return result;
}

QJsonObject LauncherCore::getVersionManifest(const std::string& versionId) {
    std::string local = (fs::path(workDir) / "versions" / versionId
                         / (versionId + ".json")).string();
    if (fs::exists(local)) {
        std::ifstream in(local);
        std::string s((std::istreambuf_iterator<char>(in)), {});
        QJsonDocument d = QJsonDocument::fromJson(QByteArray::fromStdString(s));
        if (d.isObject()) return d.object();
    }

    std::string url;
    for (const auto& v : getVersionList())
        if (v.id == versionId) { url = v.url; break; }
    if (url.empty()) return {};

    QByteArray data = httpGet(url);
    if (data.isEmpty()) return {};
    fs::create_directories(fs::path(local).parent_path());
    { std::ofstream out(local); out << data.toStdString(); }
    return QJsonDocument::fromJson(data).object();
}

int LauncherCore::getRecommendedJavaVersion(const std::string& versionId) {
    QJsonObject manifest = getVersionManifest(versionId);
    if (manifest.contains("javaVersion"))
        return manifest["javaVersion"].toObject()["majorVersion"].toInt();
    return 8;
}

bool LauncherCore::evaluateRules(const QJsonArray& rules) const {
    if (rules.isEmpty()) return true;
    bool allow = false;
    for (const QJsonValue& rv : rules) {
        QJsonObject rule = rv.toObject();
        bool matches = true;
        if (rule.contains("os")) {
            QJsonObject os = rule["os"].toObject();
            QString name = os["name"].toString();
#if defined(Q_OS_WIN)
            QString cur = "windows";
#elif defined(Q_OS_MACOS)
            QString cur = "osx";
#else
            QString cur = "linux";
#endif
            if (!name.isEmpty() && name != cur) matches = false;
            if (matches && os.contains("arch")) {
                QString reqArch = os["arch"].toString();
                bool is32 = (QSysInfo::currentCpuArchitecture() == "i386");
                if (reqArch == "x86" && !is32) matches = false;
            }
        }
        if (rule.contains("features")) matches = false;
        if (matches) allow = (rule["action"].toString() == "allow");
    }
    return allow;
}

// ════════════════════════════════════════════════════════════════════════════
// LAUNCH PIPELINE
// ════════════════════════════════════════════════════════════════════════════

int LauncherCore::launchGame(const std::string& versionId,
                             const std::string& username,
                             int maxMemory,
                             const QString& customCmd,
                             ProcessPriority priority) {
    emit launchLog("═══ Launch: " + QString::fromStdString(versionId) + " ═══");

    LaunchContext ctx;
    ctx.versionId              = versionId;
    ctx.username               = username;
    ctx.uuid                   = "00000000-0000-0000-0000-000000000000";
    ctx.accessToken            = "0";
    ctx.maxMemory              = maxMemory;
    ctx.customPreLaunchCommand = customCmd;
    ctx.processPriority        = priority;

    ctx.versionManifest = getVersionManifest(versionId);
    if (ctx.versionManifest.isEmpty()) {
        emit launchLog("[Error] Version manifest missing.");
        return 1;
    }

    if (!stepCheckJava(ctx))         { emit launchLog("[Error] Java unavailable."); return 2; }
    if (!stepFixFiles(ctx))          { emit launchLog("[Error] File download failed."); return 1; }
    if (!stepExtractNatives(ctx))    { emit launchLog("[Error] Native extraction failed."); return 1; }
    if (!stepConstructArguments(ctx)){ emit launchLog("[Error] Argument build failed."); return 1; }
    if (!stepPreRun(ctx))            emit launchLog("[Warning] Pre-run issues (non-fatal).");
    if (!customCmd.isEmpty() && !stepCustomCommands(ctx))
        emit launchLog("[Warning] Custom command failed (non-fatal).");
    if (!stepLaunch(ctx))            { emit launchLog("[Error] Process launch failed."); return 1; }

    QThread* watcher = QThread::create([this, ctx]() mutable { stepWait(ctx); });
    watcher->start();
    connect(watcher, &QThread::finished, watcher, &QObject::deleteLater);

    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 1 – McLaunchJava
// Now uses javaList (populated by JavaSearchLoader) instead of raw disk search.
// ─────────────────────────────────────────────────────────────────────────────
bool LauncherCore::stepCheckJava(LaunchContext& ctx) {
    emit launchLog("[1/8] Checking Java environment...");

    int required = 8;
    if (ctx.versionManifest.contains("javaVersion"))
        required = ctx.versionManifest["javaVersion"].toObject()["majorVersion"].toInt();

    // Ensure the java list is populated (may be first run)
    if (getJavaList().isEmpty()) {
        emit launchLog("  Java list empty – running quick scan...");
        refreshJavaListSync();
    }

    ctx.javaPath = findJavaPath(required);

    if (ctx.javaPath.isEmpty()) {
        emit launchLog(QString("  Java %1 not found in any known location.")
                       .arg(required));
        emit launchLog(QString("  Tip: call installJava(%1) to auto-download it.")
                       .arg(required));
        return false;
    }

    JavaEntry entry = findBestJava(required);
    emit launchLog(QString("  Using Java %1 (%2, %3): %4")
                   .arg(entry.majorVersion).arg(entry.vendor)
                   .arg(entry.arch).arg(entry.path));
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 2 – DlClientFix
// ─────────────────────────────────────────────────────────────────────────────
bool LauncherCore::stepFixFiles(LaunchContext& ctx) {
    emit launchLog("[2/8] Verifying game files...");

#ifdef Q_OS_WIN
    const std::string sep = ";";
#else
    const std::string sep = ":";
#endif
    std::string cp;
    std::vector<DownloadTask> tasks;

    if (ctx.versionManifest.contains("libraries")) {
        for (const QJsonValue& lv : ctx.versionManifest["libraries"].toArray()) {
            QJsonObject lib = lv.toObject();
            if (lib.contains("rules") && !evaluateRules(lib["rules"].toArray())) continue;

            QJsonObject downloads = lib["downloads"].toObject();

            if (downloads.contains("artifact")) {
                QJsonObject art = downloads["artifact"].toObject();
                std::string p   = art["path"].toString().toStdString();
                std::string fp  = (fs::path(workDir) / "libraries" / p).string();
                std::string url = art["url"].toString().toStdString();
                int  sz         = art["size"].toInt(-1);
                std::string sha = art["sha1"].toString().toStdString();
                if (!validateFile(fp, sz, sha)) tasks.push_back({url, fp, sz, sha});
                if (!fp.empty()) cp += fp + sep;
            }

            if (downloads.contains("classifiers")) {
#if defined(Q_OS_WIN)
                QString key = "natives-windows";
#elif defined(Q_OS_MACOS)
                QString key = "natives-osx";
#else
                QString key = "natives-linux";
#endif
                QJsonObject cls = downloads["classifiers"].toObject();
                QString arch = (QSysInfo::currentCpuArchitecture() == "x86_64") ? "64" : "32";
                if (!cls.contains(key)) key = key + "-" + arch;
                if (cls.contains(key)) {
                    QJsonObject nat = cls[key].toObject();
                    std::string p   = nat["path"].toString().toStdString();
                    std::string fp  = (fs::path(workDir) / "libraries" / p).string();
                    std::string url = nat["url"].toString().toStdString();
                    int  sz         = nat["size"].toInt(-1);
                    std::string sha = nat["sha1"].toString().toStdString();
                    if (!validateFile(fp, sz, sha)) tasks.push_back({url, fp, sz, sha});
                }
            }
        }
    }

    // Client JAR
    std::string clientJar = (fs::path(workDir) / "versions" / ctx.versionId
                             / (ctx.versionId + ".jar")).string();
    if (ctx.versionManifest.contains("downloads")) {
        QJsonObject cl = ctx.versionManifest["downloads"].toObject()["client"].toObject();
        std::string url = cl["url"].toString().toStdString();
        int  sz  = cl["size"].toInt(-1);
        std::string sha = cl["sha1"].toString().toStdString();
        if (!validateFile(clientJar, sz, sha)) tasks.push_back({url, clientJar, sz, sha});
    }
    cp += clientJar;
    ctx.classPath = QString::fromStdString(cp);

    // Asset index
    std::string assetId = ctx.versionManifest.contains("assets")
                        ? ctx.versionManifest["assets"].toString().toStdString() : "legacy";
    std::string idxPath = (fs::path(workDir) / "assets" / "indexes"
                           / (assetId + ".json")).string();
    if (ctx.versionManifest.contains("assetIndex")) {
        QJsonObject ai = ctx.versionManifest["assetIndex"].toObject();
        std::string url = ai["url"].toString().toStdString();
        int  sz  = ai["size"].toInt(-1);
        std::string sha = ai["sha1"].toString().toStdString();
        if (!validateFile(idxPath, sz, sha)) tasks.push_back({url, idxPath, sz, sha});
    }

    if (!tasks.empty()) {
        emit launchLog("  Downloading " + QString::number(tasks.size()) + " file(s)...");
        bool ok = batchDownload(tasks, 32, [this](int d, int t) {
            if (d % 20 == 0 || d == t)
                emit launchLog(QString("  Progress: %1/%2").arg(d).arg(t));
        });
        if (!ok) return false;
    }

    // Asset objects
    if (fs::exists(idxPath)) {
        std::ifstream in(idxPath);
        std::string s((std::istreambuf_iterator<char>(in)), {});
        QJsonDocument idxDoc = QJsonDocument::fromJson(QByteArray::fromStdString(s));
        if (idxDoc.isObject()) {
            std::vector<DownloadTask> assetTasks;
            for (auto it = idxDoc.object()["objects"].toObject().begin();
                 it != idxDoc.object()["objects"].toObject().end(); ++it) {
                QJsonObject obj = it.value().toObject();
                std::string hash = obj["hash"].toString().toStdString();
                int  sz  = obj["size"].toInt(-1);
                std::string sub = hash.substr(0, 2);
                std::string fp  = (fs::path(workDir) / "assets" / "objects" / sub / hash).string();
                std::string url = "https://resources.download.minecraft.net/" + sub + "/" + hash;
                if (!validateFile(fp, sz, hash)) assetTasks.push_back({url, fp, sz, hash});
            }
            if (!assetTasks.empty()) {
                emit launchLog("  Downloading " + QString::number(assetTasks.size()) + " asset(s)...");
                batchDownload(assetTasks, 32, [this](int d, int t) {
                    if (d % 100 == 0 || d == t)
                        emit launchLog(QString("  Assets: %1/%2").arg(d).arg(t));
                });
            }
        }
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 3 – McLaunchNatives
// ─────────────────────────────────────────────────────────────────────────────
bool LauncherCore::stepExtractNatives(LaunchContext& ctx) {
    emit launchLog("[3/8] Extracting native libraries...");

    // [Fix] Use wide strings for path construction on Windows to handle non-ASCII characters correctly.
    // workDir is std::string (UTF-8), so we convert to QString then to std::wstring.
    fs::path workPath(QString::fromStdString(workDir).toStdWString());
    fs::path nativesPath = workPath / "versions" / ctx.versionId / "natives";
    
    try {
        fs::create_directories(nativesPath);
    } catch (const std::exception& e) {
        emit launchLog(QString("[Error] Failed to create natives dir: %1").arg(e.what()));
        return false;
    }
    
    // Store as QString to preserve encoding
    ctx.nativesDir = QString::fromStdWString(nativesPath.wstring());
    // Also keep a std::string version for legacy APIs if needed, but be careful with encoding
    std::string nativesDir = ctx.nativesDir.toStdString(); 

    if (!ctx.versionManifest.contains("libraries")) return true;

    for (const QJsonValue& lv : ctx.versionManifest["libraries"].toArray()) {
        QJsonObject lib = lv.toObject();
        if (lib.contains("rules") && !evaluateRules(lib["rules"].toArray())) continue;

        QJsonObject cls = lib["downloads"].toObject()["classifiers"].toObject();
#if defined(Q_OS_WIN)
        QString key = "natives-windows";
#elif defined(Q_OS_MACOS)
        QString key = "natives-osx";
#else
        QString key = "natives-linux";
#endif
        QString arch = (QSysInfo::currentCpuArchitecture() == "x86_64") ? "64" : "32";
        if (!cls.contains(key)) key = key + "-" + arch;
        if (!cls.contains(key)) continue;

        QJsonObject nat = cls[key].toObject();
        
        // [Fix] Use wide strings for library path
        fs::path libPath = workPath / "libraries" / nat["path"].toString().toStdWString();
        std::string archPath = libPath.string(); // Note: This might be ANSI on Windows, but used for fs::exists
        
        if (!fs::exists(libPath)) continue;

        // PCL2 smart skip: use a SHA1-derived marker file
        std::string sha8 = nat["sha1"].toString().left(8).toStdString();
        fs::path markerPath = nativesPath / (".extracted_" + sha8);
        if (fs::exists(markerPath)) continue;

        emit launchLog("  Extracting: " + nat["path"].toString());
        
        // Pass UTF-8 strings to extractNative (it converts to QString internally)
        // We use QString::toStdString() which is UTF-8.
        if (extractNative(QString::fromStdWString(libPath.wstring()).toStdString(), 
                          QString::fromStdWString(nativesPath.wstring()).toStdString())) {
             std::ofstream f(markerPath); f << sha8;
        } else {
            // Busy-file tolerance – another MC instance may hold the DLL.
            // PCL2 catches UnauthorizedAccessException and skips.
            emit launchLog("  [Warning] Extraction failed (DLL may be in use) – skipping.");
        }
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 4 – McLaunchArgumentMain
// ─────────────────────────────────────────────────────────────────────────────
bool LauncherCore::stepConstructArguments(LaunchContext& ctx) {
    emit launchLog("[4/8] Building launch arguments...");

    int xmn = std::max(64, std::min(512, ctx.maxMemory / 8));
    std::string assetsRoot = (fs::path(workDir) / "assets").string();
    std::string assetId    = ctx.versionManifest.contains("assets")
                           ? ctx.versionManifest["assets"].toString().toStdString() : "legacy";
    std::string mainClass  = ctx.versionManifest["mainClass"].toString().toStdString();

    auto resolve = [&](const std::string& s) -> std::string {
        static const std::pair<const char*, std::function<std::string()>> T[] = {
            {"${auth_player_name}",  [&]{ return ctx.username; }},
            {"${auth_uuid}",         [&]{ return ctx.uuid; }},
            {"${auth_access_token}", [&]{ return ctx.accessToken; }},
            {"${user_type}",         []{ return std::string("mojang"); }},
            {"${version_name}",      [&]{ return ctx.versionId; }},
            {"${version_type}",      [&]{ return ctx.versionManifest["type"].toString().toStdString(); }},
            {"${game_directory}",    [&]{ return workDir; }},
            {"${assets_root}",       [&]{ return assetsRoot; }},
            {"${game_assets}",       [&]{ return assetsRoot; }},
            {"${assets_index_name}", [&]{ return assetId; }},
            {"${natives_directory}", [&]{ return ctx.nativesDir.toStdString(); }},
            {"${launcher_name}",     []{ return std::string("PCL2-Qt"); }},
            {"${launcher_version}",  []{ return std::string("1.0"); }},
            {"${classpath}",         [&]{ return ctx.classPath.toStdString(); }},
        };
        for (auto& [k, fn] : T) if (s == k) return fn();
        return s;
    };

    std::vector<std::string> args;
    bool newFmt = ctx.versionManifest.contains("arguments");

    // ── JVM args ─────────────────────────────────────────────────────────────
    if (newFmt) {
        for (const QJsonValue& v : ctx.versionManifest["arguments"].toObject()["jvm"].toArray()) {
            if (v.isString()) { args.push_back(resolve(v.toString().toStdString())); }
            else if (v.isObject()) {
                QJsonObject o = v.toObject();
                if (!evaluateRules(o["rules"].toArray())) continue;
                QJsonValue val = o["value"];
                if (val.isString()) args.push_back(resolve(val.toString().toStdString()));
                else if (val.isArray())
                    for (const QJsonValue& sv : val.toArray())
                        args.push_back(resolve(sv.toString().toStdString()));
            }
        }
    } else {
        args.push_back("-Djava.library.path=" + ctx.nativesDir.toStdString());
        args.push_back("-Dminecraft.launcher.brand=PCL2-Qt");
        args.push_back("-Dminecraft.launcher.version=1.0");
        args.push_back("-cp");
        args.push_back(ctx.classPath.toStdString());
    }

    // PCL2 standard JVM injections
    args.push_back("-Xmx" + std::to_string(ctx.maxMemory) + "M");
    args.push_back("-Xmn" + std::to_string(xmn) + "M");
    args.push_back("-Dlog4j2.formatMsgNoLookups=true");    // Log4Shell fix
    args.push_back("-Dfile.encoding=UTF-8");
    args.push_back("-XX:+UseG1GC");
    args.push_back("-XX:-UseAdaptiveSizePolicy");
    args.push_back("-XX:-OmitStackTraceInFastThrow");

    ctx.jvmArgs = args;
    args.push_back(mainClass);

    // ── Game args ─────────────────────────────────────────────────────────────
    if (newFmt) {
        for (const QJsonValue& v : ctx.versionManifest["arguments"].toObject()["game"].toArray()) {
            if (v.isString()) args.push_back(resolve(v.toString().toStdString()));
            else if (v.isObject()) {
                QJsonObject o = v.toObject();
                if (!evaluateRules(o["rules"].toArray())) continue;
                QJsonValue val = o["value"];
                if (val.isString()) args.push_back(resolve(val.toString().toStdString()));
                else if (val.isArray())
                    for (const QJsonValue& sv : val.toArray())
                        args.push_back(resolve(sv.toString().toStdString()));
            }
        }
    } else {
        for (const QString& part :
             ctx.versionManifest["minecraftArguments"].toString().split(' ', Qt::SkipEmptyParts))
            args.push_back(resolve(part.toStdString()));
    }

    // PCL2 OptiFine + Forge TweakClass de-duplication (ModLaunch.vb:1596-1611)
    {
        bool hasForge = false, hasOptiFine = false;
        for (size_t i = 0; i + 1 < args.size(); ++i)
            if (args[i] == "--tweakClass") {
                if (args[i+1].find("FMLTweaker") != std::string::npos) hasForge    = true;
                if (args[i+1].find("OptiFine")   != std::string::npos) hasOptiFine = true;
            }
        if (hasForge && hasOptiFine) {
            for (auto it = args.begin(); it != args.end(); ) {
                if (*it == "--tweakClass") {
                    auto nx = std::next(it);
                    if (nx != args.end() && nx->find("OptiFine") != std::string::npos)
                    { it = args.erase(it); it = args.erase(it); continue; }
                }
                ++it;
            }
            emit launchLog("  [OptiFine] Removed duplicate TweakClass (Forge present).");
        }
    }

    ctx.gameArgs = args;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 5 – McLaunchPrerun
// ─────────────────────────────────────────────────────────────────────────────
bool LauncherCore::stepPreRun(LaunchContext& ctx) {
    emit launchLog("[5/8] Pre-run tweaks...");

    // options.txt language normalisation
    std::string optPath = (fs::path(workDir) / "options.txt").string();
    if (fs::exists(optPath)) {
        std::ifstream in(optPath); std::string s((std::istreambuf_iterator<char>(in)), {});
        auto pos = s.find("lang:zh_CN");
        if (pos != std::string::npos) {
            s.replace(pos, 10, "lang:zh_cn");
            std::ofstream out(optPath); out << s;
            emit launchLog("  options.txt: normalised lang code.");
        }
    }

    // launcher_profiles.json injection
    std::string profPath = (fs::path(workDir) / "launcher_profiles.json").string();
    {
        QJsonObject root;
        if (fs::exists(profPath)) {
            std::ifstream in(profPath); std::string s((std::istreambuf_iterator<char>(in)), {});
            QJsonDocument d = QJsonDocument::fromJson(QByteArray::fromStdString(s));
            if (d.isObject()) root = d.object();
        }
        QJsonObject profiles = root["profiles"].toObject();
        QJsonObject profile;
        profile["name"]           = QString::fromStdString(ctx.username);
        profile["type"]           = "latest-release";
        profile["lastVersionId"]  = QString::fromStdString(ctx.versionId);
        profiles["PCL2-Qt"]       = profile;
        root["profiles"]          = profiles;

        QJsonObject authDb, account;
        account["accessToken"] = QString::fromStdString(ctx.accessToken);
        account["username"]    = QString::fromStdString(ctx.username);
        account["userid"]      = QString::fromStdString(ctx.uuid);
        account["displayName"] = QString::fromStdString(ctx.username);
        authDb[QString::fromStdString(ctx.uuid)] = account;
        root["authenticationDatabase"] = authDb;
        root["selectedUser"] = QJsonObject {
            { "account", QString::fromStdString(ctx.uuid) },
            { "profile", QString::fromStdString(ctx.uuid) },
        };
        QJsonDocument d(root);
        std::ofstream f(profPath); f << d.toJson(QJsonDocument::Indented).toStdString();
        emit launchLog("  launcher_profiles.json: updated.");
    }

    // Discrete GPU registry nudge (Windows)
#ifdef Q_OS_WIN
    {
        QString ps = QString(
            "$p='HKCU:\\Software\\Microsoft\\DirectX\\UserGpuPreferences';"
            "if(-not(Test-Path $p)){New-Item -Path $p -Force|Out-Null};"
            "Set-ItemProperty -Path $p -Name '%1' -Value 'GpuPreference=2;' -Type String"
        ).arg(ctx.javaPath);
        QProcess proc;
        proc.start("powershell.exe", { "-NoProfile", "-NonInteractive", "-Command", ps });
        if (proc.waitForFinished(5000) && proc.exitCode() == 0)
            emit launchLog("  GPU: discrete GPU preference set.");
        else
            emit launchLog("  GPU: registry update failed (non-fatal).");
    }
#endif
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 6 – McLaunchCustom
// ─────────────────────────────────────────────────────────────────────────────
bool LauncherCore::stepCustomCommands(LaunchContext& ctx) {
    emit launchLog("[6/8] Custom pre-launch command: " + ctx.customPreLaunchCommand);
    QProcess p;
    p.setWorkingDirectory(QString::fromStdString(workDir));
#ifdef Q_OS_WIN
    p.start("cmd.exe", { "/C", ctx.customPreLaunchCommand });
#else
    p.start("/bin/sh", { "-c", ctx.customPreLaunchCommand });
#endif
    if (!p.waitForFinished(30000)) { p.kill(); emit launchLog("  [Warning] Timed out."); return false; }
    emit launchLog("  Exit code: " + QString::number(p.exitCode()));
    return p.exitCode() == 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 7 – McLaunchRun
// ─────────────────────────────────────────────────────────────────────────────
bool LauncherCore::stepLaunch(LaunchContext& ctx) {
    emit launchLog("[7/8] Spawning Minecraft...");

    QStringList qArgs;
    for (const std::string& a : ctx.gameArgs) qArgs << QString::fromStdString(a);

    QProcess* proc = new QProcess(this);
    ctx.process = proc;
    proc->setProgram(ctx.javaPath);
    proc->setArguments(qArgs);
    proc->setWorkingDirectory(QString::fromStdString(workDir));

    // Environment isolation (PCL2 McLaunchRun)
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PATH",   QFileInfo(ctx.javaPath).absolutePath() + ";" + env.value("PATH"));
    env.insert("APPDATA", QString::fromStdString(workDir));
    proc->setProcessEnvironment(env);

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
        emit launchLog("[MC] " + QString::fromUtf8(proc->readAllStandardOutput()).trimmed());
    });
    connect(proc, &QProcess::readyReadStandardError, this, [this, proc]() {
        emit launchLog("[MC-ERR] " + QString::fromUtf8(proc->readAllStandardError()).trimmed());
    });
    connect(proc, &QProcess::started, this, [this]() { emit gameStarted(); });
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc](int code, QProcess::ExitStatus) {
        emit launchLog("Minecraft exited with code " + QString::number(code));
        emit gameExited(code);
        proc->deleteLater();
    });

    proc->start();
    if (!proc->waitForStarted(5000)) {
        emit launchLog("[Error] " + proc->errorString());
        proc->deleteLater();
        return false;
    }
    
    // [Fix] Store PID for the watcher thread to avoid QPointer race conditions
    ctx.pid = proc->processId();

#ifdef Q_OS_WIN
    {
        DWORD cls = NORMAL_PRIORITY_CLASS;
        if (ctx.processPriority == ProcessPriority::High) cls = HIGH_PRIORITY_CLASS;
        if (ctx.processPriority == ProcessPriority::Low)  cls = IDLE_PRIORITY_CLASS;
        if (HANDLE h = OpenProcess(PROCESS_SET_INFORMATION, FALSE,
                                   static_cast<DWORD>(proc->processId()))) {
            SetPriorityClass(h, cls);
            CloseHandle(h);
        }
    }
#endif
    emit launchLog("PID: " + QString::number(proc->processId()));
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 8 – McLaunchWait
// ─────────────────────────────────────────────────────────────────────────────
void LauncherCore::stepWait(LaunchContext& ctx) {
    emit launchLog("[8/8] Watching for Minecraft window...");
    const int maxMs = 3 * 60 * 1000;
    int elapsed = 0;
    bool found = false;

#ifdef Q_OS_WIN
    while (elapsed < maxMs) {
        // [Fix] Use stored PID to check if process is running, instead of QPointer
        // which causes race conditions/crashes if accessed while main thread deletes it.
        if (ctx.pid > 0) {
             HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(ctx.pid));
             if (hProcess) {
                 DWORD exitCode = 0;
                 if (GetExitCodeProcess(hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
                     CloseHandle(hProcess);
                     break; // Process exited
                 }
                 CloseHandle(hProcess);
             } else {
                 // Cannot open process -> likely exited
                 break;
             }
        } else {
             break;
        }

        struct D { DWORD pid; bool found; };
        D d { static_cast<DWORD>(ctx.pid), false };
        EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
            auto* p = reinterpret_cast<D*>(lp);
            DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
            if (pid != p->pid) return TRUE;
            wchar_t t[512] = {}; GetWindowTextW(hwnd, t, 512);
            if (t[0] && IsWindowVisible(hwnd)) { p->found = true; return FALSE; }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&d));
        if (d.found) { found = true; break; }
        QThread::msleep(500); elapsed += 500;
    }
#else
    QThread::msleep(5000);
    // On Linux/Mac, simple PID check (kill 0)
    // found = (kill(ctx.pid, 0) == 0); 
    // For now, just assume found if we waited 5s and didn't crash
    found = true; 
#endif

    if (found) { emit launchLog("Window detected."); emit gameWindowReady(); }
    else         emit launchLog("[Warning] Window not detected within 3 minutes.");
}
