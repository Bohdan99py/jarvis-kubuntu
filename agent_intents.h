#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "agent_tools.h"

// The agent's fast path (0.22): everyday requests in Russian and English read
// by rules — word stems, numbers in words, several actions joined by "и",
// "потом", commas — into tool calls, without a model. Anything the rules do
// not fully understand goes to the local model (or is reported as not
// understood); the rules never guess a device or an application name.
namespace jarvis::agent {

struct Parsed
{
    QList<Step> steps;
    QStringList unparsed; // clauses about the system the rules did not understand
    bool systemish = false; // the text is about the laptop at all (devices, apps, settings)
    // It names a thing on this laptop that a tool changes (sound, headphones,
    // Bluetooth, brightness, notifications, a window): a request about it is
    // never handed to a chat that cannot act (0.22.1).
    bool actionish = false;
    // A short continuation instead of steps: stop, undo, other (other headphones),
    // yes, no, choice (`choice` is 0-based), number ("нет, 30": `number`);
    // quieter/louder are plain steps.
    QString followUp;
    int choice = -1;
    int number = -1;
    // How the request is meant (0.22.1). "" — do it; "question" — how to do it
    // ("как поставить громкость 20?"); "hypothetical" — what would happen ("если я
    // попрошу…, что ты сделаешь?"); "quote" — a command quoted inside other text;
    // "negated" — only things not to do ("не меняй громкость"). Unless it is "",
    // `steps` is empty and `described` holds what the request would do.
    QString mode;
    QList<Step> described;
    QStringList declined; // clauses that say not to do something
    QStringList problems; // understood, but impossible as asked ("громкость 300")
    // "разверни его", "закрой это": the application comes from the conversation —
    // such steps carry an empty query for the agent to fill.
    bool pronoun = false;
};

Parsed parse(const QString &text);

// The text asks for something on the laptop to be done now: it has steps, or
// names a thing a tool changes and is neither a question nor a negation. Such a
// message goes to the agent even with a screenshot attached (the overlay).
bool isCommand(const QString &text);

// "двадцать пять", "25", "twenty five", "половину" → 25 / 50; -1 if there is no number.
int parseNumber(const QString &words);

// The text with the assistant's name, politeness and punctuation removed, lower case, ё → е.
QString normalizeCommand(const QString &text);

} // namespace jarvis::agent
