#include "StreamInfoPanel.h"
#include "ConfigManager.h"
#include "NetworkCommon.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QPixmap>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextCursor>
#include <QTime>
#include <QUrl>
#include <QtConcurrent/QtConcurrent>
#include <obs-module.h>

// Twitch allows ten tags of 25 characters.
static constexpr int MAX_TAGS = 10;
static constexpr int MAX_TAG_LENGTH = 25;
static constexpr int MAX_TITLE_LENGTH = 140;

// The languages Twitch itself offers in its dock, in the order it lists them.
static const QList<QPair<QString, QString>> STREAM_LANGUAGES = {
	{"de", "Deutsch"},   {"en", "English"},    {"es", "Espanol"},  {"fr", "Francais"},  {"it", "Italiano"},
	{"nl", "Nederlands"}, {"pl", "Polski"},    {"pt", "Portugues"}, {"ru", "Russkiy"},  {"tr", "Turkce"},
	{"cs", "Cestina"},   {"da", "Dansk"},      {"el", "Ellinika"}, {"fi", "Suomi"},     {"hu", "Magyar"},
	{"ja", "Nihongo"},   {"ko", "Hangugeo"},   {"no", "Norsk"},    {"ro", "Romana"},    {"sv", "Svenska"},
	{"th", "Thai"},      {"uk", "Ukrayinska"}, {"vi", "Tieng Viet"}, {"zh", "Zhongwen"}, {"ar", "Arabiya"},
};

StreamInfoPanel::StreamInfoPanel(QWidget *parent) : QGroupBox(obs_module_text("StreamInfo.GroupTitle"), parent)
{
	reloadWatcher = new QFutureWatcher<TwitchAuthManager::ChannelInfo>(this);
	searchWatcher = new QFutureWatcher<QList<TwitchAuthManager::Category>>(this);
	boxArtWatcher = new QFutureWatcher<TwitchAuthManager::Category>(this);
	labelsWatcher = new QFutureWatcher<QList<TwitchAuthManager::ClassificationLabel>>(this);
	applyWatcher = new QFutureWatcher<TwitchAuthManager::UpdateResult>(this);
	boxArtDownloadWatcher = new QFutureWatcher<QString>(this);

	connect(reloadWatcher, &QFutureWatcher<TwitchAuthManager::ChannelInfo>::finished, this,
		&StreamInfoPanel::onReloadFinished);
	connect(searchWatcher, &QFutureWatcher<QList<TwitchAuthManager::Category>>::finished, this,
		&StreamInfoPanel::onCategorySearchFinished);
	connect(boxArtWatcher, &QFutureWatcher<TwitchAuthManager::Category>::finished, this,
		&StreamInfoPanel::onBoxArtFinished);
	connect(labelsWatcher, &QFutureWatcher<QList<TwitchAuthManager::ClassificationLabel>>::finished, this,
		&StreamInfoPanel::onLabelsFinished);
	connect(applyWatcher, &QFutureWatcher<TwitchAuthManager::UpdateResult>::finished, this,
		&StreamInfoPanel::onApplyFinished);
	connect(boxArtDownloadWatcher, &QFutureWatcher<QString>::finished, this,
		[this]() { applyBoxArt(boxArtDownloadWatcher->result()); });

	buildUi();

	// The label list is fetched once; a channel update has to name every label, so
	// the ids are handed to the auth manager as soon as they arrive.
	labelsWatcher->setFuture(TwitchAuthManager::get().getClassificationLabels());
}

