#include "MainWindow.h"
#include "Texts.h"
#include "ui_MainWindow.h"
#include "AdminPanel.h"
#include "ServicePanel.h"
#include "ConnectionSettings.h"
#include "EstopOverlay.h"
#include "NatsClient.h"
#include "OverlayCard.h"
#include "PlaceOverlay.h"
#include "PrepOverlay.h"
#include "RunHistoryDialog.h"

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
// ПЛК на команды не отвечает: не сменил состояние за это время — не принял.
constexpr int kRequestTimeoutMs = 3000;

const char *kModuleOk = "color:#2E8C47; font-size:11px;";
const char *kModuleErr = "color:#C43B3B; font-size:11px;";
const char *kModuleIdle = "color:#737880; font-size:11px;";

const char *kChecklistTask =
    "background:#FCEAEA; border:1px solid #D96B6B; border-radius:8px;"
    " color:#C43B3B; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
    " font-size:13px; font-weight:600; padding:6px 12px;";

const char *kChecklistReady =
    "background:#EDF7F0; border:1px solid #59AD6B; border-radius:8px;"
    " color:#2E8C47; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
    " font-size:13px; font-weight:600; padding:6px 12px;";

const char *kChecklistWarn =
    "background:#FFF6E5; border:1px solid #E0B15A; border-radius:8px;"
    " color:#8A5A00; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
    " font-size:13px; font-weight:600; padding:6px 12px;";

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

