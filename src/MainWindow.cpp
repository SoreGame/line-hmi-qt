#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "NatsClient.h"
#include "PlcClient.h"

#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTextStream>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include <cstdio>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {
// Адрес NATS-сервера цеха.
const char *kNatsUrl = "nats://127.0.0.1:4222";
// Тот же KV bucket, что использует веб-редактор скриптов.
const char *kNatsScriptBucket = "behaviour";
// Нет heartbeat дольше этого — модуль считаем незапущенным.
constexpr qint64 kModuleTimeoutMs = 2000;
// Raw TCP ПЛК (эмулятор scripts/plc_emulator.py).
const char *kPlcHost = "127.0.0.1";
constexpr quint16 kPlcPort = 1502;

const char *kModuleOk =
    "color:#2E8C47; font-size:11px;";
const char *kModuleErr =
    "color:#C43B3B; font-size:11px;";

const char *kChecklistTask =
    "background:#FCEAEA; border:1px solid #D96B6B; border-radius:8px;"
    " color:#C43B3B; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
    " font-size:15px; font-weight:600; padding:14px 16px;";

const char *kChecklistReady =
    "background:#EDF7F0; border:1px solid #59AD6B; border-radius:8px;"
    " color:#2E8C47; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
    " font-size:15px; font-weight:600; padding:14px 16px;";
}

namespace {
const char *kProgSelected =
    "QPushButton {"
    "  background:#E6F0FF;"
    "  border:2px solid #4C8CE6;"
    "  border-radius:10px;"
    "  text-align:center;"
    "  font-family:\"Inter\",\"Segoe UI\",sans-serif;"
    "  font-size:13px;"
    "  font-weight:600;"
    "  color:#1F2126;"
    "}";

const char *kProgIdle =
    "QPushButton {"
    "  background:#F7F8FA;"
    "  border:1px solid #DBDEE3;"
    "  border-radius:10px;"
    "  text-align:center;"
    "  font-family:\"Inter\",\"Segoe UI\",sans-serif;"
    "  font-size:13px;"
    "  font-weight:600;"
    "  color:#1F2126;"
    "}";

const char *kPillOk =
    "background:#EDF7F0; border:1px solid #59AD6B; border-radius:16px;"
    " color:#2E8C47; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
    " font-size:12px; font-weight:600; padding-left:10px;";

const char *kPillRun =
    "background:#E6F0FF; border:1px solid #4C8CE6; border-radius:16px;"
    " color:#1F66B8; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
    " font-size:12px; font-weight:600; padding-left:10px;";

const char *kPillPause =
    "background:#FFF4E5; border:1px solid #E6983A; border-radius:16px;"
    " color:#B86A10; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
    " font-size:12px; font-weight:600; padding-left:10px;";

const char *kPillErr =
    "background:#FCEAEA; border:1px solid #D96B6B; border-radius:16px;"
    " color:#C43B3B; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
    " font-size:12px; font-weight:600; padding-left:10px;";
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_nats(new NatsClient(this))
    , m_plc(new PlcClient(this))
{
    ui->setupUi(this);
    setupChecklistPanel();

    const QString logDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(logDir);
    m_sessionLogPath = logDir + QStringLiteral("/line-hmi-")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))
        + QStringLiteral(".log");

    if (!openSessionLog()) {
        std::fprintf(stderr, "LOG: failed to open session file %s\n",
                     qPrintable(m_sessionLogPath));
        std::fflush(stderr);
    }

    appendLog(LogLevel::Info, QStringLiteral("Пульт запущен"));
    if (m_sessionLogFile.isOpen()) {
        appendLog(LogLevel::Info,
                  QStringLiteral("Журнал пишется в файл: %1").arg(m_sessionLogPath));
    } else {
        appendLog(LogLevel::Err,
                  QStringLiteral("Не удалось открыть файл журнала: %1").arg(m_sessionLogPath));
    }

    auto *clockTimer = new QTimer(this);
    connect(clockTimer, &QTimer::timeout, this, [this]() {
        updateClock();
        updateUptime();
        updateCycleTime();
        updateNatsStatusIndicator();
        updatePlcIndicators();
        updateModuleIndicators();
    });
    clockTimer->start(1000);
    updateClock();
    updateUptime();
    updateCycleTime();
    updateOutputCounters();

    connect(m_nats, &NatsClient::scriptStatusReceived, this, &MainWindow::onScriptStatus);

    connect(m_plc, &PlcClient::stateChanged, this, [this]() {
        updatePlcIndicators();
        updateStartupChecklist();
        if (!(m_execWatcher && m_execWatcher->isRunning())
            && !(m_stopWatcher && m_stopWatcher->isRunning())) {
            if (m_mode == Mode::Ready || m_mode == Mode::Pause)
                applyModeVisuals();
        }
    });
    m_plc->start(QString::fromUtf8(kPlcHost), kPlcPort);
    appendLog(LogLevel::Info,
              QStringLiteral("Ожидание ПЛК (%1:%2)…")
                  .arg(QString::fromUtf8(kPlcHost))
                  .arg(kPlcPort));
    ui->statusPlc->setToolTip(
        QStringLiteral("%1:%2").arg(QString::fromUtf8(kPlcHost)).arg(kPlcPort));
    updatePlcIndicators();

    // Подключение к NATS выполняется один раз при старте пульта.
    // Это блокирующий вызов, но здесь это единоразовая операция при
    // запуске приложения, поэтому делаем её синхронно.
    QString natsErr;
    std::fprintf(stderr, "NATS: connecting to %s …\n", kNatsUrl);
    std::fflush(stderr);
    if (!m_nats->connectToServer(QString::fromUtf8(kNatsUrl), &natsErr)) {
        std::fprintf(stderr, "NATS: FAILED %s: %s\n", kNatsUrl,
                     qPrintable(natsErr));
        std::fflush(stderr);
        appendLog(LogLevel::Err,
                  QStringLiteral("NATS: не удалось подключиться (%1): %2")
                      .arg(QString::fromUtf8(kNatsUrl), natsErr));
        QMessageBox::warning(this, QStringLiteral("NATS"),
                             QStringLiteral("Не удалось подключиться к NATS (%1): %2")
                                 .arg(QString::fromUtf8(kNatsUrl), natsErr));
    } else if (!m_nats->openKvBucket(QString::fromUtf8(kNatsScriptBucket), &natsErr)) {
        std::fprintf(stderr, "NATS: connected to %s, but KV bucket '%s' failed: %s\n",
                     kNatsUrl, kNatsScriptBucket, qPrintable(natsErr));
        std::fflush(stderr);
        appendLog(LogLevel::Warn,
                  QStringLiteral("NATS подключён, но KV bucket '%1' недоступен: %2")
                      .arg(QString::fromUtf8(kNatsScriptBucket), natsErr));
        QMessageBox::warning(this, QStringLiteral("NATS"),
                             QStringLiteral("Не удалось открыть KV bucket '%1': %2")
                                 .arg(QString::fromUtf8(kNatsScriptBucket), natsErr));
    } else {
        std::fprintf(stderr, "NATS: connected to %s, KV bucket '%s'\n",
                     kNatsUrl, kNatsScriptBucket);
        std::fflush(stderr);
        appendLog(LogLevel::Ok,
                  QStringLiteral("NATS подключён (%1), bucket '%2'")
                      .arg(QString::fromUtf8(kNatsUrl),
                           QString::fromUtf8(kNatsScriptBucket)));
        m_prevNatsOk = true;
    }

    if (m_nats->isConnected()) {
        if (!m_nats->startModuleWatch(&natsErr)) {
            std::fprintf(stderr, "NATS: module watch failed: %s\n", qPrintable(natsErr));
            std::fflush(stderr);
            appendLog(LogLevel::Err,
                      QStringLiteral("Не удалось подписаться на статусы модулей: %1")
                          .arg(natsErr));
        } else {
            appendLog(LogLevel::Info,
                      QStringLiteral("Ожидание модулей CTRL и Behaviour…"));
        }
    }

    ui->statusNats->setToolTip(QString::fromUtf8(kNatsUrl));
    updateNatsStatusIndicator();
    updateModuleIndicators();

    refreshUi();
}

