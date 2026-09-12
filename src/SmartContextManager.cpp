/*
 * Smart Context Mode for the OBS Game Detector plugin.
 *
 * Added by the kicodebyts fork of FabioZumbi12/game-detector.
 * Licensed under the GNU General Public License v2.0, like the rest of the plugin.
 */

#include "SmartContextManager.h"
#include "ConfigManager.h"
#include "FiveMServer.h"
#include "GameDetector.h"
#include "PlatformManager.h"

#include <QDateTime>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
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

/*
 * Der Windows-Schreibtisch ist kein Programm im Vordergrund, sondern die
 * Abwesenheit eines Programms. Er gehoert trotzdem zu explorer.exe, weshalb eine
 * Ignorierregel fuer explorer.exe ihn mit erfasst und die feste Kategorie nie
 * greifen konnte.
 *
 * Unterscheiden laesst sich das an der Fensterklasse, nachgemessen auf diesem
 * Rechner: der Schreibtisch ist "Progman" oder "WorkerW", die Taskleiste
 * "Shell_TrayWnd", ein Explorer-Dateifenster dagegen "CabinetWClass". Nur die
 * ersten drei gelten hier als "nichts im Vordergrund"; wer in Ordnern blaettert,
 * bleibt weiter ignoriert, denn das ist eine Taetigkeit.
 */
bool istSchreibtischFenster(const QString &exeName, const QString &fensterKlasse)
{
	if (exeName.compare("explorer.exe", Qt::CaseInsensitive) != 0)
		return false;

	static const QStringList schreibtisch = {"Progman", "WorkerW", "Shell_TrayWnd",
						 "Shell_SecondaryTrayWnd"};
	return schreibtisch.contains(fensterKlasse, Qt::CaseInsensitive);
}

// Reads the executable name and window title of the current foreground window.
// The process id comes along because the FiveM server lookup needs it, the
// desktop flag because an ignore rule must not swallow the empty screen.
bool getForegroundApp(QString &exeName, QString &windowTitle, quint32 &processId, bool &schreibtisch)
{
	processId = 0;
	schreibtisch = false;
#ifdef _WIN32
	HWND hwnd = GetForegroundWindow();
	if (!hwnd)
		return false;

	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	if (pid == 0)
		return false;

	processId = quint32(pid);

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

	wchar_t klasseBuffer[256] = {0};
	const int klasseLaenge = GetClassNameW(hwnd, klasseBuffer, 256);
	const QString fensterKlasse =
		klasseLaenge > 0 ? QString::fromWCharArray(klasseBuffer, klasseLaenge) : QString();
	schreibtisch = istSchreibtischFenster(exeName, fensterKlasse);

	return !exeName.isEmpty();
#else
	Q_UNUSED(exeName);
	Q_UNUSED(windowTitle);
	Q_UNUSED(schreibtisch);
	return false;
#endif
}

