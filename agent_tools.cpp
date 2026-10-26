#include "agent_tools.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QThread>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace jarvis::agent {
namespace {

QString tr2(Lang l, const QString &ru, const QString &en) { return l == Lang::Ru ? ru : en; }
QString quoted(Lang l, const QString &s) { return l == Lang::Ru ? u"«"_s + s + u"»"_s : u"“"_s + s + u"”"_s; }

QJsonObject str(int maxLength = 60) { return {{u"type"_s, u"string"_s}, {u"maxLength"_s, maxLength}}; }
QJsonObject oneOfStrings(const QStringList &values)
{
    return {{u"type"_s, u"string"_s}, {u"enum"_s, QJsonArray::fromStringList(values)}};
}
QJsonObject integer(int min, int max) { return {{u"type"_s, u"integer"_s}, {u"minimum"_s, min}, {u"maximum"_s, max}}; }
QJsonObject boolean() { return {{u"type"_s, u"boolean"_s}}; }

QString norm(const QString &s)
{
    QString t = s.toLower();
    t.replace(u'ё', u'е');
    static const QRegularExpression punct(u"[^\\p{L}\\p{N}]+"_s);
    t.replace(punct, u" "_s);
    return t.simplified();
}

// A device or application name matches what the user said: every word of the
// query starts a word of the name (endings may differ: «наушники Sony» ~ "Sony WH").
bool nameMatches(const QString &name, const QString &query)
{
    const QStringList q = norm(query).split(u' ', Qt::SkipEmptyParts);
    if (q.isEmpty())
        return false;
    const QString n = norm(name);
    if (n == norm(query) || n.contains(norm(query)))
        return true;
    const QStringList words = n.split(u' ', Qt::SkipEmptyParts);
    for (const QString &w : q) {
        const QString stem = w.size() > 4 ? w.left(w.size() - 1) : w;
        bool found = false;
        for (const QString &x : words)
            found = found || x.startsWith(stem) || (x.size() > 3 && stem.startsWith(x));
        if (!found)
            return false;
    }
    return true;
}

// Waits until `ready()` or `ms` pass; false on timeout or cancel.
template<typename F>
bool waitFor(ToolContext &ctx, int ms, F ready)
{
    QElapsedTimer t;
    t.start();
    const int step = std::max(20, 200 / ctx.scale);
    while (true) {
        if (ready())
            return true;
        if (ctx.cancelled() || t.elapsed() > ms / ctx.scale)
            return false;
        QThread::msleep(step);
    }
}

StepResult result(Outcome o, const QString &text) { return StepResult{o, text, {}, {}, {}, {}}; }

StepResult cancelled(Lang l, const QString &what = {})
{
    return result(Outcome::Cancelled, what.isEmpty() ? tr2(l, u"Остановлено."_s, u"Stopped."_s) : what);
}

QJsonObject undoStep(const QString &tool, const QJsonObject &args) { return Step{tool, args}.toJson(); }

const AudioDevice *defaultOf(const QList<AudioDevice> &list)
{
    for (const AudioDevice &d : list)
        if (d.isDefault)
            return &d;
    return nullptr;
}

QString deviceLabel(Lang l, const AudioDevice &d)
{
    QString kind;
    if (d.bus == u"bluetooth")
        kind = u"Bluetooth"_s;
    else if (d.bus == u"usb")
        kind = u"USB"_s;
    else if (d.headphones)
        kind = tr2(l, u"разъём"_s, u"jack"_s);
    return kind.isEmpty() ? quoted(l, d.description) : quoted(l, d.description) + u" ("_s + kind + u")"_s;
}

QString pairingGuide(Lang l)
{
    return tr2(l,
               u"Первое подключение (сопряжение) нужно сделать один раз с вашим участием:\n"
               u"1. Переведите наушники в режим сопряжения — обычно зажать кнопку питания или кнопку на кейсе на 3–5 секунд, пока не замигает индикатор.\n"
               u"2. В окне Bluetooth, которое я открыл, выберите их и подтвердите.\n"
               u"3. Потом скажите: «выведи звук в наушники»."_s,
               u"The first connection (pairing) needs you once:\n"
               u"1. Put the headphones into pairing mode — usually hold the power button or the case button for 3–5 seconds until the light blinks.\n"
               u"2. In the Bluetooth window I opened, pick them and confirm.\n"
               u"3. Then say: \"play sound through my headphones\"."_s);
}

// The adapter on, or why not.
StepResult ensureAdapter(ToolContext &ctx, bool *switchedOn)
{
    const Lang l = ctx.lang;
    QString error;
    BtAdapter a = ctx.system->adapter(&error);
    if (!a.present)
        return result(Outcome::Failed, tr2(l, u"Bluetooth недоступен: %1. Проверьте, что адаптер есть и служба bluetooth запущена (systemctl status bluetooth)."_s,
                                           u"Bluetooth is unavailable: %1. Check that there is an adapter and the bluetooth service runs (systemctl status bluetooth)."_s)
                                           .arg(error));
    if (a.powered)
        return result(Outcome::Verified, {});
    if (ctx.progress)
        ctx.progress(tr2(l, u"Включаю Bluetooth…"_s, u"Turning Bluetooth on…"_s));
    const QString failed = ctx.system->setAdapterPowered(true);
    const bool on = waitFor(ctx, 4000, [&] { return ctx.system->adapter(nullptr).powered; });
    if (ctx.cancelled())
        return cancelled(l);
    if (!on)
        return result(Outcome::NeedsUser,
                      tr2(l, u"Bluetooth не включился%1. Включите его в трее (значок Bluetooth) или аппаратным переключателем и повторите."_s,
                          u"Bluetooth did not turn on%1. Switch it on in the system tray (the Bluetooth icon) or with the hardware switch and try again."_s)
                          .arg(failed.isEmpty() ? QString() : u": "_s + failed));
    if (switchedOn)
        *switchedOn = true;
    return result(Outcome::Verified, {});
}

// Connects a known device and waits for BlueZ to say so.
StepResult connectBt(ToolContext &ctx, const BtDevice &d, bool *connectedNow)
{
    const Lang l = ctx.lang;
    if (!d.paired)
        return result(Outcome::NeedsUser, tr2(l, u"%1 ещё не сопряжено с ноутбуком. "_s, u"%1 is not paired with this laptop yet. "_s)
                                              .arg(quoted(l, d.name))
                                              + pairingGuide(l));
    if (d.connected)
        return result(Outcome::Verified, {});
    if (ctx.progress)
        ctx.progress(tr2(l, u"Подключаю %1…"_s, u"Connecting %1…"_s).arg(quoted(l, d.name)));
    const QString error = ctx.system->connectDevice(d.path, *ctx.cancel, 20000 / ctx.scale);
    if (ctx.cancelled() || error == u"cancelled")
        return cancelled(l, tr2(l, u"Остановлено: подключение %1 прервано."_s, u"Stopped: connecting %1 was aborted."_s).arg(quoted(l, d.name)));
    if (!error.isEmpty()) {
        const QString e = error.toLower();
        if (e.contains(u"page-timeout"_s) || e.contains(u"host is down"_s) || e.contains(u"timeout"_s)
            || e.contains(u"not available"_s) || e.contains(u"br-connection"_s) || e.contains(u"failed"_s))
            return result(Outcome::NeedsUser,
                          tr2(l, u"%1 не отвечает. Включите наушники (или достаньте из кейса), положите рядом с ноутбуком и повторите. Если они подключены к телефону — отключите их там."_s,
                              u"%1 does not answer. Switch the headphones on (or take them out of the case), keep them near the laptop and try again. If they are connected to a phone, disconnect them there."_s)
                              .arg(quoted(l, d.name)));
        return result(Outcome::Failed, tr2(l, u"Не удалось подключить %1: %2"_s, u"Could not connect %1: %2"_s).arg(quoted(l, d.name), error));
    }
    const bool ok = waitFor(ctx, 5000, [&] {
        for (const BtDevice &x : ctx.system->devices(nullptr))
            if (x.path == d.path)
                return x.connected;
        return false;
    });
    if (!ok)
        return result(Outcome::Unconfirmed, tr2(l, u"Команда подключения %1 принята, но Bluetooth не подтвердил соединение."_s,
                                                u"%1 accepted the connection request, but Bluetooth did not confirm the connection."_s)
                                                .arg(quoted(l, d.name)));
    if (connectedNow)
        *connectedNow = true;
    return result(Outcome::Verified, {});
}

// ---------------------------------------------------------------------------
// Sound

StepResult setOutput(ToolContext &ctx, const AudioDevice &target, const QList<AudioDevice> &before, const QString &how)
{
    const Lang l = ctx.lang;
    const AudioDevice *was = defaultOf(before);
    if (was && was->name == target.name)
        return result(Outcome::Verified, tr2(l, u"Звук уже идёт в %1."_s, u"Sound already plays through %1."_s).arg(deviceLabel(l, target)));
    const QString error = ctx.system->setDefaultOutput(target.name);
    if (!error.isEmpty())
        return result(Outcome::Failed, tr2(l, u"Не удалось переключить звук на %1: %2"_s, u"Could not switch the sound to %1: %2"_s)
                                           .arg(deviceLabel(l, target), error));
    AudioDevice now;
    const bool ok = waitFor(ctx, 3000, [&] {
        for (const AudioDevice &d : ctx.system->outputs(nullptr))
            if (d.isDefault && d.name == target.name) {
                now = d;
                return true;
            }
        return false;
    });
    if (!ok)
        return result(Outcome::Unconfirmed, tr2(l, u"Переключение на %1 отправлено, но звуковой сервер не подтвердил его."_s,
                                                u"Switching to %1 was sent, but the sound server did not confirm it."_s)
                                                .arg(deviceLabel(l, target)));
    StepResult r = result(Outcome::Verified, tr2(l, u"Звук идёт в %1%2, громкость %3%."_s, u"Sound plays through %1%2, volume %3%."_s)
                                                 .arg(deviceLabel(l, now), how, QString::number(std::max(0, now.volume))));
    if (now.muted)
        r.text += tr2(l, u" Звук на нём выключен — скажите «включи звук»."_s, u" It is muted — say \"unmute\"."_s);
    if (was)
        r.undo.append(undoStep(u"audio.output"_s, {{u"target"_s, u"device"_s}, {u"name"_s, was->name}}));
    r.data = {{u"output"_s, now.name}, {u"description"_s, now.description}, {u"previous"_s, was ? was->name : QString()}};
    return r;
}

struct Headphones
{
    QString label;      // what people see
    QString sink;       // node name, when it is already an output
    QString btPath;     // BlueZ device, when it is Bluetooth
    QString btAddress;
    bool connected = false;
    bool paired = true;
};

StepResult audioOutput(const Step &s, ToolContext &ctx)
{
    const Lang l = ctx.lang;
    const QString target = s.args.value(u"target"_s).toString();
    const QString name = s.args.value(u"name"_s).toString();
    QString error;
    const QList<AudioDevice> outs = ctx.system->outputs(&error);
    if (!error.isEmpty())
        return result(Outcome::Failed, tr2(l, u"Звуковая система не отвечает: %1"_s, u"The sound system does not answer: %1"_s).arg(error));

    if (target == u"speakers") {
        const AudioDevice *pick = nullptr;
        for (const AudioDevice &d : outs)
            if (d.builtIn && (!pick || d.isDefault))
                pick = &d;
        if (!pick)
            for (const AudioDevice &d : outs)
                if (d.bus != u"bluetooth" && !d.headphones && d.bus != u"usb" && (!pick || d.isDefault))
                    pick = &d;
        if (!pick)
            return result(Outcome::Failed, tr2(l, u"Не нашёл встроенных динамиков среди устройств вывода."_s,
                                               u"I found no built-in speakers among the outputs."_s));
        return setOutput(ctx, *pick, outs, {});
    }

    if (target == u"device") {
        for (const AudioDevice &d : outs)
            if (d.name == name || nameMatches(d.description, name))
                return setOutput(ctx, d, outs, {});
        // A known Bluetooth device that is not an output yet: treat it as headphones by name.
        if (name.isEmpty())
            return result(Outcome::Failed, tr2(l, u"Не указано устройство."_s, u"No device given."_s));
    }

    // Headphones: outputs that are headphones, plus known Bluetooth audio devices not connected yet.
    QList<Headphones> found;
    for (const AudioDevice &d : outs)
        if (d.headphones || d.bus == u"bluetooth")
            found.append({d.description, d.name, {}, d.btAddress.toUpper(), true, true});
    QString btError;
    const QList<BtDevice> bt = ctx.system->devices(&btError);
    for (const BtDevice &d : bt) {
        if (!d.audio)
            continue;
        bool known = false;
        for (Headphones &h : found)
            if (!h.btAddress.isEmpty() && h.btAddress == d.address.toUpper()) {
                h.btPath = d.path;
                h.label = d.name; // the name the user gave the device
                known = true;
            }
        if (!known)
            found.append({d.name, {}, d.path, d.address.toUpper(), d.connected, d.paired});
    }
    found.erase(std::remove_if(found.begin(), found.end(),
                               [&](const Headphones &h) {
                                   if (!h.paired && h.sink.isEmpty())
                                       return name.isEmpty() || !nameMatches(h.label, name);
                                   for (const QString &x : ctx.exclude)
                                       if (!x.isEmpty() && (h.sink == x || h.btAddress == x || h.label == x))
                                           return true;
                                   return false;
                               }),
                found.end());
    if (!name.isEmpty() && target != u"speakers") {
        QList<Headphones> named;
        for (const Headphones &h : found)
            if (nameMatches(h.label, name))
                named.append(h);
        if (named.isEmpty()) {
            QStringList known;
            for (const Headphones &h : found)
                known.append(quoted(l, h.label));
            return result(Outcome::NeedsUser,
                          tr2(l, u"Не нашёл наушники %1. %2"_s, u"I found no headphones called %1. %2"_s)
                              .arg(quoted(l, name),
                                   known.isEmpty() ? tr2(l, u"Знакомых наушников нет — их нужно один раз сопрячь (скажите «помоги подключить новые наушники»)."_s,
                                                         u"There are no known headphones — they need pairing once (say \"help me pair new headphones\")."_s)
                                                   : tr2(l, u"Знаю: %1."_s, u"I know: %1."_s).arg(known.join(u", "_s))));
        }
        found = named;
    }
    if (found.isEmpty()) {
        if (!ctx.exclude.isEmpty())
            return result(Outcome::NeedsUser, tr2(l, u"Других наушников я не знаю. Новые нужно один раз сопрячь — скажите «помоги подключить новые наушники»."_s,
                                                  u"I know no other headphones. New ones need pairing once — say \"help me pair new headphones\"."_s));
        return result(Outcome::NeedsUser,
                      tr2(l, u"Не вижу наушников: проводные не подключены, а знакомых Bluetooth-наушников нет%1. Если наушники беспроводные — их нужно один раз сопрячь: скажите «помоги подключить новые наушники»."_s,
                          u"I see no headphones: none are plugged in and no Bluetooth headphones are known%1. Wireless headphones need pairing once: say \"help me pair new headphones\"."_s)
                          .arg(btError.isEmpty() ? QString() : u" ("_s + btError + u")"_s));
    }
    // Several: the remembered choice, or the only one that is ready now, or ask.
    const QString preferred = ctx.memory ? ctx.memory->value(u"preferredHeadphones"_s).toString() : QString();
    int pick = -1;
    if (found.size() == 1)
        pick = 0;
    for (int i = 0; pick < 0 && i < found.size(); ++i)
        if (!preferred.isEmpty() && (found[i].btAddress == preferred || found[i].sink == preferred))
            pick = i;
    // Several and no remembered choice: the user chooses, even when one of them is connected already.
    if (pick < 0) {
        StepResult r = result(Outcome::NeedsUser, tr2(l, u"Какие наушники?"_s, u"Which headphones?"_s));
        for (const Headphones &h : found) {
            r.choices.append(h.label);
            QJsonObject a{{u"target"_s, u"headphones"_s}, {u"name"_s, h.label}};
            r.choiceArgs.append(a);
        }
        return r;
    }
    const Headphones h = found.at(pick);
    bool switchedOn = false, connectedNow = false;
    QString sink = h.sink;
    if (sink.isEmpty()) {
        StepResult a = ensureAdapter(ctx, &switchedOn);
        if (a.outcome != Outcome::Verified)
            return a;
        const BtDevice *dev = nullptr;
        const QList<BtDevice> now = ctx.system->devices(nullptr);
        for (const BtDevice &d : now)
            if (d.path == h.btPath)
                dev = &d;
        if (!dev)
            return result(Outcome::Failed, tr2(l, u"%1 пропали из списка Bluetooth-устройств."_s, u"%1 disappeared from the Bluetooth devices."_s)
                                               .arg(quoted(l, h.label)));
        StepResult c = connectBt(ctx, *dev, &connectedNow);
        if (c.outcome != Outcome::Verified) {
            if (switchedOn)
                c.undo.append(undoStep(u"bluetooth.power"_s, {{u"on"_s, false}}));
            return c;
        }
        if (ctx.progress)
            ctx.progress(tr2(l, u"Жду, когда %1 появятся как устройство звука…"_s, u"Waiting for %1 to appear as a sound device…"_s)
                             .arg(quoted(l, h.label)));
        const bool appeared = waitFor(ctx, 8000, [&] {
            for (const AudioDevice &d : ctx.system->outputs(nullptr))
                if (d.btAddress.toUpper() == h.btAddress) {
                    sink = d.name;
                    return true;
                }
            return false;
        });
        if (ctx.cancelled())
            return cancelled(l, tr2(l, u"Остановлено: %1 подключены, но звук не переключён."_s, u"Stopped: %1 are connected, but the sound was not switched."_s)
                                    .arg(quoted(l, h.label)));
        if (!appeared) {
            StepResult r = result(Outcome::Partial,
                                  tr2(l, u"%1 подключены по Bluetooth, но не появились как устройство звука. Возможно, у них выбран профиль без звука: откройте «Настройки звука» и выберите профиль A2DP."_s,
                                      u"%1 are connected over Bluetooth but did not appear as a sound output. Their profile may have no audio: open the sound settings and choose the A2DP profile."_s)
                                      .arg(quoted(l, h.label)));
            if (connectedNow)
                r.undo.append(undoStep(u"bluetooth.disconnect"_s, {{u"name"_s, h.label}}));
            return r;
        }
    }
    const QList<AudioDevice> current = ctx.system->outputs(nullptr);
    AudioDevice target_;
    for (const AudioDevice &d : current)
        if (d.name == sink)
            target_ = d;
    if (target_.name.isEmpty())
        return result(Outcome::Failed, tr2(l, u"Устройство звука %1 исчезло."_s, u"The sound device %1 disappeared."_s).arg(quoted(l, h.label)));
    QString how;
    if (connectedNow)
        how = tr2(l, u" — подключил"_s, u" — connected"_s) + (switchedOn ? tr2(l, u" (Bluetooth был выключен, включил)"_s, u" (Bluetooth was off, switched it on)"_s) : QString());
    // The previous output, as it was before anything changed.
    StepResult r = setOutput(ctx, target_, outs, how);
    if (connectedNow)
        r.undo.append(undoStep(u"bluetooth.disconnect"_s, {{u"name"_s, h.label}}));
    if (switchedOn)
        r.undo.append(undoStep(u"bluetooth.power"_s, {{u"on"_s, false}}));
    if (succeeded(r.outcome) && ctx.memory) {
        (*ctx.memory)[u"lastHeadphones"_s] = h.btAddress.isEmpty() ? sink : h.btAddress;
        if (!name.isEmpty())
            (*ctx.memory)[u"preferredHeadphones"_s] = h.btAddress.isEmpty() ? sink : h.btAddress;
    }
    r.data[u"headphones"_s] = h.label;
    return r;
}

StepResult audioVolume(const Step &s, ToolContext &ctx)
{
    const Lang l = ctx.lang;
    QString error;
    const QList<AudioDevice> outs = ctx.system->outputs(&error);
    const AudioDevice *d = defaultOf(outs);
    if (!d)
        return result(Outcome::Failed, tr2(l, u"Нет устройства вывода звука%1."_s, u"There is no sound output%1."_s)
                                           .arg(error.isEmpty() ? QString() : u": "_s + error));
    const int before = std::max(0, d->volume);
    const bool hasLevel = s.args.contains(u"level"_s);
    int wanted = hasLevel ? s.args.value(u"level"_s).toInt() : before + s.args.value(u"change"_s).toInt();
    // Over 100 % distorts: only when asked for a number above 100.
    wanted = std::clamp(wanted, 0, hasLevel ? 150 : std::max(100, before));
    if (ctx.memory && !hasLevel)
        (*ctx.memory)[u"lastVolumeStep"_s] = s.args.value(u"change"_s).toInt();
    const bool unmute = d->muted && wanted > 0;
    if (std::abs(wanted - before) <= 1 && !unmute)
        return result(Outcome::Verified, tr2(l, u"Громкость уже %1% (%2)."_s, u"The volume is already %1% (%2)."_s)
                                             .arg(before)
                                             .arg(d->description));
    if (QString e = ctx.system->setVolume(d->name, wanted); !e.isEmpty())
        return result(Outcome::Failed, tr2(l, u"Не удалось изменить громкость: %1"_s, u"Could not change the volume: %1"_s).arg(e));
    if (unmute)
        ctx.system->setMute(d->name, false);
    int now = -1;
    bool mutedNow = false;
    const bool ok = waitFor(ctx, 2000, [&] {
        for (const AudioDevice &x : ctx.system->outputs(nullptr))
            if (x.name == d->name) {
                now = x.volume;
                mutedNow = x.muted;
                return std::abs(x.volume - wanted) <= 2 && (!unmute || !x.muted);
            }
        return false;
    });
    if (!ok)
        return result(Outcome::Unconfirmed, tr2(l, u"Громкость %1% отправлена, но сейчас %2%."_s, u"Volume %1% was sent, but it reads %2% now."_s)
                                                .arg(wanted)
                                                .arg(now));
    StepResult r = result(Outcome::Verified, tr2(l, u"Громкость %1% (было %2%)%3."_s, u"Volume %1% (was %2%)%3."_s)
                                                 .arg(now)
                                                 .arg(before)
                                                 .arg(unmute ? tr2(l, u", звук включён"_s, u", unmuted"_s) : QString()));
    Q_UNUSED(mutedNow);
    r.undo.append(undoStep(u"audio.volume"_s, {{u"level"_s, before}}));
    if (unmute)
        r.undo.append(undoStep(u"audio.mute"_s, {{u"mute"_s, true}}));
    r.data = {{u"before"_s, before}, {u"after"_s, now}, {u"output"_s, d->name}};
    return r;
}

StepResult audioMute(const Step &s, ToolContext &ctx)
{
    const Lang l = ctx.lang;
    const bool mute = s.args.value(u"mute"_s).toBool();
    QString error;
    const QList<AudioDevice> outs = ctx.system->outputs(&error);
    const AudioDevice *d = defaultOf(outs);
    if (!d)
        return result(Outcome::Failed, tr2(l, u"Нет устройства вывода звука."_s, u"There is no sound output."_s));
    if (d->muted == mute)
        return result(Outcome::Verified, mute ? tr2(l, u"Звук уже выключен."_s, u"Sound is already muted."_s)
                                              : tr2(l, u"Звук уже включён, громкость %1%."_s, u"Sound is already on, volume %1%."_s).arg(d->volume));
    if (QString e = ctx.system->setMute(d->name, mute); !e.isEmpty())
        return result(Outcome::Failed, e);
    const bool ok = waitFor(ctx, 2000, [&] {
        for (const AudioDevice &x : ctx.system->outputs(nullptr))
            if (x.name == d->name)
                return x.muted == mute;
        return false;
    });
    if (!ok)
        return result(Outcome::Unconfirmed, tr2(l, u"Команда отправлена, но звуковой сервер не подтвердил."_s,
                                                u"Sent, but the sound server did not confirm it."_s));
    StepResult r = result(Outcome::Verified, mute ? tr2(l, u"Звук выключен (%1)."_s, u"Muted (%1)."_s).arg(d->description)
                                                  : tr2(l, u"Звук включён, громкость %1%."_s, u"Unmuted, volume %1%."_s).arg(d->volume));
    r.undo.append(undoStep(u"audio.mute"_s, {{u"mute"_s, !mute}}));
    return r;
}

StepResult audioInput(const Step &s, ToolContext &ctx)
{
    const Lang l = ctx.lang;
    const QString target = s.args.value(u"target"_s).toString();
    const QString name = s.args.value(u"name"_s).toString();
    QString error;
    const QList<AudioDevice> ins = ctx.system->inputs(&error);
    if (ins.isEmpty())
        return result(Outcome::Failed, tr2(l, u"Микрофонов не найдено%1."_s, u"No microphones found%1."_s)
                                           .arg(error.isEmpty() ? QString() : u": "_s + error));
    QList<const AudioDevice *> match;
    for (const AudioDevice &d : ins) {
        if (!name.isEmpty() ? nameMatches(d.description, name)
            : target == u"builtin" ? d.builtIn
            : target == u"headset" ? (d.headphones || d.bus == u"bluetooth" || d.bus == u"usb")
                                   : false)
            match.append(&d);
    }
    if (match.isEmpty()) {
        QStringList known;
        for (const AudioDevice &d : ins)
            known.append(quoted(l, d.description) + (d.isDefault ? tr2(l, u" (выбран)"_s, u" (current)"_s) : QString()));
        return result(Outcome::NeedsUser,
                      (target == u"headset" ? tr2(l, u"Микрофона гарнитуры не видно. У Bluetooth-наушников он появляется только в режиме гарнитуры (HFP) — переключите профиль в настройках звука. "_s,
                                                  u"No headset microphone is visible. Bluetooth headphones offer one only in headset mode (HFP) — switch the profile in the sound settings. "_s)
                                            : QString())
                          + tr2(l, u"Доступные микрофоны: %1."_s, u"Microphones: %1."_s).arg(known.join(u", "_s)));
    }
    if (match.size() > 1 && name.isEmpty()) {
        StepResult r = result(Outcome::NeedsUser, tr2(l, u"Какой микрофон?"_s, u"Which microphone?"_s));
        for (const AudioDevice *d : match) {
            r.choices.append(d->description);
            r.choiceArgs.append(QJsonObject{{u"name"_s, d->description}});
        }
        return r;
    }
    const AudioDevice *d = match.first();
    const AudioDevice *was = defaultOf(ins);
    if (d->isDefault)
        return result(Outcome::Verified, tr2(l, u"Микрофон уже %1."_s, u"The microphone is already %1."_s).arg(quoted(l, d->description)));
    if (QString e = ctx.system->setDefaultInput(d->name); !e.isEmpty())
        return result(Outcome::Failed, e);
    const bool ok = waitFor(ctx, 2000, [&] {
        for (const AudioDevice &x : ctx.system->inputs(nullptr))
            if (x.isDefault)
                return x.name == d->name;
        return false;
    });
    if (!ok)
        return result(Outcome::Unconfirmed, tr2(l, u"Переключение микрофона отправлено, но не подтвердилось."_s, u"The microphone switch was sent but not confirmed."_s));
    StepResult r = result(Outcome::Verified, tr2(l, u"Микрофон: %1%2."_s, u"Microphone: %1%2."_s)
                                                 .arg(quoted(l, d->description),
                                                      d->muted ? tr2(l, u" (он выключен — включите его в трее)"_s, u" (it is muted — unmute it in the tray)"_s) : QString()));
    if (was)
        r.undo.append(undoStep(u"audio.input"_s, {{u"name"_s, was->description}}));
    return r;
}

StepResult audioList(ToolContext &ctx)
{
    const Lang l = ctx.lang;
    QString error;
    const QList<AudioDevice> outs = ctx.system->outputs(&error);
    if (!error.isEmpty())
        return result(Outcome::Failed, error);
    const QList<AudioDevice> ins = ctx.system->inputs(nullptr);
    QStringList lines{tr2(l, u"Вывод звука:"_s, u"Outputs:"_s)};
    for (const AudioDevice &d : outs)
        lines.append(u"• "_s + d.description + (d.isDefault ? tr2(l, u" — сейчас, %1%%2"_s, u" — current, %1%%2"_s)
                                                                    .arg(d.volume)
                                                                    .arg(d.muted ? tr2(l, u", без звука"_s, u", muted"_s) : QString())
                                                              : QString()));
    lines.append(tr2(l, u"Микрофоны:"_s, u"Microphones:"_s));
    for (const AudioDevice &d : ins)
        lines.append(u"• "_s + d.description + (d.isDefault ? tr2(l, u" — выбран"_s, u" — current"_s) : QString())
                     + (d.isDefault && d.muted ? tr2(l, u", выключен"_s, u", muted"_s) : QString()));
    return result(Outcome::Verified, lines.join(u'\n'));
}

// ---------------------------------------------------------------------------
// Bluetooth

const BtDevice *pickBt(const QList<BtDevice> &devs, const QString &name, bool wantConnected, QList<const BtDevice *> *many)
{
    for (const BtDevice &d : devs) {
        if (wantConnected && !d.connected)
            continue;
        if (!wantConnected && !d.paired)
            continue;
        const bool headphonesWord = name.isEmpty() || norm(name).contains(u"наушник"_s) || norm(name).contains(u"headphone"_s)
                                    || norm(name).contains(u"гарнитур"_s) || norm(name).contains(u"headset"_s);
        if ((headphonesWord && d.audio) || (!name.isEmpty() && nameMatches(d.name, name)))
            many->append(&d);
    }
    return many->size() == 1 ? many->first() : nullptr;
}

StepResult btPower(const Step &s, ToolContext &ctx)
{
    const Lang l = ctx.lang;
    const bool on = s.args.value(u"on"_s).toBool();
    QString error;
    const BtAdapter a = ctx.system->adapter(&error);
    if (!a.present)
        return result(Outcome::Failed, tr2(l, u"Bluetooth недоступен: %1."_s, u"Bluetooth is unavailable: %1."_s).arg(error));
    if (a.powered == on)
        return result(Outcome::Verified, on ? tr2(l, u"Bluetooth уже включён."_s, u"Bluetooth is already on."_s)
                                            : tr2(l, u"Bluetooth уже выключен."_s, u"Bluetooth is already off."_s));
    if (on) {
        bool switched = false;
        StepResult r = ensureAdapter(ctx, &switched);
        if (r.outcome != Outcome::Verified)
            return r;
    } else {
        const QString e = ctx.system->setAdapterPowered(false);
        if (!e.isEmpty())
            return result(Outcome::Failed, e);
        if (!waitFor(ctx, 3000, [&] { return !ctx.system->adapter(nullptr).powered; }))
            return result(Outcome::Unconfirmed, tr2(l, u"Команда отправлена, но адаптер всё ещё включён."_s, u"Sent, but the adapter is still on."_s));
    }
    StepResult r = result(Outcome::Verified, on ? tr2(l, u"Bluetooth включён."_s, u"Bluetooth is on."_s)
                                                : tr2(l, u"Bluetooth выключен."_s, u"Bluetooth is off."_s));
    r.undo.append(undoStep(u"bluetooth.power"_s, {{u"on"_s, !on}}));
    return r;
}

StepResult btConnect(const Step &s, ToolContext &ctx)
{
    const Lang l = ctx.lang;
    const QString name = s.args.value(u"name"_s).toString();
    QString error;
    const QList<BtDevice> devs = ctx.system->devices(&error);
    if (!error.isEmpty())
        return result(Outcome::Failed, tr2(l, u"Bluetooth недоступен: %1."_s, u"Bluetooth is unavailable: %1."_s).arg(error));
    QList<const BtDevice *> many;
    const BtDevice *d = pickBt(devs, name, false, &many);
    if (!d && many.size() > 1) {
        StepResult r = result(Outcome::NeedsUser, tr2(l, u"Какое устройство подключить?"_s, u"Which device should I connect?"_s));
        for (const BtDevice *x : many) {
            r.choices.append(x->name);
            r.choiceArgs.append(QJsonObject{{u"name"_s, x->name}});
        }
        return r;
    }
    if (!d) {
        if (!name.isEmpty())
            for (const BtDevice &x : devs)
                if (nameMatches(x.name, name) && !x.paired) {
                    ctx.system->openBluetoothWizard();
                    return result(Outcome::NeedsUser, tr2(l, u"%1 ещё не сопряжено. "_s, u"%1 is not paired yet. "_s).arg(quoted(l, x.name))
                                                          + pairingGuide(l));
                }
        return result(Outcome::NeedsUser, tr2(l, u"Не нашёл знакомого устройства %1. Новое устройство нужно один раз сопрячь — скажите «помоги подключить новые наушники»."_s,
                                              u"I found no known device %1. A new device needs pairing once — say \"help me pair new headphones\"."_s)
                                              .arg(name.isEmpty() ? QString() : quoted(l, name)));
    }
    if (d->connected)
        return result(Outcome::Verified, tr2(l, u"%1 уже подключено."_s, u"%1 is already connected."_s).arg(quoted(l, d->name)));
    bool switched = false, connectedNow = false;
    StepResult a = ensureAdapter(ctx, &switched);
    if (a.outcome != Outcome::Verified)
        return a;
    StepResult c = connectBt(ctx, *d, &connectedNow);
    if (switched)
        c.undo.append(undoStep(u"bluetooth.power"_s, {{u"on"_s, false}}));
    if (c.outcome != Outcome::Verified)
        return c;
    c.text = tr2(l, u"%1 подключено%2."_s, u"%1 is connected%2."_s)
                 .arg(quoted(l, d->name), switched ? tr2(l, u" (Bluetooth был выключен, включил)"_s, u" (Bluetooth was off, switched it on)"_s) : QString());
    c.undo.prepend(undoStep(u"bluetooth.disconnect"_s, {{u"name"_s, d->name}}));
    return c;
}

StepResult btDisconnect(const Step &s, ToolContext &ctx)
{
    const Lang l = ctx.lang;
    const QString name = s.args.value(u"name"_s).toString();
    QString error;
    const QList<BtDevice> devs = ctx.system->devices(&error);
    if (!error.isEmpty())
        return result(Outcome::Failed, error);
    QList<const BtDevice *> many;
    const BtDevice *d = pickBt(devs, name, true, &many);
    if (!d && many.size() > 1) {
        StepResult r = result(Outcome::NeedsUser, tr2(l, u"Какое устройство отключить?"_s, u"Which device should I disconnect?"_s));
        for (const BtDevice *x : many) {
            r.choices.append(x->name);
            r.choiceArgs.append(QJsonObject{{u"name"_s, x->name}});
        }
        return r;
    }
    if (!d)
        return result(Outcome::Verified, name.isEmpty() ? tr2(l, u"Подключённых Bluetooth-наушников нет."_s, u"No Bluetooth headphones are connected."_s)
                                                        : tr2(l, u"%1 и так не подключено."_s, u"%1 is not connected."_s).arg(quoted(l, name)));
    if (QString e = ctx.system->disconnectDevice(d->path); !e.isEmpty())
        return result(Outcome::Failed, e);
    const QString path = d->path;
    const bool ok = waitFor(ctx, 5000, [&] {
        for (const BtDevice &x : ctx.system->devices(nullptr))
            if (x.path == path)
                return !x.connected;
        return true;
    });
    if (!ok)
        return result(Outcome::Unconfirmed, tr2(l, u"Отключение отправлено, но устройство ещё на связи."_s, u"Sent, but the device is still connected."_s));
    StepResult r = result(Outcome::Verified, tr2(l, u"%1 отключено."_s, u"%1 is disconnected."_s).arg(quoted(l, d->name)));
    r.undo.append(undoStep(u"bluetooth.connect"_s, {{u"name"_s, d->name}}));
    return r;
}

StepResult btStatus(ToolContext &ctx)
{
    const Lang l = ctx.lang;
    QString error;
    const BtAdapter a = ctx.system->adapter(&error);
    if (!a.present)
        return result(Outcome::Verified, tr2(l, u"Bluetooth недоступен: %1."_s, u"Bluetooth is unavailable: %1."_s).arg(error));
    QStringList lines{a.powered ? tr2(l, u"Bluetooth включён."_s, u"Bluetooth is on."_s) : tr2(l, u"Bluetooth выключен."_s, u"Bluetooth is off."_s)};
    const QList<BtDevice> devs = ctx.system->devices(nullptr);
    for (const BtDevice &d : devs)
        if (d.paired)
            lines.append(u"• "_s + d.name + (d.connected ? tr2(l, u" — подключено"_s, u" — connected"_s) : tr2(l, u" — не подключено"_s, u" — not connected"_s)));
    if (lines.size() == 1)
        lines.append(tr2(l, u"Знакомых устройств нет."_s, u"No paired devices."_s));
    return result(Outcome::Verified, lines.join(u'\n'));
}

StepResult btPair(ToolContext &ctx)
{
    const Lang l = ctx.lang;
    QString error;
    const BtAdapter a = ctx.system->adapter(&error);
    if (a.present && !a.powered) {
        bool switched = false;
        ensureAdapter(ctx, &switched);
    }
    const QString e = ctx.system->openBluetoothWizard();
    return result(Outcome::NeedsUser, (e.isEmpty() ? QString() : tr2(l, u"Не смог открыть мастер Bluetooth (%1): откройте «Параметры системы → Bluetooth» вручную.\n"_s,
                                                                     u"Could not open the Bluetooth wizard (%1): open System Settings → Bluetooth yourself.\n"_s)
                                                                     .arg(e))
                                          + pairingGuide(l));
}

// ---------------------------------------------------------------------------
// Applications and windows

struct AppMatch
{
    QString id;
    QString name;
    QStringList choices;
};

AppMatch resolveApp(ToolContext &ctx, const QString &query)
{
    AppMatch m;
    const QString kind = genericAppKind(query);
    if (!kind.isEmpty()) {
        m.id = ctx.system->defaultApp(kind);
        if (!m.id.isEmpty()) {
            for (const AppInfo &a : ctx.system->findApps(m.id))
                if (a.desktopId == m.id)
                    m.name = a.name;
            if (m.name.isEmpty())
                m.name = m.id.section(u'.', -1);
            return m;
        }
    }
    const QList<AppInfo> found = ctx.system->findApps(query);
    if (found.isEmpty() || found.first().score < 0.6)
        return m;
    // Two equally good different applications ("office"): ask.
    if (found.size() > 1 && found[1].score >= found[0].score - 0.01 && found[0].score < 1.0) {
        for (const AppInfo &a : found)
            if (a.score >= found[0].score - 0.01 && m.choices.size() < 4)
                m.choices.append(a.name);
        if (m.choices.size() > 1)
            return m;
        m.choices.clear();
    }
    m.id = found.first().desktopId;
    m.name = found.first().name;
    return m;
}

bool windowOf(const Window &w, const QString &desktopId)
{
    const QString a = w.app.toLower(), id = desktopId.toLower();
    return !a.isEmpty() && (a == id || id.endsWith(u'.' + a) || a.endsWith(u'.' + id.section(u'.', -1)) || id.section(u'.', -1) == a);
}

StepResult noApp(Lang l, const QString &query, const AppMatch &m)
{
    if (!m.choices.isEmpty()) {
        StepResult r = result(Outcome::NeedsUser, tr2(l, u"Какое приложение?"_s, u"Which application?"_s));
        for (const QString &c : m.choices) {
            r.choices.append(c);
            r.choiceArgs.append(QJsonObject{{u"query"_s, c}});
        }
        return r;
    }
    return result(Outcome::Failed, tr2(l, u"Не нашёл установленного приложения %1. Проверьте название или установите его через Discover."_s,
                                       u"I found no installed application %1. Check the name or install it with Discover."_s)
                                       .arg(quoted(l, query)));
}

StepResult appOpen(const Step &s, ToolContext &ctx, bool focusOnly)
{
    const Lang l = ctx.lang;
    const QString query = s.args.value(u"query"_s).toString();
    const AppMatch m = resolveApp(ctx, query);
    if (m.id.isEmpty())
        return noApp(l, query, m);
    QString error;
    // KWin that does not answer (another compositor, a stuck script): no waiting for windows.
    QList<Window> wins;
    if (ctx.system->windowsAvailable())
        wins = ctx.system->windows(&error);
    const bool canSee = ctx.system->windowsAvailable() && error.isEmpty();
    if (canSee) {
        for (const Window &w : wins)
            if (windowOf(w, m.id)) {
                if (w.active)
                    return result(Outcome::Verified, tr2(l, u"%1 уже открыт и активен."_s, u"%1 is already open and active."_s).arg(m.name));
                if (QString e = ctx.system->activateWindow(w.id); !e.isEmpty())
                    return result(Outcome::Failed, e);
                const bool ok = waitFor(ctx, 3000, [&] {
                    for (const Window &x : ctx.system->windows(nullptr))
                        if (x.id == w.id)
                            return x.active;
                    return false;
                });
                return ok ? result(Outcome::Verified, tr2(l, u"%1 уже был открыт — переключился на его окно."_s, u"%1 was already open — switched to its window."_s).arg(m.name))
                          : result(Outcome::Unconfirmed, tr2(l, u"Попросил KWin показать окно %1, но оно не стало активным."_s, u"Asked KWin to show %1, but it did not become active."_s).arg(m.name));
            }
    }
    if (focusOnly)
        return result(Outcome::NeedsUser, canSee ? tr2(l, u"%1 сейчас не открыт. Открыть? Скажите «открой %2»."_s, u"%1 is not open. Say \"open %2\" to start it."_s).arg(m.name, query)
                                                 : tr2(l, u"Не могу проверить окна: %1"_s, u"I cannot see the windows: %1"_s).arg(error));
    if (ctx.progress)
        ctx.progress(tr2(l, u"Запускаю %1…"_s, u"Starting %1…"_s).arg(m.name));
    if (QString e = ctx.system->launch(m.id); !e.isEmpty())
        return result(Outcome::Failed, tr2(l, u"Не удалось запустить %1: %2"_s, u"Could not start %1: %2"_s).arg(m.name, e));
    if (!canSee)
        return result(Outcome::Unconfirmed, tr2(l, u"Запускаю %1. Проверить, что окно открылось, не могу: окна видит только служба Jarvis с KWin."_s,
                                                u"Starting %1. I cannot check that its window opened: only the Jarvis service with KWin sees windows."_s)
                                                .arg(m.name));
    const bool ok = waitFor(ctx, 15000, [&] {
        for (const Window &x : ctx.system->windows(nullptr))
            if (windowOf(x, m.id))
                return true;
        return false;
    });
    if (ctx.cancelled())
        return cancelled(l, tr2(l, u"Остановлено. %1 уже запускается — закрыть его можно обычным способом."_s, u"Stopped. %1 is already starting — close it the usual way."_s).arg(m.name));
    return ok ? result(Outcome::Verified, tr2(l, u"%1 открыт."_s, u"%1 is open."_s).arg(m.name))
              : result(Outcome::Unconfirmed, tr2(l, u"%1 запущен, но его окно за 15 секунд не появилось — возможно, оно ещё загружается."_s,
                                                 u"%1 was started, but no window appeared in 15 seconds — it may still be loading."_s)
                                                 .arg(m.name));
}

StepResult appClose(const Step &s, ToolContext &ctx)
{
    const Lang l = ctx.lang;
    const QString query = s.args.value(u"query"_s).toString();
    const AppMatch m = resolveApp(ctx, query);
    if (m.id.isEmpty())
        return noApp(l, query, m);
    QString error;
    if (!ctx.system->windowsAvailable())
        return result(Outcome::Failed, tr2(l, u"Закрывать окна я могу только через службу Jarvis и KWin, а они недоступны."_s,
                                           u"I can close windows only through the Jarvis service and KWin, which are unavailable."_s));
    QStringList ids;
    for (const Window &w : ctx.system->windows(&error))
        if (windowOf(w, m.id))
            ids.append(w.id);
    if (ids.isEmpty())
        return result(Outcome::Verified, tr2(l, u"%1 не открыт."_s, u"%1 is not open."_s).arg(m.name));
    for (const QString &id : ids.mid(0, 8))
        ctx.system->closeWindow(id);
    const bool gone = waitFor(ctx, 4000, [&] {
        for (const Window &w : ctx.system->windows(nullptr))
            if (ids.contains(w.id))
                return false;
        return true;
    });
    if (gone)
        return result(Outcome::Verified, tr2(l, u"%1 закрыт."_s, u"%1 is closed."_s).arg(m.name));
    return result(Outcome::NeedsUser, tr2(l, u"Попросил %1 закрыться. Окно ещё открыто — вероятно, приложение спрашивает о сохранении: ответьте в его окне. Принудительно я не закрываю."_s,
                                          u"Asked %1 to close. A window is still open — the application probably asks about saving: answer it there. I never force-close."_s)
                                          .arg(m.name));
}

// ---------------------------------------------------------------------------
// Screen, notifications, desktop, power

StepResult brightness(const Step &s, ToolContext &ctx)
{
    const Lang l = ctx.lang;
    QString error;
    const int before = ctx.system->brightness(&error);
    if (before < 0)
        return result(Outcome::Failed, tr2(l, u"Яркость здесь не регулируется: %1. У внешнего монитора её меняют его кнопками."_s,
                                           u"Brightness cannot be adjusted here: %1. An external monitor has its own buttons for it."_s)
                                           .arg(error));
    int wanted = s.args.contains(u"level"_s) ? s.args.value(u"level"_s).toInt() : before + s.args.value(u"change"_s).toInt();
    wanted = std::clamp(wanted, 1, 100);
    if (std::abs(wanted - before) <= 1)
        return result(Outcome::Verified, tr2(l, u"Яркость уже %1%."_s, u"Brightness is already %1%."_s).arg(before));
    if (QString e = ctx.system->setBrightness(wanted); !e.isEmpty())
        return result(Outcome::Failed, tr2(l, u"Не удалось изменить яркость: %1"_s, u"Could not change the brightness: %1"_s).arg(e));
    int now = -1;
    const bool ok = waitFor(ctx, 2000, [&] {
        now = ctx.system->brightness(nullptr);
        return std::abs(now - wanted) <= 3;
    });
    if (!ok)
        return result(Outcome::Unconfirmed, tr2(l, u"Яркость %1% отправлена, сейчас %2%."_s, u"Brightness %1% sent, it reads %2%."_s).arg(wanted).arg(now));
    StepResult r = result(Outcome::Verified, tr2(l, u"Яркость %1% (было %2%)."_s, u"Brightness %1% (was %2%)."_s).arg(now).arg(before));
    r.undo.append(undoStep(u"screen.brightness"_s, {{u"level"_s, before}}));
    return r;
}

StepResult dnd(const Step &s, ToolContext &ctx)
{
    const Lang l = ctx.lang;
    const bool on = s.args.value(u"on"_s).toBool();
    QString error;
    const int before = ctx.system->doNotDisturb(&error);
    if (before < 0)
        return result(Outcome::Failed, tr2(l, u"Режим «Не беспокоить» недоступен: %1"_s, u"Do not disturb is unavailable: %1"_s).arg(error));
    if (before == int(on))
        return result(Outcome::Verified, on ? tr2(l, u"«Не беспокоить» уже включён."_s, u"Do not disturb is already on."_s)
                                            : tr2(l, u"«Не беспокоить» уже выключен."_s, u"Do not disturb is already off."_s));
    if (QString e = ctx.system->setDoNotDisturb(on); !e.isEmpty())
        return result(Outcome::Failed, e);
    const bool ok = waitFor(ctx, 2000, [&] { return ctx.system->doNotDisturb(nullptr) == int(on); });
    if (!ok && !on)
        return result(Outcome::Partial, tr2(l, u"Свой запрет на уведомления я снял, но «Не беспокоить» остаётся включённым — его включили в Plasma или другое приложение. Выключите его в окне уведомлений в трее."_s,
                                            u"I lifted my own block, but do not disturb stays on — Plasma or another application set it. Switch it off in the notifications popup in the tray."_s));
    if (!ok)
        return result(Outcome::Unconfirmed, tr2(l, u"Запрос отправлен, но служба уведомлений не подтвердила."_s, u"Sent, but the notification service did not confirm it."_s));
    StepResult r = result(Outcome::Verified, on ? tr2(l, u"«Не беспокоить» включён: уведомления не будут всплывать, пока его не выключить (или до перезапуска службы Jarvis)."_s,
                                                      u"Do not disturb is on: notifications stay quiet until it is switched off (or the Jarvis service restarts)."_s)
                                                : tr2(l, u"«Не беспокоить» выключен."_s, u"Do not disturb is off."_s));
    r.undo.append(undoStep(u"notifications.dnd"_s, {{u"on"_s, !on}}));
    return r;
}

StepResult settingsOpen(const Step &s, ToolContext &ctx)
{
    const Lang l = ctx.lang;
    const QString page = s.args.value(u"page"_s).toString();
    if (QString e = ctx.system->openSettings(page); !e.isEmpty())
        return result(Outcome::Failed, tr2(l, u"Не удалось открыть настройки: %1"_s, u"Could not open the settings: %1"_s).arg(e));
    if (!ctx.system->windowsAvailable())
        return result(Outcome::Unconfirmed, tr2(l, u"Открываю настройки (%1)."_s, u"Opening the settings (%1)."_s).arg(page));
    const bool ok = waitFor(ctx, 8000, [&] {
        for (const Window &w : ctx.system->windows(nullptr))
            if (w.app.contains(u"systemsettings"_s) || w.app.contains(u"kcmshell"_s) || w.app.contains(u"bluedevil"_s))
                return true;
        return false;
    });
    return ok ? result(Outcome::Verified, tr2(l, u"Открыл настройки (%1)."_s, u"Opened the settings (%1)."_s).arg(page))
              : result(Outcome::Unconfirmed, tr2(l, u"Настройки запускаются, но окно пока не появилось."_s, u"The settings are starting, but no window appeared yet."_s));
}

StepResult workspaceSwitch(const Step &s, ToolContext &ctx)
{
    const Lang l = ctx.lang;
    const QString profile = s.args.value(u"profile"_s).toString();
    const QString before = ctx.system->currentProfile();
    if (before == profile)
        return result(Outcome::Verified, tr2(l, u"Профиль %1 уже включён."_s, u"The %1 profile is already on."_s).arg(profile));
    if (QString e = ctx.system->switchProfile(profile); !e.isEmpty())
        return result(Outcome::Failed, e);
    if (!waitFor(ctx, 3000, [&] { return ctx.system->currentProfile() == profile; }))
        return result(Outcome::Unconfirmed, tr2(l, u"Переключение на %1 отправлено."_s, u"Switching to %1 was sent."_s).arg(profile));
    StepResult r = result(Outcome::Verified, tr2(l, u"Профиль Jarvis: %1."_s, u"Jarvis profile: %1."_s).arg(profile));
    if (!before.isEmpty())
        r.undo.append(undoStep(u"workspace.switch"_s, {{u"profile"_s, before}}));
    return r;
}

QString profileName(Lang l, const QString &p)
{
    if (p == u"power-saver")
        return tr2(l, u"энергосбережение"_s, u"power saver"_s);
    if (p == u"performance")
        return tr2(l, u"производительность"_s, u"performance"_s);
    if (p == u"balanced")
        return tr2(l, u"сбалансированный"_s, u"balanced"_s);
    return p;
}

StepResult powerProfile(const Step &s, ToolContext &ctx)
{
    const Lang l = ctx.lang;
    const QString profile = s.args.value(u"profile"_s).toString();
    QString error;
    const QString before = ctx.system->powerProfile(&error);
    if (before.isEmpty())
        return result(Outcome::Failed, tr2(l, u"Профили питания недоступны: %1."_s, u"Power profiles are unavailable: %1."_s).arg(error));
    if (before == profile)
        return result(Outcome::Verified, tr2(l, u"Профиль питания уже «%1»."_s, u"The power profile is already %1."_s).arg(profileName(l, profile)));
    if (QString e = ctx.system->setPowerProfile(profile); !e.isEmpty()) {
        if (e.contains(u"uthoriz"_s) || e.contains(u"ermission"_s) || e.contains(u"denied"_s, Qt::CaseInsensitive))
            return result(Outcome::NeedsUser, tr2(l, u"Система не разрешила сменить профиль питания (polkit). Смените его в трее (значок батареи)."_s,
                                                  u"The system did not allow changing the power profile (polkit). Change it in the tray (the battery icon)."_s));
        if (e.contains(u"not available"_s, Qt::CaseInsensitive) || e.contains(u"invalid"_s, Qt::CaseInsensitive))
            return result(Outcome::Failed, tr2(l, u"Профиль «%1» на этом ноутбуке недоступен."_s, u"The %1 profile is unavailable on this laptop."_s).arg(profileName(l, profile)));
        return result(Outcome::Failed, e);
    }
    if (!waitFor(ctx, 3000, [&] { return ctx.system->powerProfile(nullptr) == profile; }))
        return result(Outcome::Unconfirmed, tr2(l, u"Смена профиля отправлена, но не подтвердилась."_s, u"The change was sent but not confirmed."_s));
    StepResult r = result(Outcome::Verified, tr2(l, u"Профиль питания: %1 (был: %2)."_s, u"Power profile: %1 (was %2)."_s)
                                                 .arg(profileName(l, profile), profileName(l, before)));
    r.undo.append(undoStep(u"power.profile"_s, {{u"profile"_s, before}}));
    return r;
}

QString duration(Lang l, qint64 seconds)
{
    const qint64 h = seconds / 3600, m = (seconds % 3600) / 60;
    if (h > 0)
        return tr2(l, u"%1 ч %2 мин"_s, u"%1 h %2 min"_s).arg(h).arg(m);
    return tr2(l, u"%1 мин"_s, u"%1 min"_s).arg(m);
}

StepResult systemStatus(const Step &s, ToolContext &ctx)
{
    const Lang l = ctx.lang;
    const QString what = s.args.value(u"what"_s).toString(u"all"_s);
    QStringList lines;
    if (what == u"battery" || what == u"all") {
        const Battery b = ctx.system->battery();
        if (!b.present)
            lines.append(tr2(l, u"Батареи нет (питание от сети)."_s, u"There is no battery (mains power)."_s));
        else {
            QString state = b.state == u"charging" ? tr2(l, u"заряжается"_s, u"charging"_s)
                            : b.state == u"discharging" ? tr2(l, u"разряжается"_s, u"discharging"_s)
                            : b.state == u"full" ? tr2(l, u"полностью заряжена"_s, u"full"_s)
                                                 : tr2(l, u"на зарядке не идёт"_s, u"not charging"_s);
            QString line = tr2(l, u"Батарея: %1%, %2"_s, u"Battery: %1%, %2"_s).arg(int(std::lround(b.percent))).arg(state);
            if (b.state == u"discharging" && b.secondsToEmpty > 0)
                line += tr2(l, u", осталось около %1"_s, u", about %1 left"_s).arg(duration(l, b.secondsToEmpty));
            if (b.state == u"charging" && b.secondsToFull > 0)
                line += tr2(l, u", до полной около %1"_s, u", full in about %1"_s).arg(duration(l, b.secondsToFull));
            lines.append(line + u'.');
        }
    }
    if (what == u"power" || what == u"all") {
        QString error;
        const QString p = ctx.system->powerProfile(&error);
        lines.append(p.isEmpty() ? tr2(l, u"Профиль питания неизвестен: %1."_s, u"Power profile unknown: %1."_s).arg(error)
                                 : tr2(l, u"Профиль питания: %1."_s, u"Power profile: %1."_s).arg(profileName(l, p)));
    }
    if (what == u"network" || what == u"all") {
        QString detail;
        const QString n = ctx.system->network(&detail);
        const QString via = detail.isEmpty() ? QString() : tr2(l, u" (%1)"_s, u" (%1)"_s).arg(detail);
        lines.append(n == u"full"      ? tr2(l, u"Интернет есть%1."_s, u"Internet is available%1."_s).arg(via)
                     : n == u"limited" ? tr2(l, u"Сеть есть, но интернет ограничен%1."_s, u"Connected, but internet access is limited%1."_s).arg(via)
                     : n == u"portal"  ? tr2(l, u"Сеть требует входа через страницу (captive portal)%1."_s, u"The network wants a sign-in page (captive portal)%1."_s).arg(via)
                     : n == u"none"    ? tr2(l, u"Интернета нет. Всё основное я делаю и без него."_s, u"No internet. I do the essentials without it."_s)
                                       : tr2(l, u"Состояние сети неизвестно (NetworkManager не отвечает)."_s, u"Network state unknown (NetworkManager does not answer)."_s));
    }
    return result(Outcome::Verified, lines.join(u'\n'));
}

StepResult diagnose(const Step &s, ToolContext &ctx)
{
    const Lang l = ctx.lang;
    const QString what = s.args.value(u"what"_s).toString();
    QStringList lines;
    bool problem = false;
    if (what == u"audio") {
        QString error;
        const QList<AudioDevice> outs = ctx.system->outputs(&error);
        const AudioDevice *d = defaultOf(outs);
        if (!error.isEmpty()) {
            problem = true;
            lines.append(tr2(l, u"Звуковой сервер не отвечает: %1. Перезапустите его: systemctl --user restart pipewire pipewire-pulse wireplumber"_s,
                             u"The sound server does not answer: %1. Restart it: systemctl --user restart pipewire pipewire-pulse wireplumber"_s)
                             .arg(error));
        } else if (!d) {
            problem = true;
            lines.append(tr2(l, u"Нет устройства вывода по умолчанию."_s, u"There is no default output."_s));
        } else {
            lines.append(tr2(l, u"Звук идёт в %1, громкость %2%."_s, u"Sound goes to %1, volume %2%."_s).arg(deviceLabel(l, *d)).arg(d->volume));
            if (d->muted) {
                problem = true;
                lines.append(tr2(l, u"Звук выключен (mute) — скажите «включи звук»."_s, u"It is muted — say \"unmute\"."_s));
            }
            if (d->volume == 0) {
                problem = true;
                lines.append(tr2(l, u"Громкость на нуле."_s, u"The volume is zero."_s));
            }
            if (!d->headphones && d->bus != u"bluetooth")
                for (const AudioDevice &x : outs)
                    if (x.headphones && !x.isDefault)
                        lines.append(tr2(l, u"Наушники %1 подключены, но звук идёт не в них — скажите «выведи звук в наушники»."_s,
                                         u"Headphones %1 are connected, but sound goes elsewhere — say \"sound to headphones\"."_s)
                                         .arg(quoted(l, x.description)));
        }
    } else if (what == u"bluetooth") {
        QString error;
        const BtAdapter a = ctx.system->adapter(&error);
        if (!a.present) {
            problem = true;
            lines.append(tr2(l, u"Bluetooth недоступен: %1."_s, u"Bluetooth is unavailable: %1."_s).arg(error));
        } else {
            lines.append(a.powered ? tr2(l, u"Адаптер %1 включён."_s, u"Adapter %1 is on."_s).arg(quoted(l, a.name))
                                   : tr2(l, u"Адаптер %1 выключен — скажите «включи блютуз»."_s, u"Adapter %1 is off — say \"turn Bluetooth on\"."_s).arg(quoted(l, a.name)));
            problem = !a.powered;
            int paired = 0;
            for (const BtDevice &d : ctx.system->devices(nullptr))
                if (d.paired) {
                    ++paired;
                    lines.append(u"• "_s + d.name + (d.connected ? tr2(l, u" — подключено"_s, u" — connected"_s) : tr2(l, u" — не подключено"_s, u" — not connected"_s)));
                }
            if (paired == 0)
                lines.append(tr2(l, u"Знакомых устройств нет: новые нужно сопрячь («помоги подключить новые наушники»)."_s,
                                 u"No paired devices: new ones need pairing (\"help me pair new headphones\")."_s));
        }
    } else if (what == u"brightness") {
        QString error;
        const int b = ctx.system->brightness(&error);
        problem = b < 0;
        lines.append(b < 0 ? tr2(l, u"Яркость не регулируется: %1."_s, u"Brightness is not adjustable: %1."_s).arg(error)
                           : tr2(l, u"Яркость регулируется, сейчас %1%."_s, u"Brightness is adjustable, now %1%."_s).arg(b));
    } else if (what == u"apps") {
        QString error;
        const bool can = ctx.system->windowsAvailable();
        if (can)
            ctx.system->windows(&error);
        problem = !can || !error.isEmpty();
        lines.append(!problem ? tr2(l, u"Окна видны через KWin: могу переключаться между ними и закрывать их."_s, u"Windows are visible through KWin: I can switch to and close them."_s)
                              : tr2(l, u"Окна не видны (%1): приложения запускаю, но проверить окно и закрыть его не могу."_s,
                                    u"Windows are not visible (%1): I start applications but cannot check or close their windows."_s)
                                    .arg(can ? error : tr2(l, u"нужна служба Jarvis и KWin"_s, u"needs the Jarvis service and KWin"_s)));
    } else if (what == u"network") {
        return systemStatus(Step{u"system.status"_s, {{u"what"_s, u"network"_s}}}, ctx);
    } else if (ctx.diagnose) {
        const QString text = ctx.diagnose(what);
        return result(Outcome::Verified, text);
    }
    StepResult r = result(Outcome::Verified, lines.join(u'\n'));
    r.data = {{u"problem"_s, problem}};
    return r;
}

StepResult media(const Step &s, ToolContext &ctx)
{
    const Lang l = ctx.lang;
    const QString action = s.args.value(u"action"_s).toString();
    if (QString e = ctx.system->media(action); !e.isEmpty())
        return result(Outcome::Failed, e.contains(u"no media player"_s) ? tr2(l, u"Сейчас ничего не играет и проигрывателей не запущено."_s, u"Nothing plays and no player is running."_s)
                                                                       : e);
    return result(Outcome::Unconfirmed, tr2(l, u"Команда отправлена проигрывателю."_s, u"Sent to the player."_s));
}

} // namespace

