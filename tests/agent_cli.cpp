// The agent on the real system, without the window or the daemon (0.22 tests).
//
//   agent_cli [--model ID] [--mode auto|local] [--lang ru|en] [--stop-after MS]
//             [--permission category=allow|ask|off] [--no-model] COMMAND...
//   agent_cli --parse TEXT...   (only what the rules understand; changes nothing)
//
// Runs each COMMAND in turn as a chat message to the agent and prints one line
// of JSON per event: {"cmd", "event": progress|reply, "text", "ms"}. With
// --stop-after, "стоп" is sent that many milliseconds after the first command
// starts. It changes the real system — sound, Bluetooth, windows — like Jarvis
// would; run it under `unshare -c -n --keep-caps` to prove no network is used.
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <cstdio>
#include <memory>

#include "agent.h"
#include "agent_system.h"
#include "claude_client.h"
#include "local_model.h"

using namespace Qt::StringLiterals;

static void emitLine(const QJsonObject &o)
{
    std::printf("%s\n", QJsonDocument(o).toJson(QJsonDocument::Compact).constData());
    std::fflush(stdout);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStringList args = app.arguments().mid(1);
    if (args.value(0) == u"--parse") {
        for (const QString &t : args.mid(1)) {
            const jarvis::agent::Parsed p = jarvis::agent::parse(t);
            QJsonArray steps, described;
            for (const jarvis::agent::Step &s : p.steps)
                steps.append(s.toJson());
            for (const jarvis::agent::Step &s : p.described)
                described.append(s.toJson());
            emitLine({{u"text"_s, t}, {u"steps"_s, steps}, {u"mode"_s, p.mode}, {u"described"_s, described},
                      {u"unparsed"_s, QJsonArray::fromStringList(p.unparsed)}, {u"declined"_s, QJsonArray::fromStringList(p.declined)},
                      {u"problems"_s, QJsonArray::fromStringList(p.problems)}, {u"followUp"_s, p.followUp}, {u"number"_s, p.number},
                      {u"choice"_s, p.choice}, {u"actionish"_s, p.actionish}, {u"pronoun"_s, p.pronoun},
                      {u"command"_s, jarvis::agent::isCommand(t)}});
        }
        return 0;
    }
    QString model = u"qwen3.5-4b"_s, mode = u"local"_s;
    jarvis::Lang lang = jarvis::Lang::Ru;
    int stopAfter = -1;
    QJsonObject permissions;
    QStringList commands;
    for (int i = 0; i < args.size(); ++i) {
        const QString a = args[i];
        if (a == u"--model" && i + 1 < args.size())
            model = args[++i];
        else if (a == u"--no-model")
            model.clear();
        else if (a == u"--mode" && i + 1 < args.size())
            mode = args[++i];
        else if (a == u"--lang" && i + 1 < args.size())
            lang = args[++i] == u"en" ? jarvis::Lang::En : jarvis::Lang::Ru;
        else if (a == u"--stop-after" && i + 1 < args.size())
            stopAfter = args[++i].toInt();
        else if (a == u"--permission" && i + 1 < args.size()) {
            const QStringList kv = args[++i].split(u'=');
            permissions[kv.value(0)] = kv.value(1);
        } else
            commands.append(a);
    }
    auto system = std::make_shared<jarvis::agent::LinuxSystem>();
    jarvis::LocalModel local;
    local.configure(model, u"auto"_s, 5);
    jarvis::ClaudeClient cloud; // no key: never used
    jarvis::Agent agent(system, &local, &cloud);
    jarvis::Agent::Settings s;
    s.mode = mode;
    s.permissions = permissions;
    agent.configure(s);
    agent.cloudAvailable = [] { return false; };

    int next = 0;
    quint64 id = 0;
    QElapsedTimer clock, total;
    total.start();
    QString current;
    std::function<void()> run = [&] {
        if (next >= commands.size()) {
            local.stop();
            emitLine({{u"event"_s, u"done"_s}, {u"ms"_s, double(total.elapsed())}, {u"model"_s, local.status()}});
            QTimer::singleShot(0, &app, &QCoreApplication::quit);
            return;
        }
        current = commands.at(next++);
        clock.start();
        ++id;
        if (!agent.handle(id, current, lang, true) && !agent.converse(id, current, lang)) {
            emitLine({{u"cmd"_s, current}, {u"event"_s, u"reply"_s}, {u"route"_s, u"none"_s},
                      {u"text"_s, u"(not for the agent; unavailable: "_s + agent.unavailableReason(lang) + u")"_s},
                      {u"ms"_s, double(clock.elapsed())}});
            QTimer::singleShot(0, &app, run);
        }
    };
    QObject::connect(&agent, &jarvis::Agent::progress, [&](quint64, const QString &text) {
        emitLine({{u"cmd"_s, current}, {u"event"_s, u"progress"_s}, {u"text"_s, text}, {u"ms"_s, double(clock.elapsed())}});
    });
    QObject::connect(&agent, &jarvis::Agent::finished, [&](quint64 rid, const QString &text, const QString &route) {
        emitLine({{u"cmd"_s, rid == id ? current : u"(stop)"_s}, {u"event"_s, u"reply"_s}, {u"route"_s, route}, {u"text"_s, text},
                  {u"ms"_s, double(clock.elapsed())}, {u"status"_s, agent.status().value(u"task"_s).toObject().value(u"status"_s)}});
        if (rid == id)
            QTimer::singleShot(0, &app, run);
    });
    if (stopAfter >= 0)
        QTimer::singleShot(stopAfter, &app, [&] { agent.handle(100000, u"стоп"_s, lang, true); });
    QTimer::singleShot(0, &app, run);
    // Never hang a test run.
    QTimer::singleShot(10 * 60 * 1000, &app, [&] { app.exit(2); });
    return app.exec();
}
