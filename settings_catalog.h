#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace jarvis::settings {

// Every setting the settings window shows (0.21), for its search: an id (the
// window's row carries objectName "entry:<id>"), the page, a group, the title,
// a short description and synonyms, all translatable. `configKey` names the
// config.json key behind it (for "what a reset changes"); `kcm` a KDE System
// Settings module the row opens instead of a page of Jarvis's own.
// Search is local and instant: no network, no model.

// desktop, conversation, intelligence, privacy, actions, skills, system.
QStringList pages();
QString pageTitle(const QString &page);
// [{id, page, pageTitle, group, title, description, configKey, kcm}] in the
// interface language.
QVariantList entries();
QVariantMap entry(const QString &id);
// The best matches first: every word of the query must match a word of the
// title, the description or a synonym (as a word start; endings may differ).
QVariantList search(const QString &query, int limit = 40);
// Titles for config.json keys ("desktop.theme" …); unknown keys are skipped.
QStringList titlesForKeys(const QStringList &keys);
// Lower case, ё → е, punctuation → spaces, single spaces.
QString normalize(const QString &text);

} // namespace jarvis::settings
