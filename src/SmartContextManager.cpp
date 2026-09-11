/*
 * Smart Context Mode for the OBS Game Detector plugin.
 *
 * Added by the kicodebyts fork of FabioZumbi12/game-detector.
 * Licensed under the GNU General Public License v2.0, like the rest of the plugin.
 */

#include "SmartContextManager.h"
#include "ConfigManager.h"
#include "GameDetector.h"
#include "PlatformManager.h"

#include <QDateTime>
#include <QFileInfo>
#include <QRegularExpression>
#include <obs-module.h>
#include <obs-frontend-api.h>

#include <limits>

#ifdef _WIN32
// windows.h defines min/max macros that break std::numeric_limits<int>::min().
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

// Reads the executable name and window title of the current foreground window.
bool getForegroundApp(QString &exeName, QString &windowTitle)
{
#ifdef _WIN32
	HWND hwnd = GetForegroundWindow();
	if (!hwnd)
		return false;

	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	if (pid == 0)
		return false;

	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!process)
		return false;

	wchar_t imagePath[MAX_PATH] = {0};
	DWORD pathSize = MAX_PATH;
	bool ok = QueryFullProcessImageNameW(process, 0, imagePath, &pathSize) != 0;
	CloseHandle(process);

	if (!ok || pathSize == 0)
		return false;

	exeName = QFileInfo(QString::fromWCharArray(imagePath, (int)pathSize)).fileName();

	wchar_t titleBuffer[512] = {0};
	int titleLength = GetWindowTextW(hwnd, titleBuffer, 512);
	windowTitle = titleLength > 0 ? QString::fromWCharArray(titleBuffer, titleLength) : QString();

	return !exeName.isEmpty();
#else
	Q_UNUSED(exeName);
	Q_UNUSED(windowTitle);
	return false;
#endif
}

bool processMatches(const QString &pattern, const QString &exeName, bool &exactMatch)
{
	exactMatch = pattern.compare(exeName, Qt::CaseInsensitive) == 0;
	if (exactMatch)
		return true;

	if (!pattern.contains('*') && !pattern.contains('?'))
		return false;

	QRegularExpression wildcard(QRegularExpression::wildcardToRegularExpression(pattern),
				    QRegularExpression::CaseInsensitiveOption);
	return wildcard.match(exeName).hasMatch();
}

} // namespace

SmartContextManager &SmartContextManager::get()
{
	static SmartContextManager instance;
	return instance;
}

SmartContextManager::SmartContextManager(QObject *parent) : QObject(parent)
{
	pollTimer = new QTimer(this);
	pollTimer->setInterval(POLL_INTERVAL_MS);
	connect(pollTimer, &QTimer::timeout, this, &SmartContextManager::poll);

	// Seed our idea of the live category from what the platform actually reports,
	// until we have made a decision of our own.
	connect(&PlatformManager::get(), &PlatformManager::categoriesFetched, this,
		[this](const QHash<QString, QString> &categories) {
			if (hasSwitchedOnce || !categories.contains("Twitch"))
				return;

			const QString data = categories.value("Twitch");
			const int separator = data.indexOf("|||");
			const QString category = (separator >= 0 ? data.left(separator) : data).trimmed();
			if (category.isEmpty() || category == currentAppliedCategory)
				return;

			blog(LOG_INFO, "[GameDetector/SmartContext] Adopting live category '%s'.",
			     category.toStdString().c_str());
			currentAppliedCategory = category;
			appliedAtMs = QDateTime::currentMSecsSinceEpoch();

			// A pending switch to the category that is already live is pointless.
			if (candidateCategory.compare(category, Qt::CaseInsensitive) == 0)
				clearCandidate();

			emit statusUpdated();
		});
}

