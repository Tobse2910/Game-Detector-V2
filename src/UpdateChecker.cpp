#include "UpdateChecker.h"
#include "ConfigManager.h"
#include "NetworkCommon.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QStringList>
#include <obs-module.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#ifndef GAME_DETECTOR_VERSION
// Only relevant for builds that bypass CMake; the real value comes from project().
#define GAME_DETECTOR_VERSION "0.0.0"
#endif

UpdateChecker &UpdateChecker::get()
{
	static UpdateChecker instance;
	return instance;
}

UpdateChecker::UpdateChecker()
{
	threadPool.setMaxThreadCount(1);

	watcher = new QFutureWatcher<QStringList>(this);
	connect(watcher, &QFutureWatcher<QStringList>::finished, this, &UpdateChecker::onResultReady);

	downloadWatcher = new QFutureWatcher<QString>(this);
	connect(downloadWatcher, &QFutureWatcher<QString>::finished, this, &UpdateChecker::onDownloadFinished);

	startupTimer = new QTimer(this);
	startupTimer->setSingleShot(true);
	connect(startupTimer, &QTimer::timeout, this, [this]() { checkNow(false); });
}

UpdateChecker::~UpdateChecker()
{
	shutdown();
}

void UpdateChecker::shutdown()
{
	shuttingDown = true;

	if (startupTimer && startupTimer->isActive())
		startupTimer->stop();

	if (watcher && watcher->isRunning()) {
		watcher->cancel();
		watcher->waitForFinished();
	}

	// A download in flight is aborted rather than waited out: OBS closing must not
	// hang on a slow connection. If the update was already handed to the helper,
	// the ZIP is complete and this does nothing.
	downloadAbort.aborted.store(true);
	if (downloadWatcher && downloadWatcher->isRunning())
		downloadWatcher->waitForFinished();

	threadPool.waitForDone();
}

QString UpdateChecker::currentVersion()
{
	return QString::fromUtf8(GAME_DETECTOR_VERSION);
}

// Splits a version into numeric segments. A leading "v" and any pre-release suffix
// after a dash are dropped, and non-numeric leftovers in a segment count as zero, so
// a malformed tag can never make the comparison throw.
static QList<int> parseVersionSegments(const QString &raw)
{
	QString text = raw.trimmed();

	if (text.startsWith('v', Qt::CaseInsensitive))
		text = text.mid(1);

	const int dash = text.indexOf('-');
	if (dash >= 0)
		text = text.left(dash);

	QList<int> segments;
	const QStringList parts = text.split('.');
	for (const QString &part : parts) {
		QString digits;
		for (const QChar &c : part) {
			if (!c.isDigit())
				break;
			digits.append(c);
		}
		segments.append(digits.isEmpty() ? 0 : digits.toInt());
	}

	return segments;
}

int UpdateChecker::compareVersions(const QString &a, const QString &b)
{
	const QList<int> left = parseVersionSegments(a);
	const QList<int> right = parseVersionSegments(b);

	const int count = qMax(left.size(), right.size());
	for (int i = 0; i < count; ++i) {
		const int l = i < left.size() ? left.at(i) : 0;
		const int r = i < right.size() ? right.at(i) : 0;
		if (l != r)
			return l < r ? -1 : 1;
	}

	return 0;
}

void UpdateChecker::start()
{
	if (!ConfigManager::get().getUpdateCheckEnabled()) {
		blog(LOG_INFO, "[GameDetector/UpdateChecker] Disabled by setting. Running version %s.",
		     currentVersion().toStdString().c_str());
		return;
	}

	blog(LOG_INFO, "[GameDetector/UpdateChecker] Running version %s.", currentVersion().toStdString().c_str());
	startupTimer->start(10000);
}

