#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#pragma once

#include <QObject>
#include <QString>
#include <QFutureWatcher>
#include <QThreadPool>
#include <QTimer>

// Added by the kicodebyts fork.
//
// Asks the GitHub API for the newest release of the fork and reports back when it is
// newer than the running build. It only reports; it never downloads or installs
// anything. The plugin lives in C:\Program Files\obs-studio, so replacing its DLL
// needs elevation and a closed OBS, which is not something a background check should
// attempt on its own.
class UpdateChecker : public QObject {
	Q_OBJECT

public:
	static UpdateChecker &get();

	// Schedules the first check shortly after startup, so it never competes with
	// OBS' own loading. Does nothing when the check is switched off.
	void start();

	// force ignores both the daily interval and the enabled setting, for the
	// button in the settings dialog.
	void checkNow(bool force = false);

	void shutdown();

	// The version this build reports, from project() in CMakeLists.
	static QString currentVersion();

	// Returns a negative number when a is older than b, 0 when equal, positive
	// when a is newer. Missing segments count as zero, so "1.1" equals "1.1.0".
	// A leading "v" is accepted on either side.
	static int compareVersions(const QString &a, const QString &b);

signals:
	// Carries the release tag without its leading "v" and the release page URL.
	void updateAvailable(const QString &version, const QString &url);

private:
	UpdateChecker();
	~UpdateChecker();

	static constexpr const char *RELEASE_API_URL =
		"https://api.github.com/repos/Tobse2910/Game-Detector-V2/releases/latest";
	static constexpr qint64 CHECK_INTERVAL_SECONDS = 24 * 60 * 60;

	void onResultReady();

	QThreadPool threadPool;
	QFutureWatcher<QStringList> *watcher = nullptr;
	QTimer *startupTimer = nullptr;
	bool shuttingDown = false;
};

#endif // UPDATECHECKER_H
