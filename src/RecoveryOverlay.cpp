#include "RecoveryOverlay.h"

#include "NatsClient.h"

#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

namespace {

const char *kItemStyle =
    "background:#FFF6E5; border:1px solid #E0B15A; border-radius:8px;"
    " color:#8A5A00; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
    " font-size:18px; font-weight:600; padding:16px 18px;";

const char *kBtnNext =
    "QPushButton{background:#388F51; color:white; border:none; border-radius:10px;"
    " font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:18px; font-weight:700;}"
    " QPushButton:pressed{background:#2F7A44;}"
    " QPushButton:disabled{background:#A9B0B8; color:white;}";

const char *kChecklistItems[] = {
    "Если зажата физическая кнопка — отожмите её",
    "Выключите вакуум",
    "Разжмите сварку руками",
};

} // namespace

RecoveryOverlay::RecoveryOverlay(NatsClient *nats, const QString &exitScript, QWidget *parent)
    : QWidget(parent)
    , m_nats(nats)
    , m_exitScript(exitScript)
{
    setAttribute(Qt::WA_StyledBackground, false);
    setFocusPolicy(Qt::StrongFocus);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addStretch();

    auto *row = new QHBoxLayout();
    row->addStretch();

    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("recoveryCard"));
    card->setFixedSize(1080, 620);
    card->setStyleSheet(QStringLiteral(
        "QFrame#recoveryCard { background:#FFFFFF; border-radius:16px; }"));

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(36, 32, 36, 32);
    cardLayout->setSpacing(16);

    auto *title = new QLabel(QStringLiteral("Восстановление"), card);
    title->setStyleSheet(QStringLiteral(
        "color:#1F2126; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        " font-size:28px; font-weight:700; background:transparent;"));
    cardLayout->addWidget(title);

    auto *subtitle = new QLabel(QStringLiteral("После аварийного стопа"), card);
    subtitle->setStyleSheet(QStringLiteral(
        "color:#737880; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        " font-size:15px; font-weight:600; background:transparent;"));
    cardLayout->addWidget(subtitle);

    m_status = new QLabel(QStringLiteral("Выполните пункты и нажмите «Далее»"), card);
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral(
        "color:#1F2126; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        " font-size:15px; background:transparent;"));
    cardLayout->addWidget(m_status);

    auto *body = new QHBoxLayout();
    body->setSpacing(28);

    auto *tasks = new QVBoxLayout();
    tasks->setSpacing(12);
    for (const char *text : kChecklistItems) {
        auto *rowLabel = new QLabel(QString::fromUtf8(text), card);
        rowLabel->setWordWrap(true);
        rowLabel->setMinimumHeight(64);
        rowLabel->setStyleSheet(QString::fromUtf8(kItemStyle));
        tasks->addWidget(rowLabel);
    }
    tasks->addStretch();
    body->addLayout(tasks, 1);

    auto *actions = new QVBoxLayout();
    actions->setSpacing(12);

    m_btnNext = new QPushButton(QStringLiteral("Далее"), card);
    m_btnNext->setCursor(Qt::PointingHandCursor);
    m_btnNext->setFocusPolicy(Qt::NoFocus);
    m_btnNext->setFixedSize(300, 68);
    m_btnNext->setStyleSheet(QString::fromUtf8(kBtnNext));
    actions->addWidget(m_btnNext);
    actions->addStretch();
    body->addLayout(actions);

    cardLayout->addLayout(body, 1);

    row->addWidget(card);
    row->addStretch();
    root->addLayout(row);
    root->addStretch();

    connect(m_btnNext, &QPushButton::clicked, this, &RecoveryOverlay::onNextClicked);
    refreshButtons();
}

void RecoveryOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), QColor(15, 18, 24, 170));
}

bool RecoveryOverlay::isExitRunning() const
{
    return m_phase == Phase::Running;
}

void RecoveryOverlay::refreshButtons()
{
    m_btnNext->setEnabled(m_phase == Phase::Checklist);
}

void RecoveryOverlay::onNextClicked()
{
    if (m_phase != Phase::Checklist)
        return;

    m_phase = Phase::Confirming;
    refreshButtons();

    const auto answer = QMessageBox::question(
        this,
        QStringLiteral("Подтверждение"),
        QStringLiteral("Если вы все выполнили, отойдите от робота, дальше он "
                       "начнет движение — выход в стартовое положение"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (answer != QMessageBox::Yes) {
        m_phase = Phase::Checklist;
        m_status->setText(QStringLiteral("Выполните пункты и нажмите «Далее»"));
        refreshButtons();
        return;
    }

    startExitScript();
}

void RecoveryOverlay::startExitScript()
{
    m_phase = Phase::Running;
    m_waitFilename = m_exitScript;
    m_sawScriptRunning = false;
    m_status->setText(QStringLiteral("Запуск выхода в стартовое положение…"));
    refreshButtons();
    emit note(Note::Info, QStringLiteral("Восстановление · запуск '%1'")
                              .arg(m_waitFilename));

    auto gate = m_gate;
    auto epoch = m_epoch;
    NatsClient *nats = m_nats;
    const QString filename = m_waitFilename;
    m_epoch->fetch_add(1, std::memory_order_relaxed);
    const int op = m_epoch->load(std::memory_order_relaxed);

    const QFuture<QString> future = QtConcurrent::run([gate, epoch, op, nats, filename]() -> QString {
        std::lock_guard<std::mutex> lock(*gate);
        if (epoch->load(std::memory_order_relaxed) != op)
            return QStringLiteral("cancelled");
        if (!nats)
            return QStringLiteral("Нет соединения с NATS");
        QString code;
        QString err;
        if (!nats->fetchScriptCode(filename, &code, &err)) {
            return QStringLiteral("Не удалось получить скрипт '%1' из NATS KV: %2")
                .arg(filename, err);
        }
        if (!nats->publishExecCommand(filename, code, 1, &err)) {
            return QStringLiteral("Не удалось отправить команду запуска: %1").arg(err);
        }
        return QString();
    });

    if (m_scriptWatcher) {
        m_scriptWatcher->disconnect(this);
        m_scriptWatcher->deleteLater();
    }
    m_scriptWatcher = new QFutureWatcher<QString>(this);
    connect(m_scriptWatcher, &QFutureWatcher<QString>::finished, this, [this]() {
        const QString error = m_scriptWatcher->result();
        if (error == QLatin1String("cancelled"))
            return;
        if (!error.isEmpty()) {
            m_phase = Phase::Checklist;
            m_waitFilename.clear();
            m_status->setText(error);
            emit note(Note::Err, error);
            refreshButtons();
            return;
        }
        m_status->setText(QStringLiteral("Робот выходит в стартовое положение…"));
    });
    m_scriptWatcher->setFuture(future);
}

void RecoveryOverlay::onScriptStatus(bool running, bool completed, const QString &filename)
{
    if (m_phase != Phase::Running)
        return;
    if (filename != m_waitFilename)
        return;

    if (running) {
        m_sawScriptRunning = true;
        return;
    }

    if (!m_sawScriptRunning && !completed)
        return;

    if (completed) {
        m_phase = Phase::Done;
        m_waitFilename.clear();
        m_status->setText(QStringLiteral("Выход завершён"));
        refreshButtons();
        emit note(Note::Ok, QStringLiteral("Восстановление · '%1' завершён")
                                .arg(m_exitScript));
        emit finished();
        return;
    }

    m_phase = Phase::Checklist;
    m_waitFilename.clear();
    m_status->setText(QStringLiteral("Выход прерван. Выполните пункты и нажмите «Далее»"));
    emit note(Note::Err, QStringLiteral("Восстановление · '%1' не завершился")
                             .arg(m_exitScript));
    refreshButtons();
}

void RecoveryOverlay::abortExitToChecklist()
{
    if (m_phase == Phase::Checklist || m_phase == Phase::Done)
        return;
    publishStopAndReset(QStringLiteral("Повторный аварийный стоп · чек-лист снова"));
}

void RecoveryOverlay::publishStopAndReset(const QString &message)
{
    {
        std::lock_guard<std::mutex> lock(*m_gate);
        m_epoch->fetch_add(1, std::memory_order_relaxed);
    }
    m_waitFilename.clear();
    m_sawScriptRunning = false;
    m_phase = Phase::Checklist;
    m_status->setText(QStringLiteral("Выполните пункты и нажмите «Далее»"));
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
    connect(m_stopWatcher, &QFutureWatcher<QString>::finished, this, [this, message]() {
        const QString error = m_stopWatcher->result();
        if (!error.isEmpty()) {
            emit note(Note::Err, QStringLiteral("Стоп при восстановлении: %1").arg(error));
            return;
        }
        emit note(Note::Warn, message);
    });
    m_stopWatcher->setFuture(future);
}
