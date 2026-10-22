#pragma once
#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <functional>
#include <QtQml/qqmlregistration.h>
// The Electronics skill's workbench: example circuits to look at, explain and
// save (SPICE, a netlist for Pcbnew, a BOM), and work with the user's KiCad
// projects through kicad-cli — sheet previews, ERC/DRC reports to discuss with
// Jarvis, BOM, netlist, PDF and fabrication files — or open them in KiCad.
// Outputs go to ~/Documents/Jarvis/KiCad/<project>; KiCad files are only read.
class KicadController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool english READ english WRITE setEnglish NOTIFY examplesChanged)
    Q_PROPERTY(QVariantList examples READ examples NOTIFY examplesChanged)
    Q_PROPERTY(bool cliAvailable READ cliAvailable CONSTANT)
    Q_PROPERTY(QString cliVersion READ cliVersion CONSTANT)
    // ERC and DRC from the command line need KiCad 8 or newer.
    Q_PROPERTY(bool checksAvailable READ checksAvailable CONSTANT)
    Q_PROPERTY(bool kicadAvailable READ kicadAvailable CONSTANT)
    Q_PROPERTY(QString file READ file NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(bool hasSchematic READ hasSchematic NOTIFY changed)
    Q_PROPERTY(bool hasBoard READ hasBoard NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    // Preview images (file URLs), one per sheet, or the board.
    Q_PROPERTY(QVariantList pages READ pages NOTIFY changed)
    // ERC/DRC findings: [{severity, type, description, where}].
    Q_PROPERTY(QVariantList report READ report NOTIFY changed)
    Q_PROPERTY(QString reportTitle READ reportTitle NOTIFY changed)
    Q_PROPERTY(QString outputFolder READ outputFolder NOTIFY changed)
    // Recent projects in the usual folders and KiCad's demos: [{name, path, folder, demo}].
    Q_PROPERTY(QVariantList projects READ projects NOTIFY projectsChanged)
public:
    explicit KicadController(QObject *parent=nullptr);
    ~KicadController() override;
    bool english() const { return m_english; }
    void setEnglish(bool english);
    QVariantList examples() const;
    static QString cli();
    bool cliAvailable() const { return !cli().isEmpty(); }
    QString cliVersion() const;
    bool checksAvailable() const;
    bool kicadAvailable() const;
    QString file() const { return m_file; }
    QString name() const;
    bool hasSchematic() const { return !schematic().isEmpty(); }
    bool hasBoard() const { return !board().isEmpty(); }
    bool busy() const { return m_process.state()!=QProcess::NotRunning || !m_jobs.isEmpty(); }
    QString status() const { return m_status; }
    QVariantList pages() const { return m_pages; }
    QVariantList report() const { return m_report; }
    QString reportTitle() const { return m_reportTitle; }
    QString outputFolder() const;
    QVariantList projects() const { return m_projects; }

    // ---- Example circuits ----
    // Saves an example as "cir" (SPICE), "net" (KiCad netlist), "csv" (BOM), "svg" or "txt" (parts and nets);
    // returns the path, "" on failure. The files go to ~/Documents/Jarvis/Schematics.
    Q_INVOKABLE QString saveExample(const QString &id, const QString &format);
    // Parts and nets of an example as text.
    Q_INVOKABLE QString exampleDigest(const QString &id) const;
    // The chat message that asks Jarvis about an example.
    Q_INVOKABLE QString exampleQuestion(const QString &id) const;
    Q_INVOKABLE void copyText(const QString &text) const;
    Q_INVOKABLE void showFile(const QString &path) const;

    // ---- KiCad projects ----
    Q_INVOKABLE void choose(const QUrl &url);
    Q_INVOKABLE void chooseProject(const QString &path);
    Q_INVOKABLE void refreshProjects();
    Q_INVOKABLE void preview(bool board=false);
    Q_INVOKABLE void runErc();
    Q_INVOKABLE void runDrc();
    Q_INVOKABLE void exportBom();
    Q_INVOKABLE void exportNetlist();
    Q_INVOKABLE void exportPdf();
    // Gerbers, drill files and the placement file for a board house.
    Q_INVOKABLE void exportFabrication();
    Q_INVOKABLE void openInKicad();
    Q_INVOKABLE void openOutputFolder();
    // The schematic (or board) to attach to a chat question, and the question about the report.
    Q_INVOKABLE QString attachment() const;
    Q_INVOKABLE QString reportQuestion() const;
signals:
    void changed();
    void examplesChanged();
    void projectsChanged();
private:
    QString schematic() const;
    QString board() const;
    QString projectFile() const;
    QString ensureOutput();
    void setStatus(const QString &status);
    // kicad-cli jobs run one after another; `done` gets the exit code and output.
    void run(const QStringList &args,std::function<void(bool ok,const QString &output)> done);
    void next();
    void readReport(const QString &path,const QString &title);
    struct Job { QStringList args; std::function<void(bool,const QString &)> done; };
    QQueue<Job> m_jobs;
    std::function<void(bool,const QString &)> m_current;
    QProcess m_process;
    QTimer m_timeout;
    QString m_file,m_status,m_reportTitle;
    QVariantList m_pages,m_report,m_projects;
    bool m_english=false;
};
