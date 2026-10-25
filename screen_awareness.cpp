#include "screen_awareness.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUrl>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <memory>

#include "dbus_names.h"

using namespace Qt::StringLiterals;

namespace {

constexpr int kMinIntervalMinutes = 2;

// Reads a pipe to the end in a worker thread (KWin writes the picture into it).
void readPipe(int fd, const std::function<void(QByteArray)> &done)
{
    QThreadPool::globalInstance()->start([fd, done] {
        QByteArray data;
        char buffer[65536];
        const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 8000;
        while (QDateTime::currentMSecsSinceEpoch() < deadline && data.size() < 512 * 1024 * 1024) {
            pollfd p{fd, POLLIN, 0};
            if (::poll(&p, 1, 500) <= 0)
                continue;
            const ssize_t n = ::read(fd, buffer, sizeof buffer);
            if (n <= 0)
                break;
            data.append(buffer, n);
        }
        ::close(fd);
        done(data);
    });
}

} // namespace

ScreenAwareness::ScreenAwareness(QObject *parent)
    : QObject(parent)
{
    findOcrLanguages();
    m_timer.setSingleShot(false);
    connect(&m_timer, &QTimer::timeout, this, &ScreenAwareness::backgroundTick);
    // A locked screen is never observed.
    QDBusConnection::sessionBus().connect(u"org.freedesktop.ScreenSaver"_s, u"/ScreenSaver"_s, u"org.freedesktop.ScreenSaver"_s,
                                          u"ActiveChanged"_s, this, SLOT(onLocked(bool)));
}

ScreenAwareness::~ScreenAwareness()
{
    for (const auto &p : std::as_const(m_ocr))
        if (p)
            p->kill();
}

// Which Tesseract languages are installed (English, French, Russian), asked once without blocking.
void ScreenAwareness::findOcrLanguages()
{
    const QString tesseract = QStandardPaths::findExecutable(u"tesseract"_s);
    if (tesseract.isEmpty())
        return;
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [this, p] {
        const QString out = QString::fromUtf8(p->readAllStandardOutput());
        QStringList langs;
        for (const QString &l : {u"eng"_s, u"fra"_s, u"rus"_s})
            if (out.contains(QRegularExpression(u"^"_s + l + u"$"_s, QRegularExpression::MultilineOption)))
                langs.append(l);
        m_ocrLanguages = langs.join(u'+');
        p->deleteLater();
        updateTimer();
        emit changed();
    });
    connect(p, &QProcess::errorOccurred, p, &QObject::deleteLater);
    p->start(tesseract, {u"--list-langs"_s});
}

QString ScreenAwareness::runtimeDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (base.isEmpty())
        return {};
    const QString dir = base + u"/jarvis-screen"_s;
    QDir().mkpath(dir);
    QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    return dir;
}

void ScreenAwareness::setAllowed(bool on)
{
    if (m_allowed == on)
        return;
    m_allowed = on;
    updateTimer();
    emit changed();
}

void ScreenAwareness::setCloudImages(bool on)
{
    if (m_cloudImages == on)
        return;
    m_cloudImages = on;
    emit changed();
}

void ScreenAwareness::setIntervalMinutes(int minutes)
{
    minutes = minutes <= 0 ? 0 : std::max(kMinIntervalMinutes, std::min(minutes, 60));
    if (m_interval == minutes)
        return;
    m_interval = minutes;
    updateTimer();
    emit changed();
}

void ScreenAwareness::setLevel(const QString &level)
{
    if (m_level == level)
        return;
    m_level = level;
    updateTimer();
    emit changed();
}

void ScreenAwareness::setPaused(bool paused)
{
    if (m_paused == paused)
        return;
    m_paused = paused;
    updateTimer();
    emit changed();
}

void ScreenAwareness::onLocked(bool locked)
{
    m_locked = locked;
    updateTimer();
    emit changed();
}

bool ScreenAwareness::background() const
{
    return m_allowed && !m_paused && !m_locked && m_interval > 0 && ocrAvailable()
           && (m_level == u"assistive" || m_level == u"proactive");
}

void ScreenAwareness::updateTimer()
{
    if (background()) {
        if (!m_timer.isActive() || m_timer.interval() != m_interval * 60 * 1000)
            m_timer.start(m_interval * 60 * 1000);
    } else {
        m_timer.stop();
    }
}

void ScreenAwareness::setCapturing(bool on)
{
    if (m_capturing == on)
        return;
    m_capturing = on;
    emit changed();
}

void ScreenAwareness::setStatus(const QString &status)
{
    m_status = status;
    emit changed();
}