// ---------------------------------------------------------------------------

QString outcomeName(Outcome o)
{
    switch (o) {
    case Outcome::Verified: return u"verified"_s;
    case Outcome::Partial: return u"partial"_s;
    case Outcome::NeedsUser: return u"needs-user"_s;
    case Outcome::Failed: return u"failed"_s;
    case Outcome::Cancelled: return u"cancelled"_s;
    case Outcome::Unconfirmed: return u"unconfirmed"_s;
    case Outcome::Skipped: return u"skipped"_s;
    }
    return {};
}

bool succeeded(Outcome o) { return o == Outcome::Verified || o == Outcome::Unconfirmed; }

Step Step::fromJson(const QJsonObject &o)
{
    return Step{o.value(u"tool"_s).toString().left(40), o.value(u"args"_s).toObject()};
}

const QList<ToolSpec> &tools()
{
    static const QStringList pages = [] {
        QStringList p{u"sound"_s, u"network"_s, u"display"_s, u"power"_s, u"bluetooth"_s, u"keyboard"_s, u"shortcuts"_s, u"notifications"_s};
        return p;
    }();
    static const QList<ToolSpec> list{
        {u"audio.output"_s, u"audio"_s, Risk::Reversible,
         u"Where sound plays: headphones (switches Bluetooth on and connects known headphones itself), the laptop speakers, or a named device"_s,
         {{u"target"_s, oneOfStrings({u"headphones"_s, u"speakers"_s, u"device"_s})}, {u"name"_s, str()}},
         {u"target"_s}, {}, 45000, u"the default output is read back"_s, u"aborts a Bluetooth connection in progress"_s},
        {u"audio.volume"_s, u"audio"_s, Risk::Reversible, u"Volume of the current output: an exact level in percent or a change (+10 louder, -10 quieter)"_s,
         {{u"level"_s, integer(0, 150)}, {u"change"_s, integer(-100, 100)}}, {}, {u"level"_s, u"change"_s}, 5000,
         u"the volume is read back"_s, u"nothing to stop: it is instant"_s},
        {u"audio.mute"_s, u"audio"_s, Risk::Reversible, u"Mute or unmute the current output"_s, {{u"mute"_s, boolean()}}, {u"mute"_s}, {}, 5000,
         u"the mute state is read back"_s, u"instant"_s},
        {u"audio.input"_s, u"audio"_s, Risk::Reversible, u"Choose the microphone: built-in, the headset's, or by name"_s,
         {{u"target"_s, oneOfStrings({u"builtin"_s, u"headset"_s})}, {u"name"_s, str()}}, {}, {u"target"_s, u"name"_s}, 5000,
         u"the default input is read back"_s, u"instant"_s},
        {u"audio.list"_s, u"info"_s, Risk::Read, u"List sound outputs and microphones"_s, {}, {}, {}, 5000, u"read only"_s, u"read only"_s},
        {u"bluetooth.power"_s, u"bluetooth"_s, Risk::Reversible, u"Switch the Bluetooth adapter on or off"_s, {{u"on"_s, boolean()}}, {u"on"_s}, {},
         8000, u"the adapter's state is read back"_s, u"instant"_s},
        {u"bluetooth.connect"_s, u"bluetooth"_s, Risk::Reversible, u"Connect a known (paired) Bluetooth device"_s, {{u"name"_s, str()}}, {}, {}, 30000,
         u"BlueZ reports the device connected"_s, u"aborts the connection attempt"_s},
        {u"bluetooth.disconnect"_s, u"bluetooth"_s, Risk::Reversible, u"Disconnect a Bluetooth device"_s, {{u"name"_s, str()}}, {}, {}, 12000,
         u"BlueZ reports the device disconnected"_s, u"instant"_s},
        {u"bluetooth.status"_s, u"info"_s, Risk::Read, u"Bluetooth adapter state and known devices"_s, {}, {}, {}, 5000, u"read only"_s, u"read only"_s},
        {u"bluetooth.pair"_s, u"bluetooth"_s, Risk::Read, u"Help pair new headphones: opens KDE's Bluetooth wizard and explains what the user does"_s, {}, {}, {},
         8000, u"needs the user"_s, u"nothing runs in the background"_s},
        {u"app.open"_s, u"apps"_s, Risk::Reversible, u"Open an installed application, or switch to its window if it is already open"_s,
         {{u"query"_s, str()}}, {u"query"_s}, {}, 20000, u"its window appears or becomes active (KWin)"_s,
         u"stops waiting; an application already starting keeps starting"_s},
        {u"app.focus"_s, u"apps"_s, Risk::Reversible, u"Switch to the window of an open application"_s, {{u"query"_s, str()}}, {u"query"_s}, {}, 6000,
         u"the window becomes active"_s, u"instant"_s},
        {u"app.close"_s, u"apps.close"_s, Risk::Confirm, u"Ask an application's windows to close, as their close button does (it may ask to save)"_s,
         {{u"query"_s, str()}}, {u"query"_s}, {}, 8000, u"its windows are gone"_s, u"cannot be undone once the application closed"_s},
        {u"screen.brightness"_s, u"screen"_s, Risk::Reversible, u"Screen brightness: a level in percent or a change"_s,
         {{u"level"_s, integer(1, 100)}, {u"change"_s, integer(-100, 100)}}, {}, {u"level"_s, u"change"_s}, 5000, u"the brightness is read back"_s,
         u"instant"_s},
        {u"notifications.dnd"_s, u"screen"_s, Risk::Reversible, u"Do not disturb: silence notification popups"_s, {{u"on"_s, boolean()}}, {u"on"_s}, {},
         5000, u"the notification service reports it"_s, u"instant"_s},
        {u"settings.open"_s, u"desktop"_s, Risk::Read, u"Open a System Settings page"_s, {{u"page"_s, oneOfStrings(pages)}}, {u"page"_s}, {}, 10000,
         u"its window appears"_s, u"instant"_s},
        {u"workspace.switch"_s, u"desktop"_s, Risk::Reversible, u"Switch the Jarvis workspace profile"_s,
         {{u"profile"_s, oneOfStrings({u"general"_s, u"study"_s, u"work"_s, u"dev"_s})}}, {u"profile"_s}, {}, 8000, u"the profile is read back"_s,
         u"instant"_s},
        {u"power.profile"_s, u"power"_s, Risk::Reversible, u"Switch the power profile"_s,
         {{u"profile"_s, oneOfStrings({u"power-saver"_s, u"balanced"_s, u"performance"_s})}}, {u"profile"_s}, {}, 6000,
         u"the profile is read back"_s, u"instant"_s},
        {u"system.status"_s, u"info"_s, Risk::Read, u"Battery charge, power profile, network"_s,
         {{u"what"_s, oneOfStrings({u"battery"_s, u"power"_s, u"network"_s, u"all"_s})}}, {u"what"_s}, {}, 5000, u"read only"_s, u"read only"_s},
        {u"system.diagnose"_s, u"info"_s, Risk::Read, u"Explain why something does not work and what to do"_s,
         {{u"what"_s, oneOfStrings({u"audio"_s, u"bluetooth"_s, u"brightness"_s, u"apps"_s, u"network"_s, u"model"_s, u"voice"_s})}}, {u"what"_s}, {},
         8000, u"read only"_s, u"read only"_s},
        {u"media.control"_s, u"desktop"_s, Risk::Reversible, u"Control the media player"_s,
         {{u"action"_s, oneOfStrings({u"play-pause"_s, u"play"_s, u"pause"_s, u"next"_s, u"previous"_s})}}, {u"action"_s}, {}, 5000,
         u"sent to the player over MPRIS"_s, u"instant"_s},
    };
    return list;
}

