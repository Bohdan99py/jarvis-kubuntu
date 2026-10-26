#include "app_settings.h"
#include "agent_tools.h"
#include "desktop_integration.h"
#include <QJsonValue>

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>

#include <algorithm>

#include "config.h"
#include "language.h"
#include "secret_store.h"
#include "settings_catalog.h"

AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
{
    refresh(false);
    refreshKey();
    // Someone else wrote the file (the service, `jarvis --desktop-mode`, the
    // dock's settings through DesktopShell): show what is saved now.
    m_reread.setSingleShot(true);
    m_reread.setInterval(250);
    connect(&m_reread, &QTimer::timeout, this, [this] {
        watch();
        refresh(true);
    });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_reread, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_reread, qOverload<>(&QTimer::start));
    watch();
}

void AppSettings::watch()
{
    // The file is replaced on every save (QSaveFile renames): watch the folder
    // too, and the file again once it is back.
    const QString path = jarvis::Config::filePath();
    const QString dir = QFileInfo(path).absolutePath();
    QDir().mkpath(dir);
    if (!m_watcher.directories().contains(dir))
        m_watcher.addPath(dir);
    if (QFileInfo::exists(path) && !m_watcher.files().contains(path))
        m_watcher.addPath(path);
}

QString AppSettings::configPath() const
{
    return jarvis::Config::filePath();
}

QString AppSettings::backupPath() const
{
    return jarvis::Config::backupPath();
}

void AppSettings::refresh(bool notify)
{
    const jarvis::ConfigData d = jarvis::Config::loadFileOnly();
    m_model = d.model;
    m_data = d;
    if (notify)
        emit changed();
}

void AppSettings::refreshKey()
{
    jarvis::Config::KeyStore where = jarvis::Config::KeyStore::None;
    const QString key = jarvis::Config::anthropicKey(&where);
    m_hasKey = !key.isEmpty();
    m_keyHint = jarvis::Config::keyHint(key);
    static const char *names[] = {"none", "environment", "keyring", "locked", "file"};
    m_keyStore = QString::fromLatin1(names[int(where)]);
    m_keyringAvailable = jarvis::SecretStore::available();
}

QString AppSettings::save(const QString &apiKey, const QString &model)
{
    // Both are checked before anything is saved: an invalid value keeps the old one.
    const QString key = apiKey.trimmed();
    static const QRegularExpression keyShape(QStringLiteral("^[A-Za-z0-9_-]{20,400}$"));
    if (!key.isEmpty() && !keyShape.match(key).hasMatch())
        return tr("This does not look like a Claude API key: it starts with sk-ant- and has only Latin letters, digits, \"-\" and \"_\", without spaces.");
    const QString m = model.trimmed().isEmpty() ? jarvis::Config::defaultModel() : model.trimmed();
    static const QRegularExpression modelShape(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._:@-]{1,99}$"));
    if (!modelShape.match(m).hasMatch())
        return tr("The model name has letters, digits, dots and dashes only, for example claude-haiku-4-5-20251001.");

    if (!key.isEmpty()) {
        const QString error = jarvis::Config::setAnthropicKey(key);
        if (!error.isEmpty())
            return tr("The key was not saved: %1").arg(error);
    }
    jarvis::ConfigData d = jarvis::Config::loadFileOnly();
    d.model = m;
    QString error;
    if (!jarvis::Config::save(d, &error))
        return error;

    refreshKey();
    refresh(true);
    emit saved();
    return {};
}

QString AppSettings::removeKey()
{
    const QString error = jarvis::Config::setAnthropicKey({});
    refreshKey();
    refresh(true);
    if (!error.isEmpty())
        return error;
    emit saved();
    return {};
}

QString AppSettings::unlockKeyring()
{
    QString error;
    jarvis::SecretStore::unlock(QLatin1String(jarvis::SecretStore::kAnthropic), &error);
    refreshKey();
    emit changed();
    if (error.isEmpty() && m_hasKey)
        emit saved(); // the service can read the key now
    return error;
}

void AppSettings::setConnection(const QString &state, const QString &message)
{
    m_connectionState = state;
    m_connectionMessage = message;
    emit connectionChanged();
}

