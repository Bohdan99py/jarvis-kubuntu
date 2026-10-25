// Loads the views of the Jarvis desktop widgets (plasmoids/*/contents/ui) and
// the desktop core (module org.jarvis.visual, QML_IMPORT_PATH=build/qml)
// outside Plasma, with the data jarvisd reports (real shapes, online,
// offline, without data), and fails on any QML warning or error. With an
// output directory it also saves a picture of each case.
//   plasmoid_check <plasmoids directory> [output directory]
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTimer>
#include <QVariant>

#include <cstdio>
#include <memory>

namespace {

QStringList g_problems;

void collect(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    std::fprintf(stderr, "%s\n", qPrintable(message));
    // KWindowSystem (under Plasma's tool tips) has no plugin for the offscreen platform:
    // a fact of this test's environment, not of the widget.
    if (message == QLatin1String("Could not find any platform plugin"))
        return;
    if (type != QtDebugMsg && type != QtInfoMsg)
        g_problems << message;
}

} // namespace

// What Plasma's KLocalizedContext gives the widgets: i18nd() with arguments.
class Localized : public QObject
{
    Q_OBJECT
public:
    static QString fill(QString text, const QVariantList &args)
    {
        for (int i = 0; i < args.size(); ++i)
            text.replace(QStringLiteral("%%1").arg(i + 1), args.at(i).toString());
        return text;
    }
    Q_INVOKABLE QString i18nd(const QString &, const QString &t) const { return t; }
    Q_INVOKABLE QString i18nd(const QString &, const QString &t, const QVariant &a) const { return fill(t, {a}); }
    Q_INVOKABLE QString i18nd(const QString &, const QString &t, const QVariant &a, const QVariant &b) const { return fill(t, {a, b}); }
    Q_INVOKABLE QString i18nd(const QString &, const QString &t, const QVariant &a, const QVariant &b, const QVariant &c) const { return fill(t, {a, b, c}); }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc < 2) {
        std::fprintf(stderr, "usage: plasmoid_check <plasmoids directory> [output directory]\n");
        return 2;
    }
    const QString root = QDir(QString::fromLocal8Bit(argv[1])).absolutePath();
    const QString out = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
    qInstallMessageHandler(collect);

    const QString now = QString::number(QDateTime::currentMSecsSinceEpoch() - 4 * 60000);
    // Shapes as jarvisd sends them (taken from a real machine).
    // (0.19.3: media lists the players; the status carries the wallpaper's brightness.)
    const QString telemetry = QStringLiteral(R"({"cpu":{"cores":12,"history":[6,5.6,14,9,22,18,7],"load":2.56,"mhz":[2395,2394,2392,416],"mhzAverage":2050,"mhzMax":2396,"perCore":[5.6,9.6,7.7,0.8,12,5.3,8.5,1.2,92,3.6,4.8,0.4],"usage":17.2},
        "gpu":{"cards":[{"asleep":false,"boot":false,"clockMhz":210,"driver":"nvidia","load":3,"memory":0.7,"name":"NVIDIA GeForce RTX 3050 6GB Laptop GPU","temperature":35,"vramTotalMb":6144,"vramUsedMb":44},
                        {"asleep":false,"boot":true,"clockMhz":533,"driver":"amdgpu","load":66,"memory":90.9,"name":"AMD Radeon 680M","temperature":38,"vramTotalMb":512,"vramUsedMb":465}]},
        "memory":{"percent":46,"swapTotalGb":0.5,"swapUsedGb":0,"totalGb":14.9,"usedGb":6.9},
        "thermal":{"fans":[],"sensors":[{"celsius":43,"chip":"acpitz_0","label":"temp1"},{"celsius":43.1,"chip":"k10temp","label":"Tctl"},{"celsius":38,"chip":"amdgpu","label":"edge"}]},
        "net":{"interface":"wlp3s0","manager":true,"name":"Livebox-8750","rxBytesPerSecond":215500,"rxHistory":[172,2155,90000,215500,30000],"txBytesPerSecond":2816,"txHistory":[172,2816,1200,900,3000],"type":"wifi","vpn":true,"vpnName":"Work VPN"},
        "power":{"charging":false,"minutesLeft":134,"onAc":false,"percent":64,"powerSaver":false,"present":true,"profile":"balanced","status":"Discharging","watts":9.8},
        "media":{"available":true,"player":"firefox","status":"Playing","title":"Interstellar Main Theme","artist":"Hans Zimmer","album":"Interstellar","artUrl":"","volume":-1,"canNext":true,"canPrevious":true,"canPlay":true,"players":["firefox","elisa"],
                 "audio":{"output":"Built-in Audio Analog Stereo","volume":42,"muted":false}}})");
    const QString status = QStringLiteral(R"({"version":"0.19.2","state":"processing","integration":true,"claude":true,"model":"claude-haiku-4-5-20251001","webSearch":true,
        "awareness":{"level":"assistive","paused":false,"dnd":false},"voice":{"listening":true,"speaking":false,"engine":"piper","at":1},
        "activity":{"requests":12,"errors":1,"routes":["recall","llm"],"latency":1840},"workspace":{"current":"dev","previous":"general","profiles":[]},
        "desktop":{"mode":"jarvis","theme":"classic","quality":"balanced","effects":true},"task":{"text":"Call the workshop","due":%1},
        "events":[{"at":%1,"kind":"route","text":"llm"},{"at":%1,"kind":"workspace","text":"DEV"},{"at":%1,"kind":"notice","text":"Break time"}]})").arg(now);

    struct Case
    {
        QString name;
        QString widget;  // plasmoid id; "" = the visual module only
        QString qml;     // the object to create (its size and properties)
        bool plasma = false;  // needs Plasma's own QML modules (Kirigami, org.kde.plasma.core)
    };
    const auto view = [&](const QString &type, const QString &props) { return type + QStringLiteral(" { width: 360; height: 520; %1 }").arg(props); };
    const QString live = QStringLiteral("online: true; telemetry: JSON.parse(%1); status: JSON.parse(%2)").arg(QStringLiteral("'") + QString(telemetry).replace(u'\n', u' ') + u'\'', u'\'' + QString(status).replace(u'\n', u' ') + u'\'');
    const QString empty = QStringLiteral("online: false");
    const QList<Case> cases{
        {QStringLiteral("system"), QStringLiteral("org.jarvis.system"), view(QStringLiteral("SystemView"), live)},
        {QStringLiteral("system-offline"), QStringLiteral("org.jarvis.system"), view(QStringLiteral("SystemView"), empty)},
        {QStringLiteral("network"), QStringLiteral("org.jarvis.network"), view(QStringLiteral("NetworkView"), live)},
        {QStringLiteral("network-offline"), QStringLiteral("org.jarvis.network"), view(QStringLiteral("NetworkView"), empty)},
        {QStringLiteral("power"), QStringLiteral("org.jarvis.power"), view(QStringLiteral("PowerView"), live)},
        {QStringLiteral("power-offline"), QStringLiteral("org.jarvis.power"), view(QStringLiteral("PowerView"), empty)},
        {QStringLiteral("media"), QStringLiteral("org.jarvis.media"), view(QStringLiteral("MediaView"), live)},
        {QStringLiteral("media-offline"), QStringLiteral("org.jarvis.media"), view(QStringLiteral("MediaView"), empty)},
        {QStringLiteral("status"), QStringLiteral("org.jarvis.status"), view(QStringLiteral("StatusView"), live)},
        {QStringLiteral("status-offline"), QStringLiteral("org.jarvis.status"), view(QStringLiteral("StatusView"), empty)},
        {QStringLiteral("system-compact"), QStringLiteral("org.jarvis.system"), QStringLiteral("SystemViewCompact { %1 }").arg(live)},
        {QStringLiteral("status-compact"), QStringLiteral("org.jarvis.status"), QStringLiteral("StatusViewCompact { %1 }").arg(live)},
        {QStringLiteral("core"), QString(), QStringLiteral("JV.DesktopCore { width: 420; height: 420; online: true; status: JSON.parse(%1); metrics: ({cpu: 37, memory: 61}) }")
                                                .arg(u'\'' + QString(status).replace(u'\n', u' ') + u'\'')},
        {QStringLiteral("core-offline"), QString(), QStringLiteral("JV.DesktopCore { width: 300; height: 300; online: false; quality: \"low\" }")},
        {QStringLiteral("core-mark7"), QString(), QStringLiteral("JV.DesktopCore { width: 420; height: 420; online: true; quality: \"cinematic\"; Component.onCompleted: JV.JarvisTheme.preset = \"mark7\" }")},
        {QStringLiteral("core-minimal"), QString(), QStringLiteral("JV.DesktopCore { width: 300; height: 300; online: true; Component.onCompleted: JV.JarvisTheme.preset = \"minimal\" }")},
        // 0.19.3: a light wallpaper (darker panels, plates under the core's readouts, the shade).
        {QStringLiteral("core-light"), QString(), QStringLiteral("Item { width: 420; height: 420; JV.CoreBackdrop { anchors.fill: parent; strength: 0.6 } JV.DesktopCore { anchors.fill: parent; online: true; status: JSON.parse(%1) }"
                                                               " Component.onCompleted: JV.JarvisTheme.lightBackdrop = true }")
                                                    .arg(u'\'' + QString(status).replace(u'\n', u' ') + u'\'')},
        {QStringLiteral("status-light"), QStringLiteral("org.jarvis.status"), view(QStringLiteral("StatusView"), live + QStringLiteral("; Component.onCompleted: JV.JarvisTheme.lightBackdrop = true"))},
        // The dock's application button in its states (a Plasma panel draws it; here alone).
        {QStringLiteral("dock-task"), QStringLiteral("org.jarvis.dock"),
         QStringLiteral("Row { spacing: 4; DockTask { size: 44; name: \"Konsole\"; icon: \"utilities-terminal\"; launcher: true }"
                        " DockTask { size: 44; name: \"Dolphin\"; icon: \"system-file-manager\"; active: true; windows: 3; zoom: 1.3 }"
                        " DockTask { size: 44; name: \"Firefox\"; icon: \"firefox\"; attention: true; windows: 1; edge: Qt.TopEdge }"
                        " DockTask { size: 44; name: \"Kate\"; icon: \"kate\"; minimized: true; windows: 1; edge: Qt.LeftEdge; effects: false } }"),
         true},
    };

    QQmlEngine engine;
    Localized localized;
    engine.rootContext()->setContextObject(&localized);
    int failures = 0, skipped = 0;
    // Plasma's own QML modules: on a machine without Plasma 6 (the CI's Ubuntu images) the views
    // that use them cannot load at all — they are left out there, and said so; with Plasma they run.
    QQmlComponent probe(&engine);
    probe.setData("import QtQuick\nimport org.kde.kirigami\nimport org.kde.plasma.core\nItem {}\n", QUrl());
    const bool havePlasma = probe.status() == QQmlComponent::Ready;
    // The core is shaders: this checks that it loads and runs without a warning; an offscreen
    // grab may not show the shaders themselves (look at it in a Plasma session for that).
    for (const Case &c : cases) {
        if (c.plasma && !havePlasma) {
            std::printf("skip %s: Plasma 6's QML modules (Kirigami, org.kde.plasma.core) are not installed\n", qPrintable(c.name));
            ++skipped;
            continue;
        }
        g_problems.clear();
        const QString dir = c.widget.isEmpty() ? root : root + u'/' + c.widget + QStringLiteral("/contents/ui");
        const QString qml = QStringLiteral("import QtQuick\nimport org.jarvis.visual as JV\nRectangle {\n color: \"#0b1520\"; width: inner.width + 24; height: inner.height + 24\n"
                                           " Component.onDestruction: { JV.JarvisTheme.preset = \"classic\"; JV.JarvisTheme.lightBackdrop = false }\n %1\n}\n")
                                .arg(QString(c.qml).replace(QRegularExpression(QStringLiteral("^([\\w.]+) \\{")), QStringLiteral("\\1 { id: inner; x: 12; y: 12; ")));
        QQmlComponent component(&engine);
        // The base URL puts the widget's own directory in scope (its views).
        component.setData(qml.toUtf8(), QUrl::fromLocalFile(dir + QStringLiteral("/check-%1.qml").arg(c.name)));
        std::unique_ptr<QObject> object(component.create());
        auto *item = qobject_cast<QQuickItem *>(object.get());
        if (!item) {
            std::fprintf(stderr, "FAIL %s: %s\n", qPrintable(c.name), qPrintable(component.errorString()));
            ++failures;
            continue;
        }
        QQuickWindow window;
        window.resize(int(item->width()), int(item->height()));
        item->setParentItem(window.contentItem());
        window.show();
        QEventLoop loop;
        QTimer::singleShot(1200, &loop, &QEventLoop::quit); // the panels unfold, the core runs a little
        loop.exec();
        if (!out.isEmpty())
            window.grabWindow().save(out + QStringLiteral("/%1.png").arg(c.name));
        object.reset();
        if (!g_problems.isEmpty()) {
            std::fprintf(stderr, "FAIL %s: %d QML warnings\n", qPrintable(c.name), int(g_problems.size()));
            ++failures;
        }
    }
    if (failures == 0)
        std::printf("PASS: %d cases of the desktop widgets' views and the desktop core load without warnings%s\n", int(cases.size()) - skipped,
                    skipped ? qPrintable(QStringLiteral(" (%1 skipped: no Plasma 6)").arg(skipped)) : "");
    return failures == 0 ? 0 : 1;
}

#include "plasmoid_check.moc"