const ToolSpec *findTool(const QString &name)
{
    for (const ToolSpec &t : tools())
        if (t.name == name)
            return &t;
    return nullptr;
}

QStringList categories()
{
    return {u"audio"_s, u"bluetooth"_s, u"apps"_s, u"apps.close"_s, u"screen"_s, u"desktop"_s, u"power"_s, u"info"_s};
}

QString defaultPermission(const QString &category)
{
    return category == u"apps.close" ? u"ask"_s : u"allow"_s;
}

QString validate(const Step &step)
{
    const ToolSpec *spec = findTool(step.tool);
    if (!spec)
        return u"unknown tool: "_s + step.tool.left(40);
    static const QRegularExpression control(u"[\\x00-\\x1f\\x7f]"_s);
    for (auto it = step.args.begin(); it != step.args.end(); ++it) {
        if (!spec->args.contains(it.key()))
            return u"%1: unknown argument %2"_s.arg(step.tool, it.key().left(40));
        const QJsonObject schema = spec->args.value(it.key()).toObject();
        const QString type = schema.value(u"type"_s).toString();
        const QJsonValue v = it.value();
        if (type == u"string") {
            if (!v.isString())
                return u"%1.%2: a string is expected"_s.arg(step.tool, it.key());
            const QString s = v.toString();
            if (s.size() > schema.value(u"maxLength"_s).toInt(60) || s.contains(control))
                return u"%1.%2: too long or has control characters"_s.arg(step.tool, it.key());
            const QJsonArray values = schema.value(u"enum"_s).toArray();
            if (!values.isEmpty() && !values.contains(s))
                return u"%1.%2: %3 is not one of the allowed values"_s.arg(step.tool, it.key(), s.left(40));
        } else if (type == u"integer") {
            if (!v.isDouble() || std::floor(v.toDouble()) != v.toDouble())
                return u"%1.%2: an integer is expected"_s.arg(step.tool, it.key());
            const double x = v.toDouble();
            if (x < schema.value(u"minimum"_s).toDouble() || x > schema.value(u"maximum"_s).toDouble())
                return u"%1.%2: out of range"_s.arg(step.tool, it.key());
        } else if (type == u"boolean") {
            if (!v.isBool())
                return u"%1.%2: true or false is expected"_s.arg(step.tool, it.key());
        }
    }
    for (const QString &r : spec->required)
        if (!step.args.contains(r))
            return u"%1: %2 is required"_s.arg(step.tool, r);
    if (!spec->oneOf.isEmpty()) {
        int given = 0;
        for (const QString &k : spec->oneOf)
            given += step.args.contains(k) ? 1 : 0;
        if (given != 1)
            return u"%1: exactly one of %2"_s.arg(step.tool, spec->oneOf.join(u", "_s));
    }
    if (step.tool == u"audio.output" && step.args.value(u"target"_s).toString() == u"device" && step.args.value(u"name"_s).toString().isEmpty())
        return u"audio.output: a device needs its name"_s;
    return {};
}

