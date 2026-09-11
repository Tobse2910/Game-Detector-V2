/*
 * Editor for the Smart Context rule list.
 *
 * Added by the kicodebyts fork of FabioZumbi12/game-detector.
 * Licensed under the GNU General Public License v2.0, like the rest of the plugin.
 */

#include "SmartContextRulesDialog.h"
#include "SmartContextManager.h"
#include "ConfigManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>
#include <obs-module.h>

namespace {

// Centers a checkbox in a table cell.
QWidget *makeCheckboxCell(bool checked, QCheckBox **out)
{
	QCheckBox *box = new QCheckBox();
	box->setChecked(checked);

	QWidget *container = new QWidget();
	QHBoxLayout *layout = new QHBoxLayout(container);
	layout->addWidget(box);
	layout->setAlignment(Qt::AlignCenter);
	layout->setContentsMargins(0, 0, 0, 0);

	*out = box;
	return container;
}

QComboBox *makeDelayCombo(int delaySeconds)
{
	QComboBox *combo = new QComboBox();
	combo->addItem(obs_module_text("SmartContext.Delay.Default"), 0);
	combo->addItem(obs_module_text("SmartContext.Delay.30s"), 30);
	combo->addItem(obs_module_text("SmartContext.Delay.1m"), 60);
	combo->addItem(obs_module_text("SmartContext.Delay.2m"), 120);
	combo->addItem(obs_module_text("SmartContext.Delay.5m"), 300);
	combo->addItem(obs_module_text("SmartContext.Delay.10m"), 600);

	int index = combo->findData(delaySeconds);
	if (index < 0) {
		// A value someone typed into config.json by hand; keep it rather than losing it.
		combo->addItem(QString("%1 s").arg(delaySeconds), delaySeconds);
		index = combo->count() - 1;
	}
	combo->setCurrentIndex(index);

	return combo;
}

} // namespace

SmartContextRulesDialog::SmartContextRulesDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(obs_module_text("SmartContext.Rules.WindowTitle"));
	setMinimumSize(1000, 560);

	QVBoxLayout *mainLayout = new QVBoxLayout(this);

	QGroupBox *rulesGroup = new QGroupBox(obs_module_text("SmartContext.Rules.GroupTitle"));
	QVBoxLayout *rulesLayout = new QVBoxLayout();

	QLabel *hintLabel = new QLabel(obs_module_text("SmartContext.Rules.Hint"));
	hintLabel->setWordWrap(true);
	hintLabel->setStyleSheet("color: #888; font-style: italic; margin-bottom: 5px;");
	rulesLayout->addWidget(hintLabel);

	rulesTable = new QTableWidget();
	rulesTable->setColumnCount(ColumnCount);
	rulesTable->setHorizontalHeaderLabels(QStringList()
					      << obs_module_text("SmartContext.Table.Enabled")
					      << obs_module_text("SmartContext.Table.Process")
					      << obs_module_text("SmartContext.Table.Window")
					      << obs_module_text("SmartContext.Table.Category")
					      << obs_module_text("SmartContext.Table.Title")
					      << obs_module_text("SmartContext.Table.Delay")
					      << obs_module_text("SmartContext.Table.Ignore")
					      << obs_module_text("SmartContext.Table.Priority")
					      << obs_module_text("Table.Header.Actions"));

	QHeaderView *header = rulesTable->horizontalHeader();
	header->setSectionResizeMode(ColEnabled, QHeaderView::ResizeToContents);
	header->setSectionResizeMode(ColProcess, QHeaderView::Interactive);
	header->setSectionResizeMode(ColWindow, QHeaderView::Interactive);
	header->setSectionResizeMode(ColCategory, QHeaderView::Interactive);
	header->setSectionResizeMode(ColTitle, QHeaderView::Stretch);
	header->setSectionResizeMode(ColDelay, QHeaderView::ResizeToContents);
	header->setSectionResizeMode(ColIgnore, QHeaderView::ResizeToContents);
	header->setSectionResizeMode(ColPriority, QHeaderView::ResizeToContents);
	header->setSectionResizeMode(ColActions, QHeaderView::ResizeToContents);
	rulesTable->setColumnWidth(ColProcess, 170);
	rulesTable->setColumnWidth(ColWindow, 130);
	rulesTable->setColumnWidth(ColCategory, 190);

	rulesLayout->addWidget(rulesTable);

	QHBoxLayout *tableButtonsLayout = new QHBoxLayout();
	addRuleButton = new QPushButton(obs_module_text("SmartContext.Rules.Add"));
	addRuleButton->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
	restoreDefaultsButton = new QPushButton(obs_module_text("SmartContext.Rules.RestoreDefaults"));
	restoreDefaultsButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
	tableButtonsLayout->addWidget(addRuleButton);
	tableButtonsLayout->addWidget(restoreDefaultsButton);
	tableButtonsLayout->addStretch(1);
	rulesLayout->addLayout(tableButtonsLayout);

	rulesGroup->setLayout(rulesLayout);
	mainLayout->addWidget(rulesGroup);

	QHBoxLayout *dialogButtonsLayout = new QHBoxLayout();
	okButton = new QPushButton(obs_module_text("OK"));
	cancelButton = new QPushButton(obs_module_text("Cancel"));
	dialogButtonsLayout->addStretch(1);
	dialogButtonsLayout->addWidget(okButton);
	dialogButtonsLayout->addWidget(cancelButton);
	mainLayout->addLayout(dialogButtonsLayout);

	connect(addRuleButton, &QPushButton::clicked, this, &SmartContextRulesDialog::onAddRuleClicked);
	connect(restoreDefaultsButton, &QPushButton::clicked, this,
		&SmartContextRulesDialog::onRestoreDefaultsClicked);
	connect(okButton, &QPushButton::clicked, this, [this]() {
		saveRules();
		accept();
	});
	connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

	loadRules();
}

