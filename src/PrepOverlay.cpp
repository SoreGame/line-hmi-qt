#include "PrepOverlay.h"

#include "NatsClient.h"
#include "PlcClient.h"

#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

namespace {

constexpr double kHomeDurationSec = 5.0;
const char *kHomePoint = "home";
const char *kHomeScriptName = "prep-home.chai";

const char *kTaskNames[] = {
    "Заполняем ламели",
    "Заполняем гуси",
    "Робот готов",
    "Сварка готова",
};

const char *kStylePending =
    "background:#F3F4F6; border:1px solid #C5C9D0; border-radius:8px;"
    " color:#737880; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
    " font-size:18px; font-weight:600; padding:16px 18px;";

const char *kStyleActive =
    "background:#FFF6E5; border:1px solid #E0B15A; border-radius:8px;"
    " color:#8A5A00; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
    " font-size:18px; font-weight:600; padding:16px 18px;";

const char *kStyleDone =
    "background:#EDF7F0; border:1px solid #59AD6B; border-radius:8px;"
    " color:#2E8C47; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
    " font-size:18px; font-weight:600; padding:16px 18px;";

const char *kStyleError =
    "background:#FCEAEA; border:1px solid #D96B6B; border-radius:8px;"
    " color:#C43B3B; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
    " font-size:18px; font-weight:600; padding:16px 18px;";

const char *kBtnBegin =
    "QPushButton{background:#388F51; color:white; border:none; border-radius:10px;"
    " font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:18px; font-weight:700;}"
    " QPushButton:pressed{background:#2F7A44;}"
    " QPushButton:disabled{background:#A9B0B8; color:white;}";

const char *kBtnStop =
    "QPushButton{background:#C45C1A; color:white; border:none; border-radius:10px;"
    " font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:18px; font-weight:700;}"
    " QPushButton:pressed{background:#A34C16;}"
    " QPushButton:disabled{background:#A9B0B8; color:white;}";

const char *kBtnEmergency =
    "QPushButton{background:#C72929; color:white; border:2px solid #8B1A1A; border-radius:10px;"
    " font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:16px; font-weight:800;}"
    " QPushButton:pressed{background:#A82222;}"
    " QPushButton:disabled{background:#A9B0B8; color:white; border-color:#A9B0B8;}";

} // namespace

PrepOverlay::PrepOverlay(Program program, NatsClient *nats, PlcClient *plc, QWidget *parent)
    : QWidget(parent)
    , m_program(program)
    , m_nats(nats)
    , m_plc(plc)
{
    setAttribute(Qt::WA_StyledBackground, false);
    setFocusPolicy(Qt::StrongFocus);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addStretch();

    auto *row = new QHBoxLayout();
    row->addStretch();

    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("prepCard"));
    card->setFixedSize(1080, 620);
    card->setStyleSheet(QStringLiteral(
        "QFrame#prepCard { background:#FFFFFF; border-radius:16px; }"));

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(36, 32, 36, 32);
    cardLayout->setSpacing(16);

    auto *title = new QLabel(QStringLiteral("Список задач"), card);
    title->setStyleSheet(QStringLiteral(
        "color:#1F2126; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        " font-size:28px; font-weight:700; background:transparent;"));
    cardLayout->addWidget(title);

    const QString programName = program == Program::Detail1
                                    ? QStringLiteral("Деталь 1 · preProg1.chai")
                                    : QStringLiteral("Деталь 2 · preProg2.chai");
    auto *subtitle = new QLabel(programName, card);
    subtitle->setStyleSheet(QStringLiteral(
        "color:#737880; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        " font-size:15px; font-weight:600; background:transparent;"));
    cardLayout->addWidget(subtitle);

    m_status = new QLabel(QStringLiteral("Предподготовка запущена"), card);
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral(
        "color:#1F2126; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        " font-size:15px; background:transparent;"));
    cardLayout->addWidget(m_status);

    auto *body = new QHBoxLayout();
    body->setSpacing(28);

    auto *tasks = new QVBoxLayout();
    tasks->setSpacing(12);
    for (int i = 0; i < 4; ++i) {
        m_rows[i] = new QLabel(card);
        m_rows[i]->setWordWrap(true);
        m_rows[i]->setMinimumHeight(64);
        setTask(i, m_tasks[i]);
        tasks->addWidget(m_rows[i]);
    }
    tasks->addStretch();
    body->addLayout(tasks, 1);

    auto *actions = new QVBoxLayout();
    actions->setSpacing(12);

    m_btnBegin = new QPushButton(QStringLiteral("Начать выполнение"), card);
    m_btnStop = new QPushButton(QStringLiteral("Стоп"), card);
    m_btnEmergency = new QPushButton(QStringLiteral("Экстренная остановка"), card);
    for (QPushButton *button : {m_btnBegin, m_btnStop, m_btnEmergency}) {
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        button->setFixedSize(300, 68);
    }
    m_btnBegin->setStyleSheet(QString::fromUtf8(kBtnBegin));
    m_btnStop->setStyleSheet(QString::fromUtf8(kBtnStop));
    m_btnEmergency->setStyleSheet(QString::fromUtf8(kBtnEmergency));

    actions->addWidget(m_btnBegin);
    actions->addWidget(m_btnStop);
    actions->addWidget(m_btnEmergency);
    actions->addStretch();
    body->addLayout(actions);

    cardLayout->addLayout(body, 1);

    row->addWidget(card);
    row->addStretch();
    root->addLayout(row);
    root->addStretch();

    connect(m_btnBegin, &QPushButton::clicked, this, [this]() {
        if (m_phase != Phase::Running)
            return;
        for (TaskState state : m_tasks) {
            if (state != TaskState::Done)
                return;
        }
        m_phase = Phase::Exit;
        refreshButtons();
        emit startMainRequested();
    });
    connect(m_btnStop, &QPushButton::clicked, this, &PrepOverlay::beginSmoothStop);
    connect(m_btnEmergency, &QPushButton::clicked, this, &PrepOverlay::beginEmergencyStop);

    refreshButtons();
}

void PrepOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), QColor(31, 33, 38, 150));
}

void PrepOverlay::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (QWidget *parent = parentWidget())
        setGeometry(parent->rect());
    raise();
    onPlcState();
}

