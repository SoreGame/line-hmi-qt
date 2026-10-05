#ifndef RECOVERYOVERLAY_H
#define RECOVERYOVERLAY_H

#include <QFutureWatcher>
#include <QWidget>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

class NatsClient;
class QLabel;
class QPushButton;

// Окно поверх пульта после аварийного стопа: инструкция руками, затем
// скрипт выхода из e-stop (имя из админки). Кнопку «Выход из e-stop» держит
// только зажатый грибок.
class RecoveryOverlay : public QWidget
{
    Q_OBJECT

public:
    enum class Note { Info, Ok, Warn, Err };

    // allowStart: кнопка «Старт» без чек-листа (режим «Игнорировать блокировки запуска»).
    // estopHeld: грибок физически зажат — «Выход из e-stop» не запускается.
    RecoveryOverlay(NatsClient *nats, const QString &exitScript, bool allowStart,
                    std::function<bool()> estopHeld, QWidget *parent = nullptr);

    void onScriptStatus(bool running, bool completed, const QString &filename);
    // Повторный грибок во время выхода: стоп скрипта и снова чек-лист.
    void abortExitToChecklist();
    bool isExitRunning() const;

signals:
    void note(Note level, const QString &message);
    void finished();
    void startRequested();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    enum class Phase { Checklist, Confirming, Running, Done };

    void refreshButtons();
    void onNextClicked();
    void startExitScript();
    void publishStopAndReset(const QString &message);

    NatsClient *m_nats = nullptr;
    QString m_exitScript;
    std::function<bool()> m_estopHeld;
    Phase m_phase = Phase::Checklist;
    QString m_waitFilename;
    bool m_sawScriptRunning = false;

    std::shared_ptr<std::mutex> m_gate = std::make_shared<std::mutex>();
    std::shared_ptr<std::atomic<int>> m_epoch = std::make_shared<std::atomic<int>>(1);

    QFutureWatcher<QString> *m_scriptWatcher = nullptr;
    QFutureWatcher<QString> *m_stopWatcher = nullptr;

    QLabel *m_status = nullptr;
    QPushButton *m_btnNext = nullptr;
    QPushButton *m_btnStart = nullptr;
};

#endif // RECOVERYOVERLAY_H