void SmartContextRulesDialog::loadRules()
{
	rulesTable->setRowCount(0);
	const QList<SmartContextRule> rules = SmartContextManager::loadRulesFromConfig();
	for (const SmartContextRule &rule : rules)
		addRuleRow(rule);
}

void SmartContextRulesDialog::addRuleRow(const SmartContextRule &rule)
{
	const int row = rulesTable->rowCount();
	rulesTable->insertRow(row);

	QCheckBox *enabledBox = nullptr;
	rulesTable->setCellWidget(row, ColEnabled, makeCheckboxCell(rule.enabled, &enabledBox));

	rulesTable->setItem(row, ColProcess, new QTableWidgetItem(rule.process));
	rulesTable->setItem(row, ColWindow, new QTableWidgetItem(rule.window));
	rulesTable->setItem(row, ColCategory, new QTableWidgetItem(rule.category));
	rulesTable->setItem(row, ColTitle, new QTableWidgetItem(rule.titleTemplate));

	rulesTable->setCellWidget(row, ColDelay, makeDelayCombo(rule.delaySeconds));

	QCheckBox *ignoreBox = nullptr;
	rulesTable->setCellWidget(row, ColIgnore, makeCheckboxCell(rule.ignore, &ignoreBox));

	QSpinBox *prioritySpin = new QSpinBox();
	prioritySpin->setRange(0, 999);
	prioritySpin->setValue(rule.priority);
	rulesTable->setCellWidget(row, ColPriority, prioritySpin);

	// An ignore rule has no category or title of its own.
	auto applyIgnoreState = [this, ignoreBox]() {
		for (int i = 0; i < rulesTable->rowCount(); ++i) {
			QWidget *cell = rulesTable->cellWidget(i, ColIgnore);
			if (!cell || cell->findChild<QCheckBox *>() != ignoreBox)
				continue;

			const bool ignored = ignoreBox->isChecked();
			for (int column : {ColCategory, ColTitle}) {
				QTableWidgetItem *item = rulesTable->item(i, column);
				if (!item)
					continue;
				Qt::ItemFlags flags = item->flags();
				if (ignored)
					flags &= ~Qt::ItemIsEditable;
				else
					flags |= Qt::ItemIsEditable;
				item->setFlags(flags);
			}
			break;
		}
	};
	connect(ignoreBox, &QCheckBox::toggled, this, applyIgnoreState);
	applyIgnoreState();

	QPushButton *removeButton = new QPushButton();
	removeButton->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));
	removeButton->setToolTip(obs_module_text("SmartContext.Rules.Remove"));
	connect(removeButton, &QPushButton::clicked, this, [this, removeButton]() {
		for (int i = 0; i < rulesTable->rowCount(); ++i) {
			if (rulesTable->cellWidget(i, ColActions) == removeButton) {
				rulesTable->removeRow(i);
				break;
			}
		}
	});
	rulesTable->setCellWidget(row, ColActions, removeButton);
}