MainWindow::~MainWindow()
{
    if (m_plc)
        m_plc->stop();
    if (m_sessionLogFile.isOpen()) {
        m_sessionLogFile.flush();
        syncSessionLogToDisk();
        m_sessionLogFile.close();
    }
    delete ui;
}

void MainWindow::refreshUi()
{
    applyProgramVisuals();
    applyModeVisuals();
    updateNatsStatusIndicator();
    updatePlcIndicators();
}

void MainWindow::updateNatsStatusIndicator()
{
    if (m_nats && m_nats->isConnected()) {
        ui->statusNats->setText(QStringLiteral("●  NATS · OK"));
        ui->statusNats->setStyleSheet(QString::fromUtf8(kPillOk));
    } else {
        ui->statusNats->setText(QStringLiteral("●  NATS · ERR"));
        ui->statusNats->setStyleSheet(QString::fromUtf8(kPillErr));
    }
}

void MainWindow::updatePlcIndicators()
{
    const bool plcOk = plcReady();
    if (plcOk) {
        ui->statusPlc->setText(QStringLiteral("●  ПЛК · OK"));
        ui->statusPlc->setStyleSheet(QString::fromUtf8(kPillOk));
    } else {
        ui->statusPlc->setText(QStringLiteral("●  ПЛК · ERR"));
        ui->statusPlc->setStyleSheet(QString::fromUtf8(kPillErr));
    }

    const QVector<quint8> valves = m_plc ? m_plc->valves() : QVector<quint8>();
    QLabel *valveLabels[5] = {
        ui->valve1, ui->valve2, ui->valve3, ui->valve4, ui->valve5
    };
    for (int i = 0; i < 5; ++i) {
        QLabel *label = valveLabels[i];
        if (!label)
            continue;
        const bool on = plcOk && i < valves.size() && valves[i] != 0;
        label->setText(QStringLiteral("●\nК%1").arg(i + 1));
        if (!plcOk) {
            label->setStyleSheet(QString::fromUtf8(kModuleErr));
        } else if (on) {
            label->setStyleSheet(QString::fromUtf8(kModuleOk));
        } else {
            label->setStyleSheet(QStringLiteral("color:#737880; font-size:11px;"));
        }
    }
}

