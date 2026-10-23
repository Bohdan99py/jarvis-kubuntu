#pragma once

#include <QDBusMessage>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>

// Screen awareness: a picture of the window the user works in, read on this
// computer (Tesseract OCR), only with the user's permission and never in secret.
//
// How a picture is taken, best first:
//  * KWin's org.kde.KWin.ScreenShot2 — exactly the window the user was in
//    (by KWin's window id), even while Jarvis's overlay has the focus. KWin
//    allows it only to the installed Jarvis (X-KDE-DBUS-Restricted-Interfaces
//    in its desktop file), the same mechanism Spectacle uses.
//  * the xdg-desktop-portal Screenshot portal — the desktop asks the user
//    (only for a request the user made; never in the background).
//
// Pictures stay in memory. One goes to Claude only when the user allowed it
// and asked: it is written to $XDG_RUNTIME_DIR/jarvis-screen (RAM, 0600) for
// the daemon to read and removed once the answer arrives. Nothing is saved.
//
// Background analysis (assistive/proactive level, screen permission, an
// interval set): at most every N minutes, skipped for excluded and private
// windows, fullscreen applications, a locked screen or a pause; the text goes
// to jarvisd (ScreenObserved), which keeps it in memory for ten minutes.
// `capturing` is true for the whole time a picture is taken and read — the
// interface shows it.
class ScreenAwareness : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // Settings, bound from QML (AppSettings).
    Q_PROPERTY(bool allowed READ allowed WRITE setAllowed NOTIFY changed)
    Q_PROPERTY(bool cloudImages READ cloudImages WRITE setCloudImages NOTIFY changed)
    Q_PROPERTY(int intervalMinutes READ intervalMinutes WRITE setIntervalMinutes NOTIFY changed)
    Q_PROPERTY(QString level READ level WRITE setLevel NOTIFY changed)
    // Observation stops at once (the "pause" button, a locked screen).
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY changed)
    // State, for the indicator.
    Q_PROPERTY(bool capturing READ capturing NOTIFY changed)
    Q_PROPERTY(bool background READ background NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString backend READ backend NOTIFY changed)
    Q_PROPERTY(bool ocrAvailable READ ocrAvailable NOTIFY changed)
    Q_PROPERTY(QString ocrLanguages READ ocrLanguages NOTIFY changed)
    Q_PROPERTY(qint64 lastCaptureAt READ lastCaptureAt NOTIFY changed)

public:
    explicit ScreenAwareness(QObject *parent = nullptr);
    ~ScreenAwareness() override;

    bool allowed() const { return m_allowed; }
    void setAllowed(bool on);
    bool cloudImages() const { return m_cloudImages; }
    void setCloudImages(bool on);
    int intervalMinutes() const { return m_interval; }
    void setIntervalMinutes(int minutes);
    QString level() const { return m_level; }
    void setLevel(const QString &level);
    bool paused() const { return m_paused; }
    void setPaused(bool paused);
    bool capturing() const { return m_capturing; }
    bool background() const;
    QString status() const { return m_status; }
    QString backend() const { return m_backend; }
    bool ocrAvailable() const { return !m_ocrLanguages.isEmpty(); }
    QString ocrLanguages() const { return m_ocrLanguages; }
    qint64 lastCaptureAt() const { return m_lastCaptureAt; }

    // A picture for a request the user made. `handle`: KWin's id of the window
    // ("" = the active screen). `image`: also prepare a file for Claude.
    // Answers with captured(requestId, imagePath, text, error).
    Q_INVOKABLE int capture(const QString &handle, bool image);
    // Removes a file prepared by capture() (after the answer).
    Q_INVOKABLE void discard(const QString &imagePath);
    // Opens KDE's screenshot permission check now (the first picture asks).
    Q_INVOKABLE void test();

signals:
    void changed();
    void captured(int requestId, const QString &imagePath, const QString &text, const QString &error);

private:
    struct Job
    {
        int id = 0;
        bool image = false;
        bool background = false;
        QString appKey;
    };

    void updateTimer();
    void backgroundTick();
    void grab(const QString &handle, bool allowPortal, const std::function<void(const QImage &, const QString &)> &done);
    void grabKWin(const QString &handle, const std::function<void(const QImage &, const QString &)> &done);
    void grabPortal(const std::function<void(const QImage &, const QString &)> &done);
    void process(const Job &job, const QImage &image);
    void finish(const Job &job, const QString &imagePath, const QString &text, const QString &error);
    void setCapturing(bool on);
    void setStatus(const QString &status);
    void findOcrLanguages();
    static QString runtimeDir();

private slots:
    void onPortalResponse(const QDBusMessage &message);
    void onLocked(bool locked);

private:
    bool m_allowed = false;
    bool m_cloudImages = false;
    bool m_paused = false;
    bool m_capturing = false;
    bool m_locked = false;
    bool m_kwinDenied = false;
    int m_interval = 0;
    int m_nextId = 0;
    QString m_level = QStringLiteral("manual");
    QString m_status;
    QString m_backend;
    QString m_ocrLanguages;
    qint64 m_lastCaptureAt = 0;
    QTimer m_timer;
    QString m_portalRequest;
    std::function<void(const QImage &, const QString &)> m_portalDone;
    QList<QPointer<QProcess>> m_ocr;
};
