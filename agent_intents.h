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
    // A short continuation instead of steps: stop, undo, other (other headphones),
    // yes, no, choice (`choice` is 0-based), quieter/louder are plain steps.
    QString followUp;
    int choice = -1;
};

Parsed parse(const QString &text);

// "двадцать пять", "25", "twenty five", "половину" → 25 / 50; -1 if there is no number.
int parseNumber(const QString &words);

// The text with the assistant's name, politeness and punctuation removed, lower case, ё → е.
QString normalizeCommand(const QString &text);

} // namespace jarvis::agent
