#pragma once

#include <QString>

namespace jarvis::text {

// Markdown from Claude, imported skills or lesson notes, made safe for Qt's
// Markdown renderer: no images of any kind (inline, reference or HTML — a
// remote image would leak data in its URL) and no raw HTML. Outside code "<"
// becomes an entity (it still reads as "<"); fenced blocks and balanced
// `inline code` are left as they are, an unbalanced backtick is plain text.
QString safeMarkdown(const QString &text);

} // namespace jarvis::text