void StreamInfoPanel::buildUi()
{
	QVBoxLayout *layout = new QVBoxLayout();
	layout->setSpacing(6);

	// --- Title -----------------------------------------------------------
	layout->addWidget(new QLabel(obs_module_text("StreamInfo.Title"), this));

	titleEdit = new QPlainTextEdit(this);
	titleEdit->setPlaceholderText(obs_module_text("StreamInfo.Title.Placeholder"));
	titleEdit->setFixedHeight(56);
	titleEdit->setTabChangesFocus(true);
	layout->addWidget(titleEdit);

	titleCounter = new QLabel("0/140", this);
	titleCounter->setAlignment(Qt::AlignRight);
	titleCounter->setStyleSheet("font-size: 8pt; color: #888888;");
	layout->addWidget(titleCounter);

	connect(titleEdit, &QPlainTextEdit::textChanged, this, [this]() {
		QString text = titleEdit->toPlainText();

		// Enforced here rather than rejected by Twitch after the fact.
		if (text.length() > MAX_TITLE_LENGTH) {
			const int position = titleEdit->textCursor().position();
			titleEdit->blockSignals(true);
			titleEdit->setPlainText(text.left(MAX_TITLE_LENGTH));
			QTextCursor cursor = titleEdit->textCursor();
			cursor.setPosition(qMin(position, MAX_TITLE_LENGTH));
			titleEdit->setTextCursor(cursor);
			titleEdit->blockSignals(false);
			text = titleEdit->toPlainText();
		}

		titleCounter->setText(QString("%1/%2").arg(text.length()).arg(MAX_TITLE_LENGTH));
		markDirty();
	});

	// --- Category --------------------------------------------------------
	layout->addWidget(new QLabel(obs_module_text("StreamInfo.Category"), this));

	QHBoxLayout *currentCategoryRow = new QHBoxLayout();
	categoryBoxArt = new QLabel(this);
	categoryBoxArt->setFixedSize(52, 72);
	categoryBoxArt->setStyleSheet("background: #1e1e1e; border: 1px solid #2a2a2a;");
	categoryBoxArt->setAlignment(Qt::AlignCenter);
	currentCategoryRow->addWidget(categoryBoxArt);

	categoryName = new QLabel("-", this);
	categoryName->setWordWrap(true);
	categoryName->setStyleSheet("font-weight: bold;");
	categoryName->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	currentCategoryRow->addWidget(categoryName, 1);
	layout->addLayout(currentCategoryRow);

	categorySearch = new QLineEdit(this);
	categorySearch->setPlaceholderText(obs_module_text("StreamInfo.Category.Search"));
	categorySearch->setClearButtonEnabled(true);
	layout->addWidget(categorySearch);

	categorySuggestions = new QListWidget(this);
	categorySuggestions->setFixedHeight(96);
	categorySuggestions->setVisible(false);
	categorySuggestions->setIconSize(QSize(26, 36));
	layout->addWidget(categorySuggestions);

	// Typing a name fires a request per keystroke otherwise.
	searchDebounce = new QTimer(this);
	searchDebounce->setSingleShot(true);
	searchDebounce->setInterval(400);
	connect(searchDebounce, &QTimer::timeout, this, [this]() {
		searchWatcher->setFuture(TwitchAuthManager::get().searchCategories(categorySearch->text()));
	});

	connect(categorySearch, &QLineEdit::textEdited, this, &StreamInfoPanel::onSearchTextChanged);
	connect(categorySuggestions, &QListWidget::itemClicked, this, &StreamInfoPanel::onSuggestionPicked);

	// --- Tags ------------------------------------------------------------
	QHBoxLayout *tagHeader = new QHBoxLayout();
	tagHeader->addWidget(new QLabel(obs_module_text("StreamInfo.Tags"), this));
	tagCounter = new QLabel("0/10", this);
	tagCounter->setAlignment(Qt::AlignRight);
	tagCounter->setStyleSheet("font-size: 8pt; color: #888888;");
	tagHeader->addWidget(tagCounter);
	layout->addLayout(tagHeader);

	tagList = new QListWidget(this);
	tagList->setFixedHeight(76);
	tagList->setSelectionMode(QAbstractItemView::ExtendedSelection);
	layout->addWidget(tagList);

	QHBoxLayout *tagRow = new QHBoxLayout();
	tagInput = new QLineEdit(this);
	tagInput->setPlaceholderText(obs_module_text("StreamInfo.Tags.Placeholder"));
	tagInput->setMaxLength(MAX_TAG_LENGTH);
	tagRow->addWidget(tagInput, 1);

	tagAddButton = new QPushButton(obs_module_text("StreamInfo.Tags.Add"), this);
	tagAddButton->setCursor(Qt::PointingHandCursor);
	tagRow->addWidget(tagAddButton);

	tagRemoveButton = new QPushButton(obs_module_text("StreamInfo.Tags.Remove"), this);
	tagRemoveButton->setCursor(Qt::PointingHandCursor);
	tagRow->addWidget(tagRemoveButton);
	layout->addLayout(tagRow);

	connect(tagAddButton, &QPushButton::clicked, this, &StreamInfoPanel::onAddTag);
	connect(tagInput, &QLineEdit::returnPressed, this, &StreamInfoPanel::onAddTag);
	connect(tagRemoveButton, &QPushButton::clicked, this, &StreamInfoPanel::onRemoveTag);

	// --- Language and branded content ------------------------------------
	QFormLayout *form = new QFormLayout();
	form->setLabelAlignment(Qt::AlignLeft);

	languageCombo = new QComboBox(this);
	for (const auto &language : STREAM_LANGUAGES)
		languageCombo->addItem(language.second, language.first);
	form->addRow(obs_module_text("StreamInfo.Language"), languageCombo);
	layout->addLayout(form);

	connect(languageCombo, &QComboBox::currentIndexChanged, this, [this](int) { markDirty(); });

	brandedContentCheck = new QCheckBox(obs_module_text("StreamInfo.BrandedContent"), this);
	brandedContentCheck->setToolTip(obs_module_text("StreamInfo.BrandedContent.Tooltip"));
	layout->addWidget(brandedContentCheck);
	connect(brandedContentCheck, &QCheckBox::toggled, this, [this](bool) { markDirty(); });

	// --- Content classification labels -----------------------------------
	QLabel *labelsHeader = new QLabel(obs_module_text("StreamInfo.Labels"), this);
	labelsHeader->setStyleSheet("margin-top: 4px;");
	layout->addWidget(labelsHeader);

	labelsLayout = new QVBoxLayout();
	labelsLayout->setSpacing(2);
	layout->addLayout(labelsLayout);

	// --- What Helix does not expose --------------------------------------
	QLabel *missing = new QLabel(obs_module_text("StreamInfo.Missing"), this);
	missing->setWordWrap(true);
	missing->setStyleSheet("font-size: 8pt; color: #888888; margin-top: 6px;");
	layout->addWidget(missing);

	// The two fields Helix has no endpoint for still have to be set somewhere, so at
	// least the way there is one click. Same page OBS' own dock used to load.
	dashboardButton = new QPushButton(obs_module_text("StreamInfo.OpenDashboard"), this);
	dashboardButton->setCursor(Qt::PointingHandCursor);
	dashboardButton->setToolTip(obs_module_text("StreamInfo.OpenDashboard.Tooltip"));
	layout->addWidget(dashboardButton);

	connect(dashboardButton, &QPushButton::clicked, this, [this]() {
		// Without the login the per channel URL cannot be built; the generic
		// stream manager still lands in the right place after a redirect.
		const QString url = lastLoaded.login.isEmpty()
					    ? QString("https://dashboard.twitch.tv/stream-manager")
					    : QString("https://dashboard.twitch.tv/u/%1/stream-manager/edit-stream-info")
							      .arg(lastLoaded.login);

		QDesktopServices::openUrl(QUrl(url));
	});

	// --- Actions ---------------------------------------------------------
	pendingHint = new QLabel(obs_module_text("StreamInfo.Pending"), this);
	pendingHint->setWordWrap(true);
	pendingHint->setStyleSheet("font-size: 8pt; color: #c08040;");
	pendingHint->setVisible(false);
	layout->addWidget(pendingHint);

	QHBoxLayout *buttons = new QHBoxLayout();
	applyButton = new QPushButton(obs_module_text("StreamInfo.Apply"), this);
	applyButton->setCursor(Qt::PointingHandCursor);
	applyButton->setEnabled(false);
	buttons->addWidget(applyButton, 1);

	discardButton = new QPushButton(obs_module_text("StreamInfo.Discard"), this);
	discardButton->setCursor(Qt::PointingHandCursor);
	discardButton->setEnabled(false);
	buttons->addWidget(discardButton);

	reloadButton = new QPushButton(obs_module_text("StreamInfo.Reload"), this);
	reloadButton->setCursor(Qt::PointingHandCursor);
	reloadButton->setToolTip(obs_module_text("StreamInfo.Reload.Tooltip"));
	buttons->addWidget(reloadButton);
	layout->addLayout(buttons);

	connect(applyButton, &QPushButton::clicked, this, &StreamInfoPanel::onApply);
	connect(discardButton, &QPushButton::clicked, this, &StreamInfoPanel::onDiscard);
	connect(reloadButton, &QPushButton::clicked, this, [this]() { reload(true); });

	statusLabel = new QLabel("", this);
	statusLabel->setWordWrap(true);
	statusLabel->setStyleSheet("font-size: 8pt; color: #888888;");
	layout->addWidget(statusLabel);

	setLayout(layout);
}