QString formatCount(int n)
{
    // Как в макете: «1 248»
    QString s = QString::number(n);
    for (int i = s.size() - 3; i > 0; i -= 3)
        s.insert(i, QChar(0x00A0)); // неразрывный пробел
    return s;
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_nats(new NatsClient(this))
    , m_plc(new PlcClient(this))
{
    ui->setupUi(this);
    if (ui->statusPowerOff)
        ui->statusPowerOff->raise();
#ifdef Q_OS_ANDROID
    setupAndroidCanvas();
    enableKeepScreenOn();
    ui->btnAdmin->hide();
    if (ui->btnService)
        ui->btnService->hide();
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

    appendLog(LogLevel::Info, tx("Пульт запущен"));
    if (m_sessionLogFile.isOpen()) {
        appendLog(LogLevel::Info,
                  tx("Журнал пишется в файл: %1").arg(m_sessionLogPath));
    } else {
        appendLog(LogLevel::Err,
                  tx("Не удалось открыть файл журнала: %1").arg(m_sessionLogPath));
    }

    // Before LineController: a mode change is logged ahead of what the controller does about it.
    connect(m_plc, &PlcClient::stateChanged, this, &MainWindow::onPlcFrame);
    m_line = new LineController(m_nats, m_plc, this);
    connect(m_line, &LineController::note, this, [this](LineController::Level level, const QString &message) {
        switch (level) {
        case LineController::Level::Info: appendLog(LogLevel::Info, message); break;
        case LineController::Level::Ok: appendLog(LogLevel::Ok, message); break;
        case LineController::Level::Warn: appendLog(LogLevel::Warn, message); break;
        case LineController::Level::Err: appendLog(LogLevel::Err, message); break;
        }
    });
    connect(m_line, &LineController::initStateChanged, this, &MainWindow::syncOverlays);
    connect(m_line, &LineController::cycleFinished, this, &MainWindow::updateKpi);
    connect(m_line, &LineController::runFinished, this, [this](const RunRecord &run) {
        appendLog(LogLevel::Info,
                  tx("Запуск завершён · годные %1, брак %2, циклов %3 · %4")
                      .arg(run.good)
                      .arg(run.scrap)
                      .arg(run.cycles)
                      .arg(run.reason));
    });

    m_requestTimer = new QTimer(this);
    m_requestTimer->setSingleShot(true);
    m_requestTimer->setInterval(kRequestTimeoutMs);
    connect(m_requestTimer, &QTimer::timeout, this, &MainWindow::onRequestTimeout);

    auto *clockTimer = new QTimer(this);
    connect(clockTimer, &QTimer::timeout, this, [this]() {
        updateClock();
        updateModuleIndicators();
        refreshUi();
    });
    clockTimer->start(1000);
    updateClock();

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

    connect(m_plc, &PlcClient::commandFinished, this, [this](bool ok, const QString &error) {
        if (!ok)
            appendLog(LogLevel::Err, error.isEmpty() ? tx("Кадр на ПЛК не отправлен") : error);
    });

    if (!ConnectionSettings::isConfigured()) {
#ifdef Q_OS_ANDROID
        appendLog(LogLevel::Err,
                  tx("Не найден или повреждён config.json. "
                                   "Проверьте line-hmi-qt/config.json и пересоберите APK."));
#else
        appendLog(LogLevel::Info,
                  tx("Укажите адрес NATS и ПЛК в Admin"));
        QTimer::singleShot(0, this, &MainWindow::on_btnAdmin_clicked);
#endif
        refreshUi();
        return;
    }

#ifdef Q_OS_ANDROID
    {
        const ConnectionSettings cfg = ConnectionSettings::load();
        appendLog(LogLevel::Info,
                  tx("Подключение из config.json: NATS %1, ПЛК %2")
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
    delete m_line;
    m_line = nullptr;
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
    m_plc->start(m_plcHost, m_plcPort, m_plcStatusBytes);
    appendLog(LogLevel::Info, tx("Подключение к ПЛК (%1:%2), кадр %3 байт")
                                  .arg(m_plcHost)
                                  .arg(m_plcPort)
                                  .arg(m_plc->statusSize()));
    ui->statusPlc->setToolTip(QStringLiteral("%1:%2").arg(m_plcHost).arg(m_plcPort));
    updatePlcIndicators();
}

void MainWindow::applyConnectionSettings(bool notifyOnError)
{
    const ConnectionSettings cfg = ConnectionSettings::load();
    m_natsUrl = cfg.natsUrl();
    m_plcHost = cfg.plcHost;
    m_plcPort = cfg.plcPort;
    m_plcStatusBytes = cfg.plcStatusBytes;
    m_line->setSettings(cfg);
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
        appendLog(LogLevel::Info, tx("Подключение к NATS (%1)…").arg(url));
    }

    const QFuture<QString> future = QtConcurrent::run([nats, url, bucket]() -> QString {
        QString err;
        if (!nats->connectToServer(url, &err)) {
            return QStringLiteral("fail\n") +
                   tx("NATS: не удалось подключиться (%1): %2").arg(url, err);
        }
        if (!nats->openKvBucket(bucket, &err)) {
            return QStringLiteral("warn\n") +
                   tx("NATS подключён, но KV bucket '%1' недоступен: %2")
                       .arg(bucket, err);
        }
        if (!nats->startModuleWatch(&err)) {
            return QStringLiteral("fail\n") +
                   tx("Не удалось подписаться на статусы модулей: %1").arg(err);
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
                      tx("NATS подключён (%1), bucket '%2'")
                          .arg(m_natsUrl, QString::fromUtf8(kNatsScriptBucket)));
            m_prevNatsOk = true;
            m_natsFailureLogged = false;
            appendLog(LogLevel::Info,
                      tx("Ожидание CTRL и Behaviour…"));
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

        updateModuleIndicators();
        refreshUi();

        if (m_reconnectPending)
            beginNatsConnect(m_notifyOnConnectError);
    });
    m_connectWatcher->setFuture(future);
}

bool MainWindow::plcLinked() const
{
    return m_plc && m_plc->isOk();
}

PlcClient::Mode MainWindow::plcMode() const
{
    return m_plc->mode();
}

bool MainWindow::stopRequested() const
{
    return plcLinked() && plcMode() == PlcClient::Mode::Stopping;
}

bool MainWindow::finishingWork() const
{
    // ПЛК может перейти в «Готов» сразу по 7 3; скрипт, запущенный пультом, при этом ещё идёт.
    return stopRequested() || (plcLinked() && plcMode() == PlcClient::Mode::Ready && m_line->scriptActive());
}

bool MainWindow::lineRunning() const
{
    const PlcClient::Mode mode = plcMode();
    return mode == PlcClient::Mode::Run || mode == PlcClient::Mode::Stopping;
}

bool MainWindow::needsPlacement() const
{
    // Ручную установку определяет пульт: поза робота известна, робот вне зон, а линия не в работе.
    // Без связи с ПЛК окно тоже нужно: робот всё равно должен стоять в зоне к старту.
    if (!m_line->poseKnown() || m_line->zone() > 0)
        return false;
    if (!plcLinked())
        return true;
    const PlcClient::Mode mode = plcMode();
    return mode == PlcClient::Mode::Ready || mode == PlcClient::Mode::Boot;
}

void MainWindow::onPlcFrame()
{
    if (m_pending && m_pending->accepted()) {
        appendLog(LogLevel::Ok, tx("ПЛК принял «%1»").arg(m_pending->name));
        m_pending.reset();
        m_requestTimer->stop();
    }
    logModeChange();
    refreshUi();
}

void MainWindow::logModeChange()
{
    if (!plcLinked())
        return;
    const int mode = int(plcMode());
    const int program = m_plc->statusInt16(PlcClient::kProgramIndex);
    const int prepDone = m_plc->statusInt16(PlcClient::kPrepDoneIndex);
    if (mode != m_loggedMode) {
        const LogLevel level = mode == int(PlcClient::Mode::Estop) ? LogLevel::Err : LogLevel::Info;
        QString text = tx("ПЛК · режим: %1").arg(PlcClient::modeText(PlcClient::Mode(mode)));
        if (mode == int(PlcClient::Mode::Estop)) {
            const QStringList causes = m_line->alarmCauses();
            if (!causes.isEmpty())
                text += tx(" · %1").arg(causes.join(QStringLiteral(", ")));
        }
        appendLog(level, text);
        m_loggedMode = mode;
    }
    if (program != m_loggedProgram) {
        if (program > 0)
            appendLog(LogLevel::Info, tx("ПЛК · выбрана Деталь %1").arg(program));
        m_loggedProgram = program;
    }
    if (prepDone != m_loggedPrepDone) {
        if (prepDone == 1)
            appendLog(LogLevel::Info, tx("ПЛК · предподготовка закончена"));
        m_loggedPrepDone = prepDone;
    }
}

void MainWindow::sendRequest(const QByteArray &frame, const QString &name,
                             std::function<bool()> accepted, std::function<QString()> reason)
{
    QStringList bytes;
    for (char b : frame)
        bytes << QString::number(quint8(b));
    appendLog(LogLevel::Info, tx("%1 · ПЛК %2").arg(name, bytes.join(QLatin1Char(' '))));
    if (m_rejectBox)
        m_rejectBox->close();
    m_pending = PendingRequest{name, std::move(accepted), std::move(reason)};
    m_plc->sendCommand(frame);
    m_plc->pollNow();
    m_requestTimer->start();
    refreshUi();
}

void MainWindow::onRequestTimeout()
{
    if (!m_pending)
        return;
    const PendingRequest request = *m_pending;
    m_pending.reset();
    refreshUi();
    if (request.accepted())
        return;

    QString message = tx("ПЛК не принял «%1»").arg(request.name);
    const QString reason = !plcLinked() ? tx("нет связи с ПЛК")
        : request.reason                ? request.reason()
                                        : QString();
    if (!reason.isEmpty())
        message += tx(": %1").arg(reason);
    appendLog(LogLevel::Warn, message);

    if (m_estop)
        m_estop->setStatus(message);
    else if (m_place)
        m_place->setStatus(message);
    else
        showNotice(request.name, message);
}

void MainWindow::showNotice(const QString &title, const QString &message)
{
    if (m_rejectBox)
        m_rejectBox->close();
    m_rejectBox = new QMessageBox(QMessageBox::Warning, title, message, QMessageBox::Ok, this);
    m_rejectBox->setAttribute(Qt::WA_DeleteOnClose);
    m_rejectBox->setWindowModality(Qt::NonModal);
    m_rejectBox->setStyleSheet(QStringLiteral(
        "QMessageBox{background:#FFFFFF;}"
        " QLabel{color:#1F2126; background:transparent;"
        " font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:15px;}"
        " QPushButton{background:#FFFFFF; color:#1F2126; border:1px solid #DBDEE3; border-radius:6px;"
        " padding:6px 14px; min-height:32px; min-width:72px; font-weight:600;}"
        " QPushButton:pressed{background:#F0F2F5;}"));
    m_rejectBox->show();
}

void MainWindow::refreshUi()
{
    applyProgramVisuals();
    applyModeVisuals();
    updateNatsStatusIndicator();
    updatePlcIndicators();
    updateOutputCounters();
    updatePowerOff();
    updateLamellaType();
    updateChecklist();
    updateKpi();
    syncOverlays();
}

void MainWindow::syncOverlays()
{
    const bool linked = plcLinked();
    const PlcClient::Mode mode = plcMode();
    QWidget *host = ui->centralwidget;

    const bool wantPrep = linked && lineRunning() && !m_plc->prepDone();
    if (wantPrep && !m_prep) {
        m_prep = new PrepOverlay(host);
        connect(m_prep, &PrepOverlay::stopRequested, this, &MainWindow::on_btnStop_clicked);
        m_prep->show();
    } else if (!wantPrep && m_prep) {
        m_prep->deleteLater();
        m_prep = nullptr;
    }
    if (m_prep && wantPrep)
        m_prep->refresh(*m_plc, m_line->initState());

    const bool wantEstop = linked && mode == PlcClient::Mode::Estop;
    if (wantEstop && !m_estop) {
        m_estop = new EstopOverlay(host);
        connect(m_estop, &EstopOverlay::exitRequested, this, [this]() {
            sendRequest(PlcClient::controlCommand(PlcClient::CmdEstopExit), tx("Выйти из E-STOP"),
                        [this] { return plcMode() != PlcClient::Mode::Estop; },
                        [this] { return m_line->alarmCauses().join(QStringLiteral(", ")); });
        });
        m_estop->show();
    } else if (!wantEstop && m_estop) {
        m_estop->deleteLater();
        m_estop = nullptr;
    }
    if (m_estop && wantEstop)
        m_estop->refresh(m_line->alarmCauses(),
                         m_plc->statusInt16(PlcClient::kMushroomIndex) == PlcClient::kMushroomReleased,
                         m_line->zone());

    const bool wantPlace = needsPlacement();
    if (wantPlace && !m_place) {
        m_place = new PlaceOverlay(ConnectionSettings::load().placementImage, host);
        m_place->setTopInset(ui->headerBar->geometry().bottom() + 1);
        connect(m_nats, &NatsClient::motionPoseReceived, m_place,
                [place = m_place](double x, double y, double z, double r, bool) { place->setPose(x, y, z, r); });
        m_place->show();
    } else if (!wantPlace && m_place) {
        m_place->deleteLater();
        m_place = nullptr;
    }
    if (m_place && wantPlace)
        m_place->refresh(m_line->zone(), m_line->poseKnown(), m_line->pose(), m_line->settings().zones);

    const bool wantStopPopup = finishingWork();
    if (wantStopPopup && !m_stopPopup) {
        m_stopPopup = new OverlayCard(tx("Остановка работы"), QSize(760, 180), host);
        m_stopPopup->setStatus(tx("Робот заканчивает текущую работу. Новый цикл не запустится."));
        m_stopPopup->show();
    } else if (!wantStopPopup && m_stopPopup) {
        m_stopPopup->deleteLater();
        m_stopPopup = nullptr;
    }
    if (m_stopPopup && wantStopPopup)
        m_stopPopup->raise();
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
    const bool linked = plcLinked();
    if (linked) {
        ui->statusPlc->setText(tx("●  ПЛК · OK"));
        ui->statusPlc->setStyleSheet(QString::fromUtf8(kPillOk));
    } else {
        ui->statusPlc->setText(tx("●  ПЛК · ERR"));
        ui->statusPlc->setStyleSheet(QString::fromUtf8(kPillErr));
    }

    // В работе клапаны сварки открываются — это не ошибка.
    const bool weldOk = linked && m_plc->weldValvesSafe();
    const bool welding = linked && !weldOk && lineRunning();
    ui->valve4->setText(QStringLiteral("●\n%1").arg(tx("Сварка")));
    ui->valve4->setStyleSheet(QString::fromUtf8(weldOk || welding ? kModuleOk : linked ? kModuleIdle : kModuleErr));

    ui->nodeWelding->setStyleSheet(weldOk || welding
        ? QStringLiteral("background:#EDF7F0; border:2px solid #59AD6B; border-radius:10px;")
        : QStringLiteral("background:#FCEAEA; border:2px solid #D96B6B; border-radius:10px;"));
    ui->nodeWeldingState->setText(weldOk ? tx("● Готов") : welding ? tx("● В работе") : tx("● Не готов"));
    ui->nodeWeldingState->setStyleSheet(QString::fromUtf8(weldOk || welding ? kModuleOk : kModuleErr));
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
    const bool natsOk = m_nats && m_nats->isConnected();
    const bool ctrlOk = fresh(m_nats->lastCtrlMs(), now);
    const bool behaviourOk = fresh(m_nats->lastBehaviourMs(), now);
    const bool vision1Ok = m_plc->vision1Ok();
    const bool vision2Ok = m_plc->vision2Ok();
    const bool plcOk = plcLinked();

    setModuleIndicator(ui->subCtrl, ctrlOk);
    setModuleIndicator(ui->subBehaviour, behaviourOk);
    setModuleIndicator(ui->subVision1, vision1Ok);
    setModuleIndicator(ui->subVision2, vision2Ok);

    if (ctrlOk && behaviourOk && vision1Ok && vision2Ok) {
        ui->statusRobot->setText(tx("●  Робот · OK"));
        ui->statusRobot->setStyleSheet(QString::fromUtf8(kPillOk));
        ui->nodeRobot->setStyleSheet(
            QStringLiteral("background:#EDF7F0; border:2px solid #59AD6B; border-radius:10px;"));
    } else {
        ui->statusRobot->setText(tx("●  Робот · ERR"));
        ui->statusRobot->setStyleSheet(QString::fromUtf8(kPillErr));
        ui->nodeRobot->setStyleSheet(
            QStringLiteral("background:#FCEAEA; border:2px solid #D96B6B; border-radius:10px;"));
    }

    const auto logChange = [this](bool now, bool *prev, const QString &on, const QString &off) {
        if (now == *prev)
            return;
        appendLog(now ? LogLevel::Ok : LogLevel::Err, now ? on : off);
        *prev = now;
    };

    if (!m_moduleStateInitialized) {
        m_moduleStateInitialized = true;
            m_prevNatsOk = natsOk;
        const auto logInitial = [this](bool alive, bool *prev, const QString &on) {
            *prev = alive;
            if (alive)
                appendLog(LogLevel::Ok, on);
        };
        logInitial(ctrlOk, &m_prevCtrlOk, tx("CTRL включён"));
        logInitial(behaviourOk, &m_prevBehaviourOk, tx("Behaviour включён"));
        logInitial(vision1Ok, &m_prevVision1Ok, tx("Vision 1 включён"));
        logInitial(vision2Ok, &m_prevVision2Ok, tx("Vision 2 включён"));
        logInitial(plcOk, &m_prevPlcOk, tx("ПЛК на связи"));
        return;
    }
    logChange(natsOk, &m_prevNatsOk, tx("NATS в сети"), tx("NATS недоступен"));
    logChange(ctrlOk, &m_prevCtrlOk, tx("CTRL включён"), tx("CTRL отключён"));
    logChange(behaviourOk, &m_prevBehaviourOk, tx("Behaviour включён"), tx("Behaviour отключён"));
    logChange(vision1Ok, &m_prevVision1Ok, tx("Vision 1 включён"), tx("Vision 1 отключён"));
    logChange(vision2Ok, &m_prevVision2Ok, tx("Vision 2 включён"), tx("Vision 2 отключён"));
    logChange(plcOk, &m_prevPlcOk, tx("ПЛК на связи"), tx("ПЛК недоступен"));
}

void MainWindow::setupChecklistPanel()
{
    m_checklistLayout = new QVBoxLayout(ui->checklistHost);
    m_checklistLayout->setContentsMargins(0, 0, 0, 0);
    m_checklistLayout->setSpacing(6);
    m_checklistLayout->setAlignment(Qt::AlignTop);
}

void MainWindow::updateChecklist()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool linked = plcLinked();

    // "1|" — выполнено, "0|" — не выполнено, "!|" — замечание ПЛК.
    QStringList tasks;
    const auto add = [&](bool done, const QString &label) {
        tasks << (done ? QStringLiteral("1|") : QStringLiteral("0|")) + label;
    };
    if (linked && plcMode() == PlcClient::Mode::Estop) {
        const QStringList causes = m_line->alarmCauses();
        QString text = causes.isEmpty() ? tx("Авария") : tx("Авария: %1").arg(causes.first());
        if (causes.size() > 1)
            text += tx(" и ещё %1").arg(causes.size() - 1);
        tasks << QStringLiteral("!|") + text;
    }
    if (linked && plcMode() == PlcClient::Mode::Ready && m_plc->statusInt16(PlcClient::kProgramIndex) == 0)
        tasks << QStringLiteral("!|") + tx("Выберите деталь");

    add(m_nats->isConnected(), tx("Подключение к NATS"));
    add(fresh(m_nats->lastCtrlMs(), now), tx("Готовность CTRL"));
    add(fresh(m_nats->lastBehaviourMs(), now), tx("Готовность Behaviour"));
    add(m_plc->vision1Ok(), tx("Готовность Vision 1"));
    add(m_plc->vision2Ok(), tx("Готовность Vision 2"));
    add(linked, tx("Связь с ПЛК"));
    if (linked && lineRunning() && !m_plc->weldValvesSafe())
        add(true, tx("Сварка в работе"));
    else
        add(m_plc->weldValvesSafe(), tx("Клапаны сварки в безопасном положении"));
    const int zone = m_line->zone();
    add(zone > 0, zone > 0 ? tx("Робот в стартовой зоне %1").arg(zone) : tx("Робот в стартовой зоне"));
    add(linked && m_plc->statusInt16(PlcClient::kProgramIndex) > 0, tx("Деталь выбрана"));

    ui->labelChecklistTitle->setText(linked ? PlcClient::modeText(plcMode()) : tx("Нет связи с ПЛК"));

    if (tasks == m_lastChecklistTasks)
        return;
    m_lastChecklistTasks = tasks;

    while (QLayoutItem *item = m_checklistLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    for (const QString &task : tasks) {
        const QChar kind = task.at(0);
        const QString label = task.mid(2);
        const char *style = kind == u'1' ? kChecklistReady : kind == u'!' ? kChecklistWarn : kChecklistTask;
        const QString mark = kind == u'1' ? QStringLiteral("✓  ")
            : kind == u'!'               ? QStringLiteral("⚠  ")
                                          : QStringLiteral("●  ");
        auto *row = new QLabel(mark + label, ui->checklistHost);
        row->setStyleSheet(QString::fromUtf8(style));
        row->setWordWrap(true);
        m_checklistLayout->addWidget(row);
    }
}

void MainWindow::updateClock()
{
    ui->labelClock->setText(QTime::currentTime().toString(QStringLiteral("HH:mm")));
}

void MainWindow::updateKpi()
{
    const qint64 runMs = qMax<qint64>(0, m_line->runElapsedMs());
    const qint64 totalSec = runMs / 1000;
    ui->kpiUptimeValue->setText(QStringLiteral("%1:%2:%3")
                                    .arg(totalSec / 3600, 2, 10, QChar(u'0'))
                                    .arg((totalSec % 3600) / 60, 2, 10, QChar(u'0'))
                                    .arg(totalSec % 60, 2, 10, QChar(u'0')));

    const qint64 cycleMs = m_line->lastCycleMs();
    if (cycleMs < 0) {
        ui->kpiCycleValue->setText(QStringLiteral("—"));
        return;
    }
    const double sec = cycleMs / 1000.0;
    ui->kpiCycleValue->setText(tx("%1 с").arg(sec, 0, 'f', sec < 10.0 ? 1 : 0));
}

void MainWindow::updateOutputCounters()
{
    const int good = m_plc->statusInt16(PlcClient::kGoodIndex);
    const int scrap = m_plc->statusInt16(PlcClient::kScrapIndex);
    const QString goodText = formatCount(good);
    const QString scrapText = formatCount(scrap);

    ui->basketOkCount->setText(goodText);
    ui->basketScrapCount->setText(scrapText);
    ui->kpiOutputValue->setText(goodText);
    ui->kpiScrapValue->setText(scrapText);

    // Брак: серый, пока 0; красный — как только появился.
    if (scrap > 0) {
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

void MainWindow::updatePowerOff()
{
    if (!ui->statusPowerOff)
        return;
    const bool allow = plcLinked() && m_plc->statusInt16(PlcClient::kPowerOffIndex) == 1;
    ui->statusPowerOff->setText(allow ? tx("Можно выключать питание · ДА")
                                      : tx("Можно выключать питание · НЕТ"));
    ui->statusPowerOff->setStyleSheet(QString::fromUtf8(allow ? kPillOk : kPillErr)
                                      + QStringLiteral(" font-size:14px; font-weight:700;"));
}

void MainWindow::updateLamellaType()
{
    if (!plcLinked()) {
        ui->bunker1Issue->setText(tx("На выдаче: —"));
        return;
    }
    const int type = m_plc->statusInt16(PlcClient::kLamellaTypeIndex);
    ui->bunker1Issue->setText(type == 0 ? tx("На выдаче: пусто")
                                         : tx("На выдаче: тип %1").arg(type));
}

void MainWindow::applyProgramVisuals()
{
    const int program = m_plc->statusInt16(PlcClient::kProgramIndex);
    const bool d1 = program == 1;
    const bool d2 = program == 2;
    ui->btnProg1->setStyleSheet(QString::fromUtf8(d1 ? kProgSelected : kProgIdle));
    ui->btnProg2->setStyleSheet(QString::fromUtf8(d2 ? kProgSelected : kProgIdle));

    ui->btnProg1->setText(d1
                              ? tx("Деталь 1\nВыбрано\nбольшие держатели")
                              : tx("Деталь 1\nДоступно\nбольшие держатели"));
    ui->btnProg2->setText(d2
                              ? tx("Деталь 2\nВыбрано\nмалые держатели")
                              : tx("Деталь 2\nДоступно\nмалые держатели"));

    ui->labelFlow->setText(d1   ? tx("Поток: ламели + большие держатели")
                           : d2 ? tx("Поток: ламели + малые держатели")
                                : tx("Поток: программа не выбрана"));
    ui->labelFeed->setText(d1   ? tx("подача · ламели + большие держатели")
                           : d2 ? tx("подача · ламели + малые держатели")
                                : tx("подача · —"));

    // Active bunkers by program
    const char *active =
        "background:#EDF7F0; border:2px solid #59AD6B; border-radius:8px;";
    const char *idle =
        "background:#F3F4F6; border:2px solid #C5C9D0; border-radius:8px;";
    const char *lineActive = "background:#8C949E; border:none;";
    const char *lineIdle = "background:#C5C9D0; border:none;";

    ui->bunker1->setStyleSheet(QString::fromUtf8(active));
    ui->bunker1Led->setText(tx("● Готов"));
    ui->bunker1Led->setStyleSheet(QStringLiteral("color:#2E8C47; font-size:11px;"));

    // B1 всегда в потоке — вертикаль всегда активна.
    ui->lineB1Down->setStyleSheet(QString::fromUtf8(lineActive));
    ui->lineB1Down->setFixedWidth(3);

    if (d1) {
        // Поток Б1+Б2: активны B1→B2 и спуск B2; B3 — idle.
        ui->bunker2->setStyleSheet(QString::fromUtf8(active));
        ui->bunker2Led->setText(tx("● Готов"));
        ui->bunker2Led->setStyleSheet(QStringLiteral("color:#2E8C47; font-size:11px;"));
        ui->bunker3->setStyleSheet(QString::fromUtf8(idle));
        ui->bunker3Led->setText(tx("● Не в потоке"));
        ui->bunker3Led->setStyleSheet(QStringLiteral("color:#737880; font-size:11px;"));

        ui->lineB2Down->setStyleSheet(QString::fromUtf8(lineActive));
        ui->lineB2Down->setFixedWidth(3);
        ui->lineB3Down->setStyleSheet(QString::fromUtf8(lineIdle));
        ui->lineB3Down->setFixedWidth(2);
        ui->lineB1B2->setStyleSheet(QString::fromUtf8(lineActive));
        ui->lineB1B2->setFixedHeight(3);
        ui->lineB2B3->setStyleSheet(QString::fromUtf8(lineIdle));
        ui->lineB2B3->setFixedHeight(2);
    } else if (!d2) {
        // Программа не выбрана: Б2 и Б3 не в потоке.
        for (QWidget *bunker : {ui->bunker2, ui->bunker3})
            bunker->setStyleSheet(QString::fromUtf8(idle));
        for (QLabel *led : {ui->bunker2Led, ui->bunker3Led}) {
            led->setText(tx("● Не выбрано"));
            led->setStyleSheet(QStringLiteral("color:#737880; font-size:11px;"));
        }
        ui->lineB2Down->setStyleSheet(QString::fromUtf8(lineIdle));
        ui->lineB2Down->setFixedWidth(2);
        ui->lineB3Down->setStyleSheet(QString::fromUtf8(lineIdle));
        ui->lineB3Down->setFixedWidth(2);
        ui->lineB1B2->setStyleSheet(QString::fromUtf8(lineIdle));
        ui->lineB1B2->setFixedHeight(2);
        ui->lineB2B3->setStyleSheet(QString::fromUtf8(lineIdle));
        ui->lineB2B3->setFixedHeight(2);
    } else {
        // Поток Б1+Б3: активны B1→B3 через всю рейку; спуск B2 — idle.
        ui->bunker3->setStyleSheet(QString::fromUtf8(active));
        ui->bunker3Led->setText(tx("● Готов"));
        ui->bunker3Led->setStyleSheet(QStringLiteral("color:#2E8C47; font-size:11px;"));
        ui->bunker2->setStyleSheet(QString::fromUtf8(idle));
        ui->bunker2Led->setText(tx("● Не в потоке"));
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
}

void MainWindow::applyModeVisuals()
{
    const bool linked = plcLinked();
    const PlcClient::Mode mode = plcMode();
    const bool stopping = finishingWork();
    const bool busy = m_pending.has_value();
    const bool robotBusy = m_line->robotBusy();

    const auto pill = [this](const QString &text, const char *style) {
        ui->statusReady->setText(text);
        ui->statusReady->setStyleSheet(QString::fromUtf8(style));
    };
    if (!linked) {
        pill(tx("●  Готовность · НЕТ ПЛК"), kPillErr);
        } else {
        switch (mode) {
        case PlcClient::Mode::Ready:
            if (stopping)
                pill(tx("●  Готовность · СТОП"), kPillInit);
            else if (needsPlacement())
                pill(tx("●  Готовность · УСТАНОВКА"), kPillInit);
            else if (m_plc->statusInt16(PlcClient::kProgramIndex) > 0)
                pill(tx("●  Готовность · OK"), kPillOk);
            else
                pill(tx("●  Готовность · НЕТ ДЕТАЛИ"), kPillInit);
        break;
        case PlcClient::Mode::Stopping:
            pill(tx("●  Готовность · СТОП"), kPillInit);
            break;
        case PlcClient::Mode::Run:
            pill(m_plc->prepDone() ? tx("●  Готовность · RUN") : tx("●  Готовность · PREP"), kPillRun);
            break;
        case PlcClient::Mode::Estop:
            pill(tx("●  Готовность · АВАРИЯ"), kPillErr);
            break;
        case PlcClient::Mode::Service:
            pill(tx("●  Готовность · СЕРВИС"), kPillInit);
            break;
        case PlcClient::Mode::Boot:
            pill(tx("●  Готовность · ЗАГРУЗКА"), kPillInit);
        break;
    }
    }

    ui->btnStart->setEnabled(linked && mode == PlcClient::Mode::Ready && m_line->zone() > 0 && !busy
                             && !robotBusy);
    ui->btnStop->setEnabled(linked && mode == PlcClient::Mode::Run);
    ui->btnStop->setText(stopping ? tx("ОСТАНОВКА…") : tx("СТОП"));
    const bool canSelect = linked && !busy && !robotBusy
        && (mode == PlcClient::Mode::Ready || mode == PlcClient::Mode::Boot);
    ui->btnProg1->setEnabled(canSelect);
    ui->btnProg2->setEnabled(canSelect);
    ui->nodeRobotState->setText(linked ? PlcClient::modeText(mode) : tx("Нет связи с ПЛК"));
}

void MainWindow::on_btnStart_clicked()
{
    if (m_line->robotBusy()) {
        appendLog(LogLevel::Warn, tx("Старт: робот ещё выполняет программу"));
        showNotice(tx("Старт"), tx("Робот ещё заканчивает программу. Дождитесь её конца"));
        return;
    }
    if (m_plc->statusInt16(PlcClient::kProgramIndex) == 0) {
        appendLog(LogLevel::Warn, tx("Старт: деталь не выбрана"));
        showNotice(tx("Старт"), tx("Сначала выберите деталь, затем нажмите «Старт»"));
        return;
    }
    sendRequest(PlcClient::controlCommand(PlcClient::CmdStart), tx("Старт"),
                [this] { return plcMode() != PlcClient::Mode::Ready; });
}

void MainWindow::on_btnStop_clicked()
{
    sendRequest(PlcClient::controlCommand(PlcClient::CmdStop), tx("Стоп"),
                [this] { return plcMode() != PlcClient::Mode::Run; });
}

void MainWindow::selectProgram(int program)
{
    const bool useLoadCell = !ConnectionSettings::load().ignoreLoadCell;
    sendRequest(PlcClient::selectCommand(program, useLoadCell), tx("Деталь %1").arg(program),
                [this, program] { return m_plc->statusInt16(PlcClient::kProgramIndex) == program; });
}

void MainWindow::on_btnProg1_clicked()
{
    selectProgram(1);
}

void MainWindow::on_btnProg2_clicked()
{
    selectProgram(2);
}

void MainWindow::on_btnHistory_clicked()
{
    RunHistoryDialog dialog(this);
    dialog.exec();
}

void MainWindow::on_btnService_clicked()
{
#ifdef Q_OS_ANDROID
    return;
#else
    appendLog(LogLevel::Info, tx("Сервис: панель открыта"));
    ServicePanel panel(m_plc, this);
    panel.exec();
    appendLog(LogLevel::Info, tx("Сервис: панель закрыта"));
    refreshUi();
#endif
}

void MainWindow::on_btnAdmin_clicked()
{
#ifdef Q_OS_ANDROID
    return;
#else
    appendLog(LogLevel::Info, tx("Админ: попытка входа"));
    const auto auth = AdminPanel::authenticate(this);
    switch (auth) {
    case AdminPanel::AuthResult::Cancelled:
        appendLog(LogLevel::Info, tx("Админ: вход отменён"));
        return;
    case AdminPanel::AuthResult::Denied:
        appendLog(LogLevel::Warn, tx("Админ: неверный пароль"));
        return;
    case AdminPanel::AuthResult::Ok:
        appendLog(LogLevel::Ok, tx("Админ: вход выполнен"));
        break;
    }

    AdminPanel panel(ConnectionSettings::load(), m_nats, m_plc, this);
    connect(&panel, &AdminPanel::applyRequested, this, [this](const ConnectionSettings &cfg) {
        cfg.save();
        appendLog(LogLevel::Info,
                  tx("Настройки применены: NATS %1, ПЛК %2")
                      .arg(cfg.natsUrl(), cfg.plcEndpoint()));
        applyConnectionSettings(true);
    });
    connect(&panel, &AdminPanel::settingsChanged, this, [this](const ConnectionSettings &cfg) {
        m_line->setSettings(cfg);
    });
    panel.exec();
    appendLog(LogLevel::Info, tx("Админ: панель закрыта"));
    refreshUi();
#endif
}

void MainWindow::on_btnExportLog_clicked()
{
    const QString defaultName =
        QStringLiteral("line-hmi-%1.log")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const QString path = QFileDialog::getSaveFileName(
        this,
        tx("Выгрузить журнал событий"),
        QDir::homePath() + QLatin1Char('/') + defaultName,
        QStringLiteral("Log files (*.log);;All files (*)"));
    if (path.isEmpty())
        return;

    QString err;
    if (!exportLogsToFile(path, &err)) {
        appendLog(LogLevel::Err, tx("Не удалось выгрузить журнал: %1").arg(err));
        QMessageBox::warning(this, tx("Журнал"),
                             tx("Не удалось сохранить файл:\n%1").arg(err));
        return;
    }

    appendLog(LogLevel::Ok, tx("Журнал выгружен: %1").arg(path));
    QMessageBox::information(this, tx("Журнал"),
                             tx("Сохранено %1 записей в:\n%2")
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
