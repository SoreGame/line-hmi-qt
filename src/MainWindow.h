#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QMainWindow>
#include <QFutureWatcher>
#include <QString>
#include <QStringList>
#include <QVector>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class NatsClient;
class PlcClient;
class QLabel;
class QVBoxLayout;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void on_btnStart_clicked();
    void on_btnPause_clicked();
    void on_btnStop_clicked();
    void on_btnProg1_clicked();
    void on_btnProg2_clicked();
    void on_btnAdmin_clicked();
    void on_btnExportLog_clicked();

private:
    enum class Mode { Ready, Run, Pause };
    enum class Program { Detail1, Detail2 };
    enum class LogLevel { Info, Ok, Warn, Err };

    struct LogEntry {
        QDateTime time;
        LogLevel level = LogLevel::Info;
        QString message;
    };

    void refreshUi();
    void applyProgramVisuals();
    void applyModeVisuals();
    void updateNatsStatusIndicator();
    void updatePlcIndicators();
    void updateClock();
    void updateUptime();
    void startUptime();
    void pauseUptime();
    void resetUptime();
    void updateCycleTime();
    void startCycleTimer();
    void pauseCycleTimer();
    void resetCycleTimer();
    void finishCycle();
    void updateOutputCounters();
    void registerGood();
    void registerScrap();
    void onScriptStatus(bool running, bool completed, int line);
    void updateModuleIndicators();
    void setModuleIndicator(QLabel *label, bool alive);
    void setupChecklistPanel();
    void updateStartupChecklist();
    bool modulesReady() const;
    bool plcReady() const;

    void appendLog(LogLevel level, const QString &message);
    void appendLogToSessionFile(const LogEntry &entry);
    bool openSessionLog();
    void syncSessionLogToDisk();
    QString logLevelTag(LogLevel level) const;
    QColor logLevelColor(LogLevel level) const;
    bool exportLogsToFile(const QString &path, QString *errorOut = nullptr) const;

    // NATS: старт/пауза/стоп скрипта, привязанного к выбранной детали.
    QString scriptFilenameForProgram(Program p) const;
    /// freshSession=true: сброс uptime (ручной Старт). false: следующий цикл после auto-restart.
    void startSelectedProgramScript(int fromLine = 1, bool freshSession = true);
    void resumeSelectedProgramScript();
    void sendStopCommandAsync(const QString &reason, bool resetToReady);

    Ui::MainWindow *ui = nullptr;
    Mode m_mode = Mode::Ready;
    Program m_program = Program::Detail1;

    NatsClient *m_nats = nullptr;
    PlcClient *m_plc = nullptr;
    QFutureWatcher<QString> *m_execWatcher = nullptr;
    QFutureWatcher<QString> *m_stopWatcher = nullptr;

    // Uptime текущего запуска: тикает в Run, замирает на Pause, сброс на Stop.
    qint64 m_uptimeAccumulatedMs = 0;
    QElapsedTimer m_uptimeTick;
    bool m_uptimeRunning = false;

    // Замер текущего цикла (не показывается в KPI). В KPI — только m_lastCycleMs.
    qint64 m_cycleAccumulatedMs = 0;
    QElapsedTimer m_cycleTick;
    bool m_cycleRunning = false;
    qint64 m_lastCycleMs = -1; // последний завершённый цикл; -1 = ещё не было

    // Выдача (годные) / брак за сессию пульта.
    int m_goodCount = 0;
    int m_scrapCount = 0;

    // Строка, с которой продолжать после паузы (replay interrupted line).
    int m_resumeLine = 1;

    QVBoxLayout *m_checklistLayout = nullptr;
    QVector<QLabel *> m_checklistItems;
    QStringList m_lastChecklistTasks;

    QVector<LogEntry> m_logEntries;
    QString m_sessionLogPath;
    QFile m_sessionLogFile; // держим открытым; каждая запись сразу на диск (flush+fsync)
    bool m_prevNatsOk = false;
    bool m_prevCtrlOk = false;
    bool m_prevBehaviourOk = false;
    bool m_prevPlcOk = false;
    bool m_moduleStateInitialized = false;
};

#endif // MAINWINDOW_H