void StreamInfoPanel::markDirty()
{
	if (filling)
		return;
	setEditsPending(true);
}

void StreamInfoPanel::setEditsPending(bool pending)
{
	dirty = pending;
	applyButton->setEnabled(pending);
	discardButton->setEnabled(pending);
	pendingHint->setVisible(pending);
}

void StreamInfoPanel::showStatus(const QString &text, bool error)
{
	statusLabel->setText(text);
	statusLabel->setStyleSheet(error ? "font-size: 8pt; color: #d05050;" : "font-size: 8pt; color: #888888;");
}

void StreamInfoPanel::reload(bool force)
{
	if (TwitchAuthManager::get().getAccessToken().isEmpty()) {
		showStatus(obs_module_text("StreamInfo.Status.NotConnected"), true);
		return;
	}

	// The whole point of pausing here: an automatic reload must never wipe out a
	// title someone is halfway through typing.
	if (dirty && !force) {
		showStatus(obs_module_text("StreamInfo.Status.SkippedPending"));
		return;
	}

	if (reloadWatcher->isRunning())
		return;

	showStatus(obs_module_text("StreamInfo.Status.Loading"));
	reloadWatcher->setFuture(TwitchAuthManager::get().getChannelInfo());
}

void StreamInfoPanel::onReloadFinished()
{
	const TwitchAuthManager::ChannelInfo info = reloadWatcher->result();

	if (!info.valid) {
		showStatus(QString(obs_module_text("StreamInfo.Status.LoadFailed")).arg(info.error), true);
		return;
	}

	lastLoaded = info;
	fillFrom(info);
	setEditsPending(false);
	showStatus(QString(obs_module_text("StreamInfo.Status.Loaded")).arg(QTime::currentTime().toString("HH:mm:ss")));
}