void ScreenAwareness::backgroundTick()
{
    if (!background() || m_capturing)
        return;
    // Ask jarvisd what is focused and whether it may be looked at.
    auto msg = QDBusMessage::createMethodCall(QString(jarvis::dbus::kService), QString(jarvis::dbus::kPath),
                                              QString(jarvis::dbus::kInterface), u"Context"_s);
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg, 3000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w] {
        w->deleteLater();
        QDBusPendingReply<QString> reply = *w;
        if (reply.isError() || !background())
            return;
        const QJsonObject c = QJsonDocument::fromJson(reply.value().toUtf8()).object();
        const QString handle = c.value(u"handle"_s).toString();
        if (!c.value(u"available"_s).toBool() || handle.isEmpty() || c.value(u"excluded"_s).toBool()
            || c.value(u"private"_s).toBool() || c.value(u"fullscreen"_s).toBool()
            || c.value(u"pausedUntil"_s).toDouble() > 0)
            return; // nothing that may be looked at right now
        Job job;
        job.id = ++m_nextId;
        job.background = true;
        job.appKey = c.value(u"appKey"_s).toString();
        setCapturing(true);
        grab(handle, /*allowPortal=*/false, [this, job](const QImage &image, const QString &error) {
            if (image.isNull()) {
                setCapturing(false);
                setStatus(error);
                return;
            }
            process(job, image);
        });
    });
}

int ScreenAwareness::capture(const QString &handle, bool image)
{
    Job job;
    job.id = ++m_nextId;
    job.image = image && m_cloudImages;
    if (!m_allowed) {
        const int id = job.id;
        QTimer::singleShot(0, this, [this, id] {
            emit captured(id, {}, {}, tr("Screen awareness is off. Turn it on in Settings → Memory and privacy."));
        });
        return id;
    }
    setCapturing(true);
    grab(handle, /*allowPortal=*/true, [this, job](const QImage &picture, const QString &error) {
        if (picture.isNull()) {
            finish(job, {}, {}, error);
            return;
        }
        process(job, picture);
    });
    return job.id;
}

void ScreenAwareness::test()
{
    const int id = capture({}, false);
    Q_UNUSED(id)
}

void ScreenAwareness::discard(const QString &imagePath)
{
    // Only files this class wrote.
    const QString dir = runtimeDir();
    if (!imagePath.isEmpty() && !dir.isEmpty() && QFileInfo(imagePath).absolutePath() == dir)
        QFile::remove(imagePath);
}

void ScreenAwareness::grab(const QString &handle, bool allowPortal, const std::function<void(const QImage &, const QString &)> &done)
{
    if (!m_kwinDenied) {
        grabKWin(handle, [this, handle, allowPortal, done](const QImage &image, const QString &error) {
            if (!image.isNull() || !allowPortal) {
                done(image, error);
                return;
            }
            grabPortal(done);
        });
        return;
    }
    if (allowPortal)
        grabPortal(done);
    else
        done({}, tr("KWin does not let this build take screenshots; background analysis needs the installed Jarvis."));
}

void ScreenAwareness::grabKWin(const QString &handle, const std::function<void(const QImage &, const QString &)> &done)
{
    int fds[2];
    if (::pipe2(fds, O_CLOEXEC) != 0) {
        done({}, tr("Could not create a pipe for the screenshot."));
        return;
    }
    QVariantMap options{{u"include-cursor"_s, false}, {u"include-decoration"_s, true}, {u"include-shadow"_s, false},
                        {u"native-resolution"_s, false}};
    QDBusMessage msg = handle.isEmpty()
        ? QDBusMessage::createMethodCall(u"org.kde.KWin"_s, u"/org/kde/KWin/ScreenShot2"_s, u"org.kde.KWin.ScreenShot2"_s, u"CaptureActiveScreen"_s)
        : QDBusMessage::createMethodCall(u"org.kde.KWin"_s, u"/org/kde/KWin/ScreenShot2"_s, u"org.kde.KWin.ScreenShot2"_s, u"CaptureWindow"_s);
    if (!handle.isEmpty())
        msg << handle;
    msg << options << QVariant::fromValue(QDBusUnixFileDescriptor(fds[1]));
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg, 10000), this);
    ::close(fds[1]); // the message holds its own copy
    // The data and the description arrive separately: wait for both.
    struct Shared
    {
        QByteArray data;
        QVariantMap meta;
        QString error;
        int parts = 0;
    };
    auto shared = std::make_shared<Shared>();
    QPointer<ScreenAwareness> self(this);
    auto complete = [self, shared, done] {
        if (!self || ++shared->parts < 2)
            return;
        if (!shared->error.isEmpty()) {
            done({}, shared->error);
            return;
        }
        QImage image;
        const QVariantMap &m = shared->meta;
        if (m.value(u"type"_s).toString() == u"raw") {
            const int width = m.value(u"width"_s).toInt(), height = m.value(u"height"_s).toInt();
            const int stride = m.value(u"stride"_s).toInt();
            const auto format = QImage::Format(m.value(u"format"_s).toUInt());
            if (width > 0 && height > 0 && stride > 0 && qint64(stride) * height <= shared->data.size())
                image = QImage(reinterpret_cast<const uchar *>(shared->data.constData()), width, height, stride, format).copy();
        } else {
            image = QImage::fromData(shared->data);
        }
        done(image, image.isNull() ? tr("KWin returned an empty screenshot.") : QString());
    };
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w, shared, complete] {
        w->deleteLater();
        const QDBusMessage reply = w->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            if (reply.errorName().contains(u"NoAuthorized"_s) || reply.errorName().contains(u"AccessDenied"_s)) {
                m_kwinDenied = true; // not the installed Jarvis: use the portal from now on
                shared->error = tr("KWin did not allow the screenshot (only the installed Jarvis may take one without asking).");
            } else {
                shared->error = reply.errorMessage().left(300);
            }
        } else {
            m_backend = u"kwin"_s;
            shared->meta = qdbus_cast<QVariantMap>(reply.arguments().value(0));
        }
        complete();
    });
    readPipe(fds[0], [self, shared, complete](QByteArray data) {
        if (!self)
            return;
        QMetaObject::invokeMethod(self, [shared, complete, data = std::move(data)]() mutable {
            shared->data = std::move(data);
            complete();
        }, Qt::QueuedConnection);
    });
}