void UpdateChecker::checkNow(bool force)
{
	if (shuttingDown)
		return;

	if (!force && !ConfigManager::get().getUpdateCheckEnabled())
		return;

	if (watcher->isRunning())
		return;

	const qint64 now = QDateTime::currentSecsSinceEpoch();
	const qint64 last = (qint64)ConfigManager::get().getUpdateCheckLast();

	// Without this an OBS restart would hit the API every single time.
	if (!force && last > 0 && now - last < CHECK_INTERVAL_SECONDS)
		return;

	ConfigManager::get().setUpdateCheckLast(now);

	const QString url = QString::fromUtf8(RELEASE_API_URL);

	auto future = RunTaskSafe(&threadPool, "GameDetector/UpdateChecker", [url]() -> QStringList {
		struct curl_slist *headers = nullptr;
		headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
		headers = curl_slist_append(headers, "X-GitHub-Api-Version: 2022-11-28");
		// The GitHub API answers 403 without a User-Agent.
		headers = curl_slist_append(headers, "User-Agent: obs-game-detector-update-check");

		auto [http_code, response] = ExecuteNetworkRequest(url, "GET", headers);
		curl_slist_free_all(headers);

		if (http_code == 404) {
			// No release published yet. Not an error worth warning about.
			blog(LOG_INFO, "[GameDetector/UpdateChecker] No release published yet.");
			return QStringList();
		}

		if (http_code < 200 || http_code >= 300) {
			blog(LOG_WARNING, "[GameDetector/UpdateChecker] Update check failed (HTTP %ld).", http_code);
			return QStringList();
		}

		QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
		if (!doc.isObject()) {
			blog(LOG_WARNING, "[GameDetector/UpdateChecker] Unexpected answer from the GitHub API.");
			return QStringList();
		}

		const QJsonObject release = doc.object();
		if (release.value("draft").toBool() || release.value("prerelease").toBool())
			return QStringList();

		const QString tag = release.value("tag_name").toString().trimmed();
		if (tag.isEmpty())
			return QStringList();

		QString page = release.value("html_url").toString().trimmed();
		if (page.isEmpty())
			page = "https://github.com/Tobse2910/Game-Detector-V2/releases/latest";

		// The ZIP asset is what the one click update installs. A release without
		// one still reports, it just leaves the user with the manual route.
		QString download;
		const QJsonArray assets = release.value("assets").toArray();
		for (const QJsonValue &value : assets) {
			const QJsonObject asset = value.toObject();
			if (!asset.value("name").toString().endsWith(".zip", Qt::CaseInsensitive))
				continue;
			download = asset.value("browser_download_url").toString().trimmed();
			break;
		}

		return QStringList{tag, page, download};
	});

	watcher->setFuture(future);
}

void UpdateChecker::onResultReady()
{
	if (shuttingDown || watcher->isCanceled())
		return;

	const QStringList result = watcher->result();
	if (result.size() < 3)
		return;

	const QString tag = result.at(0);
	const QString page = result.at(1);
	const QString download = result.at(2);
	const QString running = currentVersion();

	if (compareVersions(running, tag) >= 0) {
		blog(LOG_INFO, "[GameDetector/UpdateChecker] Version %s is up to date (newest release: %s).",
		     running.toStdString().c_str(), tag.toStdString().c_str());
		return;
	}

	QString version = tag;
	if (version.startsWith('v', Qt::CaseInsensitive))
		version = version.mid(1);

	blog(LOG_INFO, "[GameDetector/UpdateChecker] Version %s available (running %s): %s",
	     version.toStdString().c_str(), running.toStdString().c_str(), page.toStdString().c_str());

	emit updateAvailable(version, page, download);
}

QString UpdateChecker::installedObsDir()
{
#ifdef _WIN32
	// Derived from where this DLL actually loaded from, so the helper never has to
	// guess which OBS install to patch. Expected layout:
	//   <obs>\obs-plugins\64bit\game-detector.dll
	HMODULE self = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&UpdateChecker::installedObsDir), &self) ||
	    !self) {
		return QString();
	}

	wchar_t buffer[MAX_PATH] = {};
	const DWORD length = GetModuleFileNameW(self, buffer, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
		return QString();

	const QFileInfo dll(QString::fromWCharArray(buffer, length));
	QDir dir = dll.absoluteDir(); // ...\obs-plugins\64bit

	if (!dir.cdUp()) // ...\obs-plugins
		return QString();
	if (!dir.cdUp()) // ...\obs-studio
		return QString();

	if (!QFileInfo::exists(dir.filePath("bin/64bit/obs64.exe")))
		return QString();

	return QDir::toNativeSeparators(dir.absolutePath());
#else
	return QString();
#endif
}

