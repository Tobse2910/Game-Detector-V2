#include "ConfigManager.h"
#include <obs-data.h>
#include <obs-module.h>
#include <QFileInfo>
#include <QDir>

ConfigManager &ConfigManager::get()
{
	static ConfigManager instance;
	return instance;
}

ConfigManager::ConfigManager(QObject *parent) : QObject(parent) {}

void ConfigManager::load()
{
	this->settings = obs_data_create_from_json_file(obs_module_config_path("config.json"));

	if (!settings) {
		blog(LOG_INFO, "[GameDetector] No config found. Creating new one...");
		settings = obs_data_create();

		obs_data_set_string(settings, COMMAND_KEY, "!setgame {game}");
		obs_data_set_string(settings, COMMAND_NO_GAME_KEY, "!setgame just chatting");
		obs_data_set_bool(settings, EXECUTE_AUTOMATICALLY_KEY, false);
		obs_data_set_bool(settings, BLOCK_AUTO_UPDATE_WHILE_STREAMING_KEY, false);
		// Match the defaults an upgraded config gets below: change the category
		// through the API rather than posting a chat command for a bot to pick up.
		// Smart Context Mode is useless without it.
		obs_data_set_int(settings, ACTION_MODE_KEY, 1);
		obs_data_set_bool(settings, TWITCH_UNIFIED_AUTH_KEY, true);
		obs_data_set_bool(settings, SCAN_STEAM_KEY, true);
		obs_data_set_bool(settings, SCAN_EPIC_KEY, true);
		obs_data_set_bool(settings, SCAN_GOG_KEY, true);
		obs_data_set_bool(settings, SCAN_UBISOFT_KEY, true);
		obs_data_set_bool(settings, SCAN_ON_STARTUP_KEY, true);
		obs_data_set_bool(settings, SCAN_PERIODICALLY_KEY, false);
		obs_data_set_int(settings, SCAN_PERIODICALLY_INTERVAL_KEY, 60);
		obs_data_set_string(settings, TWITCH_CHANNEL_LOGIN_KEY, "");
		obs_data_set_int(settings, ACTION_DELAY_KEY, 30);

		obs_data_set_bool(settings, SMART_CONTEXT_ENABLED_KEY, false);
		obs_data_set_bool(settings, SMART_CONTEXT_LOCK_KEY, false);
		obs_data_set_int(settings, SMART_CONTEXT_DELAY_KEY, 300);
		obs_data_set_int(settings, SMART_CONTEXT_COOLDOWN_KEY, 60);
		obs_data_set_int(settings, SMART_CONTEXT_GRACE_KEY, 60);

		obs_data_array_t *default_rules = createDefaultSmartContextRules();
		obs_data_set_array(settings, SMART_CONTEXT_RULES_KEY, default_rules);
		obs_data_array_release(default_rules);

		obs_data_array_t *empty_array = obs_data_array_create();
		obs_data_set_array(settings, MANUAL_GAMES_KEY, empty_array);
		obs_data_array_release(empty_array);

		obs_data_array_t *empty_hotkey_array = obs_data_array_create();
		obs_data_set_array(settings, HOTKEY_SET_GAME_KEY, empty_hotkey_array);
		obs_data_array_release(empty_hotkey_array);

		empty_hotkey_array = obs_data_array_create();
		obs_data_set_array(settings, HOTKEY_RESCAN_GAMES_KEY, empty_hotkey_array);
		obs_data_array_release(empty_hotkey_array);

		empty_hotkey_array = obs_data_array_create();
		obs_data_set_array(settings, HOTKEY_SET_JUST_CHATTING_KEY, empty_hotkey_array);
		obs_data_array_release(empty_hotkey_array);

		return;
	}

	blog(LOG_INFO, "[GameDetector] Settings loaded.");

	if (!obs_data_has_user_value(settings, COMMAND_KEY))
		obs_data_set_string(settings, COMMAND_KEY, "!setgame {game}");

	if (!obs_data_has_user_value(settings, COMMAND_NO_GAME_KEY))
		obs_data_set_string(settings, COMMAND_NO_GAME_KEY, "!setgame just chatting");

	if (!obs_data_has_user_value(settings, TWITCH_REFRESH_TOKEN_KEY))
		obs_data_set_string(settings, TWITCH_REFRESH_TOKEN_KEY, "");

	if (!obs_data_has_user_value(settings, TWITCH_USER_ID_KEY))
		obs_data_set_string(settings, TWITCH_USER_ID_KEY, "");

	if (!obs_data_has_user_value(settings, TWITCH_TOKEN_KEY))
		obs_data_set_string(settings, TWITCH_TOKEN_KEY, "");

	if (!obs_data_has_user_value(settings, EXECUTE_AUTOMATICALLY_KEY))
		obs_data_set_bool(settings, EXECUTE_AUTOMATICALLY_KEY, false);

	if (!obs_data_has_user_value(settings, BLOCK_AUTO_UPDATE_WHILE_STREAMING_KEY))
		obs_data_set_bool(settings, BLOCK_AUTO_UPDATE_WHILE_STREAMING_KEY, false);

	if (!obs_data_has_user_value(settings, ACTION_MODE_KEY))
		obs_data_set_int(settings, ACTION_MODE_KEY, 1);

	if (!obs_data_has_user_value(settings, TWITCH_UNIFIED_AUTH_KEY))
		obs_data_set_bool(settings, TWITCH_UNIFIED_AUTH_KEY, true);

	if (!obs_data_has_user_value(settings, SCAN_STEAM_KEY))
		obs_data_set_bool(settings, SCAN_STEAM_KEY, true);

	if (!obs_data_has_user_value(settings, SCAN_EPIC_KEY))
		obs_data_set_bool(settings, SCAN_EPIC_KEY, true);

	if (!obs_data_has_user_value(settings, SCAN_GOG_KEY))
		obs_data_set_bool(settings, SCAN_GOG_KEY, true);

	if (!obs_data_has_user_value(settings, SCAN_UBISOFT_KEY))
		obs_data_set_bool(settings, SCAN_UBISOFT_KEY, true);

	if (!obs_data_has_user_value(settings, SCAN_ON_STARTUP_KEY))
		obs_data_set_bool(settings, SCAN_ON_STARTUP_KEY, true);

	if (!obs_data_has_user_value(settings, SCAN_PERIODICALLY_KEY))
		obs_data_set_bool(settings, SCAN_PERIODICALLY_KEY, false);

	if (!obs_data_has_user_value(settings, SCAN_PERIODICALLY_INTERVAL_KEY))
		obs_data_set_int(settings, SCAN_PERIODICALLY_INTERVAL_KEY, 60);

	if (!obs_data_has_user_value(settings, TWITCH_CHANNEL_LOGIN_KEY))
		obs_data_set_string(settings, TWITCH_CHANNEL_LOGIN_KEY, "");

	if (!obs_data_has_user_value(settings, TROVO_CHANNEL_LOGIN_KEY))
		obs_data_set_string(settings, TROVO_CHANNEL_LOGIN_KEY, "");

	if (!obs_data_has_user_value(settings, ACTION_DELAY_KEY))
		obs_data_set_int(settings, ACTION_DELAY_KEY, 30);

	if (!obs_data_has_user_value(settings, SMART_CONTEXT_ENABLED_KEY))
		obs_data_set_bool(settings, SMART_CONTEXT_ENABLED_KEY, false);

	if (!obs_data_has_user_value(settings, SMART_CONTEXT_LOCK_KEY))
		obs_data_set_bool(settings, SMART_CONTEXT_LOCK_KEY, false);

	if (!obs_data_has_user_value(settings, SMART_CONTEXT_DELAY_KEY))
		obs_data_set_int(settings, SMART_CONTEXT_DELAY_KEY, 300);

	if (!obs_data_has_user_value(settings, SMART_CONTEXT_COOLDOWN_KEY))
		obs_data_set_int(settings, SMART_CONTEXT_COOLDOWN_KEY, 60);

	if (!obs_data_has_user_value(settings, SMART_CONTEXT_GRACE_KEY))
		obs_data_set_int(settings, SMART_CONTEXT_GRACE_KEY, 60);

	if (!obs_data_has_user_value(settings, SMART_CONTEXT_RULES_KEY)) {
		obs_data_array_t *default_rules = createDefaultSmartContextRules();
		obs_data_set_array(settings, SMART_CONTEXT_RULES_KEY, default_rules);
		obs_data_array_release(default_rules);
	}

	if (!obs_data_has_user_value(settings, MANUAL_GAMES_KEY)) {
		obs_data_array_t *empty_array = obs_data_array_create();
		obs_data_set_array(settings, MANUAL_GAMES_KEY, empty_array);
		obs_data_array_release(empty_array);
	} else {
		obs_data_array_t *games = obs_data_get_array(settings, MANUAL_GAMES_KEY);
		for (size_t i = 0; i < obs_data_array_count(games); ++i) {
			obs_data_t *item = obs_data_array_item(games, i);
			if (!obs_data_has_user_value(item, "enabled"))
				obs_data_set_bool(item, "enabled", true);
			obs_data_release(item);
		}
		obs_data_array_release(games);
	}

	if (!obs_data_has_user_value(settings, HOTKEY_SET_GAME_KEY)) {
		obs_data_array_t *empty_hotkey_array = obs_data_array_create();
		obs_data_set_array(settings, HOTKEY_SET_GAME_KEY, empty_hotkey_array);
		obs_data_array_release(empty_hotkey_array);
	}

	if (!obs_data_has_user_value(settings, HOTKEY_RESCAN_GAMES_KEY)) {
		obs_data_array_t *empty_hotkey_array = obs_data_array_create();
		obs_data_set_array(settings, HOTKEY_RESCAN_GAMES_KEY, empty_hotkey_array);
		obs_data_array_release(empty_hotkey_array);
	}

	if (!obs_data_has_user_value(settings, HOTKEY_SET_JUST_CHATTING_KEY)) {
		obs_data_array_t *empty_hotkey_array = obs_data_array_create();
		obs_data_set_array(settings, HOTKEY_SET_JUST_CHATTING_KEY, empty_hotkey_array);
		obs_data_array_release(empty_hotkey_array);
	}
}

