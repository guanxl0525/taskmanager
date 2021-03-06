#include "genericstatscore.h"
#include <QSqlQuery>
#include <QSqlTableModel>
#include <QStringListModel>
#include <QProcess>
#include <QDebug>

GenericStatsCore::GenericStatsCore(int msec, QObject *parent)
    :StatsCore (parent)
{
    // connect timer to update functions
    connect(&this->refreshTimer_, &QTimer::timeout, this, &GenericStatsCore::updateProcesses);
    connect(&this->refreshTimer_, &QTimer::timeout, this, &GenericStatsCore::updateSystemInfo);

    // create a in-memory sqlite database to store all information
    this->database_ = QSqlDatabase::addDatabase("QSQLITE");
    this->database_.setDatabaseName(":memory:");
    this->database_.open();
    // create process info table
    this->database_.exec("CREATE TABLE `process` (\
                         `name`	TEXT,\
                         `pid`	INTEGER UNIQUE,\
                         `cpu`	REAL,\
                         `memory`	INTEGER,\
                         `disk`	INTEGER,\
                         `network`	INTEGER,\
                         PRIMARY KEY(`pid`)\
                     );");
    // create and set a QSqlTableModel for GUI
    this->processModel_ = new QSqlTableModel(this, this->database_);
    this->processModel_->setTable("process");
    this->processModel_->select();

    // create empty system info model
    this->systemModel_ = new QStringListModel(this);
    QStringList infos;
    for(int i = 0; i < StatsCore::DynamicSystemField::TotalDyanamicProperties; i ++)
        infos << QString::null;
    this->systemModel_->setStringList(infos);
    for(int i = 0; i < StatsCore::StaticSystemField::TotalStaticProperties; i ++)
        this->staticSystemInfo_ << QString::null;

    // start timer
    this->refreshTimer_.start(msec);
}

void GenericStatsCore::setRefreshRate(int msec)
{
    this->refreshTimer_.setInterval(msec);
}

GenericStatsCore::~GenericStatsCore()
{
    emit this->shuttingDown();
    delete this->processModel_;
    delete this->systemModel_;
}

QAbstractItemModel *GenericStatsCore::processModel()
{
    this->updateProcesses();
    return static_cast<QAbstractItemModel *>(this->processModel_);
}

void GenericStatsCore::updateProcesses()
{
    // update the processes based on a `ps` implementation
    // initialize the process object and connect to a function that updates the database
    static QProcess *process = nullptr;
    if(process == nullptr)
    {
        process = new QProcess();
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), [=] {
            QString psOutput = process->readAllStandardOutput();
            QStringList processList = psOutput.split('\n');
            // remove the first line (title) and last line (blank line)
            processList.removeFirst();
            processList.removeLast();

            // begin transaction
            this->database_.transaction();
            QSqlQuery query(this->database_);

            // clear process table data
            query.exec("DELETE from process;");

            for(const QString &process: processList)
            {
                quint64 pid = process.section(' ', 0, 0, QString::SectionSkipEmpty).toULongLong();
                double cpuPercent = process.section(' ', 1, 1, QString::SectionSkipEmpty).toDouble();
                quint64 memory = process.section(' ', 2, 2, QString::SectionSkipEmpty).toULongLong();
                QString name = process.section(' ', 3, -1, QString::SectionSkipEmpty);
                name = name.mid(name.lastIndexOf('/') + 1);
                query.prepare("INSERT INTO process (pid, name, cpu, memory, disk, network)"
                              "VALUES (:pid, :name, :cpu, :memory, :disk, :network)");
                query.bindValue(":pid", pid);
                query.bindValue(":name", name);
                query.bindValue(":cpu", cpuPercent);
                query.bindValue(":memory", memory * 1024);
                query.bindValue(":disk", 0);
                query.bindValue(":network", 0);
                query.exec();
            }
            this->database_.commit();
            qDebug() << "Updated" << processList.size() << "processes.";
            this->processModel_->select();
        });
        connect(this, &StatsCore::shuttingDown, [=]{
            disconnect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                    nullptr, nullptr);
            process->kill();
            process->waitForFinished();
            delete process;
        });
    }
    // if the last update is still running
    if (process->state() == QProcess::Running)
        return;
    // else start updating
    else
        process->start("ps axo pid,%cpu,rss,comm");
}

QStringList GenericStatsCore::staticInformation()
{
    this->gatherStaticInformation();
    return this->staticSystemInfo_;
}

void GenericStatsCore::killProcess(quint64 pid)
{
    // a command based implementation
    static QProcess *process = new QProcess();
    connect(this, &StatsCore::shuttingDown, [=] {
       disconnect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
               nullptr, nullptr);
       process->waitForFinished();
       delete process;
    });
    process->start("kill", {"-s", "KILL", QString::number(pid)});
    qDebug() << "Killed " << pid;
}

QAbstractItemModel *GenericStatsCore::systemModel()
{
    this->updateSystemInfo();
    return static_cast<QAbstractItemModel *>(this->systemModel_);
}
