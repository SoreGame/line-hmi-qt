#ifndef NATSCLIENT_H
#define NATSCLIENT_H

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <atomic>
#include <nats.h>

// Тонкая обёртка над nats.c: соединение, чтение сохранённых скриптов
// из NATS KV bucket "behaviour" (тот же bucket, что использует
// веб-редактор ChaiScript) и публикация команды запуска на
// "script.command" в формате, совместимом с редактором.
//
// Методы этого класса блокирующие (используют синхронный API nats.c) —
// вызывайте их из фонового потока (см. MainWindow::startSelectedProgramScript,
// который использует QtConcurrent::run), а не напрямую из UI-слотов.
class NatsClient : public QObject
{
    Q_OBJECT
public:
    explicit NatsClient(QObject *parent = nullptr);
    ~NatsClient() override;

    // url, например: "nats://192.168.11.112:4222"
    bool connectToServer(const QString &url, QString *errorOut = nullptr);
    void disconnectFromServer();
    bool isConnected() const;

    // Открывает KV bucket (JetStream KV), созданный редактором ("behaviour").
    bool openKvBucket(const QString &bucketName, QString *errorOut = nullptr);

    // Читает код сохранённого скрипта по имени файла.
    // В KV редактор хранит запись под ключом "data.<filename>" в виде
    // JSON {id, name, code, timestamp} — здесь достаём поле "code".
    bool fetchScriptCode(const QString &filename, QString *codeOut, QString *errorOut = nullptr) const;

    // Публикует команду запуска скрипта на subject "script.command",
    // в том же формате, что и веб-редактор (executeScript()).
    // fromLine > 1 — продолжение с прерванной строки (replay interrupted).
    bool publishExecCommand(const QString &filename, const QString &code, int fromLine,
                            QString *errorOut = nullptr) const;

    // Как Stop в robot-gui: motion + behaviour + script.
    bool publishStopCommand(QString *errorOut = nullptr) const;

    bool publishMotionStart(QString *errorOut = nullptr) const;
    bool publishMotionStop(QString *errorOut = nullptr) const;
    bool publishMoveOffset(double x, double y, double z, double r, QString *errorOut = nullptr) const;

    // Строка прогресса из KV "script.progress" (после паузы/стопа).
    bool fetchScriptProgressLine(int *lineOut, QString *errorOut = nullptr) const;

    // Точки из KV bucket "robot_points" (тот же, что Robot Data Table в robot-gui).
    struct RobotPoint {
        QString kvKey;
        QString name;
        double x = 0;
        double y = 0;
        double z = 0;
        double r = 0;
    };

    bool openRobotPointsKv(QString *errorOut = nullptr);
    bool fetchRobotPoints(QVector<RobotPoint> *out, QString *errorOut = nullptr) const;
    bool saveRobotPointPose(const QString &kvKey, double x, double y, double z, double r,
                            QString *errorOut = nullptr) const;

    // Слушает motion.status, behaviour.status и script.status.
    bool startModuleWatch(QString *errorOut = nullptr);
    void stopModuleWatch();

    // Epoch-ms последнего сообщения (0 = ещё не было). Потокобезопасно.
    qint64 lastCtrlMs() const { return m_lastCtrlMs.load(std::memory_order_relaxed); }
    qint64 lastBehaviourMs() const { return m_lastBehaviourMs.load(std::memory_order_relaxed); }

    // Последняя известная строка скрипта из script.status (0 = ещё не было).
    int lastScriptLine() const { return m_lastScriptLine.load(std::memory_order_relaxed); }

signals:
    // Приходит из потока NATS — подключайте QueuedConnection (по умолчанию Auto).
    void scriptStatusReceived(bool running, bool completed, int line, const QString &filename);
    void motionPoseReceived(double x, double y, double z, double r, bool running);

private:
    bool publishJson(const char *subject, const QJsonObject &obj, QString *errorOut) const;

    static void onMotionStatus(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure);
    static void onBehaviourStatus(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure);
    static void onScriptStatus(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure);

    natsConnection *m_conn = nullptr;
    kvStore *m_kv = nullptr;
    kvStore *m_robotPointsKv = nullptr;
    natsSubscription *m_motionSub = nullptr;
    natsSubscription *m_behaviourSub = nullptr;
    natsSubscription *m_scriptStatusSub = nullptr;

    std::atomic<qint64> m_lastCtrlMs{0};
    std::atomic<qint64> m_lastBehaviourMs{0};
    std::atomic<int> m_lastScriptLine{0};
};

#endif // NATSCLIENT_H