void ConfigManager::save(obs_data_t *data)
{
	if (!data) {
		blog(LOG_ERROR, "[GameDetector] Attempt to save null config.");
		return;
	}

	const char *config_path_c = obs_module_config_path("config.json");
	if (!config_path_c) {
		blog(LOG_ERROR, "[GameDetector] Invalid path when saving config.");
		return;
	}

	QString path = QString::fromUtf8(config_path_c);
	QFileInfo info(path);
	QDir dir = info.dir();

	if (!dir.exists())
		dir.mkpath(".");

	if (obs_data_save_json(data, config_path_c)) {
		blog(LOG_INFO, "[GameDetector] Config salva em: %s", config_path_c);
		emit settingsSaved();
	} else {
		blog(LOG_WARNING, "[GameDetector] Failed to save config to: %s", config_path_c);
	}
}

void ConfigManager::shutdown()
{
	if (settings) {
		obs_data_release(settings);
		settings = nullptr;
	}
}

void ConfigManager::saveManualGames(obs_data_array_t *gamesArray)
{
	if (!settings)
		return;

	obs_data_set_array(settings, MANUAL_GAMES_KEY, gamesArray);
	save(settings);
}

obs_data_array_t *ConfigManager::getHotkeyData(const char *key) const
{
	if (!settings)
		return nullptr;
	return obs_data_get_array(settings, key);
}

