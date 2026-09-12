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

#pragma once

#include <QString>

/*
 * Bei FiveM ist die Twitch-Kategorie immer "Grand Theft Auto V", egal auf
 * welchem Server man spielt. Interessant ist deshalb der Servername.
 *
 * Zwei Quellen zusammen ergeben ihn:
 *
 * 1. FiveM.app\data\cache\servers\<hash>.json haelt die Angaben eines Servers,
 *    darunter vars.sv_projectName, also den Namen, den der Server selbst
 *    fuehrt, und seine vollstaendige Ressourcenliste. Diese Datei wird aber
 *    nicht bei jedem Verbinden neu geschrieben, ihr Datum sagt also nichts
 *    darueber, wo man gerade ist.
 *
 * 2. Das Log der laufenden Sitzung nennt unter "Required resources:" die
 *    Ressourcen, die der Server beim Verbinden angefordert hat. Diese Zeile
 *    gehoert sicher zur aktuellen Verbindung.
 *
 * Stimmen beide Listen ueberein, gehoert die Angabendatei zum Server, auf dem
 * man gerade ist, und ihr Name ist der richtige. Gemessen waren es bei einem
 * echten Treffer 227 von 230 Ressourcen, also 99 Prozent. Passt nichts, wird
 * kein Name geliefert und der Streamtitel bleibt unangetastet; ein falscher
 * Name waere schlimmer als keiner.
 */
namespace FiveMServer {

// Ist das eine FiveM-Programmdatei? Trifft auf den Launcher (FiveM.exe) und auf
// den Spielprozess zu, der die Spielversion im Namen traegt
// (FiveM_b3570_GTAProcess.exe).
bool isFiveMProcess(const QString &exeName);

// Name des Servers, auf dem der Prozess gerade ist, sonst ein leerer Text.
// pid ist der Prozess des Vordergrundfensters; ueber dessen Startzeit wird ein
// veralteter Eintrag verworfen.
QString currentName(const QString &exeName, quint32 pid);

// Verwirft den Zwischenspeicher, etwa beim Abschalten des Smart Context Mode.
void reset();

} // namespace FiveMServer