void UpdateChecker::startUpdate(const QString &downloadUrl)
{
	if (updating) {
		blog(LOG_INFO, "[GameDetector/UpdateChecker] An update is already running.");
		return;
	}

	// This URL ends up in the hands of an elevated helper, so it is only accepted
	// when it points at this fork's own releases, whatever the API answered.
	if (!downloadUrl.startsWith(QString::fromUtf8(DOWNLOAD_URL_PREFIX), Qt::CaseSensitive)) {
		blog(LOG_WARNING, "[GameDetector/UpdateChecker] Refused an unexpected download URL: %s",
		     downloadUrl.toStdString().c_str());
		emit updateFailed(obs_module_text("Update.Error.BadUrl"));
		return;
	}

	if (installedObsDir().isEmpty()) {
		blog(LOG_WARNING,
		     "[GameDetector/UpdateChecker] Cannot locate the OBS install this plugin was loaded from.");
		emit updateFailed(obs_module_text("Update.Error.NoObsDir"));
		return;
	}

	const QString targetDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
	if (targetDir.isEmpty()) {
		emit updateFailed(obs_module_text("Update.Error.Download"));
		return;
	}

	const QString zipPath = QDir(targetDir).filePath("game-detector-update.zip");

	updating = true;
	emit updateStage(obs_module_text("Update.Stage.Downloading"));
	blog(LOG_INFO, "[GameDetector/UpdateChecker] Downloading %s", downloadUrl.toStdString().c_str());

	auto future = RunTaskSafe(&threadPool, "GameDetector/UpdateChecker", [this, downloadUrl, zipPath]() -> QString {
		const long code = DownloadToFile(downloadUrl, zipPath, &downloadAbort);
		if (code < 200 || code >= 300) {
			blog(LOG_WARNING, "[GameDetector/UpdateChecker] Download failed (HTTP %ld).", code);
			return QString();
		}

		// A truncated or empty file would leave the helper with nothing to
		// unpack after OBS has already been closed.
		const QFileInfo info(zipPath);
		if (!info.exists() || info.size() < 1024) {
			blog(LOG_WARNING, "[GameDetector/UpdateChecker] Downloaded file is too small to be the ZIP.");
			return QString();
		}

		return zipPath;
	});

	downloadWatcher->setFuture(future);
}

void UpdateChecker::onDownloadFinished()
{
	if (shuttingDown)
		return;

	const QString zipPath = downloadWatcher->result();

	if (zipPath.isEmpty()) {
		updating = false;
		emit updateFailed(obs_module_text("Update.Error.Download"));
		return;
	}

	blog(LOG_INFO, "[GameDetector/UpdateChecker] Download finished, starting the helper.");
	emit updateStage(obs_module_text("Update.Stage.Elevating"));

	if (!launchElevatedHelper(zipPath)) {
		updating = false;
		return;
	}

	emit readyToRestart();
}

// Unpacks the downloaded ZIP so the helper inside it can be used. Returns the
// directory, or an empty string on failure.
static QString unpackUpdate(const QString &zipPath)
{
	const QString target =
		QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("game-detector-update");

	QDir existing(target);
	if (existing.exists())
		existing.removeRecursively();

	// Qt ships no ZIP reader, and PowerShell is already a hard dependency of the
	// update path. This runs unelevated: nothing here touches Program Files.
	const QString command = QString("Expand-Archive -LiteralPath '%1' -DestinationPath '%2' -Force")
					.arg(QDir::toNativeSeparators(zipPath), QDir::toNativeSeparators(target));

	QProcess unpack;
	unpack.start("powershell.exe", {"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command",
					command});

	if (!unpack.waitForFinished(120000) || unpack.exitCode() != 0) {
		blog(LOG_WARNING, "[GameDetector/UpdateChecker] Unpacking the update failed: %s",
		     unpack.readAllStandardError().constData());
		return QString();
	}

	return QDir::toNativeSeparators(target);
}