void ConfigManager::setHotkeyData(const char *key, obs_data_array_t *hotkeyArray)
{
	if (!settings)
		return;
	obs_data_set_array(settings, key, hotkeyArray);
}

obs_data_t *ConfigManager::getSettings() const
{
	return settings;
}

QString ConfigManager::getTwitchToken() const
{
	if (!settings)
		return "";
	return QString::fromUtf8(obs_data_get_string(settings, TWITCH_TOKEN_KEY));
}

QString ConfigManager::getTwitchRefreshToken() const
{
	if (!settings)
		return "";
	return QString::fromUtf8(obs_data_get_string(settings, TWITCH_REFRESH_TOKEN_KEY));
}

QString ConfigManager::getTwitchUserId() const
{
	if (!settings)
		return "";
	return QString::fromUtf8(obs_data_get_string(settings, TWITCH_USER_ID_KEY));
}

QString ConfigManager::getTrovoToken() const
{
	if (!settings)
		return "";
	return QString::fromUtf8(obs_data_get_string(settings, TROVO_TOKEN_KEY));
}

QString ConfigManager::getTrovoUserId() const
{
	if (!settings)
		return "";
	return QString::fromUtf8(obs_data_get_string(settings, TROVO_USER_ID_KEY));
}

QString ConfigManager::getTrovoChannelLogin() const
{
	if (!settings)
		return "";
	return QString::fromUtf8(obs_data_get_string(settings, TROVO_CHANNEL_LOGIN_KEY));
}