QJsonObject planSchema()
{
    QJsonArray variants;
    for (const ToolSpec &t : tools()) {
        QJsonObject args{{u"type"_s, u"object"_s}, {u"properties"_s, t.args}, {u"additionalProperties"_s, false}};
        if (!t.required.isEmpty())
            args[u"required"_s] = QJsonArray::fromStringList(t.required);
        variants.append(QJsonObject{{u"type"_s, u"object"_s},
                                    {u"properties"_s, QJsonObject{{u"tool"_s, QJsonObject{{u"const"_s, t.name}}}, {u"args"_s, args}}},
                                    {u"required"_s, QJsonArray{u"tool"_s, u"args"_s}},
                                    {u"additionalProperties"_s, false}});
    }
    return QJsonObject{
        {u"type"_s, u"object"_s},
        {u"properties"_s,
         QJsonObject{{u"steps"_s, QJsonObject{{u"type"_s, u"array"_s}, {u"maxItems"_s, 8}, {u"items"_s, QJsonObject{{u"anyOf"_s, variants}}}}},
                     {u"clarify"_s, QJsonObject{{u"type"_s, u"string"_s}, {u"maxLength"_s, 300}}},
                     {u"say"_s, QJsonObject{{u"type"_s, u"string"_s}, {u"maxLength"_s, 1500}}}}},
        {u"required"_s, QJsonArray{u"steps"_s, u"clarify"_s, u"say"_s}},
        {u"additionalProperties"_s, false}};
}

