#ifndef RECOVERYOVERLAY_H
#define RECOVERYOVERLAY_H

#include <QFutureWatcher>
#include <QWidget>

#include <atomic>
#include <memory>
#include <mutex>

class NatsClient;
class QLabel;
class QPushButton;

// Окно поверх пульта после аварийного стопа: инструкция руками, затем
// e-stop_out.chai. Датчики кнопку «Далее» не держат.
class RecoveryOverlay : public QWidget
{
    Q_OBJECT

public:
    enum class Note { Info, Ok, Warn, Err };

    explicit RecoveryOverlay(NatsClient *nats, QWidget *parent = nullptr);

    void onScriptStatus(bool running, bool completed, const QString &filename);
    // Повторный грибок во время выхода: стоп скрипта и снова чек-лист.
    void abortExitToChecklist();
    bool isExitRunning() const;

signals:
    void note(Note level, const QString &message);
    void finished();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    enum class Phase { Checklist, Confirming, Running, Done };

    void refreshButtons();
    void onNextClicked();
    void startExitScript();
    void publishStopAndReset(const QString &message);

    NatsClient *m_nats = nullptr;
    Phase m_phase = Phase::Checklist;
    QString m_waitFilename;
    bool m_sawScriptRunning = false;

    std::shared_ptr<std::mutex> m_gate = std::make_shared<std::mutex>();
    std::shared_ptr<std::atomic<int>> m_epoch = std::make_shared<std::atomic<int>>(1);

    QFutureWatcher<QString> *m_scriptWatcher = nullptr;
    QFutureWatcher<QString> *m_stopWatcher = nullptr;

    QLabel *m_status = nullptr;
    QPushButton *m_btnNext = nullptr;
};

#endif // RECOVERYOVERLAY_H