void MainWindow::setModuleIndicator(QLabel *label, bool alive)
{
    if (!label)
        return;
    label->setStyleSheet(QString::fromUtf8(alive ? kModuleOk : kModuleErr));
}

void MainWindow::updateModuleIndicators()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 lastCtrl = m_nats ? m_nats->lastCtrlMs() : 0;
    const qint64 lastBehaviour = m_nats ? m_nats->lastBehaviourMs() : 0;

    const bool natsOk = m_nats && m_nats->isConnected();
    const bool ctrlOk = lastCtrl > 0 && (now - lastCtrl) < kModuleTimeoutMs;
    const bool behaviourOk = lastBehaviour > 0 && (now - lastBehaviour) < kModuleTimeoutMs;
    const bool plcOk = plcReady();

    setModuleIndicator(ui->subCtrl, ctrlOk);
    setModuleIndicator(ui->subBehaviour, behaviourOk);

    if (ctrlOk && behaviourOk) {
        ui->statusRobot->setText(QStringLiteral("●  Робот · OK"));
        ui->statusRobot->setStyleSheet(QString::fromUtf8(kPillOk));
        ui->nodeRobot->setStyleSheet(
            QStringLiteral("background:#EDF7F0; border:2px solid #59AD6B; border-radius:10px;"));
    } else {
        ui->statusRobot->setText(QStringLiteral("●  Робот · ERR"));
        ui->statusRobot->setStyleSheet(QString::fromUtf8(kPillErr));
        ui->nodeRobot->setStyleSheet(
            QStringLiteral("background:#FCEAEA; border:2px solid #D96B6B; border-radius:10px;"));
    }

    if (!m_moduleStateInitialized) {
        m_prevNatsOk = natsOk;
        m_prevCtrlOk = ctrlOk;
        m_prevBehaviourOk = behaviourOk;
        m_prevPlcOk = plcOk;
        m_moduleStateInitialized = true;
        // NATS уже залогирован при подключении; здесь только модули робота и ПЛК.
        if (ctrlOk)
            appendLog(LogLevel::Ok, QStringLiteral("CTRL включён"));
        if (behaviourOk)
            appendLog(LogLevel::Ok, QStringLiteral("Behaviour включён"));
        if (plcOk)
            appendLog(LogLevel::Ok, QStringLiteral("ПЛК в сети"));
    } else {
        if (natsOk != m_prevNatsOk) {
            appendLog(natsOk ? LogLevel::Ok : LogLevel::Err,
                      natsOk ? QStringLiteral("NATS в сети")
                             : QStringLiteral("NATS недоступен"));
            m_prevNatsOk = natsOk;
        }
        if (ctrlOk != m_prevCtrlOk) {
            appendLog(ctrlOk ? LogLevel::Ok : LogLevel::Err,
                      ctrlOk ? QStringLiteral("CTRL включён")
                             : QStringLiteral("CTRL отключён"));
            m_prevCtrlOk = ctrlOk;
        }
        if (behaviourOk != m_prevBehaviourOk) {
            appendLog(behaviourOk ? LogLevel::Ok : LogLevel::Err,
                      behaviourOk ? QStringLiteral("Behaviour включён")
                                  : QStringLiteral("Behaviour отключён"));
            m_prevBehaviourOk = behaviourOk;
        }
        if (plcOk != m_prevPlcOk) {
            appendLog(plcOk ? LogLevel::Ok : LogLevel::Err,
                      plcOk ? QStringLiteral("ПЛК в сети")
                            : QStringLiteral("ПЛК недоступен"));
            m_prevPlcOk = plcOk;
        }
    }

    updateStartupChecklist();

    // Не трогаем кнопки, пока идёт асинхронный старт/стоп.
    // Готовность в хедере тоже обновляем по реальному состоянию модулей.
    if (!(m_execWatcher && m_execWatcher->isRunning())
        && !(m_stopWatcher && m_stopWatcher->isRunning())) {
        if (m_mode == Mode::Ready || m_mode == Mode::Pause)
            applyModeVisuals();
    }
}

bool MainWindow::plcReady() const
{
    return m_plc && m_plc->isOk();
}

bool MainWindow::modulesReady() const
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 lastCtrl = m_nats ? m_nats->lastCtrlMs() : 0;
    const qint64 lastBehaviour = m_nats ? m_nats->lastBehaviourMs() : 0;
    const bool natsOk = m_nats && m_nats->isConnected();
    const bool ctrlOk = lastCtrl > 0 && (now - lastCtrl) < kModuleTimeoutMs;
    const bool behaviourOk = lastBehaviour > 0 && (now - lastBehaviour) < kModuleTimeoutMs;
    return natsOk && ctrlOk && behaviourOk && plcReady();
}

