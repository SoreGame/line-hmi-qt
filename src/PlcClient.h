#ifndef PLCCLIENT_H
#define PLCCLIENT_H

#include <QByteArray>
#include <QObject>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>

// TCP-клиент ПЛК по протоколу v2 (docs/PLC_PROTOCOL.md). ПЛК — сервер.
// Раз в секунду и по запросу шлём опрос [203, f1, f2, f3, f4, Z] и ждём кадр
// int16 little-endian; размер кадра задаётся в настройках, по умолчанию 160 байт.
// ПЛК жив, пока кадры приходят.
class PlcClient : public QObject
{
    Q_OBJECT
public:
    static constexpr int kStatusIntBytes = 2;
    static constexpr int kDefaultStatusSize = 160;
    static constexpr int kMinStatusSize = 2;
    static constexpr int kMaxStatusSize = 2048;
    static constexpr int kDefaultPollMs = 1000;
    static constexpr int kMinPollMs = 100;
    static constexpr int kMaxPollMs = 5000;
    // Чётное число байт в пределах [kMinStatusSize, kMaxStatusSize].
    static int normalizeStatusSize(int bytes);
    static int normalizePollMs(int ms);

    // Ячейки кадра (индексы int16).
    static constexpr int kModeIndex = 52;
    static constexpr int kProgramIndex = 54;
    static constexpr int kLoadCellIndex = 55;
    // В режиме «в работе»: 0 — идёт предподготовка, 1 — закончена, можно запускать цикл.
    static constexpr int kPrepDoneIndex = 56;
    static constexpr int kInitPermitIndex = 60;
    static constexpr int kGoodIndex = 62;
    static constexpr int kScrapIndex = 63;
    static constexpr int kPowerOffIndex = 64;
    // Тип ламели на выдаче, 0 — пусто. Пустая выдача не авария: авария только по 59.
    static constexpr int kLamellaTypeIndex = 69;
    // Датчики наличия держателей ламелей: 1 — есть.
    static constexpr int kBigHoldersSensorIndex = 25;
    static constexpr int kSmallHoldersSensorIndex = 1;
    // Причины аварии от ПЛК: 1 — есть. Активны могут быть сразу несколько.
    static constexpr int kNoLamellaeIndex = 59;
    static constexpr int kNoHoldersIndex = 61;
    // 61: 1 — малые, 2 — большие, 3 — оба.
    static constexpr qint16 kNoHoldersSmall = 1;
    static constexpr qint16 kNoHoldersBig = 2;
    static constexpr qint16 kNoHoldersBoth = 3;
    static constexpr int kWeldTimeoutIndex = 65;
    static constexpr int kHolderPickIndex = 66;
    static constexpr int kLamellaPickIndex = 67;
    // Индикаторы, решений по ним пульт не принимает.
    // Клапаны сварки: в безопасном положении все = 1.
    static constexpr int kWeldValveInts[] = {10, 11, 12, 13, 14, 16};
    static constexpr qint16 kWeldValveSafe = 1;
    static constexpr int kVision1Index = 57;
    static constexpr int kVision2Index = 58;
    // Датчик импульса сварки (индикатор; решений пульт по нему не принимает).
    static constexpr int kWeldPulseIndex = 32;
    static constexpr int kPanelButtonsIndex = 50;
    static constexpr int kMushroomIndex = 51;
    static constexpr qint16 kMushroomReleased = 1;

    // Stopping — нажали «Стоп», робот доделывает цикл; Run включает предподготовку (ячейка 56).
    enum class Mode : qint16 { Boot = 0, Ready = 1, Stopping = 2, Run = 3, Estop = 4, Service = 5 };

    // Что пульт сообщает о роботе в каждом опросе.
    struct RobotReport {
        bool natsConnected = false;
        bool ctrlAlive = false;
        bool behaviourAlive = false;
        bool scriptRunning = false;
        int zone = 0;
    };