QString toolCatalogue()
{
    QStringList lines;
    for (const ToolSpec &t : tools()) {
        QStringList args;
        for (auto it = t.args.begin(); it != t.args.end(); ++it) {
            const QJsonObject s = it.value().toObject();
            QString type = s.value(u"type"_s).toString();
            if (s.contains(u"enum"_s)) {
                QStringList values;
                for (const QJsonValue &v : s.value(u"enum"_s).toArray())
                    values.append(u'"' + v.toString() + u'"');
                type = values.join(u'|');
            } else if (type == u"integer") {
                type = u"%1..%2"_s.arg(s.value(u"minimum"_s).toInt()).arg(s.value(u"maximum"_s).toInt());
            }
            args.append(it.key() + (t.required.contains(it.key()) ? QString() : u"?"_s) + u": "_s + type);
        }
        lines.append(u"%1 {%2} — %3"_s.arg(t.name, args.join(u", "_s), t.purpose));
    }
    return lines.join(u'\n');
}

QString genericAppKind(const QString &words)
{
    const QString t = u' ' + norm(words) + u' ';
    auto any = [&](std::initializer_list<const char16_t *> stems) {
        for (const char16_t *s : stems)
            if (t.contains(u' ' + QString::fromUtf16(s)))
                return true;
        return false;
    };
    if (any({u"браузер", u"интернет", u"сайт", u"веб", u"browser", u"web browser", u"website", u"site"}))
        return u"browser"_s;
    if (any({u"проигрыват", u"плеер", u"музык", u"player", u"music"}))
        return u"player"_s;
    if (any({u"видеоплеер", u"видео", u"фильм", u"video"}))
        return u"video"_s;
    if (any({u"терминал", u"консол", u"terminal", u"console"}))
        return u"terminal"_s;
    if (any({u"файлов", u"проводник", u"файлы ", u"file manager", u"files "}))
        return u"files"_s;
    if (any({u"почт", u"mail", u"email"}))
        return u"mail"_s;
    if (any({u"текстовый редактор", u"блокнот", u"редактор текст", u"text editor", u"notepad"}))
        return u"editor"_s;
    if (any({u"калькулятор", u"calculator", u"посчитать"}))
        return u"calculator"_s;
    if (any({u"просмотр картинок", u"просмотр фото", u"фотографи", u"картинк", u"image viewer", u"photos"}))
        return u"images"_s;
    return {};
}