QString ConfigManager::getCommand() const
{
	if (!settings)
		return "!setgame {game}";
	return QString::fromUtf8(obs_data_get_string(settings, COMMAND_KEY));
}

obs_data_array_t *ConfigManager::getManualGames() const
{
	if (!settings)
		return nullptr;
	return obs_data_get_array(settings, MANUAL_GAMES_KEY);
}

QString ConfigManager::getNoGameCommand() const
{
	if (!settings)
		return "!setgame just chatting";
	return QString::fromUtf8(obs_data_get_string(settings, COMMAND_NO_GAME_KEY));
}

bool ConfigManager::getExecuteAutomatically() const
{
	if (!settings)
		return false;
	return obs_data_get_bool(settings, EXECUTE_AUTOMATICALLY_KEY);
}

bool ConfigManager::getBlockAutoUpdateWhileStreaming() const
{
	if (!settings)
		return false;
	return obs_data_get_bool(settings, BLOCK_AUTO_UPDATE_WHILE_STREAMING_KEY);
}

int ConfigManager::getActionMode() const
{
	if (!settings)
		return 0;
	return (int)obs_data_get_int(settings, ACTION_MODE_KEY);
}

QString ConfigManager::getTwitchChannelLogin() const
{
	if (!settings)
		return "";
	return QString::fromUtf8(obs_data_get_string(settings, TWITCH_CHANNEL_LOGIN_KEY));
}

bool ConfigManager::getUnifiedAuth() const
{
	if (!settings)
		return true;
	return obs_data_get_bool(settings, TWITCH_UNIFIED_AUTH_KEY);
}

bool ConfigManager::getScanSteam() const
{
	if (!settings)
		return true;
	return obs_data_get_bool(settings, SCAN_STEAM_KEY);
}

bool ConfigManager::getScanEpic() const
{
	if (!settings)
		return true;
	return obs_data_get_bool(settings, SCAN_EPIC_KEY);
}

bool ConfigManager::getScanGog() const
{
	if (!settings)
		return true;
	return obs_data_get_bool(settings, SCAN_GOG_KEY);
}

bool ConfigManager::getScanUbisoft() const
{
	if (!settings)
		return true;
	return obs_data_get_bool(settings, SCAN_UBISOFT_KEY);
}

bool ConfigManager::getScanOnStartup() const
{
	if (!settings)
		return true;
	return obs_data_get_bool(settings, SCAN_ON_STARTUP_KEY);
}

bool ConfigManager::getScanPeriodically() const
{
	if (!settings)
		return false;
	return obs_data_get_bool(settings, SCAN_PERIODICALLY_KEY);
}

int ConfigManager::getScanPeriodicallyInterval() const
{
	if (!settings)
		return 60;
	return (int)obs_data_get_int(settings, SCAN_PERIODICALLY_INTERVAL_KEY);
}

int ConfigManager::getActionDelay() const
{
	if (!settings)
		return 30;
	return (int)obs_data_get_int(settings, ACTION_DELAY_KEY);
}