void MainWindow::setupChecklistPanel()
{
    m_checklistLayout = new QVBoxLayout(ui->checklistHost);
    m_checklistLayout->setContentsMargins(0, 0, 0, 0);
    m_checklistLayout->setSpacing(10);
    m_checklistLayout->setAlignment(Qt::AlignTop);
    updateStartupChecklist();
}

void MainWindow::updateStartupChecklist()
{
    QStringList tasks;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 lastCtrl = m_nats ? m_nats->lastCtrlMs() : 0;
    const qint64 lastBehaviour = m_nats ? m_nats->lastBehaviourMs() : 0;

    if (!m_nats || !m_nats->isConnected())
        tasks << QStringLiteral("Подключите NATS");
    if (!(lastCtrl > 0 && (now - lastCtrl) < kModuleTimeoutMs))
        tasks << QStringLiteral("Включите CTRL");
    if (!(lastBehaviour > 0 && (now - lastBehaviour) < kModuleTimeoutMs))
        tasks << QStringLiteral("Включите Behaviour");
    if (!plcReady())
        tasks << QStringLiteral("Подключите ПЛК");

    if (tasks == m_lastChecklistTasks)
        return;
    m_lastChecklistTasks = tasks;

    while (QLayoutItem *item = m_checklistLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    m_checklistItems.clear();

    if (tasks.isEmpty()) {
        auto *ready = new QLabel(QStringLiteral("✓  Готово к запуску"), ui->checklistHost);
        ready->setStyleSheet(QString::fromUtf8(kChecklistReady));
        ready->setWordWrap(true);
        m_checklistLayout->addWidget(ready);
        m_checklistItems.push_back(ready);
        return;
    }

    for (const QString &task : tasks) {
        auto *row = new QLabel(QStringLiteral("●  %1").arg(task), ui->checklistHost);
        row->setStyleSheet(QString::fromUtf8(kChecklistTask));
        row->setWordWrap(true);
        m_checklistLayout->addWidget(row);
        m_checklistItems.push_back(row);
    }
}

void MainWindow::updateClock()
{
    ui->labelClock->setText(QTime::currentTime().toString(QStringLiteral("HH:mm")));
}

void MainWindow::updateUptime()
{
    qint64 ms = m_uptimeAccumulatedMs;
    if (m_uptimeRunning)
        ms += m_uptimeTick.elapsed();

    const qint64 totalSec = ms / 1000;
    const int hours = static_cast<int>(totalSec / 3600);
    const int minutes = static_cast<int>((totalSec % 3600) / 60);
    const int seconds = static_cast<int>(totalSec % 60);
    ui->kpiUptimeValue->setText(
        QStringLiteral("%1:%2:%3")
            .arg(hours, 2, 10, QChar(u'0'))
            .arg(minutes, 2, 10, QChar(u'0'))
            .arg(seconds, 2, 10, QChar(u'0')));
}

void MainWindow::startUptime()
{
    if (m_uptimeRunning)
        return;
    m_uptimeTick.start();
    m_uptimeRunning = true;
    updateUptime();
}

void MainWindow::pauseUptime()
{
    if (!m_uptimeRunning)
        return;
    m_uptimeAccumulatedMs += m_uptimeTick.elapsed();
    m_uptimeRunning = false;
    updateUptime();
}

void MainWindow::resetUptime()
{
    m_uptimeAccumulatedMs = 0;
    m_uptimeRunning = false;
    updateUptime();
}

void MainWindow::updateCycleTime()
{
    // KPI показывает только длительность прошлого завершённого цикла.
    if (m_lastCycleMs < 0) {
        ui->kpiCycleValue->setText(QStringLiteral("—"));
        return;
    }

    const double sec = m_lastCycleMs / 1000.0;
    ui->kpiCycleValue->setText(
        QStringLiteral("%1 с").arg(sec, 0, 'f', sec < 10.0 ? 1 : 0));
}

void MainWindow::startCycleTimer()
{
    if (m_cycleRunning)
        return;
    m_cycleTick.start();
    m_cycleRunning = true;
}

void MainWindow::pauseCycleTimer()
{
    if (!m_cycleRunning)
        return;
    m_cycleAccumulatedMs += m_cycleTick.elapsed();
    m_cycleRunning = false;
}

void MainWindow::resetCycleTimer()
{
    m_cycleAccumulatedMs = 0;
    m_cycleRunning = false;
}

void MainWindow::finishCycle()
{
    qint64 ms = m_cycleAccumulatedMs;
    if (m_cycleRunning)
        ms += m_cycleTick.elapsed();

    m_cycleRunning = false;
    m_cycleAccumulatedMs = 0;

    if (ms < 0)
        ms = 0;
    m_lastCycleMs = ms;
    updateCycleTime();
    registerGood();

    appendLog(LogLevel::Ok,
              QStringLiteral("Цикл завершён · %1 с")
                  .arg(ms / 1000.0, 0, 'f', ms < 10000 ? 1 : 0));
}

void MainWindow::updateOutputCounters()
{
    auto formatCount = [](int n) -> QString {
        // Как в макете: «1 248»
        QString s = QString::number(n);
        for (int i = s.size() - 3; i > 0; i -= 3)
            s.insert(i, QChar(0x00A0)); // неразрывный пробел
        return s;
    };

    const QString goodText = formatCount(m_goodCount);
    const QString scrapText = formatCount(m_scrapCount);

    ui->basketOkCount->setText(goodText);
    ui->basketScrapCount->setText(scrapText);
    ui->kpiOutputValue->setText(goodText);
    ui->kpiScrapValue->setText(scrapText);
}

void MainWindow::registerGood()
{
    ++m_goodCount;
    updateOutputCounters();
    appendLog(LogLevel::Ok, QStringLiteral("Годна · всего %1").arg(m_goodCount));
}

void MainWindow::registerScrap()
{
    ++m_scrapCount;
    updateOutputCounters();
    appendLog(LogLevel::Warn, QStringLiteral("Брак · всего %1").arg(m_scrapCount));
}

void MainWindow::onScriptStatus(bool running, bool completed, int /*line*/)
{
    // Успешное завершение программы → сохранить время цикла и запустить снова.
    if (!completed || running)
        return;
    if (m_mode != Mode::Run)
        return;
    if (!m_cycleRunning && m_cycleAccumulatedMs <= 0)
        return; // цикл уже обработан / ещё не стартовал
    if ((m_execWatcher && m_execWatcher->isRunning())
        || (m_stopWatcher && m_stopWatcher->isRunning()))
        return;

    finishCycle();

    m_resumeLine = 1;
    appendLog(LogLevel::Info, QStringLiteral("Перезапуск программы"));
    startSelectedProgramScript(/*fromLine=*/1, /*freshSession=*/false);
}

void MainWindow::applyProgramVisuals()
{
    const bool d1 = (m_program == Program::Detail1);
    ui->btnProg1->setStyleSheet(QString::fromUtf8(d1 ? kProgSelected : kProgIdle));
    ui->btnProg2->setStyleSheet(QString::fromUtf8(d1 ? kProgIdle : kProgSelected));

    ui->btnProg1->setText(d1
                              ? QStringLiteral("Деталь 1\nВыбрано\nпоток Б1+Б2")
                              : QStringLiteral("Деталь 1\nДоступно\nпоток Б1+Б2"));
    ui->btnProg2->setText(d1
                              ? QStringLiteral("Деталь 2\nДоступно\nпоток Б1+Б3")
                              : QStringLiteral("Деталь 2\nВыбрано\nпоток Б1+Б3"));

    ui->labelFlow->setText(d1
                               ? QStringLiteral("Поток: Бункер 1 + 2")
                               : QStringLiteral("Поток: Бункер 1 + 3"));
    ui->labelFeed->setText(d1
                               ? QStringLiteral("подача · Б1+Б2")
                               : QStringLiteral("подача · Б1+Б3"));

    // Active bunkers by program
    const char *active =
        "background:#EDF7F0; border:2px solid #59AD6B; border-radius:8px;";
    const char *idle =
        "background:#F3F4F6; border:2px solid #C5C9D0; border-radius:8px;";

    ui->bunker1->setStyleSheet(QString::fromUtf8(active));
    ui->bunker1Led->setText(QStringLiteral("● Готов"));
    ui->bunker1Led->setStyleSheet(QStringLiteral("color:#2E8C47; font-size:11px;"));

    if (d1) {
        ui->bunker2->setStyleSheet(QString::fromUtf8(active));
        ui->bunker2Led->setText(QStringLiteral("● Готов"));
        ui->bunker2Led->setStyleSheet(QStringLiteral("color:#2E8C47; font-size:11px;"));
        ui->bunker3->setStyleSheet(QString::fromUtf8(idle));
        ui->bunker3Led->setText(QStringLiteral("● Не в потоке"));
        ui->bunker3Led->setStyleSheet(QStringLiteral("color:#737880; font-size:11px;"));
    } else {
        ui->bunker3->setStyleSheet(QString::fromUtf8(active));
        ui->bunker3Led->setText(QStringLiteral("● Готов"));
        ui->bunker3Led->setStyleSheet(QStringLiteral("color:#2E8C47; font-size:11px;"));
        ui->bunker2->setStyleSheet(QString::fromUtf8(idle));
        ui->bunker2Led->setText(QStringLiteral("● Не в потоке"));
        ui->bunker2Led->setStyleSheet(QStringLiteral("color:#737880; font-size:11px;"));
    }

    const bool canChangeProg = (m_mode == Mode::Ready);
    ui->btnProg1->setEnabled(canChangeProg);
    ui->btnProg2->setEnabled(canChangeProg);
}

void MainWindow::applyModeVisuals()
{
    const bool canStart = modulesReady();

    switch (m_mode) {
    case Mode::Ready:
        if (canStart) {
            ui->statusReady->setText(QStringLiteral("●  Готовность · OK"));
            ui->statusReady->setStyleSheet(QString::fromUtf8(kPillOk));
        } else {
            ui->statusReady->setText(QStringLiteral("●  Готовность · ERR"));
            ui->statusReady->setStyleSheet(QString::fromUtf8(kPillErr));
        }
        ui->btnStart->setEnabled(canStart);
        ui->btnPause->setEnabled(false);
        ui->btnStop->setEnabled(false);
        ui->nodeRobotState->setText(canStart ? QStringLiteral("Готов")
                                             : QStringLiteral("Не готов"));
        break;
    case Mode::Run:
        ui->statusReady->setText(QStringLiteral("●  Готовность · RUN"));
        ui->statusReady->setStyleSheet(QString::fromUtf8(kPillRun));
        ui->btnStart->setEnabled(false);
        ui->btnPause->setEnabled(true);
        ui->btnStop->setEnabled(true);
        ui->nodeRobotState->setText(QStringLiteral("Работа"));
        break;
    case Mode::Pause:
        ui->statusReady->setText(QStringLiteral("●  Готовность · PAUSE"));
        ui->statusReady->setStyleSheet(QString::fromUtf8(kPillPause));
        ui->btnStart->setEnabled(canStart);
        ui->btnPause->setEnabled(false);
        ui->btnStop->setEnabled(true);
        ui->nodeRobotState->setText(QStringLiteral("Пауза"));
        break;
    }
}

void MainWindow::on_btnStart_clicked()
{
    if (m_mode != Mode::Ready && m_mode != Mode::Pause)
        return;
    if (m_execWatcher && m_execWatcher->isRunning())
        return;
    if (m_stopWatcher && m_stopWatcher->isRunning())
        return;
    if (!modulesReady()) {
        appendLog(LogLevel::Warn, QStringLiteral("Старт недоступен: система не готова"));
        return;
    }

    if (m_mode == Mode::Ready) {
        // Свежий запуск: подтягиваем код нужного скрипта из KV и шлём
        // команду exec в NATS с from_line=1.
        m_resumeLine = 1;
        appendLog(LogLevel::Info,
                  QStringLiteral("Запрос запуска · %1 · скрипт '%2'")
                      .arg(m_program == Program::Detail1
                               ? QStringLiteral("Деталь 1")
                               : QStringLiteral("Деталь 2"),
                           scriptFilenameForProgram(m_program)));
        startSelectedProgramScript(/*fromLine=*/1);
        return;
    }

    // Возобновление из Pause: exec с from_line прерванной строки.
    resumeSelectedProgramScript();
}

QString MainWindow::scriptFilenameForProgram(Program p) const
{
    // ВАЖНО: имена файлов должны точно совпадать с тем, под чем скрипты
    // сохранены редактором (поле "name"/"id" в KV, т.е. то, что было
    // введено в поле "Имя файла" при сохранении).
    switch (p) {
    case Program::Detail1:
        return QStringLiteral("ai.chai");
    case Program::Detail2:
        return QStringLiteral("ai2.chai");
    }
    return {};
}

void MainWindow::startSelectedProgramScript(int fromLine, bool freshSession)
{
    const QString filename = scriptFilenameForProgram(m_program);
    const int line = fromLine < 1 ? 1 : fromLine;
    NatsClient *nats = m_nats;

    ui->btnStart->setEnabled(false);
    ui->btnPause->setEnabled(false);
    ui->btnStop->setEnabled(false);

    const QFuture<QString> future = QtConcurrent::run([nats, filename, line]() -> QString {
        QString code;
        QString err;
        if (!nats->fetchScriptCode(filename, &code, &err)) {
            return QStringLiteral("Не удалось получить скрипт '%1' из NATS KV: %2")
                .arg(filename, err);
        }
        if (!nats->publishExecCommand(filename, code, line, &err)) {
            return QStringLiteral("Не удалось отправить команду запуска в NATS: %1").arg(err);
        }
        return QString(); // пустая строка = успех
    });

    if (m_execWatcher) {
        m_execWatcher->disconnect(this);
        m_execWatcher->deleteLater();
    }
    m_execWatcher = new QFutureWatcher<QString>(this);
    connect(m_execWatcher, &QFutureWatcher<QString>::finished, this,
            [this, line, freshSession]() {
        const QString error = m_execWatcher->result();

        if (!error.isEmpty()) {
            appendLog(LogLevel::Err, error);
            QMessageBox::warning(this, QStringLiteral("Ошибка запуска"), error);
            refreshUi();
            return;
        }

        m_mode = Mode::Run;
        if (line <= 1) {
            if (freshSession) {
                resetUptime();
                startUptime();
                appendLog(LogLevel::Ok,
                          QStringLiteral("Старт · %1 · скрипт '%2'")
                              .arg(m_program == Program::Detail1
                                       ? QStringLiteral("Деталь 1")
                                       : QStringLiteral("Деталь 2"),
                                   scriptFilenameForProgram(m_program)));
            } else {
                // Следующий цикл: uptime продолжает тикать, KPI цикла не трогаем.
                if (!m_uptimeRunning)
                    startUptime();
            }
            resetCycleTimer();
            startCycleTimer();
        } else {
            startUptime();
            startCycleTimer();
            appendLog(LogLevel::Ok,
                      QStringLiteral("Продолжение со строки %1 · скрипт '%2'")
                          .arg(line)
                          .arg(scriptFilenameForProgram(m_program)));
        }
        refreshUi();
    });
    m_execWatcher->setFuture(future);
}

void MainWindow::resumeSelectedProgramScript()
{
    int line = m_resumeLine;
    if (line < 1)
        line = m_nats ? m_nats->lastScriptLine() : 1;

    // Fallback: строка из KV script.progress, если live-статус ещё не пришёл.
    if (line < 1 && m_nats) {
        int progressLine = 0;
        if (m_nats->fetchScriptProgressLine(&progressLine))
            line = progressLine;
    }
    if (line < 1)
        line = 1;

    m_resumeLine = line;
    appendLog(LogLevel::Info,
              QStringLiteral("Запрос продолжения со строки %1").arg(line));
    startSelectedProgramScript(line);
}

void MainWindow::sendStopCommandAsync(const QString &reason, bool resetToReady)
{
    if (m_stopWatcher && m_stopWatcher->isRunning())
        return;

    // Запоминаем строку ДО стопа — после abort script.status может обнулиться.
    int line = m_nats ? m_nats->lastScriptLine() : 0;
    if (line < 1)
        line = m_resumeLine;
    if (line < 1)
        line = 1;

    if (!resetToReady)
        m_resumeLine = line;
    else
        m_resumeLine = 1;

    NatsClient *nats = m_nats;
    ui->btnStart->setEnabled(false);
    ui->btnPause->setEnabled(false);
    ui->btnStop->setEnabled(false);

    const QFuture<QString> future = QtConcurrent::run([nats]() -> QString {
        QString err;
        if (!nats->publishStopCommand(&err))
            return err;
        return QString();
    });

    if (m_stopWatcher) {
        m_stopWatcher->disconnect(this);
        m_stopWatcher->deleteLater();
    }
    m_stopWatcher = new QFutureWatcher<QString>(this);
    connect(m_stopWatcher, &QFutureWatcher<QString>::finished, this,
            [this, reason, resetToReady, line]() {
        const QString error = m_stopWatcher->result();
        if (!error.isEmpty()) {
            appendLog(LogLevel::Err,
                      QStringLiteral("Не удалось отправить stop: %1").arg(error));
            QMessageBox::warning(this, QStringLiteral("Ошибка"), error);
            refreshUi();
            return;
        }

        if (resetToReady) {
            // Стоп (из Run или после Паузы) при незавершённом цикле → брак.
            const bool interruptedCycle = m_cycleRunning || m_cycleAccumulatedMs > 0;
            if (interruptedCycle)
                registerScrap();

            m_mode = Mode::Ready;
            resetUptime();
            resetCycleTimer();
            appendLog(LogLevel::Info, reason);
        } else {
            m_mode = Mode::Pause;
            pauseUptime();
            pauseCycleTimer();
            appendLog(LogLevel::Warn,
                      QStringLiteral("%1 · продолжение со строки %2").arg(reason).arg(line));
        }
        refreshUi();
    });
    m_stopWatcher->setFuture(future);
}

void MainWindow::on_btnPause_clicked()
{
    if (m_mode != Mode::Run)
        return;
    if (m_execWatcher && m_execWatcher->isRunning())
        return;
    sendStopCommandAsync(QStringLiteral("Пауза"), /*resetToReady=*/false);
}

void MainWindow::on_btnStop_clicked()
{
    if (m_mode != Mode::Run && m_mode != Mode::Pause)
        return;
    if (m_execWatcher && m_execWatcher->isRunning())
        return;
    sendStopCommandAsync(QStringLiteral("Стоп"), /*resetToReady=*/true);
}

void MainWindow::on_btnProg1_clicked()
{
    if (m_mode != Mode::Ready)
        return;
    if (m_program == Program::Detail1)
        return;
    m_program = Program::Detail1;
    m_resumeLine = 1;
    appendLog(LogLevel::Info, QStringLiteral("Выбрана Деталь 1 · поток Б1+Б2"));
    refreshUi();
}

void MainWindow::on_btnProg2_clicked()
{
    if (m_mode != Mode::Ready)
        return;
    if (m_program == Program::Detail2)
        return;
    m_program = Program::Detail2;
    m_resumeLine = 1;
    appendLog(LogLevel::Info, QStringLiteral("Выбрана Деталь 2 · поток Б1+Б3"));
    refreshUi();
}

void MainWindow::on_btnAdmin_clicked()
{
}

void MainWindow::on_btnExportLog_clicked()
{
    const QString defaultName =
        QStringLiteral("line-hmi-%1.log")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Выгрузить журнал событий"),
        QDir::homePath() + QLatin1Char('/') + defaultName,
        QStringLiteral("Log files (*.log);;All files (*)"));
    if (path.isEmpty())
        return;

    QString err;
    if (!exportLogsToFile(path, &err)) {
        appendLog(LogLevel::Err, QStringLiteral("Не удалось выгрузить журнал: %1").arg(err));
        QMessageBox::warning(this, QStringLiteral("Журнал"),
                             QStringLiteral("Не удалось сохранить файл:\n%1").arg(err));
        return;
    }

    appendLog(LogLevel::Ok, QStringLiteral("Журнал выгружен: %1").arg(path));
    QMessageBox::information(this, QStringLiteral("Журнал"),
                             QStringLiteral("Сохранено %1 записей в:\n%2")
                                 .arg(m_logEntries.size())
                                 .arg(path));
}