bool UpdateChecker::launchElevatedHelper(const QString &zipPath)
{
#ifdef _WIN32
	// The helper is taken from the update itself whenever possible, not from the
	// installed version. A bug in the helper would otherwise be unfixable: the
	// broken copy would keep running every future update. The installed one is only
	// the fallback.
	const QString unpacked = unpackUpdate(zipPath);
	QString helperSource;
	QString sourceArgument;

	if (!unpacked.isEmpty()) {
		QDirIterator it(unpacked, {"update.ps1"}, QDir::Files, QDirIterator::Subdirectories);
		if (it.hasNext()) {
			helperSource = it.next();
			sourceArgument = QString("-Source \"%1\"").arg(unpacked);
			blog(LOG_INFO, "[GameDetector/UpdateChecker] Using the helper from the update package.");
		}
	}

	if (helperSource.isEmpty()) {
		char *scriptPath = obs_module_file("update.ps1");
		if (!scriptPath) {
			blog(LOG_WARNING, "[GameDetector/UpdateChecker] update.ps1 is missing from the plugin data.");
			emit updateFailed(obs_module_text("Update.Error.HelperMissing"));
			return false;
		}

		helperSource = QString::fromUtf8(scriptPath);
		bfree(scriptPath);

		sourceArgument = QString("-Zip \"%1\"").arg(QDir::toNativeSeparators(zipPath));
		blog(LOG_INFO, "[GameDetector/UpdateChecker] Using the installed helper.");
	}

	// The helper is run from a copy: the update replaces the shipped script itself,
	// and a script file cannot be overwritten while it is being read.
	const QString helper =
		QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("game-detector-update.ps1");
	QFile::remove(helper);
	if (!QFile::copy(helperSource, helper)) {
		blog(LOG_WARNING, "[GameDetector/UpdateChecker] Cannot copy the helper to %s.",
		     helper.toStdString().c_str());
		emit updateFailed(obs_module_text("Update.Error.HelperMissing"));
		return false;
	}

	const QString arguments =
		QString("-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"%1\" %2 -ObsDir \"%3\" -WaitPid %4")
			.arg(QDir::toNativeSeparators(helper), sourceArgument, installedObsDir(),
			     QString::number((qulonglong)GetCurrentProcessId()));

	const std::wstring file = L"powershell.exe";
	const std::wstring params = arguments.toStdWString();

	SHELLEXECUTEINFOW info = {};
	info.cbSize = sizeof(info);
	info.fMask = SEE_MASK_NOASYNC;
	info.lpVerb = L"runas"; // triggers the UAC prompt; the helper needs Program Files
	info.lpFile = file.c_str();
	info.lpParameters = params.c_str();
	// Hidden: the helper puts up its own window, the console behind it would only
	// be noise.
	info.nShow = SW_HIDE;

	if (!ShellExecuteExW(&info)) {
		const DWORD error = GetLastError();
		QFile::remove(helper);

		if (error == ERROR_CANCELLED) {
			// The user clicked No on the UAC prompt. Not a failure worth
			// alarming them about, and OBS must stay open.
			blog(LOG_INFO, "[GameDetector/UpdateChecker] Update cancelled at the UAC prompt.");
			emit updateFailed(obs_module_text("Update.Error.Cancelled"));
			return false;
		}

		blog(LOG_WARNING, "[GameDetector/UpdateChecker] Cannot start the helper (error %lu).", error);
		emit updateFailed(obs_module_text("Update.Error.Elevation"));
		return false;
	}

	blog(LOG_INFO, "[GameDetector/UpdateChecker] Helper running, waiting for OBS to close.");
	return true;
#else
	Q_UNUSED(zipPath);
	emit updateFailed(obs_module_text("Update.Error.Elevation"));
	return false;
#endif
}