void ConfigManager::setTwitchToken(const QString &value)
{
	if (!settings)
		return;
	obs_data_set_string(settings, TWITCH_TOKEN_KEY, value.toUtf8().constData());
}

void ConfigManager::setTwitchRefreshToken(const QString &value)
{
	if (!settings)
		return;
	obs_data_set_string(settings, TWITCH_REFRESH_TOKEN_KEY, value.toUtf8().constData());
}

void ConfigManager::setTwitchUserId(const QString &value)
{
	if (!settings)
		return;
	obs_data_set_string(settings, TWITCH_USER_ID_KEY, value.toUtf8().constData());
}

void ConfigManager::setTrovoToken(const QString &value)
{
	if (!settings)
		return;
	obs_data_set_string(settings, TROVO_TOKEN_KEY, value.toUtf8().constData());
}

void ConfigManager::setTrovoUserId(const QString &value)
{
	if (!settings)
		return;
	obs_data_set_string(settings, TROVO_USER_ID_KEY, value.toUtf8().constData());
}

void ConfigManager::setTrovoChannelLogin(const QString &value)
{
	if (!settings)
		return;
	obs_data_set_string(settings, TROVO_CHANNEL_LOGIN_KEY, value.toUtf8().constData());
}

void ConfigManager::setTwitchChannelLogin(const QString &value)
{
	if (!settings)
		return;
	obs_data_set_string(settings, TWITCH_CHANNEL_LOGIN_KEY, value.toUtf8().constData());
}

/* ------------------------------------------------------------------------ *
 * Smart Context Mode (added by the kicodebyts fork)
 * ------------------------------------------------------------------------ */

bool ConfigManager::getSmartContextEnabled() const
{
	if (!settings)
		return false;
	return obs_data_get_bool(settings, SMART_CONTEXT_ENABLED_KEY);
}

void ConfigManager::setSmartContextEnabled(bool value)
{
	if (!settings)
		return;
	obs_data_set_bool(settings, SMART_CONTEXT_ENABLED_KEY, value);
}

bool ConfigManager::getSmartContextLock() const
{
	if (!settings)
		return false;
	return obs_data_get_bool(settings, SMART_CONTEXT_LOCK_KEY);
}

void ConfigManager::setSmartContextLock(bool value)
{
	if (!settings)
		return;
	obs_data_set_bool(settings, SMART_CONTEXT_LOCK_KEY, value);
}

int ConfigManager::getSmartContextDelay() const
{
	if (!settings)
		return 300;
	int value = (int)obs_data_get_int(settings, SMART_CONTEXT_DELAY_KEY);
	return value > 0 ? value : 300;
}

void ConfigManager::setSmartContextDelay(int seconds)
{
	if (!settings)
		return;
	obs_data_set_int(settings, SMART_CONTEXT_DELAY_KEY, seconds);
}

int ConfigManager::getSmartContextSwitchCooldown() const
{
	if (!settings)
		return 60;
	int value = (int)obs_data_get_int(settings, SMART_CONTEXT_COOLDOWN_KEY);
	return value >= 0 ? value : 60;
}

int ConfigManager::getSmartContextGrace() const
{
	if (!settings)
		return 60;
	int value = (int)obs_data_get_int(settings, SMART_CONTEXT_GRACE_KEY);
	return value >= 0 ? value : 60;
}

obs_data_array_t *ConfigManager::getSmartContextRules() const
{
	if (!settings)
		return nullptr;
	return obs_data_get_array(settings, SMART_CONTEXT_RULES_KEY);
}

void ConfigManager::saveSmartContextRules(obs_data_array_t *rulesArray)
{
	if (!settings)
		return;

	obs_data_set_array(settings, SMART_CONTEXT_RULES_KEY, rulesArray);
	save(settings);
}

