/*
 * Simple editor for the list of applications Smart Context must ignore.
 *
 * Added by the kicodebyts fork of FabioZumbi12/game-detector.
 * Licensed under the GNU General Public License v2.0, like the rest of the plugin.
 */

#include "IgnoredAppsDialog.h"
#include "SmartContextManager.h"
#include "ConfigManager.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>
#include <obs-module.h>

IgnoredAppsDialog::IgnoredAppsDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(obs_module_text("IgnoredApps.WindowTitle"));
	setMinimumSize(520, 460);

	QVBoxLayout *mainLayout = new QVBoxLayout(this);

	QLabel *hintLabel = new QLabel(obs_module_text("IgnoredApps.Hint"));
	hintLabel->setWordWrap(true);
	hintLabel->setStyleSheet("color: #888; font-style: italic; margin-bottom: 6px;");
	mainLayout->addWidget(hintLabel);

	appList = new QListWidget();
	appList->setSelectionMode(QAbstractItemView::ExtendedSelection);
	mainLayout->addWidget(appList, 1);

	// One click to ignore whatever is in front right now, so nobody has to hunt
	// down the executable name of the thing that keeps stealing the category.
	currentAppLabel = new QLabel();
	currentAppLabel->setWordWrap(true);
	mainLayout->addWidget(currentAppLabel);

	QHBoxLayout *buttonsLayout = new QHBoxLayout();
	addCurrentButton = new QPushButton(obs_module_text("IgnoredApps.AddCurrent"));
	addCurrentButton->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
	addManualButton = new QPushButton(obs_module_text("IgnoredApps.AddManual"));
	addManualButton->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
	removeButton = new QPushButton(obs_module_text("IgnoredApps.Remove"));
	removeButton->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));
	buttonsLayout->addWidget(addCurrentButton);
	buttonsLayout->addWidget(addManualButton);
	buttonsLayout->addWidget(removeButton);
	buttonsLayout->addStretch(1);
	mainLayout->addLayout(buttonsLayout);

	QHBoxLayout *dialogButtonsLayout = new QHBoxLayout();
	okButton = new QPushButton(obs_module_text("OK"));
	cancelButton = new QPushButton(obs_module_text("Cancel"));
	dialogButtonsLayout->addStretch(1);
	dialogButtonsLayout->addWidget(okButton);
	dialogButtonsLayout->addWidget(cancelButton);
	mainLayout->addLayout(dialogButtonsLayout);

	connect(addCurrentButton, &QPushButton::clicked, this, &IgnoredAppsDialog::onAddCurrentClicked);
	connect(addManualButton, &QPushButton::clicked, this, &IgnoredAppsDialog::onAddManualClicked);
	connect(removeButton, &QPushButton::clicked, this, &IgnoredAppsDialog::onRemoveClicked);
	connect(okButton, &QPushButton::clicked, this, [this]() {
		saveIgnoredApps();
		accept();
	});
	connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

	loadIgnoredApps();

	const QString current = SmartContextManager::get().activeProcess();
	if (current.isEmpty()) {
		addCurrentButton->setEnabled(false);
		currentAppLabel->setText(obs_module_text("IgnoredApps.NoCurrent"));
	} else {
		currentAppLabel->setText(QString(obs_module_text("IgnoredApps.Current")).arg(current));
		addCurrentButton->setEnabled(!alreadyListed(current));
	}
}

void IgnoredAppsDialog::loadIgnoredApps()
{
	appList->clear();
	for (const SmartContextRule &rule : SmartContextManager::loadRulesFromConfig()) {
		if (rule.ignore)
			addEntry(rule.process);
	}
	appList->sortItems();
}

void IgnoredAppsDialog::addEntry(const QString &process, bool select)
{
	QListWidgetItem *item = new QListWidgetItem(process, appList);
	if (select) {
		appList->setCurrentItem(item);
		appList->scrollToItem(item);
	}
}

bool IgnoredAppsDialog::alreadyListed(const QString &process) const
{
	for (int i = 0; i < appList->count(); ++i) {
		if (appList->item(i)->text().compare(process, Qt::CaseInsensitive) == 0)
			return true;
	}
	return false;
}

void IgnoredAppsDialog::onAddCurrentClicked()
{
	const QString current = SmartContextManager::get().activeProcess();
	if (current.isEmpty() || alreadyListed(current))
		return;

	addEntry(current, true);
	addCurrentButton->setEnabled(false);
}

void IgnoredAppsDialog::onAddManualClicked()
{
	bool accepted = false;
	QString process = QInputDialog::getText(this, obs_module_text("IgnoredApps.AddManual"),
					       obs_module_text("IgnoredApps.AddManual.Prompt"), QLineEdit::Normal,
					       QString(), &accepted)
				  .trimmed();
	if (!accepted || process.isEmpty())
		return;

	if (alreadyListed(process)) {
		QMessageBox::information(this, obs_module_text("IgnoredApps.AddManual"),
					 obs_module_text("IgnoredApps.AlreadyListed"));
		return;
	}

	addEntry(process, true);
}

void IgnoredAppsDialog::onRemoveClicked()
{
	qDeleteAll(appList->selectedItems());
}

void IgnoredAppsDialog::saveIgnoredApps()
{
	QStringList ignored;
	for (int i = 0; i < appList->count(); ++i) {
		const QString process = appList->item(i)->text().trimmed();
		if (!process.isEmpty())
			ignored << process;
	}

	obs_data_array_t *rulesArray = obs_data_array_create();

	// Keep every non-ignore rule exactly as it is; this dialog only owns the
	// ignore entries.
	for (const SmartContextRule &rule : SmartContextManager::loadRulesFromConfig()) {
		if (rule.ignore)
			continue;

		obs_data_t *item = obs_data_create();
		obs_data_set_bool(item, "enabled", rule.enabled);
		obs_data_set_string(item, "process", rule.process.toUtf8().constData());
		obs_data_set_string(item, "window", rule.window.toUtf8().constData());
		obs_data_set_string(item, "category", rule.category.toUtf8().constData());
		obs_data_set_string(item, "title_template", rule.titleTemplate.toUtf8().constData());
		obs_data_set_int(item, "delay", rule.delaySeconds);
		obs_data_set_bool(item, "ignore", false);
		obs_data_set_int(item, "priority", rule.priority);
		obs_data_array_push_back(rulesArray, item);
		obs_data_release(item);
	}

	for (const QString &process : ignored) {
		obs_data_t *item = obs_data_create();
		obs_data_set_bool(item, "enabled", true);
		obs_data_set_string(item, "process", process.toUtf8().constData());
		obs_data_set_string(item, "window", "");
		obs_data_set_string(item, "category", "");
		obs_data_set_string(item, "title_template", "");
		obs_data_set_int(item, "delay", 0);
		obs_data_set_bool(item, "ignore", true);
		// Above every category rule, so an ignored app always wins.
		obs_data_set_int(item, "priority", 90);
		obs_data_array_push_back(rulesArray, item);
		obs_data_release(item);
	}

	ConfigManager::get().saveSmartContextRules(rulesArray);
	obs_data_array_release(rulesArray);

	SmartContextManager::get().reloadRules();
}
