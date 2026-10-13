#ifndef LINECONTROLLER_H
#define LINECONTROLLER_H

#include "ConnectionSettings.h"
#include "PlcClient.h"
#include "RunHistory.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>

class NatsClient;
class QTimer;

// Связка ПЛК и скриптов робота без UI (docs/PLC_PROTOCOL.md, раздел 6).
// Запускает init по изменению ячейки 60 и основной скрипт, когда ПЛК закончил
// предподготовку (ячейка 56), после паузы 5 с, отчитывается 7 7 R S, перезапускает цикл, шлёт 7 8 в «Остановке»,
// сообщает ПЛК флаги робота и зону, останавливает робота при потере ПЛК.
class LineController : public QObject
{
    Q_OBJECT
public:
    enum class Level { Info, Ok, Warn, Err };
    enum class InitState { Idle, Running, Done, Failed };

    LineController(NatsClient *nats, PlcClient *plc, QObject *parent = nullptr);
    ~LineController() override;

    void setSettings(const ConnectionSettings &cfg);

    InitState initState() const { return m_initState; }
    // Секунд до exec основного скрипта; 0 — таймер не идёт.
    int mainDelayRemainingSec() const;
    qint64 lastCycleMs() const { return m_lastCycleMs; }
    // Время текущего запуска линии (от входа в «В работе»), -1 — запуска нет.
    qint64 runElapsedMs() const;
    int zone() const { return m_report.zone; }
    // То, что пульт шлёт ПЛК в опросе 203: флаги робота и зона.
    const PlcClient::RobotReport &report() const { return m_report; }
    // Свежая поза робота от CTRL; без неё zone() = 0 не значит «робот вне зоны».
    bool poseKnown() const { return m_poseFresh; }
    // x, y, z, r последней позы; смысл имеет только при poseKnown().
    std::array<double, 4> pose() const { return {m_pose[0], m_pose[1], m_pose[2], m_pose[3]}; }
    const ConnectionSettings &settings() const { return m_cfg; }
    bool plcLinkLost() const { return m_linkLost; }
    bool scriptActive() const { return m_role != Role::None; }
    // Робот выполняет скрипт: запущенный пультом или любой другой по script.status.
    bool robotBusy() const { return m_role != Role::None || m_report.scriptRunning; }
    // Причины аварии, видимые по кадру ПЛК и состоянию робота. Зона сюда не входит.
    QStringList alarmCauses() const;

signals:
    void note(LineController::Level level, const QString &message);
    void initStateChanged();
    void mainDelayChanged();
    void cycleFinished(qint64 ms);
    void runFinished(const RunRecord &record);

private:
    enum class Role { None, Init, Main };

    void onPlcFrame();
    void onScriptStatus(bool running, bool completed, int line, const QString &filename);
    void onPose(double x, double y, double z, double r);
    void tick();

    void baseline();
    void startScript(Role role, int program);
    void requestMain(int program);
    void cancelMainDelay();
    void finishMainDelay();
    void finishScript(bool completed);
    void sendStopDone();
    void stopRobot(const QString &why);
    // Полный стоп текущему скрипту; без подтверждения он считается законченным через kAbortConfirmMs.
    void abortScript(const QString &why);
    void checkStopTimeouts();
    void setInitState(InitState state);
    void updateReport();

    void trackRun(PlcClient::Mode mode);
    void endRun(const QString &reason);

    NatsClient *m_nats = nullptr;
    PlcClient *m_plc = nullptr;
    ConnectionSettings m_cfg;
    QTimer *m_timer = nullptr;

    bool m_seen = false;
    bool m_linkLost = false;
    PlcClient::Mode m_prevMode = PlcClient::Mode::Boot;
    qint16 m_prevInitPermit = 0;
    int m_prevProgram = 0;
    bool m_prevPrepDone = false;

    Role m_role = Role::None;
    QString m_filename;
    bool m_execInFlight = false;
    bool m_sawRunning = false;
    bool m_abortSent = false;
    // Последний скрипт робота не дошёл до конца не по стопу; сбрасывается новым запуском
    // или выходом ПЛК из аварии.
    bool m_scriptFailed = false;
    QElapsedTimer m_cycleTimer;

    bool m_awaitDecision = false;
    quint64 m_decisionTicket = 0;
    bool m_lastMainCompleted = false;

    bool m_stopDoneSent = false;
    QElapsedTimer m_stopDoneTimer;
    // Режим «Остановка» во время цикла: с какого момента ждём его конца.
    QElapsedTimer m_stopWaitTimer;
    // Роботу отправлен полный стоп: с какого момента ждём подтверждения в script.status.
    QElapsedTimer m_abortTimer;

    InitState m_initState = InitState::Idle;
    // Отсчёт 5 с перед exec основного скрипта.
    QElapsedTimer m_mainDelay;
    int m_pendingMainProgram = 0;
    qint64 m_lastCycleMs = -1;

    PlcClient::RobotReport m_report;
    bool m_scriptRunning = false;
    bool m_poseKnown = false;
    bool m_poseFresh = false;
    qint64 m_poseMs = 0;
    double m_pose[4] = {0, 0, 0, 0};

    std::optional<RunRecord> m_run;
    QElapsedTimer m_runTimer;
    qint16 m_runGoodStart = 0;
    qint16 m_runScrapStart = 0;
    bool m_runStopSeen = false;
};

#endif // LINECONTROLLER_H
