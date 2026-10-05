#ifndef PREPOVERLAY_H
#define PREPOVERLAY_H

#include <QFutureWatcher>
#include <QWidget>

#include <atomic>
#include <memory>
#include <mutex>

class NatsClient;
class PlcClient;
class QLabel;
class QPushButton;
class QTimer;

// Окно поверх пульта: предподготовка после «Старт», до основной программы.
// Список сам начинает выполняться. Жёлтый — в процессе, зелёный — готово.
// Когда всё готово — обратный отсчёт и автоматический запуск основной программы.
class PrepOverlay : public QWidget
{
    Q_OBJECT

public:
    enum class Program { Detail1, Detail2 };
    enum class Note { Info, Ok, Warn, Err };

    PrepOverlay(Program program, const QString &initFilename, int countdownSec,
                NatsClient *nats, PlcClient *plc, QWidget *parent = nullptr);

    void onScriptStatus(bool running, bool completed, const QString &filename);
    void onPlcState();

signals:
    void note(Note level, const QString &message);
    void startMainRequested();
    void dismissed();
    void emergencyStopRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    enum class Phase { Running, Countdown, Homing, Exit };
    enum class TaskState { Pending, Active, Done, Error };

    void setTask(int index, TaskState state);
    void refreshButtons();
    void beginCountdown();
    void onCountdownTick();
    void finishCountdown();
    void stopCountdown();
    void considerLaunchPreprog();
    void publishScript(const QString &filename, const QString &inlineCode, bool fromKv);
    void beginSmoothStop();
    void beginEmergencyStop();
    void powerOffRobot(const QString &doneMessage, Note level);
    void failStop(const QString &message);

    Program m_program = Program::Detail1;
    QString m_initFilename;
    int m_countdownSec = 0;
    int m_countdownLeft = 0;
    QTimer *m_countdownTimer = nullptr;
    NatsClient *m_nats = nullptr;
    PlcClient *m_plc = nullptr;

    Phase m_phase = Phase::Running;
    TaskState m_tasks[4] = {
        TaskState::Active, TaskState::Active, TaskState::Pending, TaskState::Active};
    bool m_preprogStarted = false;
    QString m_waitFilename;
    bool m_sawScriptRunning = false;

    std::shared_ptr<std::mutex> m_gate = std::make_shared<std::mutex>();
    std::shared_ptr<std::atomic<int>> m_epoch = std::make_shared<std::atomic<int>>(1);

    QFutureWatcher<QString> *m_scriptWatcher = nullptr;
    QFutureWatcher<QString> *m_stopWatcher = nullptr;

    QLabel *m_status = nullptr;
    QLabel *m_rows[4] = {};
    QLabel *m_countdownLabel = nullptr;
    QPushButton *m_btnStop = nullptr;
    QPushButton *m_btnEmergency = nullptr;
};

#endif // PREPOVERLAY_H
