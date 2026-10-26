#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>

#include "agent_system.h"
#include "language.h"

// The agent's tools (0.22): one registry for the rules, the local model and
// Claude. Each tool has a purpose, a schema of its arguments, a permission
// category, a time limit, a structured result, a check of the state it leaves
// and a note on what stopping it does. A model only proposes a call: the
// executor validates it here and runs it against the real system.
namespace jarvis::agent {

// What really happened, never what was merely sent.
enum class Outcome {
    Verified,    // done, and the new state was read back
    Partial,     // part of it was done (the text says which)
    NeedsUser,   // the user has to do something or choose (devices off, pairing, which one)
    Failed,      // not done; the text says why and what to try
    Cancelled,   // stopped by the user
    Unconfirmed, // sent, but the result could not be checked
    Skipped,     // not run: an earlier step it depends on did not succeed, or a limit
};
QString outcomeName(Outcome o);
bool succeeded(Outcome o);

// How careful the executor is with a tool.
enum class Risk {
    Read,       // only looks
    Reversible, // changes a setting that can be put back
    Confirm,    // may lose something (closing a window): asks unless the user allowed it
};

struct ToolSpec
{
    QString name;      // audio.output
    QString category;  // permission category: audio, bluetooth, apps, apps.close, screen, desktop, power, info
    Risk risk = Risk::Read;
    QString purpose;   // one line, English (the models' catalogue)
    QJsonObject args;  // property -> JSON schema (type, enum, minimum, maximum, maxLength)
    QStringList required;
    QStringList oneOf; // exactly one of these properties must be given ("level"/"change")
    int timeoutMs = 10000;
    QString verify;    // how the result is checked
    QString cancel;    // what stopping it does
};

struct Step
{
    QString tool;
    QJsonObject args;
    QJsonObject toJson() const { return {{QStringLiteral("tool"), tool}, {QStringLiteral("args"), args}}; }
    static Step fromJson(const QJsonObject &o);
};

struct StepResult
{
    Outcome outcome = Outcome::Failed;
    QString text;          // for the user, in the request's language
    QJsonArray undo;       // steps that put back what this one changed (known previous values)
    QJsonObject data;      // facts for the task state (device chosen, values before and after)
    QStringList choices;   // NeedsUser with choices: the options to ask about
    QJsonArray choiceArgs; // the same step's args for each choice
};

const QList<ToolSpec> &tools();
const ToolSpec *findTool(const QString &name);
QStringList categories();
// The default permission of a category: allow, ask or off.
QString defaultPermission(const QString &category);

// "" when the call fits its tool's schema, otherwise why not (English, technical).
// Strings are bounded and free of control characters; numbers in range; enums known.
QString validate(const Step &step);

// For the models: the JSON schema of a plan (each step is one tool with its own
// arguments, so a constrained decoder cannot invent tools or values) and a
// compact catalogue of the tools.
QJsonObject planSchema();
QString toolCatalogue();

// What a tool run may use. `memory` is the agent's state that outlives a task:
// the preferred headphones, the last device, the last volume step.
struct ToolContext
{
    System *system = nullptr;
    Lang lang = Lang::Ru;
    const std::atomic_bool *cancel = nullptr;
    QJsonObject *memory = nullptr;
    // Devices the user does not want this time ("другие наушники").
    QStringList exclude;
    // A line for the interface while a long step runs ("Подключаю «X»…").
    std::function<void(const QString &)> progress;
    // Diagnostics of parts the system does not know: the local model, the voice.
    std::function<QString(const QString &what)> diagnose;
    int scale = 1; // tests: shorter waits
    bool cancelled() const { return cancel && cancel->load(); }
};

StepResult runTool(const Step &step, ToolContext &ctx);

// Kinds of application people name by what they do: browser, player, terminal…
QString genericAppKind(const QString &words);

} // namespace jarvis::agent