StepResult runTool(const Step &s, ToolContext &ctx)
{
    if (ctx.cancelled())
        return cancelled(ctx.lang);
    const QString &n = s.tool;
    if (n == u"audio.output") return audioOutput(s, ctx);
    if (n == u"audio.volume") return audioVolume(s, ctx);
    if (n == u"audio.mute") return audioMute(s, ctx);
    if (n == u"audio.input") return audioInput(s, ctx);
    if (n == u"audio.list") return audioList(ctx);
    if (n == u"bluetooth.power") return btPower(s, ctx);
    if (n == u"bluetooth.connect") return btConnect(s, ctx);
    if (n == u"bluetooth.disconnect") return btDisconnect(s, ctx);
    if (n == u"bluetooth.status") return btStatus(ctx);
    if (n == u"bluetooth.pair") return btPair(ctx);
    if (n == u"app.open") return appOpen(s, ctx, false);
    if (n == u"app.focus") return appOpen(s, ctx, true);
    if (n == u"app.close") return appClose(s, ctx);
    if (n == u"screen.brightness") return brightness(s, ctx);
    if (n == u"notifications.dnd") return dnd(s, ctx);
    if (n == u"settings.open") return settingsOpen(s, ctx);
    if (n == u"workspace.switch") return workspaceSwitch(s, ctx);
    if (n == u"power.profile") return powerProfile(s, ctx);
    if (n == u"system.status") return systemStatus(s, ctx);
    if (n == u"system.diagnose") return diagnose(s, ctx);
    if (n == u"media.control") return media(s, ctx);
    return result(Outcome::Failed, u"unknown tool"_s);
}

} // namespace jarvis::agent
