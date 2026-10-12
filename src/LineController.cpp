#include "LineController.h"
#include "NatsClient.h"
#include "Texts.h"

#include <QDateTime>
#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

#include <functional>

namespace {

constexpr int kTickMs = 500;
// Нет heartbeat дольше этого — модуль считаем незапущенным.
constexpr qint64 kModuleTimeoutMs = 2000;
// Столько ждём, что ПЛК выйдет из «Остановки» после 7 8, прежде чем повторить.
constexpr qint64 kStopDoneRetryMs = 3000;
constexpr qint64 kAbortConfirmMs = 5000;
constexpr int kCounterModulo = 32768;

bool fresh(qint64 lastMs, qint64 now)
{
    return lastMs > 0 && (now - lastMs) < kModuleTimeoutMs;
}

bool lineActive(PlcClient::Mode mode)
{
    return mode == PlcClient::Mode::Run || mode == PlcClient::Mode::Stopping;
}

int counterDelta(qint16 from, qint16 to)
{
    return ((int(to) - int(from)) % kCounterModulo + kCounterModulo) % kCounterModulo;
}

template <typename Done>
void runAsync(QObject *owner, std::function<QString()> job, Done done)
{
    auto *watcher = new QFutureWatcher<QString>(owner);
    QObject::connect(watcher, &QFutureWatcher<QString>::finished, owner, [watcher, done]() {
        done(watcher->result());
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(std::move(job)));
}

} // namespace

LineController::LineController(NatsClient *nats, PlcClient *plc, QObject *parent)
    : QObject(parent)
    , m_nats(nats)
    , m_plc(plc)
    , m_cfg(ConnectionSettings::load())
    , m_timer(new QTimer(this))
{
    connect(m_plc, &PlcClient::stateChanged, this, &LineController::onPlcFrame);
    connect(m_nats, &NatsClient::scriptStatusReceived, this, &LineController::onScriptStatus);
    connect(m_nats, &NatsClient::motionPoseReceived, this,
            [this](double x, double y, double z, double r, bool) { onPose(x, y, z, r); });
    connect(m_timer, &QTimer::timeout, this, &LineController::tick);
    m_timer->start(kTickMs);
    updateReport();
}

LineController::~LineController()
{
    for (QFutureWatcherBase *watcher : findChildren<QFutureWatcherBase *>())
        watcher->waitForFinished();
}

void LineController::setSettings(const ConnectionSettings &cfg)
{
    m_cfg = cfg;
    updateReport();
}

qint64 LineController::runElapsedMs() const
{
    return m_run ? m_runTimer.elapsed() : -1;
}

void LineController::baseline()
{
    m_seen = true;
    m_prevMode = m_plc->mode();
    m_prevInitPermit = m_plc->statusInt16(PlcClient::kInitPermitIndex);
    m_prevProgram = m_plc->statusInt16(PlcClient::kProgramIndex);
    m_prevPrepDone = m_plc->prepDone();
    m_awaitDecision = false;
}

void LineController::onPlcFrame()
{
    if (!m_plc->isOk())
        return;
    if (m_linkLost) {
        m_linkLost = false;
        emit note(Level::Ok, tx("Связь с ПЛК восстановлена"));
        m_seen = false;
    }
    if (!m_seen) {
        // Переходы, которых пульт не видел сам, ничего не запускают.
        baseline();
        trackRun(m_prevMode);
        return;
    }

    const PlcClient::Mode mode = m_plc->mode();
    const bool stopping = mode == PlcClient::Mode::Stopping;
    const bool prepDone = m_plc->prepDone();
    const qint16 initPermit = m_plc->statusInt16(PlcClient::kInitPermitIndex);
    const int program = m_plc->statusInt16(PlcClient::kProgramIndex);

    if (m_prevMode == PlcClient::Mode::Estop && mode != PlcClient::Mode::Estop)
        m_scriptFailed = false;

    if (program != m_prevProgram) {
        m_prevProgram = program;
        m_lastCycleMs = -1;
    }

    if (initPermit != m_prevInitPermit) {
        if (m_role == Role::None) {
            setInitState(InitState::Idle);
            startScript(Role::Init, program);
        } else {
            emit note(Level::Warn, tx("ПЛК разрешил init, но скрипт робота ещё идёт · init не запущен"));
            m_plc->sendCommand(PlcClient::scriptReportCommand(PlcClient::RoleInit, false));
        }
    }

    if (mode == PlcClient::Mode::Run && prepDone && !m_prevPrepDone) {
        if (m_role == Role::None && !m_awaitDecision)
            startScript(Role::Main, program);
        else
            emit note(Level::Warn, tx("ПЛК закончил предподготовку, но скрипт робота ещё идёт · цикл не запущен"));
    }

    if (m_awaitDecision && m_plc->pollServed() >= m_decisionTicket) {
        m_awaitDecision = false;
        if (stopping) {
            emit note(Level::Info, tx("Стоп · цикл завершён, новый не запускается"));
        } else if (mode != PlcClient::Mode::Run || !prepDone) {
            emit note(Level::Info, tx("ПЛК не в работе · новый цикл не запускается"));
        } else if (!m_lastMainCompleted) {
            emit note(Level::Warn, tx("Скрипт не дошёл до конца · новый цикл не запускается"));
        } else if (m_role == Role::None) {
            startScript(Role::Main, program);
        }
    }

    // ПЛК может уйти в «Готов», не дождавшись 7 8: тогда идущий скрипт доводим как при стопе.
    const bool windingDown = stopping || mode == PlcClient::Mode::Ready;
    if (windingDown && m_role == Role::Init && !m_abortSent)
        abortScript(tx("Стоп во время предподготовки · init остановлен"));

    if (windingDown && m_role == Role::Main) {
        if (!m_stopWaitTimer.isValid())
            m_stopWaitTimer.start();
    } else {
        m_stopWaitTimer.invalidate();
    }

    if (!stopping) {
        m_stopDoneSent = false;
    } else if (m_role == Role::None && !m_awaitDecision
               && (!m_stopDoneSent || m_stopDoneTimer.elapsed() > kStopDoneRetryMs)) {
        sendStopDone();
    }

    trackRun(mode);
    m_prevMode = mode;
    m_prevInitPermit = initPermit;
    m_prevPrepDone = prepDone;
}

void LineController::startScript(Role role, int program)
{
    const QString filename = role == Role::Init ? m_cfg.initScript(program) : m_cfg.mainScript(program);
    const PlcClient::ScriptRole plcRole = role == Role::Init ? PlcClient::RoleInit : PlcClient::RoleMain;
    const QString kind = role == Role::Init ? tx("init") : tx("основной");
    if (filename.isEmpty()) {
        emit note(Level::Err, tx("ПЛК не выбрал программу (ячейка 54 = %1) · %2 скрипт не запущен")
                                  .arg(program)
                                  .arg(kind));
        m_plc->sendCommand(PlcClient::scriptReportCommand(plcRole, false));
        m_plc->pollNow();
        if (role == Role::Init)
            setInitState(InitState::Failed);
        return;
    }

    m_role = role;
    m_filename = filename;
    m_execInFlight = true;
    m_sawRunning = false;
    m_abortSent = false;
    m_scriptFailed = false;
    if (role == Role::Init)
        setInitState(InitState::Running);
    emit note(Level::Info, tx("Запуск скрипта '%1' (%2)").arg(filename, kind));
    updateReport();

    NatsClient *nats = m_nats;
    runAsync(this, [nats, filename]() -> QString {
        QString code;
        QString err;
        if (!nats->isConnected())
            return tx("нет связи с NATS");
        if (!nats->fetchScriptCode(filename, &code, &err))
            return tx("не удалось получить скрипт из NATS KV: %1").arg(err);
        if (!nats->publishExecCommand(filename, code, 1, &err))
            return tx("не удалось отправить exec в NATS: %1").arg(err);
        return QString();
    }, [this, filename](const QString &error) {
        if (m_filename != filename || !m_execInFlight)
            return;
        m_execInFlight = false;
        if (!error.isEmpty()) {
            emit note(Level::Err, tx("Скрипт '%1' не запущен: %2").arg(filename, error));
            finishScript(false);
            return;
        }
        if (m_role == Role::Main)
            m_cycleTimer.start();
        if (m_abortSent)
            abortScript(tx("Стоп пришёл во время запуска init · скрипт остановлен"));
    });
}

void LineController::onScriptStatus(bool running, bool completed, int /*line*/, const QString &filename)
{
    m_scriptRunning = running;
    updateReport();
    if (m_role == Role::None || filename != m_filename)
        return;
    // До ответа на exec statuses о конце относятся к прошлому запуску того же файла.
    if (m_execInFlight && !running)
        return;
    if (running) {
        m_sawRunning = true;
        return;
    }
    if (!m_sawRunning && !completed)
        return;
    finishScript(completed);
}

void LineController::finishScript(bool completed)
{
    const Role role = m_role;
    m_role = Role::None;
    m_filename.clear();
    m_execInFlight = false;
    m_abortTimer.invalidate();
    m_stopWaitTimer.invalidate();
    m_scriptFailed = !completed && !m_abortSent && !m_linkLost
        && m_plc->mode() != PlcClient::Mode::Stopping && m_plc->mode() != PlcClient::Mode::Ready;
    updateReport();

    if (role == Role::Init) {
        setInitState(completed ? InitState::Done : InitState::Failed);
        emit note(completed ? Level::Ok : Level::Warn,
                  completed ? tx("init-скрипт выполнен · ПЛК 7 7 1 1")
                            : tx("init-скрипт не дошёл до конца · ПЛК 7 7 1 0"));
        m_plc->sendCommand(PlcClient::scriptReportCommand(PlcClient::RoleInit, completed));
        m_plc->pollNow();
        return;
    }

    m_lastMainCompleted = completed;
    if (completed && m_cycleTimer.isValid()) {
        m_lastCycleMs = m_cycleTimer.elapsed();
        if (m_run) {
            ++m_run->cycles;
            m_run->totalCycleMs += m_lastCycleMs;
            m_run->lastCycleMs = m_lastCycleMs;
        }
        emit cycleFinished(m_lastCycleMs);
    }
    m_cycleTimer.invalidate();
    emit note(completed ? Level::Ok : Level::Warn,
              completed ? tx("Цикл завершён · ПЛК 7 7 2 1")
                        : tx("Основной скрипт не дошёл до конца · ПЛК 7 7 2 0"));
    m_plc->sendCommand(PlcClient::scriptReportCommand(PlcClient::RoleMain, completed));
    m_decisionTicket = m_plc->pollNow();
    m_awaitDecision = true;
}

void LineController::sendStopDone()
{
    m_stopDoneSent = true;
    m_stopDoneTimer.start();
    emit note(Level::Info, tx("Стоп отработан · ПЛК 7 8"));
    m_plc->sendCommand(PlcClient::controlCommand(PlcClient::CmdStopDone));
    m_plc->pollNow();
}

void LineController::stopRobot(const QString &why)
{
    emit note(Level::Warn, why);
    NatsClient *nats = m_nats;
    runAsync(this, [nats]() -> QString {
        QString err;
        if (!nats->isConnected())
            return tx("нет связи с NATS");
        if (!nats->publishStopCommand(&err))
            return err;
        return QString();
    }, [this](const QString &error) {
        if (!error.isEmpty())
            emit note(Level::Err, tx("Не удалось остановить робота: %1").arg(error));
    });
}

void LineController::abortScript(const QString &why)
{
    m_abortSent = true;
    m_abortTimer.start();
    m_stopWaitTimer.invalidate();
    stopRobot(why);
}

void LineController::checkStopTimeouts()
{
    if (m_role == Role::None)
        return;
    if (m_abortSent && m_abortTimer.isValid() && m_abortTimer.elapsed() > kAbortConfirmMs) {
        m_abortTimer.invalidate();
        emit note(Level::Warn, tx("Робот не подтвердил стоп за %1 с · скрипт считается законченным")
                                   .arg(kAbortConfirmMs / 1000));
        finishScript(false);
        return;
    }
    const qint64 limitMs = qint64(m_cfg.stopTimeoutSec) * 1000;
    if (m_role == Role::Main && !m_abortSent && m_stopWaitTimer.isValid() && limitMs > 0
        && m_stopWaitTimer.elapsed() > limitMs) {
        abortScript(tx("Стоп: цикл не закончился за %1 с · роботу отправлен полный стоп")
                        .arg(m_cfg.stopTimeoutSec));
    }
}

void LineController::tick()
{
    updateReport();
    checkStopTimeouts();
    if (m_linkLost || m_plc->lastOkMs() <= 0 || m_plc->isOk())
        return;
    m_linkLost = true;
    m_awaitDecision = false;
    emit note(Level::Err, tx("Нет связи с ПЛК"));
    stopRobot(tx("Нет связи с ПЛК · роботу отправлен полный стоп"));
    endRun(tx("Нет связи с ПЛК"));
}

void LineController::onPose(double x, double y, double z, double r)
{
    m_pose[0] = x;
    m_pose[1] = y;
    m_pose[2] = z;
    m_pose[3] = r;
    m_poseKnown = true;
    m_poseMs = QDateTime::currentMSecsSinceEpoch();
}

void LineController::updateReport()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    PlcClient::RobotReport report;
    report.natsConnected = m_nats->isConnected();
    report.ctrlAlive = report.natsConnected && fresh(m_nats->lastCtrlMs(), now);
    report.behaviourAlive = report.natsConnected && fresh(m_nats->lastBehaviourMs(), now);
    report.scriptRunning = report.behaviourAlive && (m_scriptRunning || m_role != Role::None);
    m_poseFresh = report.ctrlAlive && m_poseKnown && fresh(m_poseMs, now);
    report.zone = m_poseFresh ? m_cfg.zoneOf(m_pose[0], m_pose[1], m_pose[2], m_pose[3]) : 0;
    m_report = report;
    m_plc->setRobotReport(report);
}