void StreamInfoPanel::fillFrom(const TwitchAuthManager::ChannelInfo &info)
{
	filling = true;

	titleEdit->setPlainText(info.title);
	titleCounter->setText(QString("%1/%2").arg(info.title.length()).arg(MAX_TITLE_LENGTH));

	pickedGameId = info.gameId;
	pickedGameName = info.gameName;
	categoryName->setText(info.gameName.isEmpty() ? "-" : info.gameName);
	categorySearch->clear();
	categorySuggestions->clear();
	categorySuggestions->setVisible(false);

	categoryBoxArt->setPixmap(QPixmap());
	if (!info.gameId.isEmpty())
		boxArtWatcher->setFuture(TwitchAuthManager::get().getCategoryById(info.gameId));

	tagList->clear();
	for (const QString &tag : info.tags)
		tagList->addItem(tag);
	tagCounter->setText(QString("%1/%2").arg(info.tags.size()).arg(MAX_TAGS));

	const int languageIndex = languageCombo->findData(info.language);
	if (languageIndex >= 0) {
		languageCombo->setCurrentIndex(languageIndex);
	} else if (!info.language.isEmpty()) {
		// A language Twitch supports but this list does not; keep it rather than
		// silently changing it on the next apply.
		languageCombo->addItem(info.language, info.language);
		languageCombo->setCurrentIndex(languageCombo->count() - 1);
	}

	brandedContentCheck->setChecked(info.brandedContent);

	for (QCheckBox *check : labelChecks)
		check->setChecked(info.classificationLabels.contains(check->property("labelId").toString()));

	filling = false;
}

