/*
Game Detector V2 - FiveM server detection
Copyright (C) 2026 Tobias Schlothane

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "FiveMServer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <obs-module.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

// Der Anfang des Logs reicht: die Ressourcenliste stand in allen beobachteten
// Sitzungen zwischen 60 und 80 KB, das Dreifache ist reichlich Luft.
constexpr qint64 LOG_AUSSCHNITT = 512 * 1024;

// So viele Ressourcen muessen uebereinstimmen, damit eine Angabendatei als der
// aktuelle Server gilt. Bei echten Treffern lagen es 99 %; ein fremder Server
// kommt ueber die geteilten Standardressourcen kaum hinaus.
constexpr double MINDEST_ANTEIL = 0.6;
constexpr int MINDEST_ANZAHL = 15;

struct Zwischenspeicher {
	QString name;
	QString logDatei;
	qint64 logGroesse = 0;
	quint32 pid = 0;
	bool gueltig = false;
};

Zwischenspeicher zwischen;

// FiveM erlaubt Farbcodes im Servernamen: ^0 bis ^9, dazu GTA-Textmarken wie
// ~r~. Im Streamtitel hat das nichts zu suchen.
QString ohneFarbcodes(QString text)
{
	static const QRegularExpression farben("\\^[0-9]");
	static const QRegularExpression marken("~[a-zA-Z0-9_]*~");
	text.remove(farben);
	text.remove(marken);
	return text.simplified();
}

#ifdef _WIN32
QString prozessPfad(quint32 pid)
{
	if (pid == 0)
		return QString();

	HANDLE prozess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!prozess)
		return QString();

	wchar_t puffer[MAX_PATH] = {0};
	DWORD groesse = MAX_PATH;
	const bool ok = QueryFullProcessImageNameW(prozess, 0, puffer, &groesse) != 0;
	CloseHandle(prozess);

	if (!ok || groesse == 0)
		return QString();

	return QString::fromWCharArray(puffer, int(groesse));
}
#else
QString prozessPfad(quint32) { return QString(); }
#endif

// Der Ordner FiveM.app. Zuerst beim laufenden Programm suchen, denn FiveM muss
// nicht unter %localappdata% liegen; erst danach dort.
QString appOrdner(quint32 pid)
{
	const QString pfad = prozessPfad(pid);
	if (!pfad.isEmpty()) {
		QDir dir = QFileInfo(pfad).absoluteDir();
		// Der Spielprozess liegt in FiveM.app, der Launcher eine Ebene darueber.
		for (int ebene = 0; ebene < 3; ++ebene) {
			if (dir.dirName().compare("FiveM.app", Qt::CaseInsensitive) == 0)
				return dir.absolutePath();
			if (QDir(dir.filePath("FiveM.app")).exists())
				return dir.filePath("FiveM.app");
			if (!dir.cdUp())
				break;
		}
	}

	const QString lokal = qEnvironmentVariable("LOCALAPPDATA");
	if (lokal.isEmpty())
		return QString();

	const QString kandidat = QDir(lokal).filePath("FiveM/FiveM.app");
	return QDir(kandidat).exists() ? kandidat : QString();
}

// Die Ressourcen, die der Server beim Verbinden angefordert hat. Diese Zeile
// schreibt FiveM bei jedem Verbindungsvorgang, sie ist damit der zuverlaessige
// Teil: sie gehoert zur laufenden Sitzung.
QSet<QString> ressourcenAusLog(const QString &logPfad)
{
	QSet<QString> ergebnis;

	QFile datei(logPfad);
	if (!datei.open(QIODevice::ReadOnly))
		return ergebnis;

	const QByteArray anfang = datei.read(LOG_AUSSCHNITT);
	datei.close();

	const int start = anfang.indexOf("Required resources:");
	if (start < 0)
		return ergebnis;

	int ende = anfang.indexOf('\n', start);
	if (ende < 0)
		ende = anfang.size();

	const QString zeile = QString::fromUtf8(anfang.mid(start, ende - start));
	const QStringList teile =
		zeile.mid(zeile.indexOf(':') + 1).split(' ', Qt::SkipEmptyParts);

	for (const QString &teil : teile) {
		const QString r = teil.trimmed().toLower();
		if (!r.isEmpty())
			ergebnis.insert(r);
	}

	return ergebnis;
}

// Das neueste Log ist das der laufenden Sitzung, FiveM schreibt fortlaufend hin.
QFileInfo neuestesLog(const QString &app)
{
	QDir dir(QDir(app).filePath("logs"));
	if (!dir.exists())
		return QFileInfo();

	const QFileInfoList dateien =
		dir.entryInfoList({"CitizenFX_log_*.log"}, QDir::Files | QDir::Readable, QDir::Time);
	return dateien.isEmpty() ? QFileInfo() : dateien.first();
}

// Sucht unter den gespeicherten Serverangaben die, deren Ressourcenliste zu der
// der laufenden Sitzung passt. Das Datum der Datei spielt dabei keine Rolle:
// FiveM schreibt sie nicht bei jedem Verbinden neu, die Ressourcen verraten
// aber eindeutig, zu welchem Server sie gehoert.
QString nameAusAngaben(const QString &app, const QSet<QString> &ressourcen)
{
	QDir dir(QDir(app).filePath("data/cache/servers"));
	if (!dir.exists())
		return QString();

	QString besterName;
	double besterAnteil = 0.0;

	const QFileInfoList dateien =
		dir.entryInfoList({"*.json"}, QDir::Files | QDir::Readable, QDir::Time);

	for (const QFileInfo &fi : dateien) {
		QFile datei(fi.absoluteFilePath());
		if (!datei.open(QIODevice::ReadOnly))
			continue;

		const QJsonDocument doc = QJsonDocument::fromJson(datei.readAll());
		datei.close();
		if (!doc.isObject())
			continue;

		const QJsonObject wurzel = doc.object();
		const QJsonArray liste = wurzel.value("resources").toArray();
		if (liste.isEmpty())
			continue;

		int gemeinsam = 0;
		for (const QJsonValue &wert : liste) {
			if (ressourcen.contains(wert.toString().toLower()))
				++gemeinsam;
		}

		const int kleinere = qMin(ressourcen.size(), liste.size());
		if (kleinere <= 0)
			continue;

		const double anteil = double(gemeinsam) / double(kleinere);
		if (gemeinsam < MINDEST_ANZAHL || anteil < MINDEST_ANTEIL || anteil <= besterAnteil)
			continue;

		const QString name =
			ohneFarbcodes(wurzel.value("vars").toObject().value("sv_projectName").toString());
		if (name.isEmpty())
			continue;

		besterAnteil = anteil;
		besterName = name;
	}

	if (!besterName.isEmpty())
		blog(LOG_INFO, "[GameDetector/FiveM] Server recognised: %s (%.0f%% of the resources match)",
		     qUtf8Printable(besterName), besterAnteil * 100.0);

	return besterName;
}

} // namespace

namespace FiveMServer {

bool isFiveMProcess(const QString &exeName)
{
	return exeName.startsWith("FiveM", Qt::CaseInsensitive);
}

QString currentName(const QString &exeName, quint32 pid)
{
	if (!isFiveMProcess(exeName)) {
		reset();
		return QString();
	}

	const QString app = appOrdner(pid);
	if (app.isEmpty()) {
		if (!zwischen.gueltig) {
			zwischen.gueltig = true;
			blog(LOG_INFO, "[GameDetector/FiveM] FiveM folder not found, no server name.");
		}
		return QString();
	}

	const QFileInfo log = neuestesLog(app);
	if (!log.exists())
		return QString();

	// Solange dieselbe Sitzung in dieselbe Datei schreibt und die Datei nicht
	// gewachsen ist, bleibt das Ergebnis gleich. Das haelt den Aufwand klein,
	// denn diese Abfrage laeuft jede Sekunde.
	if (zwischen.gueltig && zwischen.pid == pid && zwischen.logDatei == log.fileName() &&
	    zwischen.logGroesse == log.size())
		return zwischen.name;

	const QSet<QString> ressourcen = ressourcenAusLog(log.absoluteFilePath());

	QString name;
	if (ressourcen.isEmpty()) {
		// Launcher offen, aber noch nicht verbunden: dann gibt es keinen Server,
		// und der Titel bleibt unangetastet.
		if (!zwischen.name.isEmpty())
			blog(LOG_INFO, "[GameDetector/FiveM] Not connected to a server.");
	} else {
		name = nameAusAngaben(app, ressourcen);
		if (name.isEmpty() && zwischen.name.isEmpty())
			blog(LOG_INFO,
			     "[GameDetector/FiveM] Connected, but no stored details match these %d resources. "
			     "Opening the server once in the FiveM server list stores its name.",
			     ressourcen.size());
	}

	zwischen.name = name;
	zwischen.logDatei = log.fileName();
	zwischen.logGroesse = log.size();
	zwischen.pid = pid;
	zwischen.gueltig = true;
	return name;
}

void reset()
{
	zwischen = Zwischenspeicher();
}

} // namespace FiveMServer
