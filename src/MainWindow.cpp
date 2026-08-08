#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "NatsClient.h"

#include <QMessageBox>
#include <QPixmap>
#include <QtConcurrent/QtConcurrent>

namespace {
// Адрес NATS-сервера цеха.
const char *kNatsUrl = "nats://192.168.11.112:4222";
// Тот же KV bucket, что использует веб-редактор скриптов.
const char *kNatsScriptBucket = "behaviour";
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
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_nats(new NatsClient(this))
{
    ui->setupUi(this);

    // Подключение к NATS выполняется один раз при старте пульта.
    // Это блокирующий вызов, но здесь это единоразовая операция при
    // запуске приложения, поэтому делаем её синхронно.
    QString natsErr;
    if (!m_nats->connectToServer(QString::fromUtf8(kNatsUrl), &natsErr)) {
        QMessageBox::warning(this, QStringLiteral("NATS"),
                             QStringLiteral("Не удалось подключиться к NATS (%1): %2")
                                 .arg(QString::fromUtf8(kNatsUrl), natsErr));
    } else if (!m_nats->openKvBucket(QString::fromUtf8(kNatsScriptBucket), &natsErr)) {
        QMessageBox::warning(this, QStringLiteral("NATS"),
                             QStringLiteral("Не удалось открыть KV bucket '%1': %2")
                                 .arg(QString::fromUtf8(kNatsScriptBucket), natsErr));
    }

    const QPixmap cvPos(QStringLiteral(":/elements/img_cv_position_frame.png"));
    if (!cvPos.isNull())
        ui->imgCvPosition->setPixmap(cvPos.scaled(ui->imgCvPosition->size(),
                                                  Qt::KeepAspectRatio,
                                                  Qt::SmoothTransformation));

    const QPixmap cvOut(QStringLiteral(":/elements/img_cv_output_frame.png"));
    if (!cvOut.isNull())
        ui->imgCvOutput->setPixmap(cvOut.scaled(ui->imgCvOutput->size(),
                                                Qt::KeepAspectRatio,
                                                Qt::SmoothTransformation));

    refreshUi();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::refreshUi()
{
    applyProgramVisuals();
    applyModeVisuals();
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
    switch (m_mode) {
    case Mode::Ready:
        ui->statusReady->setText(QStringLiteral("●  Готовность · OK"));
        ui->statusReady->setStyleSheet(QString::fromUtf8(kPillOk));
        ui->btnStart->setEnabled(true);
        ui->btnPause->setEnabled(false);
        ui->btnStop->setEnabled(false);
        ui->nodeRobotState->setText(QStringLiteral("Готов"));
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
        ui->btnStart->setEnabled(true);
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

    if (m_mode == Mode::Ready) {
        // Свежий запуск: подтягиваем код нужного скрипта из KV и шлём
        // команду exec в NATS. Режим переключится в Run только после
        // успешной публикации (см. startSelectedProgramScript()).
        startSelectedProgramScript();
        return;
    }

    // Возобновление из Pause: локальный переход состояния, без повторной
    // отправки exec. Если бэкенду нужна отдельная команда "продолжить" —
    // публикуйте её здесь тем же способом, что и в startSelectedProgramScript().
    m_mode = Mode::Run;
    refreshUi();
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
        return QStringLiteral("detail2.chai");
    }
    return {};
}

void MainWindow::startSelectedProgramScript()
{
    const QString filename = scriptFilenameForProgram(m_program);
    NatsClient *nats = m_nats;

    ui->btnStart->setEnabled(false); // защита от повторного клика, пока идёт обмен с NATS

    const QFuture<QString> future = QtConcurrent::run([nats, filename]() -> QString {
        QString code;
        QString err;
        if (!nats->fetchScriptCode(filename, &code, &err)) {
            return QStringLiteral("Не удалось получить скрипт '%1' из NATS KV: %2")
                .arg(filename, err);
        }
        if (!nats->publishExecCommand(filename, code, /*fromLine=*/1, &err)) {
            return QStringLiteral("Не удалось отправить команду запуска в NATS: %1").arg(err);
        }
        return QString(); // пустая строка = успех
    });

    if (m_execWatcher) {
        m_execWatcher->disconnect(this);
        m_execWatcher->deleteLater();
    }
    m_execWatcher = new QFutureWatcher<QString>(this);
    connect(m_execWatcher, &QFutureWatcher<QString>::finished, this, [this]() {
        const QString error = m_execWatcher->result();

        if (!error.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Ошибка запуска"), error);
            refreshUi(); // остаёмся в Ready, кнопки возвращаются в исходное состояние
            return;
        }

        m_mode = Mode::Run;
        refreshUi();
    });
    m_execWatcher->setFuture(future);
}

void MainWindow::on_btnPause_clicked()
{
    if (m_mode == Mode::Run) {
        m_mode = Mode::Pause;
        refreshUi();
    }
}

void MainWindow::on_btnStop_clicked()
{
    if (m_mode == Mode::Run || m_mode == Mode::Pause) {
        m_mode = Mode::Ready;
        refreshUi();
    }
}

void MainWindow::on_btnProg1_clicked()
{
    if (m_mode != Mode::Ready)
        return;
    m_program = Program::Detail1;
    refreshUi();
}

void MainWindow::on_btnProg2_clicked()
{
    if (m_mode != Mode::Ready)
        return;
    m_program = Program::Detail2;
    refreshUi();
}

void MainWindow::on_btnAdmin_clicked()
{
    QMessageBox::information(
        this,
        QStringLiteral("Админ"),
        QStringLiteral("Экран админки — отдельно (демо)."));
}