QStringList LineController::alarmCauses() const
{
    QStringList causes;
    const auto flag = [this](int index) { return m_plc->statusInt16(index) == 1; };
    if (m_plc->statusInt16(PlcClient::kMushroomIndex) != PlcClient::kMushroomReleased)
        causes << tx("Отожмите грибок и/или закройте двери");
    if (flag(PlcClient::kNoLamellaeIndex))
        causes << tx("Долго нет ламелей");
    if (flag(PlcClient::kNoHoldersIndex))
        causes << tx("Долго нет держателей ламелей");
    if (flag(PlcClient::kWeldTimeoutIndex))
        causes << tx("Сварка долго не отрабатывает");
    if (flag(PlcClient::kHolderPickIndex))
        causes << tx("Долго не можем взять держатель");
    if (flag(PlcClient::kLamellaPickIndex))
        causes << tx("Долго не можем взять ламели");
    if (m_scriptFailed)
        causes << tx("Скрипт робота не дошёл до конца");
    if (!m_report.natsConnected) {
        causes << tx("Нет связи с NATS");
    } else {
        if (!m_report.ctrlAlive)
            causes << tx("CTRL не отвечает");
        if (!m_report.behaviourAlive)
            causes << tx("Behaviour не отвечает");
    }
    if (!m_plc->vision1Ok())
        causes << tx("Vision 1 не работает");
    if (!m_plc->vision2Ok())
        causes << tx("Vision 2 не работает");
    if (!m_plc->weldValvesSafe())
        causes << tx("Клапаны сварки не в безопасном положении");
    return causes;
}

