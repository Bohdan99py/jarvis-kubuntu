#pragma once

#include <QString>

namespace jarvis {

enum class Lang { Ru, En };

// Script-based detection: two or more Cyrillic letters -> Russian, otherwise
// two or more Latin letters -> English. Digits/emoji-only text is undecided.
// Returns false when the text does not decide the language.
bool detectLanguage(const QString &text, Lang *out);

// "ru" / "en" are explicit; anything else follows the system locale.
Lang resolveLanguage(const QString &preference);

QString languageCode(Lang lang);

// Installs the Russian translator for the application (and Qt's own strings)
// or removes it for English. Returns the language now in effect.
Lang applyUiLanguage(const QString &preference);

} // namespace jarvis
