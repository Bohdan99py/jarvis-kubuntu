#include "kicad_controller.h"
#include "attachments.h"
#include "config.h"
#include "language.h"
#include "schematic_library.h"
#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSet>
#include <algorithm>
#include <memory>

namespace {
constexpr int kJobTimeoutMs=300000;
constexpr int kMaxReport=300;
QString documents() {
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}
QString previewDir() {
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/kicad-preview";
}
bool writeFile(const QString &path,const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes)==bytes.size() && f.commit();
}
// "8.0.4" -> 8; 0 when kicad-cli is missing or silent.
int cliMajor() {
    static const int major=[]{
        const QString tool=KicadController::cli();
        if(tool.isEmpty()) return 0;
        QProcess p;p.start(tool,{"version"});
        if(!p.waitForFinished(8000)){p.kill();p.waitForFinished(500);return 0;}
        return QString::fromUtf8(p.readAllStandardOutput()).trimmed().section('.',0,0).toInt();
    }();
    return major;
}
}

KicadController::KicadController(QObject *parent):QObject(parent) {
    m_english=jarvis::resolveLanguage(jarvis::Config::loadFileOnly().language)==jarvis::Lang::En;
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(kJobTimeoutMs);
    connect(&m_timeout,&QTimer::timeout,this,[this]{m_process.kill();});
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus status){
        m_timeout.stop();
        const QString output=QString::fromUtf8(m_process.readAll()).trimmed().right(2000);
        auto done=std::exchange(m_current,{});
        if(done) done(status==QProcess::NormalExit && code==0,output);
        next();
    });
    connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
        if(error!=QProcess::FailedToStart) return; // a crash also ends in finished()
        m_timeout.stop();
        auto done=std::exchange(m_current,{});
        if(done) done(false,m_process.errorString());
        next();
    });
}
KicadController::~KicadController() {
    m_jobs.clear();
    if(m_process.state()!=QProcess::NotRunning){m_process.kill();m_process.waitForFinished(1000);}
}
void KicadController::setEnglish(bool english) { if(english!=m_english){m_english=english;emit examplesChanged();} }
QVariantList KicadController::examples() const { return jarvis::SchematicLibrary::list(m_english?jarvis::Lang::En:jarvis::Lang::Ru).toVariantList(); }
QString KicadController::cli() { return QStandardPaths::findExecutable("kicad-cli"); }
QString KicadController::cliVersion() const { return cliMajor()>0 ? QString::number(cliMajor()) : QString(); }
bool KicadController::checksAvailable() const { return cliMajor()>=8; }
bool KicadController::kicadAvailable() const {
    for(const char *tool:{"kicad","eeschema","pcbnew"}) if(!QStandardPaths::findExecutable(tool).isEmpty()) return true;
    return false;
}
void KicadController::setStatus(const QString &status) { m_status=status;emit changed(); }

// ---- Example circuits ----------------------------------------------------------------

QString KicadController::saveExample(const QString &id,const QString &format) {
    const auto lang=m_english?jarvis::Lang::En:jarvis::Lang::Ru;
    const QJsonObject e=jarvis::SchematicLibrary::example(id,lang);
    if(e.isEmpty()) return {};
    QByteArray bytes;
    if(format=="cir") bytes=e["spice"].toString().toUtf8();
    else if(format=="net") bytes=jarvis::SchematicLibrary::kicadNetlist(e).toUtf8();
    else if(format=="csv") bytes=jarvis::SchematicLibrary::bomCsv(e["parts"].toArray()).toUtf8();
    else if(format=="txt") bytes=(e["title"].toString()+"\n\n"+e["description"].toString()+"\n\n"+jarvis::SchematicLibrary::digest(e)).toUtf8();
    else if(format=="svg") {QFile f(":/schematics/"+id+".svg");if(f.open(QIODevice::ReadOnly))bytes=f.readAll();}
    if(bytes.isEmpty()) {setStatus(tr("This example has no %1 file.").arg(format.toUpper()));return {};}
    const QString path=documents()+"/Jarvis/Schematics/"+id+(format=="csv"?"-bom.":".")+format;
    if(!writeFile(path,bytes)) {setStatus(tr("Could not save %1.").arg(path));return {};}
    setStatus(format=="net" ? tr("Saved %1. In KiCad: PCB Editor → File → Import → Netlist.").arg(path) : tr("Saved %1.").arg(path));
    return path;
}
QString KicadController::exampleDigest(const QString &id) const {
    const QJsonObject e=jarvis::SchematicLibrary::example(id,m_english?jarvis::Lang::En:jarvis::Lang::Ru);
    return e.isEmpty() ? QString() : jarvis::SchematicLibrary::digest(e);
}
QString KicadController::exampleQuestion(const QString &id) const {
    const auto lang=m_english?jarvis::Lang::En:jarvis::Lang::Ru;
    const QJsonObject e=jarvis::SchematicLibrary::example(id,lang);
    if(e.isEmpty()) return {};
    const QString ask=m_english ? QStringLiteral("Explain the circuit \"%1\": how it works, how the values were chosen and what to check when building it.")
                                : QStringLiteral("Объясни схему «%1»: как она работает, как выбраны номиналы и что проверить при сборке.");
    return (ask.arg(e["title"].toString())+"\n\n```text\n"+jarvis::SchematicLibrary::digest(e)+"```").left(4000);
}
void KicadController::copyText(const QString &text) const { if(QClipboard *c=QGuiApplication::clipboard()) c->setText(text); }
void KicadController::showFile(const QString &path) const { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath())); }