QString MainWindow::logLevelTag(LogLevel level) const
{
    switch (level) {
    case LogLevel::Ok: return QStringLiteral("OK");
    case LogLevel::Warn: return QStringLiteral("WARN");
    case LogLevel::Err: return QStringLiteral("ERR");
    case LogLevel::Info: return QStringLiteral("INFO");
    }
    return QStringLiteral("INFO");
}

QColor MainWindow::logLevelColor(LogLevel level) const
{
    switch (level) {
    case LogLevel::Ok: return QColor(QStringLiteral("#2E8C47"));
    case LogLevel::Warn: return QColor(QStringLiteral("#B86A10"));
    case LogLevel::Err: return QColor(QStringLiteral("#C43B3B"));
    case LogLevel::Info: return QColor(QStringLiteral("#1F2126"));
    }
    return QColor(QStringLiteral("#1F2126"));
}

void MainWindow::appendLog(LogLevel level, const QString &message)
{
    LogEntry entry;
    entry.time = QDateTime::currentDateTime();
    entry.level = level;
    entry.message = message;
    m_logEntries.push_back(entry);
    appendLogToSessionFile(entry);

    if (!ui || !ui->logList)
        return;

    const QString line = QStringLiteral("%1  [%2]  %3")
                             .arg(entry.time.toString(QStringLiteral("HH:mm:ss")),
                                  logLevelTag(level),
                                  message);
    auto *item = new QListWidgetItem(line);
    item->setForeground(logLevelColor(level));
    ui->logList->insertItem(0, item);

    // В UI держим разумный хвост; полный журнал остаётся в m_logEntries / файле сессии.
    while (ui->logList->count() > 500)
        delete ui->logList->takeItem(ui->logList->count() - 1);
}