void ScreenAwareness::grabPortal(const std::function<void(const QImage &, const QString &)> &done)
{
    if (m_portalDone) {
        done({}, tr("A screenshot request is already open."));
        return;
    }
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString token = u"jarvis"_s + QString::number(QDateTime::currentMSecsSinceEpoch() % 100000000);
    QString sender = bus.baseService().mid(1);
    sender.replace(u'.', u'_');
    m_portalRequest = u"/org/freedesktop/portal/desktop/request/"_s + sender + u'/' + token;
    // Listen before asking, so the answer cannot be missed.
    bus.connect(u"org.freedesktop.portal.Desktop"_s, m_portalRequest, u"org.freedesktop.portal.Request"_s, u"Response"_s,
                this, SLOT(onPortalResponse(QDBusMessage)));
    m_portalDone = done;
    auto msg = QDBusMessage::createMethodCall(u"org.freedesktop.portal.Desktop"_s, u"/org/freedesktop/portal/desktop"_s,
                                              u"org.freedesktop.portal.Screenshot"_s, u"Screenshot"_s);
    msg << QString() << QVariantMap{{u"handle_token"_s, token}, {u"interactive"_s, false}, {u"modal"_s, true}};
    auto *w = new QDBusPendingCallWatcher(bus.asyncCall(msg, 5000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w] {
        w->deleteLater();
        if (w->isError() && m_portalDone) {
            auto callback = std::exchange(m_portalDone, nullptr);
            callback({}, tr("The screenshot portal is not available: %1").arg(w->error().message().left(200)));
        }
    });
    // The user may leave the desktop's dialog open: give up after two minutes.
    QTimer::singleShot(120000, this, [this, token] {
        if (m_portalDone && m_portalRequest.endsWith(token)) {
            auto callback = std::exchange(m_portalDone, nullptr);
            callback({}, tr("No answer from the screenshot portal."));
        }
    });
}

void ScreenAwareness::onPortalResponse(const QDBusMessage &message)
{
    QDBusConnection::sessionBus().disconnect(u"org.freedesktop.portal.Desktop"_s, m_portalRequest, u"org.freedesktop.portal.Request"_s,
                                             u"Response"_s, this, SLOT(onPortalResponse(QDBusMessage)));
    auto callback = std::exchange(m_portalDone, nullptr);
    if (!callback)
        return;
    const uint response = message.arguments().value(0).toUInt();
    const QVariantMap results = qdbus_cast<QVariantMap>(message.arguments().value(1));
    if (response != 0) {
        callback({}, response == 1 ? tr("The screenshot was cancelled.") : tr("The desktop refused the screenshot."));
        return;
    }
    const QString path = QUrl(results.value(u"uri"_s).toString()).toLocalFile();
    QImage image(path);
    // Jarvis keeps no copy: a file the portal left in a temporary place goes.
    for (const QString &temp : {QStandardPaths::writableLocation(QStandardPaths::TempLocation),
                                QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation),
                                QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)})
        if (!temp.isEmpty() && path.startsWith(temp + u'/'))
            QFile::remove(path);
    m_backend = u"portal"_s;
    callback(image, image.isNull() ? tr("Could not read the screenshot.") : QString());
}