// ---- Project files ---------------------------------------------------------------------

namespace {
QString sibling(const QString &file,const QString &suffix) {
    if(file.isEmpty()) return {};
    const QFileInfo info(file);
    const QString path=info.absolutePath()+"/"+info.completeBaseName()+"."+suffix;
    return QFileInfo::exists(path) ? path : QString();
}
}
QString KicadController::schematic() const { return sibling(m_file,"kicad_sch"); }
QString KicadController::board() const { return sibling(m_file,"kicad_pcb"); }
QString KicadController::projectFile() const { return sibling(m_file,"kicad_pro"); }
QString KicadController::name() const { return m_file.isEmpty() ? QString() : QFileInfo(m_file).completeBaseName(); }
QString KicadController::outputFolder() const { return m_file.isEmpty() ? QString() : documents()+"/Jarvis/KiCad/"+name(); }
QString KicadController::ensureOutput() { const QString dir=outputFolder();QDir().mkpath(dir);return dir; }
QString KicadController::attachment() const { const QString s=schematic(); return !s.isEmpty() ? s : !board().isEmpty() ? board() : m_file; }

void KicadController::choose(const QUrl &url) { chooseProject(url.toLocalFile()); }
void KicadController::chooseProject(const QString &path) {
    const QFileInfo info(path);
    static const QStringList suffixes{"kicad_pro","kicad_sch","kicad_pcb"};
    if(!info.isFile() || !suffixes.contains(info.suffix().toLower())) {setStatus(tr("Choose a KiCad project, schematic or board."));return;}
    m_file=info.canonicalFilePath();
    m_pages.clear();m_report.clear();m_reportTitle.clear();
    setStatus(tr("Project %1.").arg(name()));
    if(cliAvailable()) preview(!hasSchematic());
}