QString PrepOverlay::preprogFilename() const
{
    return m_program == Program::Detail1 ? QStringLiteral("preProg1.chai")
                                         : QStringLiteral("preProg2.chai");
}

void PrepOverlay::setTask(int index, TaskState state)
{
    m_tasks[index] = state;
    if (!m_rows[index])
        return;

    const QString label = QString::fromUtf8(kTaskNames[index]);
    const char *style = kStylePending;
    QString mark = QStringLiteral("●  ");
    switch (state) {
    case TaskState::Pending:
        break;
    case TaskState::Active:
        style = kStyleActive;
        break;
    case TaskState::Done:
        style = kStyleDone;
        mark = QStringLiteral("✓  ");
        break;
    case TaskState::Error:
        style = kStyleError;
        mark = QStringLiteral("!  ");
        break;
    }
    m_rows[index]->setText(mark + label);
    m_rows[index]->setStyleSheet(QString::fromUtf8(style));
}

void PrepOverlay::refreshButtons()
{
    bool allDone = true;
    for (TaskState state : m_tasks) {
        if (state != TaskState::Done)
            allDone = false;
    }
    m_btnBegin->setEnabled(m_phase == Phase::Running && allDone);
    m_btnStop->setEnabled(m_phase == Phase::Running);
    m_btnEmergency->setEnabled(m_phase != Phase::Exit);
    if (m_phase == Phase::Running && allDone && m_status)
        m_status->setText(QStringLiteral("Все задачи выполнены — можно начать выполнение"));
}

void PrepOverlay::onPlcState()
{
    if (m_phase != Phase::Running || !m_plc)
        return;

    const bool lamellae = m_plc->bytesAreOne(PlcClient::kLamellaeOffset, 1);
    const bool geese = m_plc->bytesAreOne(PlcClient::kGeeseOffset, 1);
    const bool weld = m_plc->bytesAreOne(PlcClient::kWeldOffset, PlcClient::kWeldSensorCount);

    if (m_tasks[0] != TaskState::Done && m_tasks[0] != TaskState::Error) {
        if (lamellae) {
            setTask(0, TaskState::Done);
            emit note(Note::Ok, QStringLiteral("Ламели заполнены"));
        } else {
            setTask(0, TaskState::Active);
        }
    }
    if (m_tasks[1] != TaskState::Done && m_tasks[1] != TaskState::Error) {
        if (geese) {
            setTask(1, TaskState::Done);
            emit note(Note::Ok, QStringLiteral("Гуси заполнены"));
        } else {
            setTask(1, TaskState::Active);
        }
    }
    if (m_tasks[3] != TaskState::Done && m_tasks[3] != TaskState::Error) {
        if (weld) {
            setTask(3, TaskState::Done);
            emit note(Note::Ok, QStringLiteral("Сварка готова"));
        } else {
            setTask(3, TaskState::Active);
        }
    }

    considerLaunchPreprog();
    refreshButtons();
}