void LineController::setInitState(InitState state)
{
    if (m_initState == state)
        return;
    m_initState = state;
    emit initStateChanged();
}

void LineController::trackRun(PlcClient::Mode mode)
{
    if (!m_run) {
        if (lineActive(mode) && !lineActive(m_prevMode) && m_prevMode != mode) {
            RunRecord run;
            run.program = m_plc->statusInt16(PlcClient::kProgramIndex);
            run.started = QDateTime::currentDateTime();
            m_run = run;
            m_runTimer.start();
            m_runGoodStart = m_plc->statusInt16(PlcClient::kGoodIndex);
            m_runScrapStart = m_plc->statusInt16(PlcClient::kScrapIndex);
            m_runStopSeen = false;
        }
        return;
    }
    if (mode == PlcClient::Mode::Stopping)
        m_runStopSeen = true;
    if (lineActive(mode))
        return;
    if (mode == PlcClient::Mode::Estop) {
        const QStringList causes = alarmCauses();
        endRun(causes.isEmpty() ? tx("Авария") : tx("Авария · %1").arg(causes.join(QStringLiteral(", "))));
    } else if (m_runStopSeen) {
        endRun(tx("Стоп"));
    } else {
        endRun(tx("ПЛК завершил работу"));
    }
}

void LineController::endRun(const QString &reason)
{
    if (!m_run)
        return;
    RunRecord run = *m_run;
    m_run.reset();
    run.finished = QDateTime::currentDateTime();
    run.good = counterDelta(m_runGoodStart, m_plc->statusInt16(PlcClient::kGoodIndex));
    run.scrap = counterDelta(m_runScrapStart, m_plc->statusInt16(PlcClient::kScrapIndex));
    run.reason = reason;
    RunHistory::append(run);
    emit runFinished(run);
}