void KicadController::refreshProjects() {
    // The usual places, a few levels deep; hidden folders and huge trees are skipped.
    const QString home=QDir::homePath();
    QStringList roots{documents(),home+"/Documents",home+"/Документы",home+"/Projects",home+"/projects",home+"/KiCad",home+"/kicad",
                      home+"/Electronics",home+"/Desktop",home+"/Рабочий стол",home+"/src"};
    roots.removeDuplicates();
    QList<QFileInfo> found;
    int visited=0;
    std::function<void(const QString &,int)> walk=[&](const QString &dir,int depth){
        if(depth>4 || visited>4000 || found.size()>200) return;
        ++visited;
        const QDir d(dir);
        for(const QFileInfo &f:d.entryInfoList({"*.kicad_pro"},QDir::Files)) found.append(f);
        for(const QFileInfo &sub:d.entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot|QDir::NoSymLinks)) {
            const QString n=sub.fileName();
            if(n.startsWith('.') || n=="node_modules" || n=="build" || n.endsWith("-backups")) continue;
            walk(sub.absoluteFilePath(),depth+1);
        }
    };
    for(const QString &root:std::as_const(roots)) if(QFileInfo(root).isDir()) walk(root,0);
    std::sort(found.begin(),found.end(),[](const QFileInfo &a,const QFileInfo &b){return a.lastModified()>b.lastModified();});
    m_projects.clear();
    QSet<QString> seen;
    for(const QFileInfo &f:std::as_const(found)) {
        if(m_projects.size()>=40 || seen.contains(f.canonicalFilePath())) continue;
        seen.insert(f.canonicalFilePath());
        m_projects.append(QVariantMap{{"name",f.completeBaseName()},{"path",f.canonicalFilePath()},
                                      {"folder",QDir(home).relativeFilePath(f.absolutePath())},{"demo",false}});
    }
    // KiCad's own demos (package kicad-demos) are good examples of real boards.
    for(const QString &demos:{QStringLiteral("/usr/share/kicad/demos"),QStringLiteral("/usr/local/share/kicad/demos")}) {
        QDirIterator it(demos,{"*.kicad_pro"},QDir::Files,QDirIterator::Subdirectories);
        while(it.hasNext()) {
            const QFileInfo f(it.next());
            m_projects.append(QVariantMap{{"name",f.completeBaseName()},{"path",f.absoluteFilePath()},{"folder",f.absolutePath()},{"demo",true}});
        }
    }
    emit projectsChanged();
}

// ---- kicad-cli jobs ------------------------------------------------------------------------

void KicadController::run(const QStringList &args,std::function<void(bool,const QString &)> done) {
    if(!cliAvailable()) {setStatus(tr("kicad-cli was not found. Install KiCad 7 or newer (sudo apt install kicad)."));return;}
    m_jobs.enqueue({args,std::move(done)});
    if(m_process.state()==QProcess::NotRunning && !m_current) next();
    emit changed();
}
void KicadController::next() {
    if(m_jobs.isEmpty()) {emit changed();return;}
    Job job=m_jobs.dequeue();
    m_current=std::move(job.done);
    m_process.start(cli(),job.args);
    m_timeout.start();
    emit changed();
}

void KicadController::preview(bool showBoard) {
    const QString source=showBoard ? board() : schematic();
    if(source.isEmpty()) {setStatus(showBoard ? tr("The project has no board file.") : tr("The project has no schematic."));return;}
    const QString dir=previewDir()+"/"+name()+(showBoard?"-pcb":"-sch");
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    setStatus(tr("Drawing the preview…"));
    const QStringList args=showBoard
        ? QStringList{"pcb","export","svg","-o",dir+"/board.svg","--layers","F.Cu,B.Cu,F.Silkscreen,B.Silkscreen,Edge.Cuts",
                      "--page-size-mode","2","--exclude-drawing-sheet",source}
        : QStringList{"sch","export","svg","-o",dir,source};
    run(args,[this,dir,showBoard](bool ok,const QString &output){
        QFileInfoList files=QDir(dir).entryInfoList({"*.svg"},QDir::Files,QDir::Name);
        // The root sheet first, then the sub-sheets.
        std::stable_sort(files.begin(),files.end(),[this](const QFileInfo &a,const QFileInfo &b){
            return (a.completeBaseName()==name())>(b.completeBaseName()==name());});
        m_pages.clear();
        const QString stamp=QString::number(QDateTime::currentMSecsSinceEpoch());
        for(const QFileInfo &f:std::as_const(files)) {
            QUrl url=QUrl::fromLocalFile(f.absoluteFilePath());url.setQuery("v="+stamp); // a fresh image, not the cached one
            m_pages.append(QVariantMap{{"url",url},{"title",f.completeBaseName()==name()?(showBoard?tr("Board"):tr("Root sheet")):f.completeBaseName().section('-',1)}});
        }
        setStatus(!m_pages.isEmpty() ? tr("%1: %2 page(s).").arg(name()).arg(m_pages.size())
                                     : tr("kicad-cli could not draw the project: %1").arg(ok?tr("no pages"):output.right(300)));
    });
}

