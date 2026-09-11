#ifndef TWITCHAUTHMANAGER_H
#define TWITCHAUTHMANAGER_H

#pragma once

#include <QObject>
#include <QString>
#include <QFuture>
#include <QJsonObject>
#include <QThreadPool>
#include <QPointer>
#include <QList>
#include <QStringList>

class QTcpServer;
class QTcpSocket;
class QTimer;

class TwitchAuthManager : public QObject {
	Q_OBJECT

public:
	enum UpdateResult { Failed, Success, AuthError };

	// Everything OBS' own Twitch "Stream Information" dock shows that Helix
	// actually exposes. Missing from the API, and therefore from here: the go live
	// notification and the audience setting.
	struct ChannelInfo {
		bool valid = false;
		QString title;
		QString gameId;
		QString gameName;
		QString language;               // broadcaster_language, e.g. "de"
		QStringList tags;               // up to 10, 25 characters each
		QStringList classificationLabels; // enabled content_classification_labels
		bool brandedContent = false;
		QString error;
	};

	struct Category {
		QString id;
		QString name;
		QString boxArtUrl;
	};

	struct ClassificationLabel {
		QString id;
		QString name;
		QString description;
	};

public:
	static TwitchAuthManager &get()
	{
		static TwitchAuthManager instance;
		return instance;
	}

	QString getAccessToken();
	QString getClientId();
	QString getUserId();
	std::pair<QString, QString> getTokenUserInfo();

	QFuture<QString> getGameId(const QString &gameName);
	QFuture<UpdateResult> updateChannelCategory(const QString &gameId);
	QFuture<QString> getChannelTitle();
	QFuture<UpdateResult> updateChannelCategory(const QString &gameId, const QString &title);
	QFuture<bool> sendChatMessage(const QString &broadcasterId, const QString &senderId, const QString &message);

	QFuture<QString> getChannelCategory();

	// Stream information panel (added by the kicodebyts fork)
	QFuture<ChannelInfo> getChannelInfo();
	QFuture<UpdateResult> updateChannelInfo(const ChannelInfo &info);
	QFuture<QList<Category>> searchCategories(const QString &query);
	QFuture<Category> getCategoryById(const QString &gameId);
	QFuture<QList<ClassificationLabel>> getClassificationLabels();

	// A channel update has to state every label, enabled or not, so the panel hands
	// over the list it got from Twitch once it has it.
	void rememberClassificationLabelIds(const QStringList &ids);

	// Twitch derives this one from the category. Sending it, even as false, fails
	// the whole request with "label provided is not editable by the caller", and
	// the endpoint takes at most six labels anyway.
	static constexpr const char *NON_EDITABLE_LABEL = "MatureGame";

signals:
	void authenticationFinished(bool success, const QString &info);
	void reauthenticationNeeded();
	void authenticationDataNeedsClearing();
	void authenticationTimerTick(int remainingSeconds);

private slots:
	void onNewConnection();
	void onAuthTimerTick();

public slots:
	void startAuthentication(int mode = -1, int unifiedAuth = -1);
	void clearAuthentication();
	void loadToken();
	void shutdown();

private:
	TwitchAuthManager(QObject *parent = nullptr);
	~TwitchAuthManager();

	QFuture<std::pair<long, QString>> performGET(const QString &url, const QString &token);
	QFuture<std::pair<long, QString>> performPATCH(const QString &url, const QJsonObject &body,
						       const QString &token);
	QFuture<std::pair<long, QString>> performPOST(const QString &url, const QJsonObject &body,
						      const QString &token);

	std::pair<long, QString> performGETSync(const QString &url, const QString &token);
	std::pair<long, QString> performPATCHSync(const QString &url, const QJsonObject &body, const QString &token);
	std::pair<long, QString> performPOSTSync(const QString &url, const QJsonObject &body, const QString &token);

	QString accessToken;
	QString userId;
	QStringList knownClassificationLabelIds;
	bool isAuthenticating = false;

	QTcpServer *server = nullptr;
	QTimer *authTimeoutTimer = nullptr;
	int authRemainingSeconds = 0;
	QThreadPool threadPool;
	QList<QPointer<QTcpSocket>> clientSockets;

	static constexpr const char *CLIENT_ID = "wl4mx2l4sgmdvpwoek6pjronpor9en";
	static constexpr const char *REDIRECT_URI = "http://localhost:30000/";
};

#endif // TWITCHAUTHMANAGER_H