    enum Command : quint8 {
        CmdStart = 2,
        CmdStop = 3,
        CmdEstopExit = 5,
        CmdStopDone = 8,
    };
    enum ScriptRole : quint8 { RoleInit = 1, RoleMain = 2 };

    // [7, code, 0, 0].
    static QByteArray controlCommand(Command code);
    // [7, 1, N, X]: программа N (0 — снять выбор), X = 1 — учитывать тензодатчик.
    static QByteArray selectCommand(int program, bool useLoadCell);
    // [7, 7, R, S]: скрипт R закончился, S = 1 — дошёл до конца.
    static QByteArray scriptReportCommand(ScriptRole role, bool completed);
    // Сервисный режим: [99, 1] вход, [99, 0] выход.
    static QByteArray serviceModeCommand(bool on);
    // Дискретный выход: [2, n, 1] вкл, [2, n, 0] выкл.
    static QByteArray doCommand(int n, bool on);

    // Тексты для экрана; код без текста — «Код N».
    static QString modeText(Mode mode);
    // Нет кадра столько времени — ПЛК недоступен.
    static constexpr qint64 kTimeoutMs = 3000;

    explicit PlcClient(QObject *parent = nullptr);
    ~PlcClient() override;

    void start(const QString &host, quint16 port, int statusBytes = kDefaultStatusSize,
               int pollMs = kDefaultPollMs);
    void stop();
    int pollMs() const { return m_pollMs.load(std::memory_order_relaxed); }

    bool isOk() const;
    qint64 lastOkMs() const { return m_lastOkMs.load(std::memory_order_relaxed); }
    qint16 statusInt16(int index) const;
    int statusSize() const { return m_statusSize.load(std::memory_order_relaxed); }
    int statusIntCount() const { return statusSize() / kStatusIntBytes; }
    // Сколько байт сверх кадра пришло к прошлому опросу: не 0 — ПЛК шлёт больше, чем задано.
    int extraBytes() const { return m_extraBytes.load(std::memory_order_relaxed); }

    Mode mode() const { return static_cast<Mode>(statusInt16(kModeIndex)); }
    bool prepDone() const { return statusInt16(kPrepDoneIndex) == 1; }
    bool vision1Ok() const { return isOk() && statusInt16(kVision1Index) == 1; }
    bool vision2Ok() const { return isOk() && statusInt16(kVision2Index) == 1; }
    bool weldValvesSafe() const;
    bool lamellaIssued() const { return isOk() && statusInt16(kLamellaTypeIndex) != 0; }
    // Держатели для программы: 1 — большие (25), 2 — малые (1). Без программы — нет.
    bool holdersPresent(int program) const;

    void setRobotReport(const RobotReport &report);

    // Кадр в то же соединение, что и опрос. ПЛК на команды не отвечает.
    void sendCommand(const QByteArray &frame);
    // Внеочередной опрос после уже поставленных в очередь команд. Вернёт номер;
    // кадр, пришедший в ответ на этот опрос или позже, даёт pollServed() >= номер.
    quint64 pollNow();
    quint64 pollServed() const { return m_pollServed.load(std::memory_order_acquire); }

signals:
    void stateChanged();
    void commandFinished(bool ok, const QString &error);

private:
    class Worker;

    QByteArray pollFrame() const;

    Worker *m_worker = nullptr;

    mutable std::mutex m_dataMutex;
    QByteArray m_status;
    std::atomic<int> m_statusSize{kDefaultStatusSize};
    std::atomic<int> m_pollMs{kDefaultPollMs};
    std::atomic<int> m_extraBytes{0};
    RobotReport m_report;
    std::mutex m_cmdMutex;
    std::condition_variable m_cmdCv;
    std::deque<QByteArray> m_cmdQueue;
    std::atomic<qint64> m_lastOkMs{0};
    std::atomic<quint64> m_pollRequested{0};
    std::atomic<quint64> m_pollServed{0};
};

#endif // PLCCLIENT_H