void KicadController::readReport(const QString &path,const QString &title) {
    QFile f(path);
    const QJsonObject o=f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject{};
    m_report.clear();
    int errors=0,warnings=0;
    auto add=[&](const QJsonArray &list,const QString &where){
        for(const auto &v:list) {
            const QJsonObject x=v.toObject();
            const QString severity=x["severity"].toString();
            errors+=severity=="error";warnings+=severity=="warning";
            if(m_report.size()>=kMaxReport) continue;
            QStringList items;
            for(const auto &i:x["items"].toArray()) items.append(i.toObject()["description"].toString());
            m_report.append(QVariantMap{{"severity",severity},{"type",x["type"].toString()},{"description",x["description"].toString()},
                                        {"where",(where.isEmpty()||where=="/"?QString():where+": ")+items.join("; ")}});
        }
    };
    for(const auto &sheet:o["sheets"].toArray()) add(sheet.toObject()["violations"].toArray(),sheet.toObject()["path"].toString());
    for(const char *key:{"violations","unconnected_items","schematic_parity"}) add(o[QLatin1String(key)].toArray(),QString());
    if(o.isEmpty()) {setStatus(tr("%1 produced no report.").arg(title));return;}
    m_reportTitle=errors+warnings==0 ? tr("%1: no problems found").arg(title) : tr("%1: %2 errors, %3 warnings").arg(title).arg(errors).arg(warnings);
    setStatus(m_reportTitle);
}
void KicadController::runErc() {
    if(!hasSchematic()) {setStatus(tr("The project has no schematic."));return;}
    if(!checksAvailable()) {setStatus(tr("ERC from Jarvis needs KiCad 8 or newer (found %1). Run it in KiCad: Inspect → Electrical Rules Checker.").arg(cliVersion()));return;}
    const QString out=previewDir()+"/"+name()+"-erc.json";
    QFile::remove(out);QDir().mkpath(previewDir());
    setStatus(tr("Checking the schematic (ERC)…"));
    run({"sch","erc","--format","json","--severity-all","-o",out,schematic()},[this,out](bool,const QString &){readReport(out,QStringLiteral("ERC"));});
}
void KicadController::runDrc() {
    if(!hasBoard()) {setStatus(tr("The project has no board file."));return;}
    if(!checksAvailable()) {setStatus(tr("DRC from Jarvis needs KiCad 8 or newer (found %1). Run it in KiCad: Inspect → Design Rules Checker.").arg(cliVersion()));return;}
    const QString out=previewDir()+"/"+name()+"-drc.json";
    QFile::remove(out);QDir().mkpath(previewDir());
    QStringList args{"pcb","drc","--format","json","--severity-all","-o",out};
    if(hasSchematic()) args.append("--schematic-parity");
    args.append(board());
    setStatus(tr("Checking the board (DRC)…"));
    run(args,[this,out](bool,const QString &){readReport(out,QStringLiteral("DRC"));});
}
QString KicadController::reportQuestion() const {
    if(m_report.isEmpty() && m_reportTitle.isEmpty()) return {};
    QString text=(m_english ? QStringLiteral("KiCad %1. Explain what each problem means for this circuit and how to fix it, most important first.")
                            : QStringLiteral("KiCad %1. Объясни, что значит каждая проблема для этой схемы и как её исправить, начиная с самых важных.")).arg(m_reportTitle)+"\n";
    int shown=0;
    for(const QVariant &v:m_report) {
        const QVariantMap r=v.toMap();
        const QString line="- ["+r["severity"].toString()+"] "+r["description"].toString()+(r["where"].toString().isEmpty()?QString():" — "+r["where"].toString())+"\n";
        if(text.size()+line.size()>3800) break; // a chat message holds 4096 characters
        text+=line;++shown;
    }
    if(shown<m_report.size()) text+=(m_english?QStringLiteral("… and %1 more."):QStringLiteral("… и ещё %1.")).arg(m_report.size()-shown);
    return text;
}

// ---- Exports ----------------------------------------------------------------------------------