namespace {
obs_data_t *makeRule(const char *process, const char *category, const char *titleTemplate, bool ignore, int priority)
{
	obs_data_t *rule = obs_data_create();
	obs_data_set_bool(rule, "enabled", true);
	obs_data_set_string(rule, "process", process);
	obs_data_set_string(rule, "window", "");
	obs_data_set_string(rule, "category", category);
	obs_data_set_string(rule, "title_template", titleTemplate);
	obs_data_set_int(rule, "delay", 0); // 0 = use the global Smart Context delay
	obs_data_set_bool(rule, "ignore", ignore);
	obs_data_set_int(rule, "priority", priority);
	return rule;
}

void pushRule(obs_data_array_t *array, obs_data_t *rule)
{
	obs_data_array_push_back(array, rule);
	obs_data_release(rule);
}
} // namespace

obs_data_array_t *ConfigManager::createDefaultSmartContextRules()
{
	obs_data_array_t *rules = obs_data_array_create();

	static const char *const TITLE_CODING = "Coding & Development | !music | !PartyFlow";
	static const char *const TITLE_CHATTING = "Just Chatting | !music | !wunsch | !PartyFlow";
	static const char *const CATEGORY_CODING = "Software and Game Development";
	static const char *const CATEGORY_CHATTING = "Just Chatting";

	// Applications that must never take over the category. Highest priority so
	// they win over any wildcard rule below.
	static const char *const ignored[] = {"discord.exe",
					      "discordptb.exe",
					      "obs64.exe",
					      "obs32.exe",
					      "explorer.exe",
					      "*WaveLink*.exe", // Elgato.WaveLink.exe, WaveLink.exe, …
					      "StreamDeck.exe",
					      "Spotify.exe",
					      "steam.exe",
					      "steamwebhelper.exe",
					      "EpicGamesLauncher.exe",
					      "Launcher.exe", // Rockstar Games Launcher
					      "RockstarService.exe",
					      "SocialClubHelper.exe",
					      "Medal.exe",
					      "NVIDIA Overlay.exe",
					      "OBSBOT_Main.exe"};
	for (const char *process : ignored)
		pushRule(rules, makeRule(process, "", "", true, 90));

	// Games / explicit category rules.
	// The shipped executable is WardogsClient-Win64-Shipping.exe; the wildcard also
	// covers a launcher or a renamed build.
	pushRule(rules, makeRule("Wardogs*.exe", "WARDOGS", "WARDOGS | !music | web ansicht !wunsch | !PartyFlow",
				 false, 50));
	pushRule(rules, makeRule("FiveM.exe", "Grand Theft Auto V",
				 "BSG RP | Boris Brown | !music | !wunsch | !PartyFlow", false, 50));
	pushRule(rules, makeRule("FiveM_*.exe", "Grand Theft Auto V",
				 "BSG RP | Boris Brown | !music | !wunsch | !PartyFlow", false, 50));

	// Development tools.
	pushRule(rules, makeRule("Code.exe", CATEGORY_CODING, TITLE_CODING, false, 40));
	pushRule(rules, makeRule("Code - Insiders.exe", CATEGORY_CODING, TITLE_CODING, false, 40));
	pushRule(rules, makeRule("devenv.exe", CATEGORY_CODING, TITLE_CODING, false, 40));
	pushRule(rules, makeRule("idea64.exe", CATEGORY_CODING, TITLE_CODING, false, 40));
	pushRule(rules, makeRule("pycharm64.exe", CATEGORY_CODING, TITLE_CODING, false, 40));
	pushRule(rules, makeRule("webstorm64.exe", CATEGORY_CODING, TITLE_CODING, false, 40));
	pushRule(rules, makeRule("rider64.exe", CATEGORY_CODING, TITLE_CODING, false, 40));
	pushRule(rules, makeRule("studio64.exe", CATEGORY_CODING, TITLE_CODING, false, 40));

	// Browsers.
	pushRule(rules, makeRule("firefox.exe", CATEGORY_CHATTING, TITLE_CHATTING, false, 10));
	pushRule(rules, makeRule("chrome.exe", CATEGORY_CHATTING, TITLE_CHATTING, false, 10));
	pushRule(rules, makeRule("msedge.exe", CATEGORY_CHATTING, TITLE_CHATTING, false, 10));

	return rules;
}
