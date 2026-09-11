/*
 * Simple editor for the list of applications Smart Context must ignore.
 *
 * The full rule editor can do this too, but burying "Discord should never touch
 * my category" in a nine column table is the wrong place for the one thing
 * people actually change often.
 *
 * Added by the kicodebyts fork of FabioZumbi12/game-detector.
 * Licensed under the GNU General Public License v2.0, like the rest of the plugin.
 */

#ifndef IGNOREDAPPSDIALOG_H
#define IGNOREDAPPSDIALOG_H

#include <QDialog>

class QListWidget;
class QPushButton;
class QLabel;

class IgnoredAppsDialog : public QDialog {
	Q_OBJECT

public:
	explicit IgnoredAppsDialog(QWidget *parent = nullptr);

private:
	void loadIgnoredApps();
	void saveIgnoredApps();
	void addEntry(const QString &process, bool select = false);
	bool alreadyListed(const QString &process) const;

	QListWidget *appList = nullptr;
	QLabel *currentAppLabel = nullptr;
	QPushButton *addCurrentButton = nullptr;
	QPushButton *addManualButton = nullptr;
	QPushButton *removeButton = nullptr;
	QPushButton *okButton = nullptr;
	QPushButton *cancelButton = nullptr;

private slots:
	void onAddCurrentClicked();
	void onAddManualClicked();
	void onRemoveClicked();
};

#endif // IGNOREDAPPSDIALOG_H
