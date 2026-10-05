#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QByteArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QMainWindow>
#include <QFutureWatcher>
#include <QPointer>
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
class PrepOverlay;
class RecoveryOverlay;
class QCloseEvent;
class QGraphicsView;
class QLabel;
class QResizeEvent;
class QShowEvent;
class QTimer;
class QVBoxLayout;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void on_btnStart_clicked();
    void on_btnStop_clicked();
    void on_btnEmergencyStop_clicked();
    void on_btnProg1_clicked();
    void on_btnProg2_clicked();
    void on_btnAdmin_clicked();
    void on_btnService_clicked();
    void on_btnExportLog_clicked();

private:
    enum class Mode { Ready, Run, Recovery };
    enum class Program { Detail1, Detail2 };
    enum class LogLevel { Info, Ok, Warn, Err };
    // Очередь кадров на ПЛК: commandFinished приходит в том же порядке.
    enum class PlcAck { Program, Start, Control };

    struct LogEntry {
        QDateTime time;
        LogLevel level = LogLevel::Info;
        QString message;
    };

    void refreshUi();
    void selectProgram(Program program);
    void sendPlcControl(int code);
    void applyProgramVisuals();
    void applyModeVisuals();
    void updateNatsStatusIndicator();
    void updatePlcIndicators();
    void updateClock();
    void updateUptime();
    void startUptime();
    void resetUptime();
    void updateCycleTime();
    void startCycleTimer();
    void resetCycleTimer();
    void finishCycle();
    void updateOutputCounters();
    void registerGood();
    void registerScrap();
    void onScriptStatus(bool running, bool completed, int line, const QString &filename);
    void openPrepOverlay();
    void updateModuleIndicators();
    void setModuleIndicator(QLabel *label, bool alive);
    void setupChecklistPanel();
    void updateLeftPanel();
    void updateStartupChecklist();
    void updateSuspicionWarnings();
    bool modulesReady() const;
    bool plcReady() const;
    bool bufferReady() const;
    // E-stop на ПЛК: 30 в int16[50] или зажатый грибок в int16[51].
    bool plcEstopPressed() const;
    bool plcEstopHeld() const;
    bool canStartNow(QString *reason = nullptr) const;
    bool canPowerOff() const;
    void syncPowerOffLatch();
    // Кнопки корпуса (int16[50] ПЛК) — те же действия, что кнопки на экране.
    void handlePanelButtons();
    void panelStopPressed();
    void panelStartPressed();
    void maybeEnterRecoveryFromPlc();
    void forceLeaveRecovery();
    void enterRecovery(const QString &reason);
    void openRecoveryOverlay();
    void onRecoveryFinished();
    void closePrepOverlay();

    void appendLog(LogLevel level, const QString &message);
    void appendLogToSessionFile(const LogEntry &entry);
    bool openSessionLog();
    void syncSessionLogToDisk();
    QString logLevelTag(LogLevel level) const;
    QColor logLevelColor(LogLevel level) const;
    bool exportLogsToFile(const QString &path, QString *errorOut = nullptr) const;

    // NATS: старт/стоп скрипта, привязанного к выбранной детали.
    QString scriptFilenameForProgram(Program p) const;
    /// freshSession=true: сброс uptime (ручной Старт). false: следующий цикл после auto-restart.
    void startSelectedProgramScript(int fromLine = 1, bool freshSession = true);
    void sendStopCommandAsync(const QString &reason, LogLevel logLevel = LogLevel::Info,
                              bool countScrap = false);
    void sendStopThenRecover(const QString &reason);

    void setupAndroidCanvas();
    void fitAndroidCanvas();
    void enableKeepScreenOn();
    void applyConnectionSettings(bool notifyOnError);
    void beginNatsConnect(bool notifyOnError, bool quiet = false);
    void startPlcClient();

    Ui::MainWindow *ui = nullptr;
    Mode m_mode = Mode::Ready;
    Program m_program = Program::Detail1;

    NatsClient *m_nats = nullptr;
    PlcClient *m_plc = nullptr;
    QPointer<PrepOverlay> m_prep;
    QPointer<RecoveryOverlay> m_recovery;
    bool m_prepArming = false;
    QVector<PlcAck> m_plcAcks;
    // Стоп основной программы: дожать текущий цикл и не запускать следующий.
    bool m_stopAfterCycle = false;
    QFutureWatcher<QString> *m_execWatcher = nullptr;
    QFutureWatcher<QString> *m_stopWatcher = nullptr;
    QFutureWatcher<QString> *m_connectWatcher = nullptr;
    bool m_reconnectPending = false;
    bool m_notifyOnConnectError = false;
    bool m_natsFailureLogged = false;
    bool m_androidCanvasReady = false;

    QString m_natsUrl;
    QString m_plcHost;
    quint16 m_plcPort = 1502;
    QByteArray m_plcReadyMask;
    QByteArray m_plcReadyValue;
    int m_plcStatusBytes = 120;

    QGraphicsView *m_androidView = nullptr;

    // Uptime текущего запуска: тикает в Run, сброс на Stop / EMERGENCY STOP.
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

    QVBoxLayout *m_checklistLayout = nullptr;
    QVector<QLabel *> m_checklistItems;
    QStringList m_lastChecklistTasks;
    enum class LeftPanelKind { Startup, Warnings };
    LeftPanelKind m_leftPanelKind = LeftPanelKind::Startup;

    QVector<LogEntry> m_logEntries;
    QString m_sessionLogPath;
    QFile m_sessionLogFile; // держим открытым; каждая запись сразу на диск (flush+fsync)
    bool m_prevNatsOk = false;
    bool m_prevCtrlOk = false;
    bool m_prevBehaviourOk = false;
    bool m_prevVision1Ok = false;
    bool m_prevVision2Ok = false;
    bool m_prevPlcLinked = false;
    bool m_prevPlcOk = false;
    qint16 m_prevPanelValue = 0;
    bool m_panelValueKnown = false;
    bool m_prevEstopActive = false;
    // Аварийный стоп запрошен, окно восстановления ещё не открыто.
    // Пока стоит — exec/stop не переводят пульт в Run/Ready.
    bool m_estopPending = false;
    QString m_estopPendingReason;
    // После 7 5: ждём, пока ПЛК снимет 30 в int16[50] и грибок отжат. Старт заблокирован.
    bool m_awaitEstopClear = false;
    QTimer *m_estopClearTimer = nullptr;
    bool m_moduleStateInitialized = false;
    bool m_initReadyLogged = false;
};

#endif // MAINWINDOW_H
