#include "UpdateChecker.h"
#include "ConfigManager.h"
#include "NetworkCommon.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <obs-module.h>

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

		return QStringList{tag, page};
	});

	watcher->setFuture(future);
}

void UpdateChecker::onResultReady()
{
	if (shuttingDown || watcher->isCanceled())
		return;

	const QStringList result = watcher->result();
	if (result.size() < 2)
		return;

	const QString tag = result.at(0);
	const QString page = result.at(1);
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

	emit updateAvailable(version, page);
}
