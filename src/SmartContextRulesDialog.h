/*
 * Editor for the Smart Context rule list.
 *
 * Added by the kicodebyts fork of FabioZumbi12/game-detector.
 * Licensed under the GNU General Public License v2.0, like the rest of the plugin.
 */

#ifndef SMARTCONTEXTRULESDIALOG_H
#define SMARTCONTEXTRULESDIALOG_H

#include <QDialog>

class QTableWidget;
class QPushButton;

struct SmartContextRule;

class SmartContextRulesDialog : public QDialog {
	Q_OBJECT

public:
	explicit SmartContextRulesDialog(QWidget *parent = nullptr);

private:
	void loadRules();
	void saveRules();
	void addRuleRow(const SmartContextRule &rule);

	QTableWidget *rulesTable = nullptr;
	QPushButton *addRuleButton = nullptr;
	QPushButton *restoreDefaultsButton = nullptr;
	QPushButton *okButton = nullptr;
	QPushButton *cancelButton = nullptr;

	enum Column {
		ColEnabled = 0,
		ColProcess,
		ColWindow,
		ColCategory,
		ColTitle,
		ColDelay,
		ColIgnore,
		ColPriority,
		ColActions,
		ColumnCount
	};

private slots:
	void onAddRuleClicked();
	void onRestoreDefaultsClicked();
};

#endif // SMARTCONTEXTRULESDIALOG_H
