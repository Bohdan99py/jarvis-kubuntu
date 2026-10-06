#pragma once

#include <QSet>
#include <QString>
#include <QStringList>

namespace jarvis::text {

// Lowercase, ё -> е, everything that is not a letter or digit becomes a space.
QString normalize(const QString &text);

// Content words for matching and the memory graph: normalized words longer
// than two characters, Russian/English stop words removed, de-duplicated,
// in order of first appearance.
QStringList contentWords(const QString &text, int limit = 20);

// Same words cut to a crude stem (first 5 letters) so "редактор" and
// "редактора" or "editor"/"editors" compare equal.
QSet<QString> stems(const QString &text);

// |a ∩ b| / |a ∪ b|, 0 for empty input.
double overlap(const QSet<QString> &a, const QSet<QString> &b);

// Card numbers, API keys, passwords: never stored in memory.
bool looksSensitive(const QString &text);

} // namespace jarvis::text
