#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#pragma once

#include <QObject>
#include <QString>
#include <QFutureWatcher>
#include <QThreadPool>
#include <QTimer>

#include "NetworkCommon.h"

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

	// Downloads the release ZIP and hands it to the elevated helper, which waits for
	// OBS to close before replacing anything. The caller closes OBS once
	// readyToRestart() arrives: the helper cannot replace a loaded DLL.
	void startUpdate(const QString &downloadUrl);

	// True while a download or handover is in progress.
	bool isUpdating() const { return updating; }

	// The OBS folder this plugin is actually installed in, derived from the path of
	// the loaded DLL rather than guessed from the registry. Empty if it cannot be
	// determined, which is the case for any layout other than an OBS install.
	static QString installedObsDir();

signals:
	// Carries the release tag without its leading "v", the release page URL and the
	// direct download URL of the ZIP asset (empty when the release has no ZIP).
	void updateAvailable(const QString &version, const QString &url, const QString &downloadUrl);

	// Progress text for the dock, already translated.
	void updateStage(const QString &text);

	// The helper is running and waiting for OBS to exit. OBS has to be closed now.
	void readyToRestart();

	void updateFailed(const QString &reason);

private:
	UpdateChecker();
	~UpdateChecker();

	static constexpr const char *RELEASE_API_URL =
		"https://api.github.com/repos/Tobse2910/Game-Detector-V2/releases/latest";
	static constexpr qint64 CHECK_INTERVAL_SECONDS = 24 * 60 * 60;

	// A download URL from the API ends up being handled by an elevated helper, so it
	// is only accepted when it comes from this fork's own releases.
	static constexpr const char *DOWNLOAD_URL_PREFIX =
		"https://github.com/Tobse2910/Game-Detector-V2/releases/download/";

	void onResultReady();
	void onDownloadFinished();
	bool launchElevatedHelper(const QString &zipPath);

	QThreadPool threadPool;
	QFutureWatcher<QStringList> *watcher = nullptr;
	QFutureWatcher<QString> *downloadWatcher = nullptr;
	QTimer *startupTimer = nullptr;
	DownloadAbortFlag downloadAbort;
	bool shuttingDown = false;
	bool updating = false;
};

#endif // UPDATECHECKER_H