void PrepOverlay::considerLaunchPreprog()
{
    if (m_phase != Phase::Running || m_preprogStarted)
        return;
    if (m_tasks[0] != TaskState::Done || m_tasks[1] != TaskState::Done)
        return;

    m_preprogStarted = true;
    setTask(2, TaskState::Active);
    m_waitFilename = preprogFilename();
    m_sawScriptRunning = false;
    m_status->setText(QStringLiteral("Запуск подпрограммы %1").arg(m_waitFilename));
    emit note(Note::Info,
              QStringLiteral("Ламели и гуси готовы · запуск '%1'").arg(m_waitFilename));
    publishScript(m_waitFilename, QString(), /*fromKv=*/true);
}

void PrepOverlay::publishScript(const QString &filename, const QString &inlineCode, bool fromKv)
{
    const int op = m_epoch->load(std::memory_order_relaxed);
    auto gate = m_gate;
    auto epoch = m_epoch;
    NatsClient *nats = m_nats;
    const QString code = inlineCode;

    const QFuture<QString> future = QtConcurrent::run(
        [gate, epoch, op, nats, filename, code, fromKv]() -> QString {
            std::lock_guard<std::mutex> lock(*gate);
            if (epoch->load(std::memory_order_relaxed) != op)
                return QStringLiteral("cancelled");
            if (!nats) {
                return QStringLiteral("Нет соединения с NATS");
            }
            QString script = code;
            QString err;
            if (fromKv) {
                if (!nats->fetchScriptCode(filename, &script, &err)) {
                    return QStringLiteral("Не удалось получить скрипт '%1' из NATS KV: %2")
                        .arg(filename, err);
                }
            }
            if (epoch->load(std::memory_order_relaxed) != op)
                return QStringLiteral("cancelled");
            if (!nats->publishExecCommand(filename, script, 1, &err))
                return QStringLiteral("Не удалось отправить команду запуска: %1").arg(err);
            return QString();
        });

    if (m_scriptWatcher) {
        m_scriptWatcher->disconnect(this);
        m_scriptWatcher->deleteLater();
    }
    m_scriptWatcher = new QFutureWatcher<QString>(this);
    connect(m_scriptWatcher, &QFutureWatcher<QString>::finished, this, [this, filename]() {
        const QString error = m_scriptWatcher->result();
        if (error == QLatin1String("cancelled") || m_phase == Phase::Exit)
            return;
        if (error.isEmpty())
            return;

        if (filename == QLatin1String(kHomeScriptName)) {
            emit note(Note::Err, error);
            powerOffRobot(QStringLiteral("Стоп · не удалось отвести робота: %1").arg(error),
                          Note::Err);
            return;
        }

        if (m_phase != Phase::Running)
            return;
        setTask(2, TaskState::Error);
        m_status->setText(error);
        emit note(Note::Err, error);
        refreshButtons();
    });
    m_scriptWatcher->setFuture(future);
}

void PrepOverlay::onScriptStatus(bool running, bool completed, const QString &filename)
{
    if (m_phase == Phase::Exit || m_waitFilename.isEmpty() || filename != m_waitFilename)
        return;

    if (running) {
        m_sawScriptRunning = true;
        if (m_status) {
            m_status->setText(filename == QLatin1String(kHomeScriptName)
                                  ? QStringLiteral("Отвод в домашнюю точку")
                                  : QStringLiteral("Подпрограмма выполняется"));
        }
    }
    if (running || (!completed && !m_sawScriptRunning))
        return;

    const bool home = filename == QLatin1String(kHomeScriptName);
    m_waitFilename.clear();

    if (completed) {
        if (home) {
            m_status->setText(QStringLiteral("Робот в домашней точке, выключение"));
            powerOffRobot(QStringLiteral("Стоп · робот в домашней точке, приводы выключены. ПЛК работает"),
                          Note::Ok);
            return;
        }
        setTask(2, TaskState::Done);
        m_status->setText(QStringLiteral("Подпрограмма завершена, робот в точке"));
        emit note(Note::Ok, QStringLiteral("Робот готов · '%1' завершён").arg(filename));
        refreshButtons();
        return;
    }

    if (home) {
        emit note(Note::Err, QStringLiteral("Отвод в домашнюю точку прерван"));
        powerOffRobot(QStringLiteral("Стоп · отвод прерван, приводы выключены"), Note::Warn);
        return;
    }

    setTask(2, TaskState::Error);
    m_status->setText(QStringLiteral("Подпрограмма остановилась с ошибкой"));
    emit note(Note::Err, QStringLiteral("Подпрограмма '%1' не завершилась").arg(filename));
    refreshButtons();
}

