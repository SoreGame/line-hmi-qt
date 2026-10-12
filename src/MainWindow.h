#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "LineController.h"
#include "PlcClient.h"

#include <QDateTime>
#include <QFile>
#include <QFutureWatcher>
#include <QMainWindow>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <optional>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class EstopOverlay;
class NatsClient;
class OverlayCard;
class PlaceOverlay;
class PrepOverlay;
class QGraphicsView;
class QLabel;
class QMessageBox;
class QResizeEvent;
class QShowEvent;
class QTimer;
class QVBoxLayout;

// Главный экран. Показывает кадр ПЛК и передаёт на ПЛК запросы оператора;
// решения принимает ПЛК, скрипты робота ведёт LineController.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void on_btnStart_clicked();
    void on_btnStop_clicked();
    void on_btnHistory_clicked();
    void on_btnProg1_clicked();
    void on_btnProg2_clicked();
    void on_btnAdmin_clicked();
    void on_btnService_clicked();
    void on_btnExportLog_clicked();

private:
    enum class LogLevel { Info, Ok, Warn, Err };

    struct LogEntry {
        QDateTime time;
        LogLevel level = LogLevel::Info;
        QString message;
    };

    // Запрос на ПЛК, результат которого виден только по кадру.
    struct PendingRequest {
        QString name;
        std::function<bool()> accepted;
        // Почему ПЛК мог не принять запрос; пустая строка — причина неизвестна.
        std::function<QString()> reason;
    };

    void onPlcFrame();
    void refreshUi();
    void sendRequest(const QByteArray &frame, const QString &name, std::function<bool()> accepted,
                     std::function<QString()> reason = {});
    void showNotice(const QString &title, const QString &message);
    void onRequestTimeout();
    void selectProgram(int program);
    void syncOverlays();
    void logModeChange();

    void applyProgramVisuals();
    void applyModeVisuals();
    void updateNatsStatusIndicator();
    void updatePlcIndicators();
    void updateModuleIndicators();
    void setModuleIndicator(QLabel *label, bool alive);
    void setupChecklistPanel();
    void updateChecklist();
    struct ReadinessItem {
        bool done;
        QString label;
    };
    // Пункты готовности из панели слева; «Старт» доступен, только когда все выполнены.
    QList<ReadinessItem> readinessItems() const;
    QStringList startBlockers() const;
    // Что ПЛК видел в кадре, когда не принял «Старт»: ячейки условий старта и опрос пульта.
    QString startRejectDetails() const;
    // Изменения ячеек кадра ПЛК — только в файл сессии, не на экран.
    void logBufferChanges();
    void appendLogFileOnly(LogLevel level, const QString &message);
    void updateOutputCounters();
    void updatePowerOff();
    void updateLamellaType();
    void updateClock();
    void updateKpi();

    void appendLog(LogLevel level, const QString &message);
    void appendLogToSessionFile(const LogEntry &entry);
    bool openSessionLog();
    void syncSessionLogToDisk();
    QString logLevelTag(LogLevel level) const;
    QColor logLevelColor(LogLevel level) const;
    bool exportLogsToFile(const QString &path, QString *errorOut = nullptr) const;

    void setupAndroidCanvas();
    void fitAndroidCanvas();
    void enableKeepScreenOn();
    void applyConnectionSettings(bool notifyOnError);
    void beginNatsConnect(bool notifyOnError, bool quiet = false);
    void startPlcClient();

    bool plcLinked() const;
    PlcClient::Mode plcMode() const;
    bool stopRequested() const;
    bool needsPlacement() const;
    // Режим 2 или ПЛК уже в «Готов», а скрипт робота ещё доигрывается.
    bool finishingWork() const;
    // Режим 3 «в работе» или 2 «остановка»: робот ещё может работать, клапаны сварки открыты.
    bool lineRunning() const;

    Ui::MainWindow *ui = nullptr;
    NatsClient *m_nats = nullptr;
    PlcClient *m_plc = nullptr;
    LineController *m_line = nullptr;

    QPointer<PrepOverlay> m_prep;
    QPointer<EstopOverlay> m_estop;
    QPointer<PlaceOverlay> m_place;
    QPointer<OverlayCard> m_stopPopup;

    std::optional<PendingRequest> m_pending;
    // Деталь, выбор которой ждёт подтверждения ПЛК (54); 0 — не ждём.
    int m_pendingProgram = 0;
    QTimer *m_requestTimer = nullptr;
    QPointer<QMessageBox> m_rejectBox;

    QFutureWatcher<QString> *m_connectWatcher = nullptr;
    bool m_reconnectPending = false;
    bool m_notifyOnConnectError = false;
    bool m_natsFailureLogged = false;
    bool m_androidCanvasReady = false;

    QString m_natsUrl;
    QString m_plcHost;
    quint16 m_plcPort = 2025;
    int m_plcStatusBytes = PlcClient::kDefaultStatusSize;

    QGraphicsView *m_androidView = nullptr;

    QVBoxLayout *m_checklistLayout = nullptr;
    QStringList m_lastChecklistTasks;

    QVector<LogEntry> m_logEntries;
    QString m_sessionLogPath;
    QFile m_sessionLogFile; // держим открытым; каждая запись сразу на диск (flush+fsync)

    bool m_moduleStateInitialized = false;
    bool m_prevNatsOk = false;
    bool m_prevCtrlOk = false;
    bool m_prevBehaviourOk = false;
    bool m_prevVision1Ok = false;
    bool m_prevVision2Ok = false;
    bool m_prevPlcOk = false;
    int m_loggedMode = -1;
    int m_loggedProgram = -1;
    int m_loggedPrepDone = -1;
    // Последний залогированный кадр ПЛК; пусто — следующий кадр пишется снимком.
    QVector<qint16> m_loggedBuffer;
};

#endif // MAINWINDOW_H