void KicadController::exportBom() {
    if(!hasSchematic()) {setStatus(tr("The project has no schematic."));return;}
    const QString out=ensureOutput()+"/"+name()+"-bom.csv";
    auto save=[this,out](const QJsonArray &parts){
        if(parts.isEmpty()) {setStatus(tr("No parts were found in the schematic."));return;}
        setStatus(writeFile(out,jarvis::SchematicLibrary::bomCsv(parts).toUtf8()) ? tr("Bill of materials: %1").arg(out) : tr("Could not save %1.").arg(out));
    };
    if(!cliAvailable()) { // without KiCad: the root sheet's symbols
        QFile f(schematic());
        save(f.open(QIODevice::ReadOnly) ? jarvis::Attachments::kicadParts(f.readAll()) : QJsonArray{});
        return;
    }
    const QString net=previewDir()+"/"+name()+"-bom.net";
    QDir().mkpath(previewDir());
    setStatus(tr("Collecting the parts…"));
    run({"sch","export","netlist","--format","kicadsexpr","-o",net,schematic()},[net,save](bool,const QString &){
        QFile f(net);
        save(f.open(QIODevice::ReadOnly) ? jarvis::Attachments::kicadParts(f.readAll()) : QJsonArray{});
    });
}
void KicadController::exportNetlist() {
    if(!hasSchematic()) {setStatus(tr("The project has no schematic."));return;}
    const QString out=ensureOutput()+"/"+name()+".net";
    run({"sch","export","netlist","--format","kicadsexpr","-o",out,schematic()},[this,out](bool ok,const QString &output){
        setStatus(ok && QFileInfo::exists(out) ? tr("Netlist: %1").arg(out) : tr("kicad-cli failed: %1").arg(output.right(300)));});
}
void KicadController::exportPdf() {
    if(!hasSchematic()) {setStatus(tr("The project has no schematic."));return;}
    const QString out=ensureOutput()+"/"+name()+"-schematic.pdf";
    run({"sch","export","pdf","-o",out,schematic()},[this,out](bool ok,const QString &output){
        setStatus(ok && QFileInfo::exists(out) ? tr("Schematic PDF: %1").arg(out) : tr("kicad-cli failed: %1").arg(output.right(300)));});
}
void KicadController::exportFabrication() {
    if(!hasBoard()) {setStatus(tr("The project has no board file."));return;}
    const QString dir=ensureOutput()+"/fabrication/";
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    setStatus(tr("Making Gerber and drill files…"));
    auto failed=std::make_shared<QString>();
    auto check=[failed](bool ok,const QString &output){if(!ok && failed->isEmpty()) *failed=output.right(300);};
    // Only what a board house needs: every copper layer of the board, mask, paste, silkscreen and the outline.
    QStringList layers;
    QFile pcb(board());
    if(pcb.open(QIODevice::ReadOnly)) {
        static const QRegularExpression copper(QStringLiteral("\\(\\s*\\d+\\s+\"([^\"]+\\.Cu)\""));
        auto it=copper.globalMatch(QString::fromUtf8(pcb.read(256*1024)));
        while(it.hasNext()) layers.append(it.next().captured(1));
    }
    layers.removeDuplicates();
    if(layers.isEmpty()) layers={"F.Cu","B.Cu"};
    layers+=QStringList{"F.Mask","B.Mask","F.Paste","B.Paste","F.Silkscreen","B.Silkscreen","Edge.Cuts"};
    run({"pcb","export","gerbers","-o",dir,"--layers",layers.join(','),board()},check);
    run({"pcb","export","drill","-o",dir,board()},check);
    run({"pcb","export","pos","--format","csv","--units","mm","-o",dir+name()+"-pos.csv",board()},[this,dir,failed,check](bool ok,const QString &output){
        check(ok,output);
        const int files=int(QDir(dir).entryList(QDir::Files).size());
        setStatus(failed->isEmpty() ? tr("Fabrication files (%1): %2 — zip the folder for the board house.").arg(files).arg(dir)
                                    : tr("kicad-cli failed: %1").arg(*failed));
    });
}
void KicadController::openInKicad() {
    struct Tool { QString file; const char *program; };
    for(const Tool &t:{Tool{projectFile(),"kicad"},Tool{schematic(),"eeschema"},Tool{board(),"pcbnew"}}) {
        if(t.file.isEmpty()) continue;
        const QString program=QStandardPaths::findExecutable(t.program);
        if(!program.isEmpty() && QProcess::startDetached(program,{t.file})) {setStatus(tr("Opening %1 in KiCad…").arg(QFileInfo(t.file).fileName()));return;}
    }
    if(!m_file.isEmpty() && QDesktopServices::openUrl(QUrl::fromLocalFile(m_file))) return;
    setStatus(tr("KiCad is not installed: sudo apt install kicad"));
}
void KicadController::openOutputFolder() {
    if(m_file.isEmpty()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(ensureOutput()));
}
