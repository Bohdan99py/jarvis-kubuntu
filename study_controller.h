#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>

#include "claude_client.h"
#include "knowledge_store.h"
#include "vocab_store.h"

// Lines of the open transcript: time, French text and its Russian translation.
// A real model, so live lines append and translations fill in without the
// view losing its scroll position.
class StudyLines : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role { StartRole = Qt::UserRole + 1, EndRole, FrenchRole, RussianRole };
    struct Line
    {
        double start = 0;
        double end = 0;
        QString fr;
        QString ru;
    };

    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(m_lines.size()); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void reset(const QList<Line> &lines);
    void append(const Line &line);
    void setTranslation(int row, const QString &ru);
    const QList<Line> &lines() const { return m_lines; }

signals:
    void countChanged();

private:
    QList<Line> m_lines;
};

// The study window's engine (French lessons): records lessons, transcribes
// them locally with Whisper (scripts/study.py), translates transcripts, typed
// or spoken phrases and PDFs into Russian with Claude, writes lesson notes and
// keeps flashcards with spaced repetition.
// Files: ~/Documents/Jarvis/French/{recordings,pdf}; for every recording
// <name>.wav, <name>.fr.json|.fr.txt (transcript), <name>.ru.json|.ru.txt
// (translation), <name>.notes.md. Audio never leaves the computer; text goes
// to Claude only for translation and notes.
class StudyController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // Setup of local recognition.
    Q_PROPERTY(bool ready READ ready NOTIFY setupChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY setupChanged)
    Q_PROPERTY(bool claudeReady READ claudeReady NOTIFY setupChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY setupChanged)
    Q_PROPERTY(bool autoTranslate READ autoTranslate WRITE setAutoTranslate NOTIFY setupChanged)
    Q_PROPERTY(bool speakTranslation READ speakTranslation WRITE setSpeakTranslation NOTIFY setupChanged)
    // The long job (setup, import, transcription, translation, notes, PDF); one at a time.
    Q_PROPERTY(bool busy READ busy NOTIFY statusChanged)
    Q_PROPERTY(QString job READ job NOTIFY statusChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(double progress READ progress NOTIFY statusChanged)
    Q_PROPERTY(QString pdfResult READ pdfResult NOTIFY statusChanged)
    // Microphone.
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(bool liveRecording READ liveRecording NOTIFY recordingChanged)
    Q_PROPERTY(bool listening READ listening NOTIFY recordingChanged)
    Q_PROPERTY(double level READ level NOTIFY levelChanged)
    Q_PROPERTY(double seconds READ seconds NOTIFY levelChanged)
    // Library and the open recording.
    Q_PROPERTY(QString folder READ folder CONSTANT)
    Q_PROPERTY(QVariantList recordings READ recordings NOTIFY recordingsChanged)
    Q_PROPERTY(QString current READ current NOTIFY viewerChanged)
    Q_PROPERTY(QObject *lines READ lines CONSTANT)
    Q_PROPERTY(QString notes READ notes NOTIFY viewerChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    // Phrase translator.
    Q_PROPERTY(QString phrase READ phrase NOTIFY phraseChanged)
    Q_PROPERTY(QString phraseTranslation READ phraseTranslation NOTIFY phraseChanged)
    Q_PROPERTY(bool phraseBusy READ phraseBusy NOTIFY phraseChanged)
    // Flashcards.
    Q_PROPERTY(QVariantList cards READ cards NOTIFY cardsChanged)
    Q_PROPERTY(QVariantMap card READ card NOTIFY reviewChanged)
    Q_PROPERTY(QVariantMap vocabStats READ vocabStats NOTIFY reviewChanged)
    Q_PROPERTY(QString vocabStatus READ vocabStatus NOTIFY reviewChanged)

public:
    explicit StudyController(QObject *parent = nullptr);
    ~StudyController() override;

    bool ready() const;
    QString model() const { return m_model; }
    void setModel(const QString &model);
    bool claudeReady() const { return m_claudeReady; }
    QString language() const { return m_language; }
    void setLanguage(const QString &language);
    bool autoTranslate() const { return m_autoTranslate; }
    void setAutoTranslate(bool on);
    bool speakTranslation() const { return m_speakTranslation; }
    void setSpeakTranslation(bool on);

    bool busy() const { return !m_job.isEmpty(); }
    QString job() const { return m_job; }
    QString status() const { return m_status; }
    double progress() const { return m_progress; }
    QString pdfResult() const { return m_pdfResult; }

    bool recording() const { return m_recorder.state() != QProcess::NotRunning; }
    bool liveRecording() const { return recording() && m_live; }
    bool listening() const { return m_listener.state() != QProcess::NotRunning; }
    double level() const { return m_level; }
    double seconds() const { return m_seconds; }

    QString folder() const;
    QVariantList recordings() const { return m_recordings; }
    QString current() const { return m_current; }
    QObject *lines() { return &m_lines; }
    QString notes() const { return m_notes; }
    bool playing() const { return m_player.state() != QProcess::NotRunning; }

    QString phrase() const { return m_phrase; }
    QString phraseTranslation() const { return m_phraseTranslation; }
    bool phraseBusy() const { return m_phraseBusy; }

    QVariantList cards() const;
    QVariantMap card() const;
    QVariantMap vocabStats() const;
    QString vocabStatus() const { return m_vocabStatus; }

    // Setup and the long job.
    Q_INVOKABLE void setup();
    Q_INVOKABLE void cancel();
    // Re-read the Claude key (after the settings dialog saved).
    Q_INVOKABLE void reloadConfig();

    // Recording: `live` also transcribes (and, with autoTranslate, translates) while recording.
    Q_INVOKABLE void startRecording(bool live);
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void importAudio(const QUrl &url);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void open(const QString &path);
    Q_INVOKABLE void transcribe(const QString &path);
    Q_INVOKABLE void translateRecording(const QString &path);
    Q_INVOKABLE void makeNotes(const QString &path);
    // Moves the recording and its transcript files to the trash.
    Q_INVOKABLE void remove(const QString &path);
    Q_INVOKABLE void play(double start, double end);
    Q_INVOKABLE void stopPlayback();
    Q_INVOKABLE void openFolder();
    Q_INVOKABLE void openFile(const QString &path);
    // The open recording's transcript file (.ru.txt if translated, else .fr.txt).
    Q_INVOKABLE void openTranscript();

    // Phrase translator: typed or spoken (French → Russian; Russian → French).
    Q_INVOKABLE void translatePhrase(const QString &text);
    Q_INVOKABLE void listenPhrase();
    Q_INVOKABLE void stopPhrase();
    // lang: "fr", "ru" or "" (by script).
    Q_INVOKABLE void speak(const QString &text, const QString &lang);

    // PDF → Russian (or bilingual) PDF and text in folder/pdf.
    Q_INVOKABLE void translatePdf(const QUrl &url, bool bilingual);

    // Flashcards. An empty translation is filled in by Claude.
    Q_INVOKABLE void addWord(const QString &front, const QString &back, const QString &example);
    Q_INVOKABLE void editWord(const QString &id, const QString &back, const QString &example);
    Q_INVOKABLE void removeWord(const QString &id);
    // grade: 0 again, 1 hard, 2 good, 3 easy
    Q_INVOKABLE void review(const QString &id, int grade);
    Q_INVOKABLE void refreshCards();

signals:
    void setupChanged();
    void statusChanged();
    void recordingChanged();
    void levelChanged();
    void recordingsChanged();
    void viewerChanged();
    void playingChanged();
    void phraseChanged();
    void cardsChanged();
    void reviewChanged();
    // A fact about the user's studies was added to Jarvis's memory.
    void memoryLearned();

private:
    struct ClaudeJob
    {
        QString system;
        QString text;
        int maxTokens = 4096;
        int attempts = 0;
        quint64 generation = 0; // 0: not part of the long job, never cancelled
        std::function<void(bool, const QString &)> done;
    };
    using Translated = std::function<void(bool ok, const QStringList &out, const QString &error)>;

    QString root() const;
    QString recordingsDir() const;
    void setStatus(const QString &status, double progress = -1);
    void startJob(const QString &job, const QString &status);
    void finishJob(const QString &status);
    bool needReady();
    bool needClaude();
    void runScript(QProcess &process, bool venv, const QStringList &args);
    void readLines(QProcess &process, const std::function<void(const QJsonObject &)> &handler);
    void scriptEvent(const QJsonObject &event);
    void recorderEvent(const QJsonObject &event);
    void listenerEvent(const QJsonObject &event);
    void jobFinished(int code);

    void enqueueClaude(ClaudeJob job);
    void dispatchClaude();
    void translateItems(const QStringList &items, bool toFrench, quint64 generation, const Translated &done);
    void translateLiveLine(int row);
    void continueTranslation();
    void saveTranslations(const QString &path, const QStringList &ru);
    void writePdf(const QString &path, const QString &title, bool bilingual);
    void learnStudies();
    void speakNext();
    void repairPartials();

    // Settings
    QString m_model;
    QString m_language = QStringLiteral("ru");
    bool m_autoTranslate = true;
    bool m_speakTranslation = false;
    bool m_claudeReady = false;
    QString m_claudeModel;
    QString m_apiKey;
    bool m_learnDialog = true;

    // Long job
    QProcess m_worker;
    QString m_job;
    QString m_jobPath;
    QString m_status;
    double m_progress = -1;
    QString m_pdfResult;
    quint64 m_generation = 1;
    bool m_jobFailed = false;
    // Translation of a transcript or PDF in progress.
    QStringList m_sourceItems;
    QStringList m_targetItems;
    QList<int> m_pageOf; // PDF: page of each paragraph
    int m_batchesLeft = 0;
    int m_batchesTotal = 0;
    QString m_jobError;
    bool m_pdfBilingual = true;
    bool m_transcribeThenTranslate = false;

    // Microphone
    QProcess m_recorder;
    QProcess m_listener;
    bool m_live = false;
    double m_level = 0;
    double m_seconds = 0;
    QString m_livePath;
    QStringList m_liveRu;

    // Library and viewer
    QVariantList m_recordings;
    QString m_current;
    StudyLines m_lines;
    QString m_notes;
    QProcess m_player;

    // Phrase translator and speech
    QString m_phrase;
    QString m_phraseTranslation;
    bool m_phraseBusy = false;
    QProcess m_speech;
    QQueue<std::pair<QString, QString>> m_speechQueue;

    QHash<QProcess *, QByteArray> m_buffers;
    QString m_workerErrors; // tail of the job's stderr, for pip failures

    // Claude workers
    QList<jarvis::ClaudeClient *> m_workers;
    QHash<jarvis::ClaudeClient *, ClaudeJob> m_running;
    QQueue<ClaudeJob> m_claudeQueue;

    jarvis::VocabStore m_vocab;
    jarvis::KnowledgeStore m_knowledge;
    QString m_vocabStatus;
};