void StreamInfoPanel::onBoxArtFinished()
{
	const TwitchAuthManager::Category category = boxArtWatcher->result();
	if (category.boxArtUrl.isEmpty())
		return;

	if (!category.name.isEmpty()) {
		categoryName->setText(category.name);
		pickedGameName = category.name;
	}

	requestBoxArt(category.boxArtUrl);
}

void StreamInfoPanel::requestBoxArt(const QString &boxArtUrl)
{
	if (boxArtDownloadWatcher->isRunning())
		return;

	// The URL carries {width}x{height} placeholders.
	QString url = boxArtUrl;
	url.replace("{width}", "52").replace("{height}", "72");

	const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
	QDir().mkpath(cacheDir);
	const QString target = QDir(cacheDir).filePath("game-detector-boxart.jpg");

	auto future = QtConcurrent::run([url, target]() -> QString {
		const long code = DownloadToFile(url, target);
		if (code < 200 || code >= 300)
			return QString();
		return target;
	});

	boxArtDownloadWatcher->setFuture(future);
}

void StreamInfoPanel::applyBoxArt(const QString &localFile)
{
	if (localFile.isEmpty())
		return;

	QPixmap pixmap;
	if (!pixmap.load(localFile))
		return;

	categoryBoxArt->setPixmap(pixmap.scaled(52, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void StreamInfoPanel::onSearchTextChanged(const QString &text)
{
	if (text.trimmed().length() < 2) {
		categorySuggestions->clear();
		categorySuggestions->setVisible(false);
		searchDebounce->stop();
		return;
	}

	searchDebounce->start();
}

void StreamInfoPanel::onCategorySearchFinished()
{
	const QList<TwitchAuthManager::Category> results = searchWatcher->result();

	categorySuggestions->clear();

	if (results.isEmpty()) {
		categorySuggestions->setVisible(false);
		showStatus(obs_module_text("StreamInfo.Status.NoCategory"));
		return;
	}

	for (const TwitchAuthManager::Category &category : results) {
		QListWidgetItem *item = new QListWidgetItem(category.name, categorySuggestions);
		item->setData(Qt::UserRole, category.id);
		item->setData(Qt::UserRole + 1, category.boxArtUrl);
	}

	categorySuggestions->setVisible(true);
	showStatus("");
}

void StreamInfoPanel::onSuggestionPicked(QListWidgetItem *item)
{
	if (!item)
		return;

	pickedGameId = item->data(Qt::UserRole).toString();
	pickedGameName = item->text();

	categoryName->setText(pickedGameName);
	categorySearch->clear();
	categorySuggestions->clear();
	categorySuggestions->setVisible(false);

	requestBoxArt(item->data(Qt::UserRole + 1).toString());
	markDirty();
}

void StreamInfoPanel::onAddTag()
{
	const QString tag = tagInput->text().trimmed();
	if (tag.isEmpty())
		return;

	if (tagList->count() >= MAX_TAGS) {
		showStatus(QString(obs_module_text("StreamInfo.Status.TooManyTags")).arg(MAX_TAGS), true);
		return;
	}

	// Twitch rejects tags with spaces or special characters outright.
	static const QRegularExpression allowed("^[A-Za-z0-9]+$");
	if (!allowed.match(tag).hasMatch()) {
		showStatus(obs_module_text("StreamInfo.Status.BadTag"), true);
		return;
	}

	for (int i = 0; i < tagList->count(); ++i) {
		if (tagList->item(i)->text().compare(tag, Qt::CaseInsensitive) == 0) {
			showStatus(obs_module_text("StreamInfo.Status.DuplicateTag"), true);
			return;
		}
	}

	tagList->addItem(tag);
	tagInput->clear();
	tagCounter->setText(QString("%1/%2").arg(tagList->count()).arg(MAX_TAGS));
	showStatus("");
	markDirty();
}

void StreamInfoPanel::onRemoveTag()
{
	const QList<QListWidgetItem *> selected = tagList->selectedItems();
	if (selected.isEmpty()) {
		showStatus(obs_module_text("StreamInfo.Status.PickTag"));
		return;
	}

	for (QListWidgetItem *item : selected)
		delete item;

	tagCounter->setText(QString("%1/%2").arg(tagList->count()).arg(MAX_TAGS));
	markDirty();
}

void StreamInfoPanel::onLabelsFinished()
{
	const QList<TwitchAuthManager::ClassificationLabel> labels = labelsWatcher->result();
	if (labels.isEmpty())
		return;

	QStringList ids;
	for (const TwitchAuthManager::ClassificationLabel &label : labels) {
		QCheckBox *check = new QCheckBox(label.name, this);
		check->setProperty("labelId", label.id);

		// Twitch sets this one itself from the category and rejects any attempt
		// to change it, so it is shown but not offered for editing.
		const bool editable = label.id != QString::fromUtf8(TwitchAuthManager::NON_EDITABLE_LABEL);
		check->setEnabled(editable);

		if (!editable)
			check->setToolTip(obs_module_text("StreamInfo.Labels.NotEditable"));
		else if (!label.description.isEmpty())
			check->setToolTip(label.description);

		if (editable)
			connect(check, &QCheckBox::toggled, this, [this](bool) { markDirty(); });

		labelsLayout->addWidget(check);
		labelChecks.append(check);
		ids.append(label.id);
	}

	// An update must state every label with its state, or unchecking one would
	// never reach Twitch.
	TwitchAuthManager::get().rememberClassificationLabelIds(ids);

	// The state that arrived before the checkboxes existed.
	if (lastLoaded.valid) {
		filling = true;
		for (QCheckBox *check : labelChecks)
			check->setChecked(lastLoaded.classificationLabels.contains(check->property("labelId").toString()));
		filling = false;
	}
}

TwitchAuthManager::ChannelInfo StreamInfoPanel::collectFromUi() const
{
	TwitchAuthManager::ChannelInfo info;
	info.valid = true;
	info.title = titleEdit->toPlainText().trimmed();
	info.gameId = pickedGameId;
	info.gameName = pickedGameName;
	info.language = languageCombo->currentData().toString();
	info.brandedContent = brandedContentCheck->isChecked();

	for (int i = 0; i < tagList->count(); ++i)
		info.tags.append(tagList->item(i)->text());

	for (QCheckBox *check : labelChecks) {
		if (check->isChecked())
			info.classificationLabels.append(check->property("labelId").toString());
	}

	return info;
}

void StreamInfoPanel::onApply()
{
	if (applyWatcher->isRunning())
		return;

	const TwitchAuthManager::ChannelInfo info = collectFromUi();

	if (info.title.isEmpty()) {
		showStatus(obs_module_text("StreamInfo.Status.EmptyTitle"), true);
		return;
	}

	applyButton->setEnabled(false);
	showStatus(obs_module_text("StreamInfo.Status.Applying"));

	applyWatcher->setFuture(TwitchAuthManager::get().updateChannelInfo(info));
}

void StreamInfoPanel::onApplyFinished()
{
	const TwitchAuthManager::UpdateResult result = applyWatcher->result();

	if (result == TwitchAuthManager::Success) {
		blog(LOG_INFO, "[GameDetector/StreamInfo] Channel information updated.");
		setEditsPending(false);
		showStatus(QString(obs_module_text("StreamInfo.Status.Applied"))
				   .arg(QTime::currentTime().toString("HH:mm:ss")));
		// Read back, so what is on screen is what Twitch actually stored.
		reload(true);
		return;
	}

	applyButton->setEnabled(true);
	showStatus(result == TwitchAuthManager::AuthError ? obs_module_text("StreamInfo.Status.AuthError")
							  : obs_module_text("StreamInfo.Status.ApplyFailed"),
		   true);
}

void StreamInfoPanel::onDiscard()
{
	if (!lastLoaded.valid) {
		reload(true);
		return;
	}

	fillFrom(lastLoaded);
	setEditsPending(false);
	showStatus(obs_module_text("StreamInfo.Status.Discarded"));
}