QList<SmartContextRule> SmartContextManager::loadRulesFromConfig()
{
	QList<SmartContextRule> loaded;

	obs_data_array_t *array = ConfigManager::get().getSmartContextRules();
	if (!array)
		return loaded;

	size_t count = obs_data_array_count(array);
	for (size_t i = 0; i < count; ++i) {
		obs_data_t *item = obs_data_array_item(array, i);

		SmartContextRule rule;
		rule.enabled = obs_data_get_bool(item, "enabled");
		rule.process = QString::fromUtf8(obs_data_get_string(item, "process")).trimmed();
		rule.window = QString::fromUtf8(obs_data_get_string(item, "window")).trimmed();
		rule.category = QString::fromUtf8(obs_data_get_string(item, "category")).trimmed();
		rule.titleTemplate = QString::fromUtf8(obs_data_get_string(item, "title_template"));
		rule.delaySeconds = (int)obs_data_get_int(item, "delay");
		rule.ignore = obs_data_get_bool(item, "ignore");
		rule.priority = (int)obs_data_get_int(item, "priority");

		if (!rule.process.isEmpty())
			loaded.append(rule);

		obs_data_release(item);
	}
	obs_data_array_release(array);

	return loaded;
}

void SmartContextManager::reloadRules()
{
	rules = loadRulesFromConfig();
	blog(LOG_INFO, "[GameDetector/SmartContext] %d rules loaded.", (int)rules.size());
}

void SmartContextManager::reloadSettings()
{
	globalDelayMs = ConfigManager::get().getSmartContextDelay() * 1000;
	switchCooldownMs = ConfigManager::get().getSmartContextSwitchCooldown() * 1000;
	graceMs = ConfigManager::get().getSmartContextGrace() * 1000;
}

void SmartContextManager::start()
{
	reloadRules();
	reloadSettings();

	clearCandidate();
	parkedProgress.clear();

	// Assume what the platform layer last set, then correct it as soon as the real
	// category comes back from the platform (see the categoriesFetched handler).
	currentAppliedCategory = PlatformManager::get().getLastSetCategory();
	appliedAtMs = QDateTime::currentMSecsSinceEpoch();
	switchCooldownUntilMs = 0;
	hasSwitchedOnce = false;
	PlatformManager::get().fetchCurrentCategories();

	if (!pollTimer->isActive())
		pollTimer->start();

	blog(LOG_INFO, "[GameDetector/SmartContext] Enabled. Delay: %ds, switch cooldown: %ds, grace: %ds.",
	     globalDelayMs / 1000, switchCooldownMs / 1000, graceMs / 1000);
}

void SmartContextManager::stop()
{
	if (pollTimer->isActive())
		pollTimer->stop();

	clearCandidate();
	parkedProgress.clear();
	currentProcess.clear();
	currentWindowTitle.clear();
	currentContext.clear();
	currentRuleLabel.clear();
	currentIgnored = false;

	blog(LOG_INFO, "[GameDetector/SmartContext] Disabled.");
	emit statusUpdated();
}

SmartContextResolution SmartContextManager::resolve(const QString &exeName, const QString &windowTitle) const
{
	SmartContextResolution best;
	int bestPriority = std::numeric_limits<int>::min();
	bool bestExact = false;

	for (const SmartContextRule &rule : rules) {
		if (!rule.enabled || rule.process.isEmpty())
			continue;

		bool exact = false;
		if (!processMatches(rule.process, exeName, exact))
			continue;

		if (!rule.window.isEmpty() && !windowTitle.contains(rule.window, Qt::CaseInsensitive))
			continue;

		// Higher priority wins; on a tie an exact process name beats a wildcard.
		bool better = rule.priority > bestPriority || (rule.priority == bestPriority && exact && !bestExact);
		if (!better)
			continue;

		bestPriority = rule.priority;
		bestExact = exact;

		best = SmartContextResolution();
		best.matched = true;
		best.ignored = rule.ignore;
		best.category = rule.category;
		best.titleTemplate = rule.titleTemplate;
		best.delaySeconds = rule.delaySeconds;
		best.ruleLabel = rule.process;
	}

	// Games the user is actually looking at win over generic and wildcard rules,
	// but never over an exact rule (which is the user's explicit intent) and
	// never over the high priority ignore rules.
	QString gameName = GameDetector::get().getGameNameForExe(exeName);
	if (!gameName.isEmpty() && (!best.matched || (!bestExact && bestPriority < GAME_LIST_PRIORITY))) {
		best = SmartContextResolution();
		best.matched = true;
		best.fromGameList = true;
		best.category = gameName;
		best.ruleLabel = exeName;
	}

	// A rule that neither ignores nor names a category cannot do anything useful.
	if (best.matched && !best.ignored && best.category.isEmpty())
		best.ignored = true;

	return best;
}

