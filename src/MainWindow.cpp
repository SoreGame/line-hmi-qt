#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "AdminPanel.h"
#include "ConnectionSettings.h"
#include "NatsClient.h"
#include "PlcClient.h"
#include "PrepOverlay.h"
#include "RecoveryOverlay.h"

#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGuiApplication>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPainter>
#include <QResizeEvent>
#include <QScreen>
#include <QShowEvent>
#include <QStandardPaths>
#include <QTextStream>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent/QtConcurrent>

#include <cmath>

#include <cstdio>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <unistd.h>
#endif

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniObject>
#endif

namespace {
constexpr int kCanvasWidth = 1920;
constexpr int kCanvasHeight = 1080;
// Тот же KV bucket, что использует веб-редактор скриптов.
const char *kNatsScriptBucket = "behaviour";
// Нет heartbeat дольше этого — модуль считаем незапущенным.
constexpr qint64 kModuleTimeoutMs = 2000;

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

const char *kChecklistWarn =
    "background:#FFF6E5; border:1px solid #E0B15A; border-radius:8px;"
    " color:#8A5A00; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
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
    " font-size:12px; font-weight:600;";

const char *kPillRun =
    "background:#E6F0FF; border:1px solid #4C8CE6; border-radius:16px;"
    " color:#1F66B8; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
    " font-size:12px; font-weight:600;";

const char *kPillErr =
    "background:#FCEAEA; border:1px solid #D96B6B; border-radius:16px;"
    " color:#C43B3B; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
    " font-size:12px; font-weight:600;";

const char *kPillInit =
    "background:#FFF6E5; border:1px solid #E0B15A; border-radius:16px;"
    " color:#8A5A00; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
    " font-size:12px; font-weight:600;";

bool fresh(qint64 lastMs, qint64 now)
{
    return lastMs > 0 && (now - lastMs) < kModuleTimeoutMs;
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_nats(new NatsClient(this))
    , m_plc(new PlcClient(this))
{
    ui->setupUi(this);
#ifdef Q_OS_ANDROID
    setupAndroidCanvas();
    enableKeepScreenOn();
    ui->btnAdmin->hide();
#endif
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
    appendLog(LogLevel::Info,
              QStringLiteral("Инициализация: NATS, CTRL, Behaviour и кадр ПЛК "
                             "(камеры — байты в этом кадре)"));
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

    auto *natsReconnectTimer = new QTimer(this);
    natsReconnectTimer->setInterval(3000);
    connect(natsReconnectTimer, &QTimer::timeout, this, [this]() {
        if (!ConnectionSettings::isConfigured())
            return;
        if (!m_nats || m_nats->isConnected())
            return;
        if (m_connectWatcher && m_connectWatcher->isRunning())
            return;
        beginNatsConnect(false, true);
    });
    natsReconnectTimer->start();

    connect(m_nats, &NatsClient::scriptStatusReceived, this, &MainWindow::onScriptStatus);

    connect(m_plc, &PlcClient::stateChanged, this, [this]() {
        updatePlcIndicators();
        updateLeftPanel();
        maybeEnterRecoveryFromPlc();
        if (!(m_execWatcher && m_execWatcher->isRunning())
            && !(m_stopWatcher && m_stopWatcher->isRunning())) {
            if (m_mode == Mode::Ready)
                applyModeVisuals();
        }
    });
    connect(m_plc, &PlcClient::commandFinished, this,
            [this](bool ok, const QString &error) {
        if (m_plcAcks.isEmpty())
            return;
        const PlcAck ack = m_plcAcks.takeFirst();
        if (ack == PlcAck::Program || ack == PlcAck::Control) {
            if (!ok) {
                appendLog(LogLevel::Err,
                          error.isEmpty()
                              ? QStringLiteral("ПЛК не принял команду")
                              : error);
            } else if (ack == PlcAck::Program) {
                appendLog(LogLevel::Ok, QStringLiteral("ПЛК: выбор программы отправлен"));
            }
            return;
        }
        if (!m_prepArming)
            return;
        m_prepArming = false;
        if (!ok) {
            appendLog(LogLevel::Err,
                      error.isEmpty() ? QStringLiteral("ПЛК не принял команду старта") : error);
            refreshUi();
            return;
        }
        appendLog(LogLevel::Ok, QStringLiteral("ПЛК: команда старта отправлена"));
        openPrepOverlay();
    });

    if (!ConnectionSettings::isConfigured()) {
#ifdef Q_OS_ANDROID
        appendLog(LogLevel::Err,
                  QStringLiteral("Не найден или повреждён config.json. "
                                   "Проверьте line-hmi-qt/config.json и пересоберите APK."));
#else
        appendLog(LogLevel::Info,
                  QStringLiteral("Укажите адрес NATS и ПЛК в Admin"));
        QTimer::singleShot(0, this, &MainWindow::on_btnAdmin_clicked);
#endif
        updateNatsStatusIndicator();
        updatePlcIndicators();
        updateModuleIndicators();
        refreshUi();
        return;
    }

#ifdef Q_OS_ANDROID
    {
        const ConnectionSettings cfg = ConnectionSettings::load();
        appendLog(LogLevel::Info,
                  QStringLiteral("Подключение из config.json: NATS %1, ПЛК %2")
                      .arg(cfg.natsUrl(), cfg.plcEndpoint()));
    }
#endif

    applyConnectionSettings(false);
    refreshUi();
}

MainWindow::~MainWindow()
{
    if (m_connectWatcher && m_connectWatcher->isRunning())
        m_connectWatcher->waitForFinished();
    if (m_execWatcher && m_execWatcher->isRunning())
        m_execWatcher->waitForFinished();
    if (m_stopWatcher && m_stopWatcher->isRunning())
        m_stopWatcher->waitForFinished();
    if (m_plc)
        m_plc->stop();
    if (m_sessionLogFile.isOpen()) {
        m_sessionLogFile.flush();
        syncSessionLogToDisk();
        m_sessionLogFile.close();
    }
    delete ui;
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    fitAndroidCanvas();
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    fitAndroidCanvas();
}

void MainWindow::setupAndroidCanvas()
{
    QWidget *canvas = takeCentralWidget();
    if (!canvas)
        return;

    canvas->setFixedSize(kCanvasWidth, kCanvasHeight);

    auto *view = new QGraphicsView(this);
    view->setFrameShape(QFrame::NoFrame);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setAlignment(Qt::AlignCenter);
    view->setBackgroundBrush(Qt::black);
    view->setRenderHint(QPainter::SmoothPixmapTransform, true);
    view->setResizeAnchor(QGraphicsView::AnchorViewCenter);
    view->setTransformationAnchor(QGraphicsView::AnchorViewCenter);

    auto *scene = new QGraphicsScene(view);
    scene->setSceneRect(0, 0, kCanvasWidth, kCanvasHeight);
    scene->addWidget(canvas);
    view->setScene(scene);

    setCentralWidget(view);
    m_androidView = view;
    m_androidCanvasReady = true;

    setMinimumSize(0, 0);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
#ifndef Q_OS_ANDROID
    setWindowFlag(Qt::FramelessWindowHint);
    setStyleSheet(QStringLiteral("QMainWindow { background:#000000; }"));
#endif
}

void MainWindow::fitAndroidCanvas()
{
    if (!m_androidCanvasReady || !m_androidView || !m_androidView->scene())
        return;

    const QRect vp = m_androidView->viewport()->rect();
    if (vp.width() < 2 || vp.height() < 2)
        return;

    const QRectF scene = m_androidView->scene()->sceneRect();
    const double scale = qMin(vp.width() / scene.width(), vp.height() / scene.height());
    QTransform transform;
    transform.translate(vp.center().x(), vp.center().y());
    transform.scale(scale, scale);
    transform.translate(-scene.center().x(), -scene.center().y());
    m_androidView->setTransform(transform);
}

void MainWindow::enableKeepScreenOn()
{
#ifdef Q_OS_ANDROID
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([] {
        QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (!activity.isValid())
            return;
        QJniObject window = activity.callObjectMethod("getWindow", "()Landroid/view/Window;");
        if (!window.isValid())
            return;
        constexpr jint kFlagKeepScreenOn = 128; // WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
        window.callMethod<void>("addFlags", "(I)V", kFlagKeepScreenOn);
    });
#endif
}

void MainWindow::startPlcClient()
{
    m_plc->start(m_plcHost, m_plcPort, m_plcReadyValue, m_plcReadyMask);
    appendLog(LogLevel::Info,
              QStringLiteral("Подключение к серверу ПЛК (%1:%2), кадр 60 int16. "
                             "Vision 1/2: buffer[%3]/buffer[%4] == 1")
                  .arg(m_plcHost)
                  .arg(m_plcPort)
                  .arg(PlcClient::kVision1IntIndex)
                  .arg(PlcClient::kVision2IntIndex));
    ui->statusPlc->setToolTip(QStringLiteral("%1:%2").arg(m_plcHost).arg(m_plcPort));
    updatePlcIndicators();
}

void MainWindow::applyConnectionSettings(bool notifyOnError)
{
    const ConnectionSettings cfg = ConnectionSettings::load();
    m_natsUrl = cfg.natsUrl();
    m_plcHost = cfg.plcHost;
    m_plcPort = cfg.plcPort;
    m_plcReadyMask = cfg.plcReadyMask;
    m_plcReadyValue = cfg.plcReadyValue;
    if (!cfg.plcPatternWarning.isEmpty())
        appendLog(LogLevel::Warn, cfg.plcPatternWarning);
    ui->statusNats->setToolTip(m_natsUrl);
    startPlcClient();
    beginNatsConnect(notifyOnError);
}

void MainWindow::beginNatsConnect(bool notifyOnError, bool quiet)
{
    if (m_connectWatcher && m_connectWatcher->isRunning()) {
        m_reconnectPending = true;
        m_notifyOnConnectError = notifyOnError;
        return;
    }

    m_reconnectPending = false;
    m_notifyOnConnectError = notifyOnError;

    NatsClient *nats = m_nats;
    const QString url = m_natsUrl;
    const QString bucket = QString::fromUtf8(kNatsScriptBucket);

    std::fprintf(stderr, "NATS: connecting to %s …\n", qPrintable(url));
    std::fflush(stderr);
    if (!quiet) {
        appendLog(LogLevel::Info, QStringLiteral("Подключение к NATS (%1)…").arg(url));
    }

    const QFuture<QString> future = QtConcurrent::run([nats, url, bucket]() -> QString {
        QString err;
        if (!nats->connectToServer(url, &err)) {
            return QStringLiteral("fail\n") +
                   QStringLiteral("NATS: не удалось подключиться (%1): %2").arg(url, err);
        }
        if (!nats->openKvBucket(bucket, &err)) {
            return QStringLiteral("warn\n") +
                   QStringLiteral("NATS подключён, но KV bucket '%1' недоступен: %2")
                       .arg(bucket, err);
        }
        if (!nats->startModuleWatch(&err)) {
            return QStringLiteral("fail\n") +
                   QStringLiteral("Не удалось подписаться на статусы модулей: %1").arg(err);
        }
        return QString();
    });

    if (m_connectWatcher) {
        m_connectWatcher->disconnect(this);
        m_connectWatcher->deleteLater();
    }
    m_connectWatcher = new QFutureWatcher<QString>(this);
    connect(m_connectWatcher, &QFutureWatcher<QString>::finished, this, [this]() {
        const QString result = m_connectWatcher->result();
        const bool notify = m_notifyOnConnectError;

        if (result.isEmpty()) {
            std::fprintf(stderr, "NATS: connected to %s, KV bucket '%s'\n",
                         qPrintable(m_natsUrl), kNatsScriptBucket);
            std::fflush(stderr);
            appendLog(LogLevel::Ok,
                      QStringLiteral("NATS подключён (%1), bucket '%2'")
                          .arg(m_natsUrl, QString::fromUtf8(kNatsScriptBucket)));
            m_prevNatsOk = true;
            m_natsFailureLogged = false;
            appendLog(LogLevel::Info,
                      QStringLiteral("Ожидание CTRL и Behaviour…"));
        } else {
            const bool isWarn = result.startsWith(QLatin1String("warn\n"));
            const QString message = result.section(QLatin1Char('\n'), 1);
            std::fprintf(stderr, "NATS: %s\n", qPrintable(message));
            std::fflush(stderr);
            if (!m_natsFailureLogged) {
                appendLog(isWarn ? LogLevel::Warn : LogLevel::Err, message);
                m_natsFailureLogged = true;
            }
            if (notify) {
                QMessageBox::warning(this, QStringLiteral("NATS"), message);
            }
            m_prevNatsOk = m_nats && m_nats->isConnected();
        }

        updateNatsStatusIndicator();
        updateModuleIndicators();
        refreshUi();

        if (m_reconnectPending)
            beginNatsConnect(m_notifyOnConnectError);
    });
    m_connectWatcher->setFuture(future);
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
    const bool linked = m_plc && m_plc->isOk();
    if (linked) {
        ui->statusPlc->setText(QStringLiteral("●  ПЛК · OK"));
        ui->statusPlc->setStyleSheet(QString::fromUtf8(kPillOk));
    } else {
        ui->statusPlc->setText(QStringLiteral("●  ПЛК · ERR"));
        ui->statusPlc->setStyleSheet(QString::fromUtf8(kPillErr));
    }

    // Стейты станций и сварка нужны только до старта. В работе и при
    // восстановлении кадр меняется, и пульт эти пункты больше не перекрашивает.
    if (m_mode == Mode::Ready) {
        struct BufferCheck {
            QLabel *label;
            int index;
            qint16 expected;
            const char *name;
        };
        const BufferCheck checks[] = {
            {ui->valve1, PlcClient::kLamelStateOffset, PlcClient::kLamelStateReady, "Ламели"},
            {ui->valve2, PlcClient::kGooseStateOffset, PlcClient::kGooseStateReady, "Гуси"},
            {ui->valve3, PlcClient::kBigGooseStateOffset, PlcClient::kBigGooseStateReady, "Б. гуси"},
        };
        for (const BufferCheck &check : checks) {
            if (!check.label)
                continue;
            const qint16 actual = linked && m_plc ? m_plc->statusInt16(check.index) : -1;
            const bool ok = linked && actual == check.expected;
            check.label->setText(QStringLiteral("●\n%1").arg(QString::fromUtf8(check.name)));
            if (!linked) {
                check.label->setStyleSheet(QString::fromUtf8(kModuleErr));
            } else if (ok) {
                check.label->setStyleSheet(QString::fromUtf8(kModuleOk));
            } else {
                check.label->setStyleSheet(QStringLiteral("color:#737880; font-size:11px;"));
            }
        }

        if (ui->valve4) {
            const bool weldOk = linked && m_plc && m_plc->weldingReady();
            ui->valve4->setText(QStringLiteral("●\nСварка"));
            if (!linked) {
                ui->valve4->setStyleSheet(QString::fromUtf8(kModuleErr));
            } else if (weldOk) {
                ui->valve4->setStyleSheet(QString::fromUtf8(kModuleOk));
            } else {
                ui->valve4->setStyleSheet(QStringLiteral("color:#737880; font-size:11px;"));
            }
        }
    }

    if (!linked) {
        ui->nodeWelding->setStyleSheet(
            QStringLiteral("background:#FCEAEA; border:2px solid #D96B6B; border-radius:10px;"));
        ui->nodeWeldingState->setText(QStringLiteral("● ERR"));
        ui->nodeWeldingState->setStyleSheet(QString::fromUtf8(kModuleErr));
    } else {
        ui->nodeWelding->setStyleSheet(
            QStringLiteral("background:#EDF7F0; border:2px solid #59AD6B; border-radius:10px;"));
        ui->nodeWeldingState->setText(QStringLiteral("● Готов"));
        ui->nodeWeldingState->setStyleSheet(QString::fromUtf8(kModuleOk));
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
    const bool ctrlOk = fresh(lastCtrl, now);
    const bool behaviourOk = fresh(lastBehaviour, now);
    const bool vision1Ok = m_plc && m_plc->vision1Ok();
    const bool vision2Ok = m_plc && m_plc->vision2Ok();
    const bool plcLinked = m_plc && m_plc->isOk();
    const bool plcOk = plcReady();

    setModuleIndicator(ui->subCtrl, ctrlOk);
    setModuleIndicator(ui->subBehaviour, behaviourOk);
    setModuleIndicator(ui->subVision1, vision1Ok);
    setModuleIndicator(ui->subVision2, vision2Ok);

    if (ctrlOk && behaviourOk && vision1Ok && vision2Ok) {
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

    const auto logPlc = [this](bool ready) {
        appendLog(ready ? LogLevel::Ok : LogLevel::Err,
                  ready ? QStringLiteral("ПЛК готов: ответ получен")
                        : QStringLiteral("ПЛК недоступен"));
    };

    if (!m_moduleStateInitialized) {
        m_prevNatsOk = natsOk;
        m_prevCtrlOk = ctrlOk;
        m_prevBehaviourOk = behaviourOk;
        m_prevVision1Ok = vision1Ok;
        m_prevVision2Ok = vision2Ok;
        m_prevPlcLinked = plcLinked;
        m_prevPlcOk = plcOk;
        m_moduleStateInitialized = true;
        if (ctrlOk)
            appendLog(LogLevel::Ok, QStringLiteral("CTRL включён"));
        if (behaviourOk)
            appendLog(LogLevel::Ok, QStringLiteral("Behaviour включён"));
        if (vision1Ok)
            appendLog(LogLevel::Ok, QStringLiteral("Vision 1 включён"));
        if (vision2Ok)
            appendLog(LogLevel::Ok, QStringLiteral("Vision 2 включён"));
        if (plcOk)
            logPlc(true);
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
        if (vision1Ok != m_prevVision1Ok) {
            appendLog(vision1Ok ? LogLevel::Ok : LogLevel::Err,
                      vision1Ok ? QStringLiteral("Vision 1 включён")
                                : QStringLiteral("Vision 1 отключён"));
            m_prevVision1Ok = vision1Ok;
        }
        if (vision2Ok != m_prevVision2Ok) {
            appendLog(vision2Ok ? LogLevel::Ok : LogLevel::Err,
                      vision2Ok ? QStringLiteral("Vision 2 включён")
                                : QStringLiteral("Vision 2 отключён"));
            m_prevVision2Ok = vision2Ok;
        }
        if (plcOk != m_prevPlcOk) {
            logPlc(plcOk);
            m_prevPlcOk = plcOk;
            m_prevPlcLinked = plcLinked;
        }
    }

    if (modulesReady()) {
        if (!m_initReadyLogged) {
            appendLog(LogLevel::Ok,
                      QStringLiteral("Инициализация завершена — можно выбрать программу и нажать Старт"));
            m_initReadyLogged = true;
        }
    } else if (m_initReadyLogged) {
        appendLog(LogLevel::Warn, QStringLiteral("Инициализация сброшена: система не готова"));
        m_initReadyLogged = false;
    }

    updateLeftPanel();

    // Не трогаем кнопки, пока идёт асинхронный старт/стоп.
    // Готовность в хедере тоже обновляем по реальному состоянию модулей.
    if (!(m_execWatcher && m_execWatcher->isRunning())
        && !(m_stopWatcher && m_stopWatcher->isRunning())) {
        if (m_mode == Mode::Ready)
            applyModeVisuals();
    }
}

bool MainWindow::plcReady() const
{
    return m_plc && m_plc->isOk();
}

bool MainWindow::plcEstopPressed() const
{
    return m_plc && m_plc->isOk()
        && m_plc->statusInt16(PlcClient::kEstopIntIndex) == PlcClient::kEstopPressedValue;
}

bool MainWindow::modulesReady() const
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 lastCtrl = m_nats ? m_nats->lastCtrlMs() : 0;
    const qint64 lastBehaviour = m_nats ? m_nats->lastBehaviourMs() : 0;
    const bool natsOk = m_nats && m_nats->isConnected();
    const bool ctrlOk = fresh(lastCtrl, now);
    const bool behaviourOk = fresh(lastBehaviour, now);
    const bool vision1Ok = m_plc && m_plc->vision1Ok();
    const bool vision2Ok = m_plc && m_plc->vision2Ok();
    const bool systems = natsOk && ctrlOk && behaviourOk && vision1Ok && vision2Ok && plcReady();
    if (m_mode == Mode::Recovery)
        return false;
    if (m_mode == Mode::Run)
        return systems;
    return systems && bufferReady();
}

bool MainWindow::bufferReady() const
{
    if (!m_plc || !m_plc->isOk())
        return false;
    return m_plc->statusInt16(PlcClient::kLamelStateOffset) == PlcClient::kLamelStateReady
        && m_plc->statusInt16(PlcClient::kGooseStateOffset) == PlcClient::kGooseStateReady
        && m_plc->statusInt16(PlcClient::kBigGooseStateOffset) == PlcClient::kBigGooseStateReady
        && m_plc->weldingReady();
}

void MainWindow::setupChecklistPanel()
{
    m_checklistLayout = new QVBoxLayout(ui->checklistHost);
    m_checklistLayout->setContentsMargins(0, 0, 0, 0);
    m_checklistLayout->setSpacing(10);
    m_checklistLayout->setAlignment(Qt::AlignTop);
    updateLeftPanel();
}

void MainWindow::updateLeftPanel()
{
    if (m_mode == Mode::Run || m_mode == Mode::Recovery)
        updateSuspicionWarnings();
    else
        updateStartupChecklist();
}

void MainWindow::updateSuspicionWarnings()
{
    ui->labelChecklistTitle->setText(QStringLiteral("Замечания"));

    QStringList tasks;
    if (m_plc && m_plc->isOk()) {
        for (int i = PlcClient::kSuspicionFirstIntIndex; i <= PlcClient::kSuspicionLastIntIndex; ++i) {
            if (m_plc->statusInt16(i) != PlcClient::kSuspicionActiveValue)
                continue;
            tasks << QStringLiteral("1|Ячейка %1").arg(i);
        }
    }

    if (tasks == m_lastChecklistTasks && m_leftPanelKind == LeftPanelKind::Warnings)
        return;
    m_leftPanelKind = LeftPanelKind::Warnings;
    m_lastChecklistTasks = tasks;

    while (QLayoutItem *item = m_checklistLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    m_checklistItems.clear();

    for (const QString &task : tasks) {
        const QString label = task.mid(2);
        auto *row = new QLabel(QStringLiteral("⚠  ") + label, ui->checklistHost);
        row->setStyleSheet(QString::fromUtf8(kChecklistWarn));
        row->setWordWrap(true);
        m_checklistLayout->addWidget(row);
        m_checklistItems.push_back(row);
    }
}

void MainWindow::updateStartupChecklist()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 lastCtrl = m_nats ? m_nats->lastCtrlMs() : 0;
    const qint64 lastBehaviour = m_nats ? m_nats->lastBehaviourMs() : 0;
    const bool natsOk = m_nats && m_nats->isConnected();
    const bool ctrlOk = fresh(lastCtrl, now);
    const bool behaviourOk = fresh(lastBehaviour, now);
    const bool vision1Ok = m_plc && m_plc->vision1Ok();
    const bool vision2Ok = m_plc && m_plc->vision2Ok();
    const bool plcOk = plcReady();

    QStringList tasks;
    const auto add = [&](bool done, const QString &label) {
        tasks << (done ? QStringLiteral("1|") : QStringLiteral("0|")) + label;
    };
    add(natsOk, QStringLiteral("Подключение к NATS"));
    add(ctrlOk, QStringLiteral("Готовность CTRL"));
    add(behaviourOk, QStringLiteral("Готовность Behaviour"));
    add(vision1Ok, QStringLiteral("Готовность Vision 1"));
    add(vision2Ok, QStringLiteral("Готовность Vision 2"));
    add(plcOk, QStringLiteral("Готовность ПЛК"));

    bool stationsOk = true;
    {
        const auto intOk = [this](int index, qint16 expected) {
            return m_plc && m_plc->isOk() && m_plc->statusInt16(index) == expected;
        };
        const bool lamellaeOk = intOk(PlcClient::kLamelStateOffset, PlcClient::kLamelStateReady);
        const bool smallGeeseOk = intOk(PlcClient::kGooseStateOffset, PlcClient::kGooseStateReady);
        const bool bigGeeseOk = intOk(PlcClient::kBigGooseStateOffset, PlcClient::kBigGooseStateReady);
        const bool weldOk = m_plc && m_plc->weldingReady();
        add(lamellaeOk, QStringLiteral("Готовность станции ламелей"));
        add(bigGeeseOk, QStringLiteral("Готовность станции б.гусей"));
        add(smallGeeseOk, QStringLiteral("Готовность станции м.гусей"));
        add(weldOk, QStringLiteral("Готовность сварочного модуля"));
        stationsOk = lamellaeOk && smallGeeseOk && bigGeeseOk && weldOk;
    }

    const bool allDone = natsOk && ctrlOk && behaviourOk && vision1Ok && vision2Ok && plcOk
        && stationsOk;
    ui->labelChecklistTitle->setText(allDone ? QStringLiteral("Перед запуском")
                                             : QStringLiteral("Инициализация"));

    if (tasks == m_lastChecklistTasks && m_leftPanelKind == LeftPanelKind::Startup)
        return;
    m_leftPanelKind = LeftPanelKind::Startup;
    m_lastChecklistTasks = tasks;

    while (QLayoutItem *item = m_checklistLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    m_checklistItems.clear();

    for (const QString &task : tasks) {
        const bool done = task.startsWith(QLatin1String("1|"));
        const QString label = task.mid(2);
        auto *row = new QLabel((done ? QStringLiteral("✓  ") : QStringLiteral("●  ")) + label,
                               ui->checklistHost);
        row->setStyleSheet(QString::fromUtf8(done ? kChecklistReady : kChecklistTask));
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

    // Брак: серый, пока 0; красный — как только появился.
    if (m_scrapCount > 0) {
        ui->basketScrap->setStyleSheet(
            QStringLiteral("background:#FDECEC; border:2px solid #C73333; border-radius:10px;"));
        ui->basketScrapTitle->setStyleSheet(
            QStringLiteral("color:#C73333; font-size:12px; font-weight:600;"));
    } else {
        ui->basketScrap->setStyleSheet(
            QStringLiteral("background:#F3F4F6; border:2px solid #C5C9D0; border-radius:10px;"));
        ui->basketScrapTitle->setStyleSheet(
            QStringLiteral("color:#737880; font-size:12px; font-weight:600;"));
    }
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

void MainWindow::onScriptStatus(bool running, bool completed, int /*line*/,
                                 const QString &filename)
{
    if (m_recovery) {
        m_recovery->onScriptStatus(running, completed, filename);
        return;
    }
    if (m_prep) {
        m_prep->onScriptStatus(running, completed, filename);
        return;
    }
    // Завершение подпрограммы предподготовки не является циклом основной программы.
    if (filename == QLatin1String("preProg1.chai")
        || filename == QLatin1String("preProg2.chai")
        || filename == QLatin1String("prep-home.chai")
        || filename == QLatin1String("e-stop_out.chai"))
        return;

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

    if (m_stopAfterCycle) {
        m_stopAfterCycle = false;
        m_mode = Mode::Ready;
        resetUptime();
        appendLog(LogLevel::Info, QStringLiteral("Стоп · цикл завершён, новый не запущен"));
        m_lastChecklistTasks.clear();
        refreshUi();
        updateLeftPanel();
        return;
    }

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
    const char *lineActive = "background:#8C949E; border:none;";
    const char *lineIdle = "background:#C5C9D0; border:none;";

    ui->bunker1->setStyleSheet(QString::fromUtf8(active));
    ui->bunker1Led->setText(QStringLiteral("● Готов"));
    ui->bunker1Led->setStyleSheet(QStringLiteral("color:#2E8C47; font-size:11px;"));

    // B1 всегда в потоке — вертикаль всегда активна.
    ui->lineB1Down->setStyleSheet(QString::fromUtf8(lineActive));
    ui->lineB1Down->setFixedWidth(3);

    if (d1) {
        // Поток Б1+Б2: активны B1→B2 и спуск B2; B3 — idle.
        ui->bunker2->setStyleSheet(QString::fromUtf8(active));
        ui->bunker2Led->setText(QStringLiteral("● Готов"));
        ui->bunker2Led->setStyleSheet(QStringLiteral("color:#2E8C47; font-size:11px;"));
        ui->bunker3->setStyleSheet(QString::fromUtf8(idle));
        ui->bunker3Led->setText(QStringLiteral("● Не в потоке"));
        ui->bunker3Led->setStyleSheet(QStringLiteral("color:#737880; font-size:11px;"));

        ui->lineB2Down->setStyleSheet(QString::fromUtf8(lineActive));
        ui->lineB2Down->setFixedWidth(3);
        ui->lineB3Down->setStyleSheet(QString::fromUtf8(lineIdle));
        ui->lineB3Down->setFixedWidth(2);
        ui->lineB1B2->setStyleSheet(QString::fromUtf8(lineActive));
        ui->lineB1B2->setFixedHeight(3);
        ui->lineB2B3->setStyleSheet(QString::fromUtf8(lineIdle));
        ui->lineB2B3->setFixedHeight(2);
    } else {
        // Поток Б1+Б3: активны B1→B3 через всю рейку; спуск B2 — idle.
        ui->bunker3->setStyleSheet(QString::fromUtf8(active));
        ui->bunker3Led->setText(QStringLiteral("● Готов"));
        ui->bunker3Led->setStyleSheet(QStringLiteral("color:#2E8C47; font-size:11px;"));
        ui->bunker2->setStyleSheet(QString::fromUtf8(idle));
        ui->bunker2Led->setText(QStringLiteral("● Не в потоке"));
        ui->bunker2Led->setStyleSheet(QStringLiteral("color:#737880; font-size:11px;"));

        ui->lineB2Down->setStyleSheet(QString::fromUtf8(lineIdle));
        ui->lineB2Down->setFixedWidth(2);
        ui->lineB3Down->setStyleSheet(QString::fromUtf8(lineActive));
        ui->lineB3Down->setFixedWidth(3);
        ui->lineB1B2->setStyleSheet(QString::fromUtf8(lineActive));
        ui->lineB1B2->setFixedHeight(3);
        ui->lineB2B3->setStyleSheet(QString::fromUtf8(lineActive));
        ui->lineB2B3->setFixedHeight(3);
    }

    ui->lineRailRobot->setStyleSheet(QString::fromUtf8(lineActive));
    ui->lineRailRobot->setFixedWidth(3);

    const bool canChangeProg = (m_mode == Mode::Ready) && !m_prep && !m_prepArming && !m_recovery;
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
            ui->statusReady->setText(QStringLiteral("●  Готовность · INIT"));
            ui->statusReady->setStyleSheet(QString::fromUtf8(kPillInit));
        }
        ui->btnStart->setEnabled(canStart && !m_prepArming && !m_prep && !m_recovery);
        ui->btnProg1->setEnabled(!m_prepArming && !m_prep && !m_recovery);
        ui->btnProg2->setEnabled(!m_prepArming && !m_prep && !m_recovery);
        ui->btnStop->setEnabled(false);
        ui->btnStop->setText(QStringLiteral("СТОП"));
        ui->btnEmergencyStop->setEnabled(true);
        ui->nodeRobotState->setText(canStart ? QStringLiteral("Готов")
                                             : QStringLiteral("Не готов"));
        break;
    case Mode::Run:
        if (m_stopAfterCycle) {
            ui->statusReady->setText(QStringLiteral("●  Готовность · СТОП"));
            ui->statusReady->setStyleSheet(QString::fromUtf8(kPillInit));
            ui->nodeRobotState->setText(QStringLiteral("Дожим цикла"));
        } else {
            ui->statusReady->setText(QStringLiteral("●  Готовность · RUN"));
            ui->statusReady->setStyleSheet(QString::fromUtf8(kPillRun));
            ui->nodeRobotState->setText(QStringLiteral("Работа"));
        }
        ui->btnStart->setEnabled(false);
        ui->btnStop->setEnabled(!m_stopAfterCycle);
        ui->btnStop->setText(m_stopAfterCycle ? QStringLiteral("ПОСЛЕ ЦИКЛА")
                                             : QStringLiteral("СТОП"));
        ui->btnEmergencyStop->setEnabled(true);
        break;
    case Mode::Recovery:
        ui->statusReady->setText(QStringLiteral("●  Готовность · АВАРИЯ"));
        ui->statusReady->setStyleSheet(QString::fromUtf8(kPillErr));
        ui->btnStart->setEnabled(false);
        ui->btnProg1->setEnabled(false);
        ui->btnProg2->setEnabled(false);
        ui->btnStop->setEnabled(false);
        ui->btnStop->setText(QStringLiteral("СТОП"));
        ui->btnEmergencyStop->setEnabled(true);
        ui->nodeRobotState->setText(QStringLiteral("Восстановление"));
        break;
    }
}

void MainWindow::on_btnStart_clicked()
{
    if (m_mode != Mode::Ready)
        return;
    if (m_execWatcher && m_execWatcher->isRunning())
        return;
    if (m_stopWatcher && m_stopWatcher->isRunning())
        return;
    if (!modulesReady()) {
        appendLog(LogLevel::Warn, QStringLiteral("Старт недоступен: система не готова"));
        return;
    }
    if (m_prepArming || m_prep)
        return;

    const QByteArray frame = PlcClient::controlCommand(2);
    m_prepArming = true;
    ui->btnStart->setEnabled(false);
    appendLog(LogLevel::Info, QStringLiteral("Команда старта на ПЛК · 7 2"));
    m_plcAcks.append(PlcAck::Start);
    m_plc->sendCommand(frame);
}

void MainWindow::openPrepOverlay()
{
    if (m_prep)
        return;

    const auto program = m_program == Program::Detail1 ? PrepOverlay::Program::Detail1
                                                       : PrepOverlay::Program::Detail2;
    auto *overlay = new PrepOverlay(program, m_nats, m_plc, ui->centralwidget);
    m_prep = overlay;
    overlay->setGeometry(ui->centralwidget->rect());

    connect(m_plc, &PlcClient::stateChanged, overlay, &PrepOverlay::onPlcState);
    connect(overlay, &PrepOverlay::note, this, [this](PrepOverlay::Note level, const QString &message) {
        LogLevel mapped = LogLevel::Info;
        switch (level) {
        case PrepOverlay::Note::Info: mapped = LogLevel::Info; break;
        case PrepOverlay::Note::Ok: mapped = LogLevel::Ok; break;
        case PrepOverlay::Note::Warn: mapped = LogLevel::Warn; break;
        case PrepOverlay::Note::Err: mapped = LogLevel::Err; break;
        }
        appendLog(mapped, message);
    });
    connect(overlay, &PrepOverlay::startMainRequested, this, [this]() {
        if (m_prep) {
            m_prep->deleteLater();
            m_prep = nullptr;
        }
        startSelectedProgramScript(/*fromLine=*/1);
    });
    connect(overlay, &PrepOverlay::dismissed, this, [this]() {
        if (m_prep) {
            m_prep->deleteLater();
            m_prep = nullptr;
        }
        refreshUi();
    });
    connect(overlay, &PrepOverlay::emergencyStopRequested, this, [this]() {
        closePrepOverlay();
        enterRecovery(QStringLiteral("EMERGENCY STOP · предподготовка"));
    });

    appendLog(LogLevel::Info,
              QStringLiteral("Предподготовка · %1")
                  .arg(m_program == Program::Detail1 ? QStringLiteral("Деталь 1")
                                                    : QStringLiteral("Деталь 2")));
    overlay->show();
    overlay->raise();
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
    ui->btnStop->setEnabled(false);
    ui->btnEmergencyStop->setEnabled(false);

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
            [this, freshSession]() {
        const QString error = m_execWatcher->result();

        if (!error.isEmpty()) {
            appendLog(LogLevel::Err, error);
            QMessageBox::warning(this, QStringLiteral("Ошибка запуска"), error);
            refreshUi();
            return;
        }

        m_mode = Mode::Run;
        if (freshSession) {
            m_stopAfterCycle = false;
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
        m_lastChecklistTasks.clear();
        refreshUi();
        updateLeftPanel();
    });
    m_execWatcher->setFuture(future);
}

void MainWindow::sendStopCommandAsync(const QString &reason, LogLevel logLevel,
                                      bool countScrap)
{
    if (m_stopWatcher && m_stopWatcher->isRunning())
        return;

    NatsClient *nats = m_nats;
    ui->btnStart->setEnabled(false);
    ui->btnStop->setEnabled(false);
    ui->btnEmergencyStop->setEnabled(false);

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
            [this, reason, logLevel, countScrap]() {
        const QString error = m_stopWatcher->result();
        if (!error.isEmpty()) {
            appendLog(LogLevel::Err,
                      QStringLiteral("Не удалось отправить stop: %1").arg(error));
            QMessageBox::warning(this, QStringLiteral("Ошибка"), error);
            refreshUi();
            return;
        }

        // Брак только при EMERGENCY STOP и незавершённом цикле.
        if (countScrap) {
            const bool interruptedCycle = m_cycleRunning || m_cycleAccumulatedMs > 0;
            if (interruptedCycle)
                registerScrap();
        }

        m_mode = Mode::Ready;
        resetUptime();
        resetCycleTimer();
        appendLog(logLevel, reason);
        m_lastChecklistTasks.clear();
        refreshUi();
        updateLeftPanel();
    });
    m_stopWatcher->setFuture(future);
}

void MainWindow::sendStopThenRecover(const QString &reason)
{
    if (m_stopWatcher && m_stopWatcher->isRunning())
        return;

    NatsClient *nats = m_nats;
    ui->btnStart->setEnabled(false);
    ui->btnStop->setEnabled(false);
    ui->btnEmergencyStop->setEnabled(false);

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
    connect(m_stopWatcher, &QFutureWatcher<QString>::finished, this, [this, reason]() {
        const QString error = m_stopWatcher->result();
        if (!error.isEmpty()) {
            appendLog(LogLevel::Err,
                      QStringLiteral("Не удалось отправить stop: %1").arg(error));
            QMessageBox::warning(this, QStringLiteral("Ошибка"), error);
            refreshUi();
            return;
        }

        const bool interruptedCycle = m_cycleRunning || m_cycleAccumulatedMs > 0;
        if (interruptedCycle)
            registerScrap();

        m_mode = Mode::Recovery;
        m_stopAfterCycle = false;
        resetUptime();
        resetCycleTimer();
        appendLog(LogLevel::Err, reason);
        openRecoveryOverlay();
        m_lastChecklistTasks.clear();
        refreshUi();
        updateLeftPanel();
    });
    m_stopWatcher->setFuture(future);
}

void MainWindow::closePrepOverlay()
{
    if (!m_prep)
        return;
    m_prep->deleteLater();
    m_prep = nullptr;
}

void MainWindow::maybeEnterRecoveryFromPlc()
{
    const bool pressed = plcEstopPressed();
    const bool rising = pressed && !m_prevPlcEstop;
    m_prevPlcEstop = pressed;
    if (!rising)
        return;

    enterRecovery(QStringLiteral("EMERGENCY STOP · ПЛК"));
}

void MainWindow::enterRecovery(const QString &reason)
{
    if (m_execWatcher && m_execWatcher->isRunning())
        return;

    sendPlcControl(4);

    if (m_mode == Mode::Recovery && m_recovery) {
        if (m_recovery->isExitRunning())
            m_recovery->abortExitToChecklist();
        return;
    }

    closePrepOverlay();
    m_prepArming = false;
    m_stopAfterCycle = false;
    sendStopThenRecover(reason);
}

void MainWindow::openRecoveryOverlay()
{
    if (m_recovery)
        return;

    auto *overlay = new RecoveryOverlay(m_nats, ui->centralwidget);
    m_recovery = overlay;
    overlay->setGeometry(ui->centralwidget->rect());

    connect(overlay, &RecoveryOverlay::note, this,
            [this](RecoveryOverlay::Note level, const QString &message) {
        LogLevel mapped = LogLevel::Info;
        switch (level) {
        case RecoveryOverlay::Note::Info: mapped = LogLevel::Info; break;
        case RecoveryOverlay::Note::Ok: mapped = LogLevel::Ok; break;
        case RecoveryOverlay::Note::Warn: mapped = LogLevel::Warn; break;
        case RecoveryOverlay::Note::Err: mapped = LogLevel::Err; break;
        }
        appendLog(mapped, message);
    });
    connect(overlay, &RecoveryOverlay::finished, this, &MainWindow::onRecoveryFinished);

    appendLog(LogLevel::Warn, QStringLiteral("Восстановление · чек-лист"));
    overlay->show();
    overlay->raise();
}

void MainWindow::onRecoveryFinished()
{
    if (m_recovery) {
        m_recovery->deleteLater();
        m_recovery = nullptr;
    }

    m_mode = Mode::Ready;
    m_lastChecklistTasks.clear();
    refreshUi();
    updateLeftPanel();

    if (plcEstopPressed()) {
        enterRecovery(QStringLiteral("EMERGENCY STOP · ПЛК после выхода"));
        return;
    }

    sendPlcControl(5);
    appendLog(LogLevel::Ok, QStringLiteral("Восстановление завершено — можно запускать снова"));
}

void MainWindow::on_btnStop_clicked()
{
    if (m_mode != Mode::Run || m_stopAfterCycle)
        return;
    m_stopAfterCycle = true;
    appendLog(LogLevel::Info, QStringLiteral("Стоп · после текущего цикла"));
    sendPlcControl(3);
    refreshUi();
}

void MainWindow::on_btnEmergencyStop_clicked()
{
    if (m_execWatcher && m_execWatcher->isRunning())
        return;
    enterRecovery(QStringLiteral("EMERGENCY STOP"));
}

void MainWindow::selectProgram(Program program)
{
    if (m_mode != Mode::Ready || m_prep || m_prepArming || m_recovery)
        return;

    m_program = program;
    const int id = program == Program::Detail1 ? 1 : 2;
    const QByteArray frame = PlcClient::programSelectCommand(id);
    appendLog(LogLevel::Info,
              program == Program::Detail1
                  ? QStringLiteral("Выбрана Деталь 1 · поток Б1+Б2 · ПЛК 7 1 1")
                  : QStringLiteral("Выбрана Деталь 2 · поток Б1+Б3 · ПЛК 7 1 2"));
    m_plcAcks.append(PlcAck::Program);
    m_plc->sendCommand(frame);
    refreshUi();
}

void MainWindow::sendPlcControl(int code)
{
    appendLog(LogLevel::Info, QStringLiteral("ПЛК · 7 %1").arg(code));
    m_plcAcks.append(PlcAck::Control);
    m_plc->sendCommand(PlcClient::controlCommand(code));
}

void MainWindow::on_btnProg1_clicked()
{
    selectProgram(Program::Detail1);
}

void MainWindow::on_btnProg2_clicked()
{
    selectProgram(Program::Detail2);
}

void MainWindow::on_btnAdmin_clicked()
{
#ifdef Q_OS_ANDROID
    return;
#else
    appendLog(LogLevel::Info, QStringLiteral("Админ: попытка входа"));
    const auto auth = AdminPanel::authenticate(this);
    switch (auth) {
    case AdminPanel::AuthResult::Cancelled:
        appendLog(LogLevel::Info, QStringLiteral("Админ: вход отменён"));
        return;
    case AdminPanel::AuthResult::Denied:
        appendLog(LogLevel::Warn, QStringLiteral("Админ: неверный пароль"));
        return;
    case AdminPanel::AuthResult::Ok:
        appendLog(LogLevel::Ok, QStringLiteral("Админ: вход выполнен"));
        break;
    }

    AdminPanel panel(ConnectionSettings::load(), m_nats, m_plc, this);
    connect(&panel, &AdminPanel::applyRequested, this, [this](const ConnectionSettings &cfg) {
        cfg.save();
        appendLog(LogLevel::Info,
                  QStringLiteral("Настройки применены: NATS %1, ПЛК %2")
                      .arg(cfg.natsUrl(), cfg.plcEndpoint()));
        applyConnectionSettings(true);
    });
    panel.exec();
    appendLog(LogLevel::Info, QStringLiteral("Админ: панель закрыта"));
#endif
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