void AppSettings::testConnection(const QString &apiKey, const QString &model)
{
    if (m_check)
        return;
    // "Only local": not even a check goes to Anthropic.
    if (m_data.agentMode == QLatin1String("local")) {
        setConnection(QStringLiteral("warn"), tr("\"Only local\" mode is on (Settings → Autonomy): Jarvis does not contact Anthropic, not even to check the key."));
        return;
    }
    const QString key = apiKey.trimmed().isEmpty() ? jarvis::Config::anthropicKey() : apiKey.trimmed();
    const QString m = model.trimmed().isEmpty() ? m_model : model.trimmed();
    if (key.isEmpty()) {
        setConnection(QStringLiteral("error"), tr("Enter a key first: there is none to check."));
        return;
    }
    if (!m_network)
        m_network = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl(QStringLiteral("https://api.anthropic.com/v1/models/") + QString::fromLatin1(QUrl::toPercentEncoding(m))));
    request.setRawHeader("x-api-key", key.toUtf8());
    request.setRawHeader("anthropic-version", "2023-06-01");
    request.setTransferTimeout(15000);
    setConnection(QStringLiteral("checking"), tr("Asking Anthropic…"));
    m_check = m_network->get(request);
    connect(m_check, &QNetworkReply::finished, this, [this, reply = m_check.data(), m] {
        reply->deleteLater();
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // Never the reply's own text: it may quote the request.
        if (code == 200)
            setConnection(QStringLiteral("ok"), tr("Connected: the key works and the model %1 is available.").arg(m));
        else if (code == 401)
            setConnection(QStringLiteral("error"), tr("Anthropic rejected the key (401). Check that it was copied completely, or create a new one in the Claude Console."));
        else if (code == 403)
            setConnection(QStringLiteral("error"), tr("The key has no access to the API or to this model (403)."));
        else if (code == 404)
            setConnection(QStringLiteral("error"), tr("The model %1 was not found (404). Check its name.").arg(m));
        else if (code == 429)
            setConnection(QStringLiteral("warn"), tr("The key works, but there are too many requests right now (429). Try again in a minute."));
        else if (code >= 500)
            setConnection(QStringLiteral("warn"), tr("Anthropic is overloaded or unavailable (%1). Try again later.").arg(code));
        else if (reply->error() == QNetworkReply::OperationCanceledError || reply->error() == QNetworkReply::TimeoutError)
            setConnection(QStringLiteral("error"), tr("Anthropic did not answer within 15 seconds. Check the internet connection."));
        else if (code == 0)
            setConnection(QStringLiteral("error"), tr("api.anthropic.com cannot be reached: no internet connection, a proxy or a firewall."));
        else
            setConnection(QStringLiteral("error"), tr("Unexpected answer from Anthropic (%1).").arg(code));
    });
}

QStringList AppSettings::resetPreview(const QString &section) const
{
    const jarvis::ConfigData now = jarvis::Config::loadFileOnly();
    QStringList out = jarvis::settings::titlesForKeys(jarvis::Config::changedKeys(now, jarvis::Config::resetSection(now, section)));
    if (section == QLatin1String("desktop") || section == QLatin1String("all")) {
        if (hudQuality() != QLatin1String("balanced"))
            out << jarvis::settings::entry(QStringLiteral("window.hud")).value(QStringLiteral("title")).toString();
        if (overlay())
            out << jarvis::settings::entry(QStringLiteral("chat.overlay")).value(QStringLiteral("title")).toString();
    }
    return out;
}

QString AppSettings::resetSection(const QString &section)
{
    if (!jarvis::Config::sections().contains(section))
        return tr("Unknown option.");
    if (section == QLatin1String("desktop") || section == QLatin1String("all")) {
        setHudQuality(QStringLiteral("balanced"));
        setOverlay(false);
    }
    const jarvis::ConfigData now = jarvis::Config::loadFileOnly();
    const jarvis::ConfigData next = jarvis::Config::resetSection(now, section);
    const QString error = store(next);
    if (error.isEmpty() && next.language != now.language) {
        jarvis::applyUiLanguage(next.language);
        if (QQmlEngine *engine = qmlEngine(this))
            engine->retranslate();
        emit languageApplied();
    }
    return error;
}

QString AppSettings::uiLanguage() const
{
    return jarvis::languageCode(jarvis::resolveLanguage(m_data.language));
}

QString AppSettings::store(const jarvis::ConfigData &data)
{
    QString error;
    if (!jarvis::Config::save(data, &error))
        return error;
    refresh(true);
    emit saved();
    return {};
}

QString AppSettings::setLanguage(const QString &language)
{
    jarvis::ConfigData d = jarvis::Config::loadFileOnly();
    d.language = (language == QLatin1String("ru") || language == QLatin1String("en")) ? language : QStringLiteral("auto");
    const QString error = store(d);
    if (!error.isEmpty())
        return error;
    jarvis::applyUiLanguage(d.language);
    if (QQmlEngine *engine = qmlEngine(this))
        engine->retranslate();
    emit languageApplied();
    return {};
}