void SmartContextRulesDialog::saveRules()
{
	obs_data_array_t *rulesArray = obs_data_array_create();

	for (int row = 0; row < rulesTable->rowCount(); ++row) {
		QTableWidgetItem *processItem = rulesTable->item(row, ColProcess);
		const QString process = processItem ? processItem->text().trimmed() : QString();
		if (process.isEmpty())
			continue; // a rule without a process can never match

		QWidget *enabledCell = rulesTable->cellWidget(row, ColEnabled);
		QCheckBox *enabledBox = enabledCell ? enabledCell->findChild<QCheckBox *>() : nullptr;
		QWidget *ignoreCell = rulesTable->cellWidget(row, ColIgnore);
		QCheckBox *ignoreBox = ignoreCell ? ignoreCell->findChild<QCheckBox *>() : nullptr;
		QComboBox *delayCombo = qobject_cast<QComboBox *>(rulesTable->cellWidget(row, ColDelay));
		QSpinBox *prioritySpin = qobject_cast<QSpinBox *>(rulesTable->cellWidget(row, ColPriority));

		QTableWidgetItem *windowItem = rulesTable->item(row, ColWindow);
		QTableWidgetItem *categoryItem = rulesTable->item(row, ColCategory);
		QTableWidgetItem *titleItem = rulesTable->item(row, ColTitle);

		obs_data_t *item = obs_data_create();
		obs_data_set_bool(item, "enabled", enabledBox ? enabledBox->isChecked() : true);
		obs_data_set_string(item, "process", process.toUtf8().constData());
		obs_data_set_string(item, "window",
				    windowItem ? windowItem->text().trimmed().toUtf8().constData() : "");
		obs_data_set_string(item, "category",
				    categoryItem ? categoryItem->text().trimmed().toUtf8().constData() : "");
		obs_data_set_string(item, "title_template", titleItem ? titleItem->text().toUtf8().constData() : "");
		obs_data_set_int(item, "delay", delayCombo ? delayCombo->currentData().toInt() : 0);
		obs_data_set_bool(item, "ignore", ignoreBox ? ignoreBox->isChecked() : false);
		obs_data_set_int(item, "priority", prioritySpin ? prioritySpin->value() : 0);

		obs_data_array_push_back(rulesArray, item);
		obs_data_release(item);
	}

	ConfigManager::get().saveSmartContextRules(rulesArray);
	obs_data_array_release(rulesArray);

	SmartContextManager::get().reloadRules();
}

void SmartContextRulesDialog::onAddRuleClicked()
{
	SmartContextRule rule;
	rule.enabled = true;
	rule.process = "app.exe";
	rule.category = "Just Chatting";
	addRuleRow(rule);

	rulesTable->scrollToBottom();
	rulesTable->editItem(rulesTable->item(rulesTable->rowCount() - 1, ColProcess));
}

void SmartContextRulesDialog::onRestoreDefaultsClicked()
{
	QMessageBox::StandardButton answer =
		QMessageBox::question(this, obs_module_text("SmartContext.Rules.RestoreDefaults"),
				      obs_module_text("SmartContext.Rules.RestoreDefaults.Confirm"),
				      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
	if (answer != QMessageBox::Yes)
		return;

	rulesTable->setRowCount(0);

	obs_data_array_t *defaults = ConfigManager::createDefaultSmartContextRules();
	size_t count = obs_data_array_count(defaults);
	for (size_t i = 0; i < count; ++i) {
		obs_data_t *item = obs_data_array_item(defaults, i);

		SmartContextRule rule;
		rule.enabled = obs_data_get_bool(item, "enabled");
		rule.process = QString::fromUtf8(obs_data_get_string(item, "process"));
		rule.window = QString::fromUtf8(obs_data_get_string(item, "window"));
		rule.category = QString::fromUtf8(obs_data_get_string(item, "category"));
		rule.titleTemplate = QString::fromUtf8(obs_data_get_string(item, "title_template"));
		rule.delaySeconds = (int)obs_data_get_int(item, "delay");
		rule.ignore = obs_data_get_bool(item, "ignore");
		rule.priority = (int)obs_data_get_int(item, "priority");
		addRuleRow(rule);

		obs_data_release(item);
	}
	obs_data_array_release(defaults);
}