void MainWindow::appendLogToSessionFile(const LogEntry &entry)
{
    if (!m_sessionLogFile.isOpen())
        return;

    // Пишем сразу и сбрасываем на диск — чтобы запись пережила kill -9 / пропажу питания.
    QTextStream out(&m_sessionLogFile);
    out << entry.time.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
        << " [" << logLevelTag(entry.level) << "] "
        << entry.message << '\n';
    out.flush();
    m_sessionLogFile.flush();
    syncSessionLogToDisk();
}

bool MainWindow::openSessionLog()
{
    if (m_sessionLogPath.isEmpty())
        return false;

    m_sessionLogFile.setFileName(m_sessionLogPath);
    if (!m_sessionLogFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Unbuffered))
        return false;

    QTextStream out(&m_sessionLogFile);
    out << QStringLiteral("# line-hmi-qt session ")
        << QDateTime::currentDateTime().toString(Qt::ISODate) << '\n';
    out.flush();
    m_sessionLogFile.flush();
    syncSessionLogToDisk();

#ifdef Q_OS_UNIX
    // fsync каталога — иначе при power-loss новое имя файла может не закрепиться в FS.
    const QByteArray dirPath = QFile::encodeName(QFileInfo(m_sessionLogPath).absolutePath());
    const int dirFd = ::open(dirPath.constData(), O_RDONLY | O_DIRECTORY);
    if (dirFd >= 0) {
        ::fsync(dirFd);
        ::close(dirFd);
    }
#endif
    return true;
}

void MainWindow::syncSessionLogToDisk()
{
    if (!m_sessionLogFile.isOpen())
        return;
#ifdef Q_OS_UNIX
    const int fd = m_sessionLogFile.handle();
    if (fd >= 0)
        ::fsync(fd);
#endif
}

bool MainWindow::exportLogsToFile(const QString &path, QString *errorOut) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorOut)
            *errorOut = file.errorString();
        return false;
    }

    QTextStream out(&file);
    out << QStringLiteral("# line-hmi-qt event log\n");
    out << QStringLiteral("# exported ")
        << QDateTime::currentDateTime().toString(Qt::ISODate) << '\n';
    out << QStringLiteral("# session file: ") << m_sessionLogPath << '\n';
    out << QStringLiteral("# entries: ") << m_logEntries.size() << "\n\n";

    for (const LogEntry &entry : m_logEntries) {
        out << entry.time.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
            << " [" << logLevelTag(entry.level) << "] "
            << entry.message << '\n';
    }
    out.flush();
    file.flush();
#ifdef Q_OS_UNIX
    const int fd = file.handle();
    if (fd >= 0)
        ::fsync(fd);
#endif
    return true;
}