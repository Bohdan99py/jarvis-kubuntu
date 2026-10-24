#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

// JARVIS DESKTOP (0.19.2): the Plasma scripts (org.kde.PlasmaShell.evaluateScript)
// the desktop shell runs, and the plan they follow. Pure functions — the
// tests read every script — so the rules are easy to check:
//  * inspect reads the layout and changes nothing;
//  * apply only adds: the Jarvis core and widgets on the desktop, one Jarvis
//    dock panel (with Plasma's own system tray and clock); it reports the ids
//    it created;
//  * finalize removes the standard panels — only the ids the plan named, and
//    only after verify found everything that apply created;
//  * remove takes away what Jarvis created (and any leftover Jarvis widget);
//  * update changes Jarvis's own panel and core, by id, nothing else.
// The full restore of the previous desktop is a file copy (DesktopShell),
// not a script.
namespace jarvis::shell {

// Plugin ids of the Jarvis widgets.
QStringList widgetIds();

QString inspectScript();
// From the inspection, the desktop settings (integration::normalizeDesktop) and
// the widgets of the current workspace: {core: {x,y,width,height}, widgets:
// [{type,x,y,width,height}], dock: {location, height, hiding, launchers,
// filterByScreen}, replacePanels: [ids], screen: {width,height}}.
QJsonObject makePlan(const QJsonObject &inspection, const QJsonObject &desktop, const QStringList &workspaceWidgets);
// The changes a plan makes, for people (the preview), in English or Russian.
QStringList describePlan(const QJsonObject &plan, bool russian);
QString applyScript(const QJsonObject &plan);
// Prints {ok, missing: [ids]}.
QString verifyScript(const QJsonObject &created);
QString finalizeScript(const QJsonArray &panelIds);
QString removeScript(const QJsonObject &created);
QString updateScript(const QJsonObject &created, const QJsonObject &plan);

// The first {…} JSON object Plasma printed ("" output, errors: empty object).
QJsonObject parsePrinted(const QString &output);

} // namespace jarvis::shell