void ScreenAwareness::process(const Job &job, const QImage &picture)
{
    m_lastCaptureAt = QDateTime::currentMSecsSinceEpoch();
    const QString dir = job.image ? runtimeDir() : QString();
    QPointer<ScreenAwareness> self(this);
    // Scaling and encoding are heavy for a 4K screen: off the GUI thread.
    QThreadPool::globalInstance()->start([self, job, picture, dir] {
        QImage gray = picture.convertToFormat(QImage::Format_Grayscale8);
        if (gray.width() < 1400)
            gray = gray.scaledToWidth(gray.width() * 2, Qt::SmoothTransformation); // small text reads better larger
        if (gray.width() > 4000)
            gray = gray.scaledToWidth(4000, Qt::SmoothTransformation);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        gray.save(&buffer, "PNG", 1); // fast compression: it only goes through a pipe
        QString imagePath;
        if (!dir.isEmpty()) {
            QImage forClaude = picture;
            if (std::max(forClaude.width(), forClaude.height()) > 1568)
                forClaude = forClaude.scaled(1568, 1568, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            imagePath = dir + u"/screen-"_s + QString::number(QDateTime::currentMSecsSinceEpoch()) + u".jpg"_s;
            QFile f(imagePath);
            if (f.open(QIODevice::WriteOnly) && f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
                forClaude.convertToFormat(QImage::Format_RGB888).save(&f, "JPEG", 85);
            else
                imagePath.clear();
        }
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, job, png, imagePath] {
            if (!self) {
                if (!imagePath.isEmpty())
                    QFile::remove(imagePath);
                return;
            }
            if (!self->ocrAvailable()) {
                self->finish(job, imagePath, {}, imagePath.isEmpty() ? tr("Text recognition needs Tesseract: sudo apt install tesseract-ocr tesseract-ocr-fra") : QString());
                return;
            }
            auto *ocr = new QProcess(self);
            self->m_ocr.append(ocr);
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert(u"OMP_THREAD_LIMIT"_s, u"1"_s); // one core: OCR must not take the machine
            ocr->setProcessEnvironment(env);
            ocr->setChildProcessModifier([] { [[maybe_unused]] int n = ::nice(10); });
            QObject::connect(ocr, &QProcess::finished, self, [self, ocr, job, imagePath](int code, QProcess::ExitStatus) {
                const QString text = code == 0 ? QString::fromUtf8(ocr->readAllStandardOutput()).trimmed().left(8000) : QString();
                ocr->deleteLater();
                self->finish(job, imagePath, text, code == 0 ? QString() : tr("Text recognition failed."));
            });
            QObject::connect(ocr, &QProcess::errorOccurred, self, [self, ocr, job, imagePath](QProcess::ProcessError e) {
                if (e != QProcess::FailedToStart)
                    return;
                ocr->deleteLater();
                self->finish(job, imagePath, {}, tr("Tesseract could not start."));
            });
            ocr->start(QStandardPaths::findExecutable(u"tesseract"_s),
                       {u"stdin"_s, u"stdout"_s, u"-l"_s, self->m_ocrLanguages, u"--psm"_s, u"3"_s});
            ocr->write(png);
            ocr->closeWriteChannel();
            QTimer::singleShot(30000, ocr, [ocr] { ocr->kill(); });
        });
    });
}

void ScreenAwareness::finish(const Job &job, const QString &imagePath, const QString &text, const QString &error)
{
    m_ocr.removeAll(nullptr);
    setCapturing(false);
    // The permission was taken back while the picture was being read: nothing of it is used.
    if (!m_allowed) {
        discard(imagePath);
        setStatus({});
        if (!job.background)
            emit captured(job.id, {}, {}, tr("Screen awareness is off. Turn it on in Settings → Memory and privacy."));
        return;
    }
    if (!m_cloudImages && !imagePath.isEmpty()) {
        discard(imagePath);
        emit captured(job.id, {}, text, error);
        return;
    }
    setStatus(error.isEmpty() ? QString() : error);
    if (job.background) {
        if (text.size() < 20)
            return;
        auto msg = QDBusMessage::createMethodCall(QString(jarvis::dbus::kService), QString(jarvis::dbus::kPath),
                                                  QString(jarvis::dbus::kInterface), u"ScreenObserved"_s);
        msg << job.appKey << text;
        QDBusConnection::sessionBus().asyncCall(msg, 3000);
        return;
    }
    emit captured(job.id, imagePath, text, error);
}
