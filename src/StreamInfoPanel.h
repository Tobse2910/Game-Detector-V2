#ifndef STREAMINFOPANEL_H
#define STREAMINFOPANEL_H

#pragma once

#include "TwitchAuthManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

// Added by the kicodebyts fork.
//
// Replaces OBS' own Twitch "Stream Information" dock, which is a browser dock on a
// Twitch dashboard page: it cannot be written to, never refreshes itself, and its
// Done button posts whatever stale value it still shows. Everything here goes
// straight through the Helix API instead.
//
// Covered: title, category (with search and box art), tags, stream language,
// content classification labels and branded content. Not covered because Helix has
// no endpoint for them: the go live notification and the audience setting.
class StreamInfoPanel : public QGroupBox {
	Q_OBJECT

public:
	explicit StreamInfoPanel(QWidget *parent = nullptr);

	// Loads the current state from Twitch. Skipped while the user has unsaved
	// edits, unless force is set.
	void reload(bool force = false);

	// True once a field was edited and neither applied nor discarded.
	bool hasPendingEdits() const { return dirty; }

private slots:
	void onReloadFinished();
	void onCategorySearchFinished();
	void onBoxArtFinished();
	void onLabelsFinished();
	void onApplyFinished();

	void onSearchTextChanged(const QString &text);
	void onSuggestionPicked(QListWidgetItem *item);
	void onAddTag();
	void onRemoveTag();
	void onApply();
	void onDiscard();

private:
	void buildUi();
	void markDirty();
	void setEditsPending(bool pending);
	void showStatus(const QString &text, bool error = false);
	void fillFrom(const TwitchAuthManager::ChannelInfo &info);
	void requestBoxArt(const QString &boxArtUrl);
	void applyBoxArt(const QString &localFile);
	TwitchAuthManager::ChannelInfo collectFromUi() const;

	// Title
	QPlainTextEdit *titleEdit = nullptr;
	QLabel *titleCounter = nullptr;

	// Category
	QLineEdit *categorySearch = nullptr;
	QListWidget *categorySuggestions = nullptr;
	QLabel *categoryBoxArt = nullptr;
	QLabel *categoryName = nullptr;
	QTimer *searchDebounce = nullptr;
	QString pickedGameId;
	QString pickedGameName;

	// Tags
	QListWidget *tagList = nullptr;
	QLineEdit *tagInput = nullptr;
	QPushButton *tagAddButton = nullptr;
	QPushButton *tagRemoveButton = nullptr;
	QLabel *tagCounter = nullptr;

	// Remaining fields
	QComboBox *languageCombo = nullptr;
	QCheckBox *brandedContentCheck = nullptr;
	QVBoxLayout *labelsLayout = nullptr;
	QList<QCheckBox *> labelChecks;

	QPushButton *applyButton = nullptr;
	QPushButton *discardButton = nullptr;
	QPushButton *reloadButton = nullptr;
	QPushButton *dashboardButton = nullptr;
	QLabel *statusLabel = nullptr;
	QLabel *pendingHint = nullptr;

	QFutureWatcher<TwitchAuthManager::ChannelInfo> *reloadWatcher = nullptr;
	QFutureWatcher<QList<TwitchAuthManager::Category>> *searchWatcher = nullptr;
	QFutureWatcher<TwitchAuthManager::Category> *boxArtWatcher = nullptr;
	QFutureWatcher<QList<TwitchAuthManager::ClassificationLabel>> *labelsWatcher = nullptr;
	QFutureWatcher<TwitchAuthManager::UpdateResult> *applyWatcher = nullptr;
	QFutureWatcher<QString> *boxArtDownloadWatcher = nullptr;

	bool dirty = false;
	bool filling = false; // suppresses dirty marking while fields are being filled
	TwitchAuthManager::ChannelInfo lastLoaded;
};

#endif // STREAMINFOPANEL_H
