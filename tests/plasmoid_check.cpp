// Loads the Plasma widget's views (plasmoid/contents/ui: HudView, MiniCore)
// outside Plasma with the states jarvisd reports, and fails on any QML warning
// or error. With an output directory it also saves a picture of each state.
//   plasmoid_check <plasmoid/contents/ui> [output directory]
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QVariant>

#include <cstdio>
#include <memory>

namespace {

QStringList g_problems;

void collect(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    std::fprintf(stderr, "%s\n", qPrintable(message));
    if (type != QtDebugMsg && type != QtInfoMsg)
        g_problems << message;
}

} // namespace

// What Plasma's KLocalizedContext gives the widget: i18n() with arguments.
class Localized : public QObject
{
    Q_OBJECT
public:
    Q_INVOKABLE QString i18n(const QString &text) const { return text; }
    Q_INVOKABLE QString i18n(const QString &text, const QVariant &arg) const { return QString(text).replace(QLatin1String("%1"), arg.toString()); }
};

struct Case
{
    const char *name;
    int width, height;
    const char *properties; // QML property assignments for HudView
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc < 2) {
        std::fprintf(stderr, "usage: plasmoid_check <plasmoid/contents/ui> [output directory]\n");
        return 2;
    }
    const QString ui = QDir(QString::fromLocal8Bit(argv[1])).absolutePath();
    const QString out = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
    qInstallMessageHandler(collect);

    const char *status = R"({"version":"0.19.1","state":"idle","integration":true,"background":true,"gui":true,
        "awareness":{"level":"assistive","paused":false,"dnd":false},
        "animation":{"wanted":"normal","effective":"normal","onBattery":false},
        "notice":{"kind":"reminder","title":"Call the workshop about the car","at":%1}})";
    const char *metrics = R"({"cpu":23.5,"memory":61.2,"temperature":54,"battery":{"present":true,"percent":76,"onBattery":false}})";
    const QString now = QString::number(QDateTime::currentMSecsSinceEpoch() - 4 * 60000);
    const QString idle = QString::fromLatin1(status).arg(now);
    QString busy = idle;
    busy.replace(QLatin1String("\"state\":\"idle\""), QLatin1String("\"state\":\"processing\""))
        .replace(QLatin1String("\"onBattery\":false}"), QLatin1String("\"onBattery\":true}"))
        .replace(QLatin1String("\"paused\":false"), QLatin1String("\"paused\":true"));
    QString failed = idle;
    failed.replace(QLatin1String("\"state\":\"idle\""), QLatin1String("\"state\":\"error\""));
    QString off = idle;
    off.replace(QLatin1String("\"integration\":true"), QLatin1String("\"integration\":false"));

    const QList<std::pair<QString, QString>> cases{
        {QStringLiteral("idle"), QStringLiteral("width: 320; height: 420; online: true; intensity: \"normal\"; status: JSON.parse('%1'); metrics: JSON.parse('%2')").arg(idle, QLatin1String(metrics))},
        {QStringLiteral("processing-battery"), QStringLiteral("width: 320; height: 420; online: true; intensity: \"high\"; status: JSON.parse('%1'); metrics: JSON.parse('%2')").arg(busy, QLatin1String(metrics))},
        {QStringLiteral("error-small"), QStringLiteral("width: 190; height: 230; online: true; intensity: \"low\"; status: JSON.parse('%1'); metrics: JSON.parse('%2')").arg(failed, QLatin1String(metrics))},
        {QStringLiteral("covered"), QStringLiteral("width: 320; height: 420; online: true; covered: true; intensity: \"off\"; status: JSON.parse('%1'); metrics: JSON.parse('%2')").arg(idle, QLatin1String(metrics))},
        {QStringLiteral("integration-off"), QStringLiteral("width: 320; height: 420; online: true; intensity: \"normal\"; status: JSON.parse('%1')").arg(off)},
        {QStringLiteral("outdated"), QStringLiteral("width: 320; height: 420; online: true; intensity: \"normal\"")},
        {QStringLiteral("offline"), QStringLiteral("width: 320; height: 420; online: false; intensity: \"off\"")},
        {QStringLiteral("panel-icon"), QString()},
    };

    QQmlEngine engine;
    Localized localized;
    engine.rootContext()->setContextObject(&localized);
    int failures = 0;
    for (const auto &[name, props] : cases) {
        g_problems.clear();
        QString qml = QStringLiteral("import QtQuick\nRectangle { color: \"#1b2330\"; width: inner.width + 24; height: inner.height + 24\n");
        qml += props.isEmpty() ? QStringLiteral("MiniCore { id: inner; x: 12; y: 12; width: 48; height: 48; compact: true; hudState: \"notification\"; intensity: \"low\" }\n}")
                               : QStringLiteral("HudView { id: inner; x: 12; y: 12; %1 }\n}").arg(props);
        QQmlComponent component(&engine);
        // The base URL puts the widget's directory in scope: HudView, MiniCore, theme.js.
        component.setData(qml.toUtf8(), QUrl::fromLocalFile(ui + QStringLiteral("/check-%1.qml").arg(name)));
        std::unique_ptr<QObject> root(component.create());
        auto *item = qobject_cast<QQuickItem *>(root.get());
        if (!item) {
            std::fprintf(stderr, "FAIL %s: %s\n", qPrintable(name), qPrintable(component.errorString()));
            ++failures;
            continue;
        }
        QQuickWindow window;
        window.resize(int(item->width()), int(item->height()));
        item->setParentItem(window.contentItem());
        window.show();
        QEventLoop loop;
        QTimer::singleShot(700, &loop, &QEventLoop::quit); // a few frames of animation
        loop.exec();
        if (!out.isEmpty())
            window.grabWindow().save(out + QStringLiteral("/%1.png").arg(name));
        if (!g_problems.isEmpty()) {
            std::fprintf(stderr, "FAIL %s: %d QML warnings\n", qPrintable(name), int(g_problems.size()));
            ++failures;
        }
    }
    if (failures == 0)
        std::printf("PASS: the desktop widget's views load in %d states without warnings\n", int(cases.size()));
    return failures == 0 ? 0 : 1;
}

#include "plasmoid_check.moc"