QString AppSettings::setReplyLanguage(const QString &language)
{
    jarvis::ConfigData d = jarvis::Config::loadFileOnly();
    d.replyLanguage = (language == QLatin1String("ru") || language == QLatin1String("en")) ? language : QStringLiteral("auto");
    return store(d);
}

QString AppSettings::setOption(const QString &name, bool value)
{
    jarvis::ConfigData d = jarvis::Config::loadFileOnly();
    if (name == QLatin1String("learnDialog"))
        d.learnDialog = value;
    else if (name == QLatin1String("curiosity"))
        d.curiosity = value;
    else if (name == QLatin1String("trackActivity"))
        d.trackActivity = value;
    else if (name == QLatin1String("trackTitles"))
        d.trackTitles = value;
    else if (name == QLatin1String("shareActivity"))
        d.shareActivity = value;
    else if (name == QLatin1String("proactive"))
        d.proactive = value;
    else if (name == QLatin1String("morningSummary"))
        d.morningSummary = value;
    else if (name == QLatin1String("deepReflection"))
        d.deepReflection = value;
    else if (name == QLatin1String("notifyReplies"))
        d.notifyReplies = value;
    else if (name == QLatin1String("reminderSound"))
        d.reminderSound = value;
    else if (name == QLatin1String("awarenessWindow"))
        d.awarenessWindow = value;
    else if (name == QLatin1String("awarenessScreen")) {
        d.awarenessScreen = value;
        if (!value)
            d.awarenessCloudImages = false; // no screen, nothing to send
    } else if (name == QLatin1String("awarenessCloudImages"))
        d.awarenessCloudImages = value && d.awarenessScreen;
    else if (name == QLatin1String("awarenessFullscreen"))
        d.awarenessFullscreen = value;
    else if (name == QLatin1String("webSearch"))
        d.webSearch = value;
    else if (name == QLatin1String("background"))
        d.background = value;
    else if (name == QLatin1String("desktopIntegration")) {
        d.desktopIntegration = value;
        if (!value)
            d.shareActiveApp = false; // nothing to share through
    } else if (name == QLatin1String("shareActiveApp"))
        d.shareActiveApp = value && d.desktopIntegration;
    else
        return tr("Unknown option.");
    return store(d);
}

QVariantMap AppSettings::agentPermissions() const
{
    // Every category with its effective value, so the page shows the defaults too.
    QVariantMap out;
    for (const QString &c : jarvis::agent::categories()) {
        const QString v = m_data.agentPermissions.value(c).toString();
        out.insert(c, v.isEmpty() ? jarvis::agent::defaultPermission(c) : v);
    }
    return out;
}

QString AppSettings::setAgent(const QString &name, const QVariant &value)
{
    jarvis::ConfigData d = jarvis::Config::loadFileOnly();
    if (name == QLatin1String("mode")) {
        if (!jarvis::Config::agentModes().contains(value.toString()))
            return tr("This value is not allowed.");
        d.agentMode = value.toString();
    } else if (name == QLatin1String("cloud")) {
        d.agentCloud = value.toBool();
    } else if (name == QLatin1String("model")) {
        static const QRegularExpression id(QStringLiteral("^[a-z0-9._-]{1,40}$"));
        if (!id.match(value.toString()).hasMatch())
            return tr("This value is not allowed.");
        d.agentModel = value.toString();
    } else if (name == QLatin1String("device")) {
        d.agentDevice = value.toString() == QLatin1String("cpu") ? QStringLiteral("cpu") : QStringLiteral("auto");
    } else if (name == QLatin1String("keepMinutes")) {
        d.agentKeepMinutes = std::clamp(value.toInt(), 1, 120);
    } else if (name == QLatin1String("maxSteps")) {
        d.agentMaxSteps = std::clamp(value.toInt(), 1, 12);
    } else if (name == QLatin1String("maxSeconds")) {
        d.agentMaxSeconds = std::clamp(value.toInt(), 10, 300);
    } else if (name.startsWith(QLatin1String("permission:"))) {
        const QString category = name.mid(11);
        const QString v = value.toString();
        if (!jarvis::agent::categories().contains(category) || (v != u"allow" && v != u"ask" && v != u"off"))
            return tr("This value is not allowed.");
        if (v == jarvis::agent::defaultPermission(category))
            d.agentPermissions.remove(category);
        else
            d.agentPermissions.insert(category, v);
    } else {
        return tr("Unknown option.");
    }
    return store(d);
}

QString AppSettings::setDesktop(const QString &key, const QVariant &value)
{
    jarvis::ConfigData d = jarvis::Config::loadFileOnly();
    const QString error = jarvis::integration::setDesktopValue(d.desktop, key, QJsonValue::fromVariant(value));
    if (!error.isEmpty())
        return error == u"unknown setting" ? tr("Unknown option.") : tr("This value is not allowed.");
    return store(d);
}