QString SmartContextManager::renderTitle(const QString &templateText, const QString &category) const
{
	QString text = templateText.trimmed();
	if (text.isEmpty())
		return QString();

	QString appName = currentProcess;
	if (appName.endsWith(".exe", Qt::CaseInsensitive))
		appName.chop(4);

	text.replace("{game}", category);
	text.replace("{category}", category);
	text.replace("{app}", appName);
	text.replace("{window}", currentWindowTitle);

	return text;
}

void SmartContextManager::poll()
{
	QString exeName;
	QString windowTitle;

	if (!getForegroundApp(exeName, windowTitle)) {
		// No usable foreground window (lock screen, UAC prompt, …). Treat it as
		// neutral rather than losing the accumulated time.
		emit statusUpdated();
		return;
	}

	currentProcess = exeName;
	currentWindowTitle = windowTitle;

	const SmartContextResolution resolution = resolve(exeName, windowTitle);

	currentIgnored = resolution.ignored || !resolution.matched;
	currentRuleLabel = resolution.ruleLabel;

	if (currentIgnored) {
		// Ignored applications are neutral: they neither change the category nor
		// age the pending one, so a quick look at Discord costs nothing.
		currentContext.clear();
		emit statusUpdated();
		return;
	}

	currentContext = resolution.category;
	expireParkedEntries(POLL_INTERVAL_MS);

	if (resolution.category.compare(currentAppliedCategory, Qt::CaseInsensitive) == 0) {
		// Already live. Nothing pending, and the "active since" clock keeps running.
		if (!candidateCategory.isEmpty()) {
			parkCandidate();
			clearCandidate();
		}
		emit statusUpdated();
		return;
	}

	if (candidateCategory.compare(resolution.category, Qt::CaseInsensitive) != 0) {
		parkCandidate();
		candidateCategory = resolution.category;
		candidateElapsedMs = unparkCandidate(resolution.category);
	}

	// Re-read each tick so edits in the rule list take effect without a restart.
	candidateTitleTemplate = resolution.titleTemplate;
	candidateDelayMs = resolution.delaySeconds > 0 ? resolution.delaySeconds * 1000 : globalDelayMs;
	candidateElapsedMs += POLL_INTERVAL_MS;

	if (candidateElapsedMs >= candidateDelayMs)
		pushCategory(resolution.category, resolution.titleTemplate, false);

	emit statusUpdated();
}