void PrepOverlay::beginSmoothStop()
{
    if (m_phase != Phase::Running)
        return;

    m_phase = Phase::Homing;
    {
        std::lock_guard<std::mutex> lock(*m_gate);
        m_epoch->fetch_add(1, std::memory_order_relaxed);
    }
    m_waitFilename = QString::fromUtf8(kHomeScriptName);
    m_sawScriptRunning = false;
    m_status->setText(QStringLiteral("Плавная остановка: отвод в домашнюю точку, ПЛК не выключается"));
    refreshButtons();
    emit note(Note::Info, QStringLiteral("Стоп · отвод робота в точку '%1', ПЛК остаётся включён")
                              .arg(QString::fromUtf8(kHomePoint)));

    const QString code = QStringLiteral("moveL(\"%1\", %2);\n")
                             .arg(QString::fromUtf8(kHomePoint))
                             .arg(kHomeDurationSec, 0, 'f', 0);
    publishScript(m_waitFilename, code, /*fromKv=*/false);
}

void PrepOverlay::beginEmergencyStop()
{
    if (m_phase == Phase::Exit)
        return;

    m_phase = Phase::Exit;
    m_waitFilename.clear();
    m_status->setText(QStringLiteral("Экстренная остановка"));
    refreshButtons();

    auto gate = m_gate;
    auto epoch = m_epoch;
    NatsClient *nats = m_nats;
    const QFuture<QString> future = QtConcurrent::run([gate, epoch, nats]() -> QString {
        std::lock_guard<std::mutex> lock(*gate);
        epoch->fetch_add(1, std::memory_order_relaxed);
        if (!nats)
            return QStringLiteral("Нет соединения с NATS");
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
    connect(m_stopWatcher, &QFutureWatcher<QString>::finished, this, [this]() {
        const QString error = m_stopWatcher->result();
        if (!error.isEmpty()) {
            failStop(QStringLiteral("Экстренная остановка не отправлена: %1").arg(error));
            return;
        }
        emit note(Note::Err, QStringLiteral("Экстренная остановка"));
        emit emergencyStopRequested();
    });
    m_stopWatcher->setFuture(future);
}

void PrepOverlay::powerOffRobot(const QString &doneMessage, Note level)
{
    m_phase = Phase::Exit;
    m_waitFilename.clear();
    refreshButtons();

    auto gate = m_gate;
    auto epoch = m_epoch;
    NatsClient *nats = m_nats;
    const int op = m_epoch->load(std::memory_order_relaxed);
    const QFuture<QString> future = QtConcurrent::run([gate, epoch, op, nats]() -> QString {
        std::lock_guard<std::mutex> lock(*gate);
        if (epoch->load(std::memory_order_relaxed) != op)
            return QStringLiteral("cancelled");
        if (!nats)
            return QStringLiteral("Нет соединения с NATS");
        QString err;
        if (!nats->publishMotionStop(&err))
            return err;
        return QString();
    });

    if (m_stopWatcher) {
        m_stopWatcher->disconnect(this);
        m_stopWatcher->deleteLater();
    }
    m_stopWatcher = new QFutureWatcher<QString>(this);
    connect(m_stopWatcher, &QFutureWatcher<QString>::finished, this,
            [this, doneMessage, level]() {
        const QString error = m_stopWatcher->result();
        if (error == QLatin1String("cancelled"))
            return;
        if (!error.isEmpty()) {
            failStop(QStringLiteral("Не удалось выключить робота: %1").arg(error));
            return;
        }
        emit note(level, doneMessage);
        emit dismissed();
    });
    m_stopWatcher->setFuture(future);
}

void PrepOverlay::failStop(const QString &message)
{
    m_phase = Phase::Homing;
    m_status->setText(message);
    emit note(Note::Err, message);
    refreshButtons();
}