QString AppSettings::setHudAnimation(const QString &level)
{
    if (!jarvis::Config::hudAnimationLevels().contains(level))
        return tr("Unknown option.");
    jarvis::ConfigData d = jarvis::Config::loadFileOnly();
    d.hudAnimation = level;
    return store(d);
}

QString AppSettings::setBreakMinutes(int minutes)
{
    jarvis::ConfigData d = jarvis::Config::loadFileOnly();
    d.breakMinutes = std::clamp(minutes, 0, 600);
    return store(d);
}

QString AppSettings::setNumber(const QString &name, int value)
{
    jarvis::ConfigData d = jarvis::Config::loadFileOnly();
    if (name == QLatin1String("snoozeMinutes"))
        d.snoozeMinutes = std::clamp(value, 1, 720);
    else if (name == QLatin1String("morningHour"))
        d.morningHour = std::clamp(value, 4, 12);
    else if (name == QLatin1String("afternoonHour"))
        d.afternoonHour = std::clamp(value, 11, 18);
    else if (name == QLatin1String("eveningHour"))
        d.eveningHour = std::clamp(value, 16, 23);
    else if (name == QLatin1String("nagMinutes"))
        d.nagMinutes = std::clamp(value, 0, 120);
    else if (name == QLatin1String("screenIntervalMinutes"))
        d.screenIntervalMinutes = std::clamp(value, 0, 60);
    else if (name == QLatin1String("suggestionCooldownMinutes"))
        d.suggestionCooldownMinutes = std::clamp(value, 10, 24 * 60);
    else if (name == QLatin1String("suggestionsPerDay"))
        d.suggestionsPerDay = std::clamp(value, 1, 20);
    else
        return tr("Unknown option.");
    return store(d);
}

QString AppSettings::setAwarenessLevel(const QString &level)
{
    if (!jarvis::Config::awarenessLevels().contains(level))
        return tr("Unknown option.");
    jarvis::ConfigData d = jarvis::Config::loadFileOnly();
    d.awarenessLevel = level;
    return store(d);
}

QString AppSettings::setList(const QString &name, const QStringList &values)
{
    QStringList clean;
    for (const QString &v : values) {
        const QString item = v.trimmed().toLower().left(120);
        if (!item.isEmpty() && !clean.contains(item))
            clean.append(item);
    }
    jarvis::ConfigData d = jarvis::Config::loadFileOnly();
    if (name == QLatin1String("awarenessMuted"))
        d.awarenessMuted = clean.mid(0, 20);
    else if (name == QLatin1String("awarenessExcluded"))
        d.awarenessExcluded = clean.mid(0, 100);
    else
        return tr("Unknown option.");
    return store(d);
}

QString AppSettings::hudQuality() const
{
    const QString q = QSettings().value(QStringLiteral("hud/quality"), QStringLiteral("balanced")).toString();
    static const QStringList known{QStringLiteral("off"), QStringLiteral("low"), QStringLiteral("balanced"), QStringLiteral("cinematic")};
    return known.contains(q) ? q : QStringLiteral("balanced");
}

void AppSettings::setHudQuality(const QString &quality)
{
    static const QStringList known{QStringLiteral("off"), QStringLiteral("low"), QStringLiteral("balanced"), QStringLiteral("cinematic")};
    if (!known.contains(quality) || quality == hudQuality())
        return;
    QSettings().setValue(QStringLiteral("hud/quality"), quality);
    emit hudChanged();
}

bool AppSettings::memoryOpen() const
{
    return QSettings().value(QStringLiteral("hud/memoryOpen"), false).toBool();
}

void AppSettings::setMemoryOpen(bool open)
{
    if (open == memoryOpen())
        return;
    QSettings().setValue(QStringLiteral("hud/memoryOpen"), open);
    emit hudChanged();
}

bool AppSettings::overlay() const
{
    return QSettings().value(QStringLiteral("hud/overlay"), false).toBool();
}

void AppSettings::setOverlay(bool on)
{
    if (on == overlay())
        return;
    QSettings().setValue(QStringLiteral("hud/overlay"), on);
    emit hudChanged();
}

QVariantList AppSettings::searchSettings(const QString &query) const
{
    return jarvis::settings::search(query);
}

QVariantMap AppSettings::settingEntry(const QString &id) const
{
    return jarvis::settings::entry(id);
}

QString AppSettings::pageTitle(const QString &page) const
{
    return jarvis::settings::pageTitle(page);
}
