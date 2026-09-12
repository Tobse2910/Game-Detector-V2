/*
 * Smart Context Mode for the OBS Game Detector plugin.
 *
 * Added by the kicodebyts fork of FabioZumbi12/game-detector.
 * Licensed under the GNU General Public License v2.0, like the rest of the plugin.
 *
 * Where GameDetector answers "is a known game running?", this answers
 * "which application is the user actually looking at right now?" by polling the
 * Windows foreground window, and only switches the stream category once that
 * answer has been stable for a configurable amount of time.
 */

#ifndef SMARTCONTEXTMANAGER_H
#define SMARTCONTEXTMANAGER_H

#include <QObject>
#include <QString>
#include <QList>
#include <QHash>
#include <QTimer>

// One row of the user editable rule list.
struct SmartContextRule {
	bool enabled = true;
	QString process;       // exe name, may contain * and ? wildcards
	QString window;        // optional substring that must appear in the window title
	QString category;      // Twitch category to set
	QString titleTemplate; // optional stream title, empty = leave the title alone
	int delaySeconds = 0;  // 0 = use the global Smart Context delay
	bool ignore = false;   // true = never change the category for this app
	int priority = 0;      // higher wins when several rules match
};

// What the rule engine made of the current foreground window.
struct SmartContextResolution {
	bool matched = false;  // a rule or the game list produced a category
	bool ignored = false;  // explicitly ignored, keep the current category
	bool fromGameList = false;
	QString category;
	QString titleTemplate;
	int delaySeconds = 0;
	QString ruleLabel; // what matched, for the dock and the log
};

class SmartContextManager : public QObject {
	Q_OBJECT

public:
	static SmartContextManager &get();

	void start();
	void stop();
	void reloadRules();
	void reloadSettings();

	// Current state, read by the dock once per tick.
	QString activeProcess() const { return currentProcess; }
	QString activeWindowTitle() const { return currentWindowTitle; }
	// Bei FiveM der Name des Servers, sonst leer.
	QString activeServerName() const { return currentServerName; }
	QString detectedContext() const { return currentContext; }
	bool contextIsIgnored() const { return currentIgnored; }
	QString appliedCategory() const { return currentAppliedCategory; }
	// Seconds the detected context has been the foreground application.
	int activeForSeconds() const;
	// Seconds until the pending category switch fires, or -1 when nothing is pending.
	int secondsUntilSwitch() const;
	QString pendingCategory() const { return candidateCategory; }
	bool hasPendingContext() const { return !candidateCategory.isEmpty(); }

	// Manual override. These are explicit user actions, so they skip the stability
	// delay, the switch cooldown and the category lock. They still respect the
	// platform rate limit and return false when it blocks them.
	bool applyPendingNow();
	bool applyCategoryManually(const QString &category, const QString &titleTemplate);
	void resetPending();

	const QList<SmartContextRule> &currentRules() const { return rules; }
	QString renderTitle(const QString &templateText, const QString &category) const;

	static QList<SmartContextRule> loadRulesFromConfig();

signals:
	// Emitted once per poll so the dock can refresh its labels.
	void statusUpdated();
	// Emitted after a category was actually pushed to the platforms.
	void contextApplied(const QString &category, const QString &title);

private slots:
	void poll();

private:
	explicit SmartContextManager(QObject *parent = nullptr);

	SmartContextResolution resolve(const QString &exeName, const QString &windowTitle) const;
	bool pushCategory(const QString &category, const QString &titleTemplate, bool manual);
	void parkCandidate();
	int unparkCandidate(const QString &category);
	void expireParkedEntries(int elapsedMs);
	void clearCandidate();

	QTimer *pollTimer = nullptr;
	QList<SmartContextRule> rules;

	// Foreground state as of the last poll.
	QString currentProcess;
	QString currentWindowTitle;
	QString currentServerName;
	bool fiveMTitleLogged = false;
	QString currentContext;
	QString currentRuleLabel;
	bool currentIgnored = false;

	// The category Smart Context believes is live right now.
	QString currentAppliedCategory;
	qint64 appliedAtMs = 0;
	// Until we have switched once ourselves, trust what the platform reports
	// instead of assuming. Otherwise a restart schedules a pointless switch to
	// the category that is already live.
	bool hasSwitchedOnce = false;

	// The category waiting to become stable.
	QString candidateCategory;
	QString candidateTitleTemplate;
	int candidateDelayMs = 0;
	int candidateElapsedMs = 0;

	// Progress of candidates the user briefly alt-tabbed away from, so a short
	// detour does not throw away minutes of accumulated time.
	struct ParkedProgress {
		int elapsedMs = 0;
		int ageMs = 0;
	};
	QHash<QString, ParkedProgress> parkedProgress;

	qint64 switchCooldownUntilMs = 0;

	int globalDelayMs = 300 * 1000;
	int switchCooldownMs = 60 * 1000;
	int graceMs = 60 * 1000;

	static constexpr int POLL_INTERVAL_MS = 1000;
	// Implicit priority of a match against the configured game list. Rules with a
	// higher priority (the ignore rules) still win over it.
	static constexpr int GAME_LIST_PRIORITY = 60;
};

#endif // SMARTCONTEXTMANAGER_H