bool SmartContextManager::pushCategory(const QString &category, const QString &titleTemplate, bool manual)
{
	if (category.isEmpty())
		return false;

	const qint64 now = QDateTime::currentMSecsSinceEpoch();

	// A manual click is not an automatic change, so it bypasses the lock, the
	// stability delay and the switch cooldown. Everything else must not.
	if (!manual) {
		if (ConfigManager::get().getSmartContextLock()) {
			// Locked: stay pending and try again once the user unlocks it.
			return false;
		}

		if (now < switchCooldownUntilMs)
			return false;

		if (ConfigManager::get().getBlockAutoUpdateWhileStreaming() && !obs_frontend_streaming_active())
			return false;
	}

	// The platform rate limit applies either way.
	if (PlatformManager::get().isOnCooldown())
		return false;

	const QString title = renderTitle(titleTemplate, category);
	const bool sent = PlatformManager::get().updateCategory(category, title, manual);

	// updateCategory() also returns false when the platform already is on that
	// category, which counts as applied for our purposes.
	const bool alreadyLive =
		PlatformManager::get().getLastSetCategory().compare(category, Qt::CaseInsensitive) == 0;
	if (!sent && !alreadyLive)
		return false;

	if (manual)
		blog(LOG_INFO, "[GameDetector/SmartContext] Manual switch -> category '%s'.",
		     category.toStdString().c_str());
	else
		blog(LOG_INFO, "[GameDetector/SmartContext] Automatic switch via %s -> category '%s'.",
		     currentProcess.toStdString().c_str(), category.toStdString().c_str());

	currentAppliedCategory = category;
	appliedAtMs = now;
	switchCooldownUntilMs = now + switchCooldownMs;
	hasSwitchedOnce = true;

	clearCandidate();
	parkedProgress.clear();

	if (ConfigManager::get().getSmartContextAnnounceChat()) {
		QString announcement = ConfigManager::get().getSmartContextAnnounceMessage().trimmed();
		if (!announcement.isEmpty()) {
			announcement.replace("{game}", category);
			announcement.replace("{category}", category);
			announcement.replace("{title}", title.isEmpty() ? currentAppliedCategory : title);
			PlatformManager::get().sendChatAnnouncement(announcement);
		}
	}

	emit contextApplied(category, title);
	return true;
}

bool SmartContextManager::applyPendingNow()
{
	if (candidateCategory.isEmpty())
		return false;

	// clearCandidate() runs inside pushCategory(), so copy first.
	const QString category = candidateCategory;
	const QString titleTemplate = candidateTitleTemplate;

	return pushCategory(category, titleTemplate, true);
}

bool SmartContextManager::applyCategoryManually(const QString &category, const QString &titleTemplate)
{
	return pushCategory(category, titleTemplate, true);
}

void SmartContextManager::resetPending()
{
	clearCandidate();
	parkedProgress.clear();
	emit statusUpdated();
}

void SmartContextManager::parkCandidate()
{
	if (candidateCategory.isEmpty() || candidateElapsedMs <= 0)
		return;

	ParkedProgress parked;
	parked.elapsedMs = candidateElapsedMs;
	parked.ageMs = 0;
	parkedProgress.insert(candidateCategory, parked);
}

int SmartContextManager::unparkCandidate(const QString &category)
{
	auto it = parkedProgress.find(category);
	if (it == parkedProgress.end())
		return 0;

	int elapsed = it->elapsedMs;
	parkedProgress.erase(it);
	return elapsed;
}

void SmartContextManager::expireParkedEntries(int elapsedMs)
{
	for (auto it = parkedProgress.begin(); it != parkedProgress.end();) {
		it->ageMs += elapsedMs;
		if (it->ageMs >= graceMs)
			it = parkedProgress.erase(it);
		else
			++it;
	}
}

void SmartContextManager::clearCandidate()
{
	candidateCategory.clear();
	candidateTitleTemplate.clear();
	candidateDelayMs = 0;
	candidateElapsedMs = 0;
}

int SmartContextManager::activeForSeconds() const
{
	if (!candidateCategory.isEmpty())
		return candidateElapsedMs / 1000;

	if (appliedAtMs > 0)
		return (int)((QDateTime::currentMSecsSinceEpoch() - appliedAtMs) / 1000);

	return 0;
}

int SmartContextManager::secondsUntilSwitch() const
{
	if (candidateCategory.isEmpty() || candidateDelayMs <= 0)
		return -1;

	int remainingMs = candidateDelayMs - candidateElapsedMs;
	return remainingMs > 0 ? (remainingMs + 999) / 1000 : 0;
}