// Setzt einen Text in eine Vorlage ein und raeumt die Trennzeichen auf, die
// sonst uebrig bleiben. Aus "{server} | Titel" wird ohne Server also "Titel"
// und nicht " | Titel".
void ersetzeMitTrenner(QString &text, const QString &platzhalter, const QString &wert)
{
	if (!text.contains(platzhalter))
		return;

	if (!wert.isEmpty()) {
		text.replace(platzhalter, wert);
		return;
	}

	// Den Platzhalter samt anhaengendem oder vorangehendem Trenner entfernen.
	static const QString trenner = "\\s*(?:[|\\-–•/]|::|:)\\s*";
	const QString maske = QRegularExpression::escape(platzhalter);

	text.remove(QRegularExpression(maske + trenner));
	text.remove(QRegularExpression(trenner + maske));
	text.remove(platzhalter);
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
			if (!categories.contains("Twitch"))
				return;

			const QString data = categories.value("Twitch");
			const int separator = data.indexOf("|||");

			// Der Titelabgleich muss vor dem Abbruch weiter unten stehen: er
			// gehoert zu jedem Abruf, nicht nur zum ersten.
			if (separator >= 0)
				uebernimmLiveTitel(data.mid(separator + 3).trimmed());

			if (hasSwitchedOnce)
				return;

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

/*
 * Haelt den eigenen Titel des Nutzers fest, also den Teil, der nicht vom Plugin
 * stammt. Erkennungsmerkmal: steht auf dem Kanal genau der Titel, den das Plugin
 * zuletzt selbst gesetzt hat, dann hat niemand etwas geaendert und der bisherige
 * Grundtitel gilt weiter. Steht dort etwas anderes, hat der Nutzer den Titel in
 * die Hand genommen, und das ist ab dann der Grundtitel.
 *
 * Beides liegt in den Einstellungen, damit es einen OBS-Neustart uebersteht.
 * Ohne das waere nach dem Start der Titel "StateV Roleplay | Mein Text" als
 * Grundtitel durchgegangen und beim naechsten Wechsel stuende dort
 * "Corleone City | StateV Roleplay | Mein Text".
 */
void SmartContextManager::uebernimmLiveTitel(const QString &liveTitel)
{
	if (liveTitel.isEmpty())
		return;

	// Alles, was wir selbst gesetzt haben, ist kein eigener Titel des Nutzers.
	// Die Liste statt nur des letzten Werts, weil ein Abruf Twitch erreichen
	// kann, bevor der neue Titel dort angekommen ist.
	if (!selbstGesetzterTitel.isEmpty() && liveTitel == selbstGesetzterTitel)
		return;
	if (zuletztGesetzteTitel.contains(liveTitel))
		return;

	QString neuerGrund = liveTitel;

	// Sicherheitsnetz: hat der Nutzer den Servernamen selbst vorangestellt,
	// oder stammt der Titel aus einer aelteren Fassung ohne diese Verfolgung,
	// dann wird er hier abgetrennt statt doppelt zu erscheinen.
	if (!currentServerName.isEmpty() && neuerGrund.startsWith(currentServerName, Qt::CaseInsensitive)) {
		const QString rest = neuerGrund.mid(currentServerName.length());
		static const QRegularExpression fuehrenderTrenner("^\\s*(?:[|\\-–•/]|::|:)\\s*");
		const QRegularExpressionMatch treffer = fuehrenderTrenner.match(rest);
		if (treffer.hasMatch())
			neuerGrund = rest.mid(treffer.capturedLength()).trimmed();
	}

	if (neuerGrund == grundTitel)
		return;

	grundTitel = neuerGrund;
	ConfigManager::get().setSmartContextBaseTitle(grundTitel);
	blog(LOG_INFO, "[GameDetector/SmartContext] Own title noted: '%s'",
	     qUtf8Printable(grundTitel));
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
	fallbackAktiv = ConfigManager::get().getSmartContextFallbackEnabled();
	fallbackKategorie = ConfigManager::get().getSmartContextFallbackCategory().trimmed();
}

void SmartContextManager::start()
{
	reloadRules();
	reloadSettings();

	clearCandidate();
	parkedProgress.clear();

	grundTitel = ConfigManager::get().getSmartContextBaseTitle();
	selbstGesetzterTitel = ConfigManager::get().getSmartContextOwnTitle();
	zuletztGesetzteTitel.clear();
	if (!selbstGesetzterTitel.isEmpty())
		zuletztGesetzteTitel.append(selbstGesetzterTitel);

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
	currentServerName.clear();
	currentIgnored = false;
	fiveMTitleLogged = false;
	FiveMServer::reset();

	blog(LOG_INFO, "[GameDetector/SmartContext] Disabled.");
	emit statusUpdated();
}

SmartContextResolution SmartContextManager::resolve(const QString &exeName, const QString &windowTitle,
						   bool schreibtisch) const
{
	SmartContextResolution best;

	/*
	 * Der leere Schreibtisch faellt an den Regeln vorbei. Sonst wuerde ihn eine
	 * Ignorierregel fuer explorer.exe verschlucken, und die feste Kategorie kaeme
	 * nie zum Zug, obwohl gerade dann wirklich nichts laeuft. Ordnerfenster sind
	 * davon nicht betroffen, die bleiben ein Programm wie jedes andere.
	 */
	if (schreibtisch) {
		if (fallbackAktiv && !fallbackKategorie.isEmpty()) {
			best.matched = true;
			best.fromFallback = true;
			best.category = fallbackKategorie;
			best.titleTemplate = "{titel}";
			best.ruleLabel = exeName;
		}
		return best;
	}

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

	/*
	 * Ganz zum Schluss die Ausweichkategorie: sie greift nur, wenn wirklich
	 * nichts erkannt wurde, also weder eine Regel noch die Spieleliste etwas
	 * hergab. Ein ignoriertes Programm bleibt weiter neutral, denn genau das ist
	 * der Sinn der Ignorierliste: ein Blick in Discord soll die Kategorie nicht
	 * bewegen. Gewechselt wird auch hier erst nach der eingestellten Zeit, ein
	 * kurzer Ausflug auf den Schreibtisch kostet also nichts.
	 */
	if (!best.matched && fallbackAktiv && !fallbackKategorie.isEmpty()) {
		best = SmartContextResolution();
		best.matched = true;
		best.fromFallback = true;
		best.category = fallbackKategorie;
		// Der eigene Titel des Nutzers, ohne einen Servernamen davor: wer FiveM
		// verlaesst, soll nicht weiter den Server im Titel stehen haben.
		best.titleTemplate = "{titel}";
		best.ruleLabel = exeName;
	}

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

	// Der Server ist nur bei FiveM bekannt. Steht er nicht fest, faellt der
	// Platzhalter samt Trennzeichen weg, damit kein " | " am Anfang stehen bleibt.
	ersetzeMitTrenner(text, "{server}", currentServerName);

	// Der eigene Titel des Nutzers. Damit bleibt bei "{server} | {titel}" alles
	// erhalten, was er selbst geschrieben hat, und der Server steht davor.
	ersetzeMitTrenner(text, "{titel}", grundTitel);
	ersetzeMitTrenner(text, "{title}", grundTitel);

	return text.simplified();
}

void SmartContextManager::poll()
{
	QString exeName;
	QString windowTitle;
	quint32 processId = 0;
	bool schreibtisch = false;

	if (!getForegroundApp(exeName, windowTitle, processId, schreibtisch)) {
		// No usable foreground window (lock screen, UAC prompt, …). Treat it as
		// neutral rather than losing the accumulated time.
		emit statusUpdated();
		return;
	}

	currentProcess = exeName;
	currentWindowTitle = windowTitle;
	currentServerName = FiveMServer::currentName(exeName, processId);

	// Einmal je Sitzung festhalten, wie FiveM sein Fenster benennt. Traegt der
	// Titel den Servernamen, genuegt kuenftig {window} ohne Dateizugriff.
	if (!currentServerName.isEmpty() && !fiveMTitleLogged) {
		fiveMTitleLogged = true;
		blog(LOG_INFO, "[GameDetector/FiveM] Window title while connected: '%s'",
		     qUtf8Printable(windowTitle));
	}

	currentIstSchreibtisch = schreibtisch;
	const SmartContextResolution resolution = resolve(exeName, windowTitle, schreibtisch);

	currentIgnored = resolution.ignored || !resolution.matched;
	currentRuleLabel = resolution.ruleLabel;

	if (currentIgnored) {
		// Ignored applications are neutral: they neither change the category nor
		// age the pending one, so a quick look at Discord costs nothing.
		currentContext.clear();
		emit statusUpdated();
		return;
	}

	// Kommt die Kategorie aus der festen Vorgabe und nicht aus einer Regel, steht
	// das dabei. Sonst raetselt man, warum gerade diese Kategorie erkannt wurde.
	currentContext = resolution.fromFallback
				 ? QString("%1 (%2)").arg(resolution.category,
							  obs_module_text("SmartContext.Status.FromFallback"))
				 : resolution.category;
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

	// Merken, was wir selbst gesetzt haben. Nur daran laesst sich beim naechsten
	// Abruf erkennen, ob der Titel auf dem Kanal von uns stammt oder vom Nutzer.
	if (!title.isEmpty() && title != selbstGesetzterTitel) {
		selbstGesetzterTitel = title;
		ConfigManager::get().setSmartContextOwnTitle(title);
	}
	if (!title.isEmpty() && !zuletztGesetzteTitel.contains(title)) {
		zuletztGesetzteTitel.append(title);
		while (zuletztGesetzteTitel.size() > 5)
			zuletztGesetzteTitel.removeFirst();
	}

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
