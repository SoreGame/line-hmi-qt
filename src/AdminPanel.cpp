#include "AdminPanel.h"
#include "PlcClient.h"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent/QtConcurrent>

namespace {

const char *kFieldStyle =
    "QLineEdit, QSpinBox, QDoubleSpinBox {"
    "  background:#FFFFFF; color:#1F2126;"
    "  border:1px solid #DBDEE3; border-radius:6px;"
    "  padding:2px 6px; min-height:26px; max-height:26px;"
    "  font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:12px;"
    "}"
    "QSpinBox::up-button, QSpinBox::down-button,"
    "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width:16px; }";

const char *kPrimaryBtn =
    "QPushButton {"
    "  background:#2F6FED; color:#FFFFFF; border:none; border-radius:6px;"
    "  padding:6px 14px; min-height:32px; min-width:96px;"
    "  font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:12px; font-weight:600;"
    "}"
    "QPushButton:pressed { background:#2558C7; }"
    "QPushButton:disabled { background:#A8B4C8; }";

const char *kSecondaryBtn =
    "QPushButton {"
    "  background:#FFFFFF; color:#1F2126; border:1px solid #DBDEE3; border-radius:6px;"
    "  padding:6px 14px; min-height:32px; min-width:68px;"
    "  font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:12px; font-weight:600;"
    "}"
    "QPushButton:pressed { background:#F0F2F5; }"
    "QPushButton:disabled { color:#8A9099; }";

const char *kCompactBtn =
    "QPushButton {"
    "  background:#FFFFFF; color:#1F2126; border:1px solid #DBDEE3; border-radius:4px;"
    "  padding:1px 6px; min-height:20px; max-height:20px;"
    "  font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:11px; font-weight:600;"
    "}"
    "QPushButton:pressed { background:#F0F2F5; }"
    "QPushButton:disabled { color:#8A9099; }";

const char *kJogBtn =
    "QPushButton {"
    "  background:#FFFFFF; color:#1F2126; border:1px solid #DBDEE3; border-radius:8px;"
    "  min-width:44px; min-height:44px; max-width:44px; max-height:44px;"
    "  font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:16px; font-weight:700;"
    "}"
    "QPushButton:pressed { background:#E6F0FF; }"
    "QPushButton:disabled { color:#8A9099; }";

const char *kPowerOffBtn =
    "QPushButton {"
    "  background:#2E8C47; color:#FFFFFF; border:none; border-radius:8px;"
    "  padding:8px 16px; min-height:40px; min-width:96px;"
    "  font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:13px; font-weight:600;"
    "}"
    "QPushButton:pressed { background:#246F38; }";

const char *kPowerOnBtn =
    "QPushButton {"
    "  background:#C43B3B; color:#FFFFFF; border:none; border-radius:8px;"
    "  padding:8px 16px; min-height:40px; min-width:96px;"
    "  font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:13px; font-weight:600;"
    "}"
    "QPushButton:pressed { background:#9E2E2E; }";

void setToggleCaption(QPushButton *button, const QString &title, bool on)
{
    button->setText(title + (on ? QStringLiteral(":  ВКЛ") : QStringLiteral(":  ВЫКЛ")));
    button->setStyleSheet(QLatin1String(on ? kPowerOffBtn : kSecondaryBtn));
}

const double kStepValues[] = {0.001, 0.005, 0.01, 0.05, 0.1, 0.5, 1.0, 5.0, 10.0, 50.0};
constexpr int kStepCount = int(sizeof(kStepValues) / sizeof(kStepValues[0]));
constexpr int kDefaultStepIndex = 6; // 1.0

QString formatStep(double v)
{
    if (v >= 1.0)
        return QString::number(v, 'g', 8);
    if (v >= 0.1)
        return QString::number(v, 'f', 1);
    if (v >= 0.01)
        return QString::number(v, 'f', 2);
    return QString::number(v, 'f', 3);
}

QLabel *makeDroValue(const QString &axis, QWidget *parent)
{
    auto *label = new QLabel(axis + QStringLiteral(": —"), parent);
    label->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        "  font-size:13px; font-weight:600; color:#1F2126;"
        "  background:#F7F8FA; border:1px solid #DBDEE3; border-radius:6px;"
        "  padding:6px 8px; min-width:118px;"
        "}"));
    return label;
}

const char *kTableStyle =
    "QTableWidget {"
    "  background:#FFFFFF; color:#1F2126;"
    "  border:none; gridline-color:#EEF0F3; outline:none;"
    "  font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:11px;"
    "  alternate-background-color:#F7F8FA;"
    "  selection-background-color:#E6F0FF; selection-color:#1F2126;"
    "}"
    "QHeaderView::section {"
    "  background:#F7F8FA; color:#3A3F4A;"
    "  border:none; border-bottom:1px solid #DBDEE3; border-right:1px solid #EEF0F3;"
    "  padding:3px 4px;"
    "  font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:10px; font-weight:600;"
    "}"
    "QTableWidget::item { padding:1px 4px; }"
    "QTableCornerButton::section { background:#F7F8FA; border:none; }";

QWidget *makeLabeledField(const QString &labelText, QWidget *editor, QWidget *parent)
{
    auto *wrap = new QWidget(parent);
    auto *lay = new QVBoxLayout(wrap);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(2);

    auto *label = new QLabel(labelText, wrap);
    label->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        "  font-size:11px; color:#3A3F4A;"
        "}"));
    lay->addWidget(label);
    lay->addWidget(editor);
    return wrap;
}

QString formatCoord(double v)
{
    return QString::number(v, 'f', 2);
}

QDoubleSpinBox *makeCoordSpin(double value, QWidget *parent)
{
    auto *spin = new QDoubleSpinBox(parent);
    spin->setDecimals(3);
    spin->setRange(-999999.999, 999999.999);
    spin->setSingleStep(0.1);
    spin->setValue(value);
    spin->setStyleSheet(QStringLiteral(
        "QDoubleSpinBox {"
        "  background:#FFFFFF; color:#1F2126;"
        "  border:1px solid #DBDEE3; border-radius:6px;"
        "  padding:4px 6px; min-height:32px;"
        "  font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:13px;"
        "}"
        "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width:18px; }"));
    return spin;
}

} // namespace

AdminPanel::AuthResult AdminPanel::authenticate(QWidget *parent)
{
    bool ok = false;
    const QString password = QInputDialog::getText(
        parent,
        QStringLiteral("Админ-панель"),
        QStringLiteral("Пароль:"),
        QLineEdit::Password,
        QString(),
        &ok);
    if (!ok)
        return AuthResult::Cancelled;

    if (password != QLatin1String(kPassword)) {
        QMessageBox::warning(parent,
                             QStringLiteral("Админ-панель"),
                             QStringLiteral("Неверный пароль"));
        return AuthResult::Denied;
    }
    return AuthResult::Ok;
}

AdminPanel::AdminPanel(const ConnectionSettings &initial, NatsClient *nats, PlcClient *plc,
                       QWidget *parent)
    : QDialog(parent)
    , m_nats(nats)
    , m_plc(plc)
{
    setWindowTitle(QStringLiteral("Админ-панель"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setWindowModality(Qt::ApplicationModal);
    m_layoutW = kWidth;
    m_layoutH = kHeight;
    QScreen *scr = parent ? parent->screen() : nullptr;
    if (!scr)
        scr = QApplication::primaryScreen();
    if (scr) {
        const QSize sz = scr->geometry().size();
        if (sz.width() > 0 && sz.height() > 0) {
            m_layoutW = sz.width();
            m_layoutH = sz.height();
        }
    }
    resize(m_layoutW, m_layoutH);
    setStyleSheet(QStringLiteral("QDialog { background:#EEF0F3; }"));

    // --- Header ---------------------------------------------------------
    auto *header = new QFrame(this);
    header->setGeometry(0, 0, m_layoutW, 52);
    header->setStyleSheet(QStringLiteral(
        "QFrame {"
        "  background:#FFFFFF;"
        "  border:none;"
        "  border-bottom:1px solid #DBDEE3;"
        "}"));

    auto *btnBack = new QPushButton(QStringLiteral("Назад"), header);
    btnBack->setStyleSheet(QLatin1String(kSecondaryBtn));
    btnBack->setCursor(Qt::PointingHandCursor);
    btnBack->setFocusPolicy(Qt::NoFocus);

    auto *title = new QLabel(QStringLiteral("Админ-панель"), header);
    title->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        "  font-size:16px; font-weight:600; color:#1F2126;"
        "  background:transparent; border:none;"
        "}"));

    auto *btnApply = new QPushButton(QStringLiteral("Применить"), header);
    btnApply->setStyleSheet(QLatin1String(kPrimaryBtn));
    btnApply->setCursor(Qt::PointingHandCursor);
    btnApply->setDefault(true);
    btnApply->setFocusPolicy(Qt::NoFocus);

    auto *headerLay = new QHBoxLayout(header);
    headerLay->setContentsMargins(12, 0, 12, 0);
    headerLay->setSpacing(12);
    headerLay->addWidget(btnBack);
    headerLay->addWidget(title, 1);
    headerLay->addWidget(btnApply);

    // --- Network panel: компактный блок в углу -------------------------
    m_networkPanel = new QFrame(this);
    auto *networkPanel = m_networkPanel;
    networkPanel->setObjectName(QStringLiteral("networkPanel"));
    networkPanel->setStyleSheet(QStringLiteral(
        "QFrame#networkPanel {"
        "  background:#FFFFFF;"
        "  border:1px solid #DBDEE3;"
        "  border-radius:8px;"
        "}"));

    auto *sectionTitle = new QLabel(QStringLiteral("Сеть"), networkPanel);
    sectionTitle->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        "  font-size:13px; font-weight:600; color:#1F2126;"
        "  background:transparent; border:none;"
        "}"));

    m_natsHost = new QLineEdit(initial.natsHost, networkPanel);
    m_natsPort = new QSpinBox(networkPanel);
    m_natsPort->setRange(1, 65535);
    m_natsPort->setValue(initial.natsPort);
    m_natsPort->setFixedWidth(88);

    m_plcHost = new QLineEdit(initial.plcHost, networkPanel);
    m_plcPort = new QSpinBox(networkPanel);
    m_plcPort->setRange(1, 65535);
    m_plcPort->setValue(initial.plcPort > 0 ? initial.plcPort : 1502);
    m_plcPort->setFixedWidth(88);

    for (QWidget *w : {static_cast<QWidget *>(m_natsHost),
                       static_cast<QWidget *>(m_natsPort),
                       static_cast<QWidget *>(m_plcHost),
                       static_cast<QWidget *>(m_plcPort)}) {
        w->setStyleSheet(QLatin1String(kFieldStyle));
    }

    auto *rowNats = new QWidget(networkPanel);
    auto *rowNatsLay = new QHBoxLayout(rowNats);
    rowNatsLay->setContentsMargins(0, 0, 0, 0);
    rowNatsLay->setSpacing(8);
    rowNatsLay->addWidget(makeLabeledField(QStringLiteral("NATS хост"), m_natsHost, rowNats), 1);
    rowNatsLay->addWidget(makeLabeledField(QStringLiteral("Порт"), m_natsPort, rowNats), 0);

    auto *rowPlc = new QWidget(networkPanel);
    auto *rowPlcLay = new QHBoxLayout(rowPlc);
    rowPlcLay->setContentsMargins(0, 0, 0, 0);
    rowPlcLay->setSpacing(8);
    rowPlcLay->addWidget(makeLabeledField(QStringLiteral("ПЛК хост"), m_plcHost, rowPlc), 1);
    rowPlcLay->addWidget(makeLabeledField(QStringLiteral("Порт"), m_plcPort, rowPlc), 0);

    auto *panelLay = new QVBoxLayout(networkPanel);
    panelLay->setContentsMargins(12, 10, 12, 10);
    panelLay->setSpacing(8);
    panelLay->addWidget(sectionTitle);
    panelLay->addWidget(rowNats);
    panelLay->addWidget(rowPlc);

    const bool hmiOn = QFile::exists(
        QStringLiteral("/etc/systemd/system/getty@tty1.service.d/autologin.conf"));
    m_btnHmiAutostart = new QPushButton(networkPanel);
    m_btnHmiAutostart->setCheckable(true);
    m_btnHmiAutostart->setChecked(hmiOn);
    m_btnHmiAutostart->setCursor(Qt::PointingHandCursor);
    m_btnHmiAutostart->setFocusPolicy(Qt::NoFocus);
    m_btnHmiAutostart->setMinimumHeight(44);
    setToggleCaption(m_btnHmiAutostart,
                     QStringLiteral("Автозапуск пульта на этом компьютере"),
                     hmiOn);

    m_btnRemoteAutostart = new QPushButton(networkPanel);
    m_btnRemoteAutostart->setCheckable(true);
    m_btnRemoteAutostart->setChecked(initial.autostart);
    m_btnRemoteAutostart->setCursor(Qt::PointingHandCursor);
    m_btnRemoteAutostart->setFocusPolicy(Qt::NoFocus);
    m_btnRemoteAutostart->setMinimumHeight(44);
    setToggleCaption(m_btnRemoteAutostart,
                     QStringLiteral("Автозапуск системы на NATS-хосте"),
                     initial.autostart);

    m_btnShowCursor = new QPushButton(networkPanel);
    m_btnShowCursor->setCheckable(true);
    m_btnShowCursor->setChecked(initial.showCursor);
    m_btnShowCursor->setCursor(Qt::PointingHandCursor);
    m_btnShowCursor->setFocusPolicy(Qt::NoFocus);
    m_btnShowCursor->setMinimumHeight(44);
    setToggleCaption(m_btnShowCursor,
                     QStringLiteral("Курсор мыши"),
                     initial.showCursor);

    auto *targetHint = new QLabel(
        QStringLiteral("Пульт включается на этой панели. Автозапуск системы — на компьютере с NATS-хостом."),
        networkPanel);
    targetHint->setWordWrap(true);
    targetHint->setStyleSheet(QStringLiteral(
        "QLabel { color:#737880; font-size:11px; background:transparent; border:none; }"));

    panelLay->addWidget(m_btnHmiAutostart);
    panelLay->addWidget(m_btnRemoteAutostart);
    panelLay->addWidget(m_btnShowCursor);
    panelLay->addWidget(targetHint);
    panelLay->addStretch(1);

    // --- Jog / motion --------------------------------------------------
    m_jogPanel = new QFrame(this);
    m_jogPanel->setObjectName(QStringLiteral("jogPanel"));
    m_jogPanel->setStyleSheet(QStringLiteral(
        "QFrame#jogPanel {"
        "  background:#FFFFFF;"
        "  border:1px solid #DBDEE3;"
        "  border-radius:8px;"
        "}"));

    auto *jogTitle = new QLabel(QStringLiteral("Управление"), m_jogPanel);
    jogTitle->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        "  font-size:13px; font-weight:600; color:#1F2126;"
        "  background:transparent; border:none;"
        "}"));

    m_btnMotionPower = new QPushButton(m_jogPanel);
    m_btnMotionPower->setCursor(Qt::PointingHandCursor);
    m_btnMotionPower->setFocusPolicy(Qt::NoFocus);
    updateMotionPowerButton();

    m_droX = makeDroValue(QStringLiteral("X"), m_jogPanel);
    m_droY = makeDroValue(QStringLiteral("Y"), m_jogPanel);
    m_droZ = makeDroValue(QStringLiteral("Z"), m_jogPanel);
    m_droR = makeDroValue(QStringLiteral("R"), m_jogPanel);

    auto *droGrid = new QWidget(m_jogPanel);
    auto *droLay = new QGridLayout(droGrid);
    droLay->setContentsMargins(0, 0, 0, 0);
    droLay->setSpacing(6);
    droLay->addWidget(m_droX, 0, 0);
    droLay->addWidget(m_droY, 0, 1);
    droLay->addWidget(m_droZ, 1, 0);
    droLay->addWidget(m_droR, 1, 1);

    auto *xyPad = new QWidget(m_jogPanel);
    auto *xyLay = new QGridLayout(xyPad);
    xyLay->setContentsMargins(0, 0, 0, 0);
    xyLay->setSpacing(4);
    auto *btnYPlus = makeJogButton(QStringLiteral("↑"), xyPad);
    auto *btnXMinus = makeJogButton(QStringLiteral("←"), xyPad);
    auto *btnXPlus = makeJogButton(QStringLiteral("→"), xyPad);
    auto *btnYMinus = makeJogButton(QStringLiteral("↓"), xyPad);
    xyLay->addWidget(btnYPlus, 0, 1);
    xyLay->addWidget(btnXMinus, 1, 0);
    xyLay->addWidget(btnXPlus, 1, 2);
    xyLay->addWidget(btnYMinus, 2, 1);

    auto *zPad = new QWidget(m_jogPanel);
    auto *zLay = new QVBoxLayout(zPad);
    zLay->setContentsMargins(0, 0, 0, 0);
    zLay->setSpacing(4);
    auto *zLabel = new QLabel(QStringLiteral("Z"), zPad);
    zLabel->setAlignment(Qt::AlignCenter);
    zLabel->setStyleSheet(QStringLiteral(
        "QLabel { font-size:11px; font-weight:600; color:#3A3F4A; background:transparent; }"));
    auto *btnZPlus = makeJogButton(QStringLiteral("↑"), zPad);
    auto *btnZMinus = makeJogButton(QStringLiteral("↓"), zPad);
    zLay->addWidget(zLabel);
    zLay->addWidget(btnZPlus);
    zLay->addWidget(btnZMinus);

    auto *rPad = new QWidget(m_jogPanel);
    auto *rLay = new QVBoxLayout(rPad);
    rLay->setContentsMargins(0, 0, 0, 0);
    rLay->setSpacing(4);
    auto *rLabel = new QLabel(QStringLiteral("R"), rPad);
    rLabel->setAlignment(Qt::AlignCenter);
    rLabel->setStyleSheet(QStringLiteral(
        "QLabel { font-size:11px; font-weight:600; color:#3A3F4A; background:transparent; }"));
    auto *btnRPlus = makeJogButton(QStringLiteral("↻"), rPad);
    auto *btnRMinus = makeJogButton(QStringLiteral("↺"), rPad);
    rLay->addWidget(rLabel);
    rLay->addWidget(btnRPlus);
    rLay->addWidget(btnRMinus);

    auto *stepCol = new QWidget(m_jogPanel);
    auto *stepColLay = new QVBoxLayout(stepCol);
    stepColLay->setContentsMargins(0, 0, 0, 0);
    stepColLay->setSpacing(2);
    auto *stepTitle = new QLabel(QStringLiteral("Шаг"), stepCol);
    stepTitle->setAlignment(Qt::AlignCenter);
    stepTitle->setStyleSheet(QStringLiteral(
        "QLabel { font-size:11px; font-weight:600; color:#3A3F4A; background:transparent; }"));
    m_stepValue = new QLabel(stepCol);
    m_stepValue->setAlignment(Qt::AlignCenter);
    m_stepValue->setStyleSheet(QStringLiteral(
        "QLabel { font-size:12px; font-weight:600; color:#1F2126; background:transparent; }"));

    m_stepSlider = new QSlider(Qt::Vertical, stepCol);
    m_stepSlider->setRange(0, kStepCount - 1);
    m_stepSlider->setValue(kDefaultStepIndex);
    m_stepSlider->setInvertedAppearance(false);
    m_stepSlider->setInvertedControls(false);
    m_stepSlider->setTickPosition(QSlider::TicksRight);
    m_stepSlider->setTickInterval(1);
    m_stepSlider->setSingleStep(1);
    m_stepSlider->setPageStep(1);
    m_stepSlider->setFixedHeight(180);
    m_stepSlider->setFixedWidth(28);
    m_stepSlider->setFocusPolicy(Qt::NoFocus);

    auto *tickLabels = new QWidget(stepCol);
    auto *tickLay = new QVBoxLayout(tickLabels);
    tickLay->setContentsMargins(0, 0, 0, 0);
    tickLay->setSpacing(0);
    for (int i = kStepCount - 1; i >= 0; --i) {
        auto *t = new QPushButton(formatStep(kStepValues[i]), tickLabels);
        t->setFlat(true);
        t->setCursor(Qt::PointingHandCursor);
        t->setFocusPolicy(Qt::NoFocus);
        t->setStyleSheet(QStringLiteral(
            "QPushButton {"
            "  font-family:\"Inter\",\"Segoe UI\",sans-serif;"
            "  font-size:9px; color:#8A9099;"
            "  border:none; background:transparent; text-align:left; padding:0;"
            "}"
            "QPushButton:pressed { color:#1F2126; }"));
        const int index = i;
        connect(t, &QPushButton::clicked, this, [this, index]() {
            m_stepSlider->setValue(index);
        });
        tickLay->addWidget(t, 1, Qt::AlignLeft | Qt::AlignVCenter);
    }

    auto *sliderRow = new QWidget(stepCol);
    auto *sliderRowLay = new QHBoxLayout(sliderRow);
    sliderRowLay->setContentsMargins(0, 0, 0, 0);
    sliderRowLay->setSpacing(4);
    sliderRowLay->addWidget(m_stepSlider);
    sliderRowLay->addWidget(tickLabels);

    stepColLay->addWidget(stepTitle);
    stepColLay->addWidget(m_stepValue);
    stepColLay->addWidget(sliderRow, 1);

    auto *pads = new QWidget(m_jogPanel);
    auto *padsLay = new QHBoxLayout(pads);
    padsLay->setContentsMargins(0, 0, 0, 0);
    padsLay->setSpacing(12);
    padsLay->addWidget(xyPad);
    padsLay->addWidget(zPad);
    padsLay->addWidget(rPad);
    padsLay->addStretch(1);
    padsLay->addWidget(stepCol);

    auto *jogLay = new QVBoxLayout(m_jogPanel);
    jogLay->setContentsMargins(12, 10, 12, 12);
    jogLay->setSpacing(10);
    auto *jogTop = new QHBoxLayout();
    jogTop->setContentsMargins(0, 0, 0, 0);
    jogTop->addWidget(jogTitle);
    jogTop->addStretch(1);
    jogTop->addWidget(m_btnMotionPower);
    jogLay->addLayout(jogTop);
    jogLay->addWidget(droGrid);
    jogLay->addWidget(pads);

    m_jogPanel->adjustSize();
    const QSize jogHint = m_jogPanel->sizeHint();
    m_leftColW = qMax(qMax(networkPanel->sizeHint().width(), jogHint.width()), 420);
    networkPanel->layout()->activate();
    const int netH = networkPanel->sizeHint().height();
    networkPanel->setGeometry(12, 64, m_leftColW, netH);
    const int jogY = 64 + netH + 12;
    const int jogH = qMax(jogHint.height(), 320);
    m_jogPanel->setGeometry(12, jogY, m_leftColW, jogH);

    // --- PLC buffer log (левый нижний угол, под «Управление») ---
    m_plcLogPanel = new QFrame(this);
    m_plcLogPanel->setObjectName(QStringLiteral("plcLogPanel"));
    m_plcLogPanel->setStyleSheet(QStringLiteral(
        "QFrame#plcLogPanel {"
        "  background:#FFFFFF;"
        "  border:1px solid #DBDEE3;"
        "  border-radius:8px;"
        "}"));

    auto *plcLogTitle = new QLabel(QStringLiteral("Буфер ПЛК (60 int16)"), m_plcLogPanel);
    plcLogTitle->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        "  font-size:13px; font-weight:600; color:#1F2126;"
        "  background:transparent; border:none;"
        "}"));

    m_plcBufferStatus = new QLabel(m_plcLogPanel);
    m_plcBufferStatus->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        "  font-size:11px; color:#3A3F4A;"
        "  background:transparent; border:none;"
        "}"));

    m_plcBufferLog = new QPlainTextEdit(m_plcLogPanel);
    m_plcBufferLog->setReadOnly(true);
    m_plcBufferLog->setMaximumBlockCount(kPlcLogMaxLines);
    m_plcBufferLog->setStyleSheet(QStringLiteral(
        "QPlainTextEdit {"
        "  font-family:\"JetBrains Mono\",\"Consolas\",\"Courier New\",monospace;"
        "  font-size:11px; color:#1F2126;"
        "  background:#F7F8FA; border:1px solid #DBDEE3; border-radius:6px;"
        "  padding:6px;"
        "}"));

    auto *plcLogLay = new QVBoxLayout(m_plcLogPanel);
    plcLogLay->setContentsMargins(12, 10, 12, 10);
    plcLogLay->setSpacing(6);
    plcLogLay->addWidget(plcLogTitle);
    plcLogLay->addWidget(m_plcBufferStatus);
    plcLogLay->addWidget(m_plcBufferLog, 1);

    onStepChanged(kDefaultStepIndex);

    connect(m_btnMotionPower, &QPushButton::clicked, this, &AdminPanel::onMotionPowerClicked);
    connect(m_stepSlider, &QSlider::valueChanged, this, &AdminPanel::onStepChanged);
    connect(btnXPlus, &QPushButton::clicked, this, [this]() { sendMoveOffset(currentStep(), 0, 0, 0); });
    connect(btnXMinus, &QPushButton::clicked, this, [this]() { sendMoveOffset(-currentStep(), 0, 0, 0); });
    connect(btnYPlus, &QPushButton::clicked, this, [this]() { sendMoveOffset(0, currentStep(), 0, 0); });
    connect(btnYMinus, &QPushButton::clicked, this, [this]() { sendMoveOffset(0, -currentStep(), 0, 0); });
    connect(btnZPlus, &QPushButton::clicked, this, [this]() { sendMoveOffset(0, 0, currentStep(), 0); });
    connect(btnZMinus, &QPushButton::clicked, this, [this]() { sendMoveOffset(0, 0, -currentStep(), 0); });
    connect(btnRPlus, &QPushButton::clicked, this, [this]() { sendMoveOffset(0, 0, 0, currentStep()); });
    connect(btnRMinus, &QPushButton::clicked, this, [this]() { sendMoveOffset(0, 0, 0, -currentStep()); });
    if (m_nats) {
        connect(m_nats, &NatsClient::motionPoseReceived, this, &AdminPanel::onMotionPose);
    }
    if (m_plc) {
        connect(m_plc, &PlcClient::stateChanged, this, &AdminPanel::onPlcStateChanged);
    }
    refreshPlcBufferLog();

    // --- Robot Data Table ----------------------------------------------
    m_tablePanel = new QFrame(this);
    m_tablePanel->setObjectName(QStringLiteral("tablePanel"));
    m_tablePanel->setStyleSheet(QStringLiteral(
        "QFrame#tablePanel {"
        "  background:#FFFFFF;"
        "  border:1px solid #DBDEE3;"
        "  border-radius:8px;"
        "}"));

    auto *tableTitle = new QLabel(QStringLiteral("Robot Data Table"), m_tablePanel);
    tableTitle->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        "  font-size:12px; font-weight:600; color:#1F2126;"
        "  background:transparent; border:none;"
        "}"));

    m_tableStatus = new QLabel(m_tablePanel);
    m_tableStatus->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        "  font-size:11px; color:#3A3F4A;"
        "  background:transparent; border:none;"
        "}"));

    m_btnRefresh = new QPushButton(QStringLiteral("Обновить"), m_tablePanel);
    m_btnRefresh->setStyleSheet(QLatin1String(kCompactBtn));
    m_btnRefresh->setCursor(Qt::PointingHandCursor);
    m_btnRefresh->setFocusPolicy(Qt::NoFocus);

    auto *tableHeader = new QWidget(m_tablePanel);
    tableHeader->setObjectName(QStringLiteral("tableHeaderBar"));
    auto *tableHeaderLay = new QHBoxLayout(tableHeader);
    tableHeaderLay->setContentsMargins(8, 6, 8, 4);
    tableHeaderLay->setSpacing(8);
    tableHeaderLay->addWidget(tableTitle);
    tableHeaderLay->addWidget(m_tableStatus, 1);
    tableHeaderLay->addWidget(m_btnRefresh);

    m_table = new QTableWidget(kPageSize, 6, m_tablePanel);
    m_table->setStyleSheet(QLatin1String(kTableStyle));
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("Имя"),
        QStringLiteral("X"),
        QStringLiteral("Y"),
        QStringLiteral("Z"),
        QStringLiteral("R"),
        QString(),
    });
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setShowGrid(true);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->setWordWrap(false);
    m_table->setTextElideMode(Qt::ElideRight);
    m_table->setCornerButtonEnabled(false);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_table->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    m_table->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    auto *vh = m_table->verticalHeader();
    vh->hide();
    vh->setVisible(false);
    vh->setMinimumWidth(0);
    vh->setMaximumWidth(0);
    vh->setFixedWidth(0);
    vh->setDefaultSectionSize(26);

    m_table->horizontalHeader()->setFixedHeight(24);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_table->setColumnWidth(0, 140);
    for (int col = 1; col <= 4; ++col) {
        m_table->horizontalHeader()->setSectionResizeMode(col, QHeaderView::Fixed);
        m_table->setColumnWidth(col, 64);
    }
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    m_table->setColumnWidth(5, 132);

    m_pager = new QWidget(m_tablePanel);
    m_btnPrevPage = new QPushButton(QStringLiteral("‹"), m_pager);
    m_btnNextPage = new QPushButton(QStringLiteral("›"), m_pager);
    m_pageLabel = new QLabel(m_pager);
    m_pageLabel->setAlignment(Qt::AlignCenter);
    m_pageLabel->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        "  font-size:11px; color:#3A3F4A;"
        "  background:transparent; border:none;"
        "}"));
    for (QPushButton *btn : {m_btnPrevPage, m_btnNextPage}) {
        btn->setStyleSheet(QLatin1String(kCompactBtn));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setFixedWidth(28);
    }
    auto *pagerLay = new QHBoxLayout(m_pager);
    pagerLay->setContentsMargins(8, 4, 8, 6);
    pagerLay->setSpacing(8);
    pagerLay->addStretch(1);
    pagerLay->addWidget(m_btnPrevPage);
    pagerLay->addWidget(m_pageLabel);
    pagerLay->addWidget(m_btnNextPage);
    pagerLay->addStretch(1);

    auto *tableLay = new QVBoxLayout(m_tablePanel);
    tableLay->setContentsMargins(0, 0, 0, 0);
    tableLay->setSpacing(0);
    tableLay->addWidget(tableHeader);
    tableLay->addWidget(m_table);
    tableLay->addWidget(m_pager);
    tableLay->addStretch(1);

    const int tableX = 12 + m_leftColW + 12;
    m_tablePanel->move(tableX, 64);
    updatePager();
    fitTablePanel();
    fitPlcLogPanel();

    m_loadWatcher = new QFutureWatcher<PointsLoadResult>(this);
    m_saveWatcher = new QFutureWatcher<QString>(this);

    connect(btnApply, &QPushButton::clicked, this, &AdminPanel::onApplyClicked);
    connect(btnBack, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_btnHmiAutostart, &QPushButton::toggled, this, &AdminPanel::onHmiAutostartToggled);
    connect(m_btnRemoteAutostart, &QPushButton::toggled, this, &AdminPanel::onRemoteAutostartToggled);
    connect(m_btnShowCursor, &QPushButton::toggled, this, &AdminPanel::onShowCursorToggled);
    connect(m_btnRefresh, &QPushButton::clicked, this, &AdminPanel::onRefreshClicked);
    connect(m_btnPrevPage, &QPushButton::clicked, this, [this]() { goToPage(m_page - 1); });
    connect(m_btnNextPage, &QPushButton::clicked, this, [this]() { goToPage(m_page + 1); });
    connect(m_loadWatcher, &QFutureWatcher<PointsLoadResult>::finished,
            this, &AdminPanel::onPointsLoaded);
    connect(m_saveWatcher, &QFutureWatcher<QString>::finished,
            this, &AdminPanel::onPointSaved);

    QTimer::singleShot(0, this, &AdminPanel::onRefreshClicked);
}

AdminPanel::~AdminPanel()
{
    if (m_loadWatcher && m_loadWatcher->isRunning())
        m_loadWatcher->waitForFinished();
    if (m_saveWatcher && m_saveWatcher->isRunning())
        m_saveWatcher->waitForFinished();
}

void AdminPanel::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (!m_fullScreenApplied) {
        m_fullScreenApplied = true;
        showFullScreen();
    }
}

ConnectionSettings AdminPanel::networkSettings() const
{
    ConnectionSettings c;
    c.natsHost = m_natsHost->text().trimmed();
    c.natsPort = static_cast<quint16>(m_natsPort->value());
    c.plcHost = m_plcHost->text().trimmed();
    c.plcPort = static_cast<quint16>(m_plcPort->value());
    c.autostart = m_btnRemoteAutostart && m_btnRemoteAutostart->isChecked();
    c.hmiAutostart = m_btnHmiAutostart && m_btnHmiAutostart->isChecked();
    c.showCursor = m_btnShowCursor && m_btnShowCursor->isChecked();
    return c;
}

namespace {

bool ensureRemoteNatsHost(const QString &host, QString *error)
{
    const QString normalized = host.trimmed().toLower();
    if (normalized.isEmpty()
        || normalized == QLatin1String("localhost")
        || normalized == QLatin1String("127.0.0.1")
        || normalized == QLatin1String("::1")
        || normalized == QLatin1String("0.0.0.0")) {
        *error = QStringLiteral(
            "NATS-хост указывает на эту панель. Укажите адрес компьютера, где работает NATS.");
        return false;
    }
    for (const QHostAddress &address : QNetworkInterface::allAddresses()) {
        if (address.toString().compare(host.trimmed(), Qt::CaseInsensitive) == 0) {
            *error = QStringLiteral(
                "NATS-хост %1 — это эта панель. Укажите адрес компьютера, где работает NATS.")
                         .arg(host.trimmed());
            return false;
        }
    }
    return true;
}

bool sshOnNatsHost(const QString &host, const QString &remoteCommand, int timeoutMs, QString *error)
{
    if (!ensureRemoteNatsHost(host, error))
        return false;
    QProcess proc;
    proc.start(QStringLiteral("ssh"),
               {QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
                QStringLiteral("-o"), QStringLiteral("ConnectTimeout=8"),
                QStringLiteral("-o"), QStringLiteral("StrictHostKeyChecking=accept-new"),
                QStringLiteral("root@%1").arg(host),
                remoteCommand});
    if (!proc.waitForStarted(3000)) {
        *error = QStringLiteral("Не удалось запустить ssh");
        return false;
    }
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        *error = QStringLiteral("Нет ответа от %1").arg(host);
        return false;
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        *error = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        if (error->isEmpty())
            *error = QStringLiteral("Команда на %1 завершилась с кодом %2").arg(host).arg(proc.exitCode());
        return false;
    }
    return true;
}

} // namespace

void AdminPanel::onHmiAutostartToggled(bool enabled)
{
    const auto revert = [this, enabled]() {
        m_btnHmiAutostart->blockSignals(true);
        m_btnHmiAutostart->setChecked(!enabled);
        m_btnHmiAutostart->blockSignals(false);
        setToggleCaption(m_btnHmiAutostart,
                         QStringLiteral("Автозапуск пульта на этом компьютере"),
                         !enabled);
    };

    QProcess proc;
    proc.start(QStringLiteral("sudo"),
               {QStringLiteral("-n"),
                QStringLiteral("/home/scara/bin/hmi-autostart.sh"),
                enabled ? QStringLiteral("on") : QStringLiteral("off")});
    if (!proc.waitForStarted(3000) || !proc.waitForFinished(20000) || proc.exitCode() != 0) {
        revert();
        const QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        QMessageBox::warning(this, QStringLiteral("Автозапуск пульта"),
                             err.isEmpty() ? QStringLiteral("Не удалось изменить автозапуск на этой панели")
                                           : err);
        return;
    }

    setToggleCaption(m_btnHmiAutostart,
                     QStringLiteral("Автозапуск пульта на этом компьютере"),
                     enabled);
    ConnectionSettings cfg = networkSettings();
    cfg.save();
}

void AdminPanel::onRemoteAutostartToggled(bool enabled)
{
    const QString host = m_natsHost->text().trimmed();
    const auto revert = [this, enabled]() {
        m_btnRemoteAutostart->blockSignals(true);
        m_btnRemoteAutostart->setChecked(!enabled);
        m_btnRemoteAutostart->blockSignals(false);
        setToggleCaption(m_btnRemoteAutostart,
                         QStringLiteral("Автозапуск системы на NATS-хосте"),
                         !enabled);
    };
    if (host.isEmpty()) {
        revert();
        QMessageBox::warning(this, QStringLiteral("Автозапуск"),
                             QStringLiteral("Укажите NATS-хост"));
        return;
    }

    QString error;
    const QString action = enabled ? QStringLiteral("enable") : QStringLiteral("disable");
    if (!sshOnNatsHost(host,
                       QStringLiteral("systemctl %1 robot.service").arg(action),
                       20000,
                       &error)) {
        revert();
        QMessageBox::warning(this, QStringLiteral("Автозапуск"),
                             QStringLiteral("Не удалось изменить автозапуск на %1:\n%2")
                                 .arg(host, error));
        return;
    }

    setToggleCaption(m_btnRemoteAutostart,
                     QStringLiteral("Автозапуск системы на NATS-хосте"),
                     enabled);
    ConnectionSettings cfg = networkSettings();
    cfg.save();
}

void AdminPanel::onShowCursorToggled(bool enabled)
{
    ConnectionSettings::applyShowCursor(enabled);
    setToggleCaption(m_btnShowCursor, QStringLiteral("Курсор мыши"), enabled);
    ConnectionSettings cfg = networkSettings();
    cfg.save();
}

void AdminPanel::onApplyClicked()
{
    const ConnectionSettings cfg = networkSettings();
    if (cfg.natsHost.isEmpty() || cfg.plcHost.isEmpty()) {
        QMessageBox::warning(this,
                             QStringLiteral("Сеть"),
                             QStringLiteral("Укажите хост NATS и ПЛК"));
        return;
    }
    emit applyRequested(cfg);
}

void AdminPanel::setBusy(bool busy)
{
    m_btnRefresh->setEnabled(!busy);
    m_table->setEnabled(!busy);
    updatePager();
    if (busy) {
        m_btnPrevPage->setEnabled(false);
        m_btnNextPage->setEnabled(false);
    }
}

void AdminPanel::onRefreshClicked()
{
    if (m_loadWatcher->isRunning() || m_saveWatcher->isRunning())
        return;

    if (!m_nats || !m_nats->isConnected()) {
        m_points.clear();
        fillTable();
        m_tableStatus->setText(QStringLiteral("Нет соединения с NATS"));
        m_tableStatus->setStyleSheet(QStringLiteral(
            "QLabel { font-family:\"Inter\",\"Segoe UI\",sans-serif;"
            " font-size:11px; color:#C43B3B; background:transparent; border:none; }"));
        return;
    }

    setBusy(true);
    m_tableStatus->setText(QStringLiteral("Загрузка…"));
    m_tableStatus->setStyleSheet(QStringLiteral(
        "QLabel { font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        " font-size:11px; color:#3A3F4A; background:transparent; border:none; }"));

    NatsClient *nats = m_nats;
    m_loadWatcher->setFuture(QtConcurrent::run([nats]() -> PointsLoadResult {
        PointsLoadResult result;
        QString err;
        if (!nats->openRobotPointsKv(&err)) {
            result.error = err;
            return result;
        }
        if (!nats->fetchRobotPoints(&result.points, &err)) {
            result.error = err;
            result.points.clear();
        }
        return result;
    }));
}

void AdminPanel::onPointsLoaded()
{
    const PointsLoadResult result = m_loadWatcher->result();
    setBusy(false);

    if (!result.error.isEmpty()) {
        m_points.clear();
        fillTable();
        m_tableStatus->setText(result.error);
        m_tableStatus->setStyleSheet(QStringLiteral(
            "QLabel { font-family:\"Inter\",\"Segoe UI\",sans-serif;"
            " font-size:11px; color:#C43B3B; background:transparent; border:none; }"));
        return;
    }

    m_points = result.points;
    clampPage();
    fillTable();
    m_tableStatus->setText(m_points.isEmpty()
                               ? QStringLiteral("Нет точек в robot_points")
                               : QStringLiteral("Точек: %1").arg(m_points.size()));
    m_tableStatus->setStyleSheet(QStringLiteral(
        "QLabel { font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        " font-size:11px; color:#3A3F4A; background:transparent; border:none; }"));
}

void AdminPanel::fillTable()
{
    clampPage();
    const int start = m_page * kPageSize;
    const int visible = qBound(0, m_points.size() - start, kPageSize);

    m_table->clearContents();
    m_table->setRowCount(kPageSize);

    for (int i = 0; i < visible; ++i) {
        const NatsClient::RobotPoint &p = m_points.at(start + i);

        auto makeItem = [](const QString &text, Qt::Alignment align) {
            auto *item = new QTableWidgetItem(text);
            item->setTextAlignment(align);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            return item;
        };

        m_table->setItem(i, 0, makeItem(p.name, Qt::AlignVCenter | Qt::AlignLeft));
        m_table->setItem(i, 1, makeItem(formatCoord(p.x), Qt::AlignVCenter | Qt::AlignRight));
        m_table->setItem(i, 2, makeItem(formatCoord(p.y), Qt::AlignVCenter | Qt::AlignRight));
        m_table->setItem(i, 3, makeItem(formatCoord(p.z), Qt::AlignVCenter | Qt::AlignRight));
        m_table->setItem(i, 4, makeItem(formatCoord(p.r), Qt::AlignVCenter | Qt::AlignRight));

        auto *btn = new QPushButton(QStringLiteral("Редактировать"));
        btn->setStyleSheet(QLatin1String(kCompactBtn));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        connect(btn, &QPushButton::clicked, this, [this, kvKey = p.kvKey]() {
            for (const NatsClient::RobotPoint &point : m_points) {
                if (point.kvKey == kvKey) {
                    editPoint(point);
                    return;
                }
            }
        });
        m_table->setCellWidget(i, 5, btn);
        m_table->setRowHeight(i, 26);
    }
    for (int i = visible; i < kPageSize; ++i)
        m_table->setRowHeight(i, 26);

    updatePager();
    fitTablePanel();
}

int AdminPanel::pageCount() const
{
    if (m_points.isEmpty())
        return 1;
    return (m_points.size() + kPageSize - 1) / kPageSize;
}

void AdminPanel::clampPage()
{
    m_page = qBound(0, m_page, pageCount() - 1);
}

void AdminPanel::goToPage(int page)
{
    m_page = page;
    clampPage();
    fillTable();
}

void AdminPanel::updatePager()
{
    const bool multi = m_points.size() > kPageSize;
    m_pager->setVisible(multi);
    m_pageLabel->setText(QStringLiteral("Лист %1 / %2").arg(m_page + 1).arg(pageCount()));
    m_btnPrevPage->setEnabled(multi && m_page > 0);
    m_btnNextPage->setEnabled(multi && m_page < pageCount() - 1);
}

void AdminPanel::fitTablePanel()
{
    constexpr int kNameW = 140;
    constexpr int kCoordW = 64;
    constexpr int kActionW = 132;
    constexpr int kHeaderBar = 32;
    constexpr int kPagerH = 32;
    constexpr int kTableHeaderH = 24;
    constexpr int kRowH = 26;
    constexpr int kTableExtra = 6;

    const int colsW = kNameW + kCoordW * 4 + kActionW;
    const int tableH = kTableHeaderH + kPageSize * kRowH + kTableExtra;
    m_table->setFixedHeight(tableH);

    const int pagerH = m_pager->isVisible() ? kPagerH : 0;
    const int tableX = 12 + m_leftColW + 12;
    const int panelW = colsW + 2;
    const int panelH = kHeaderBar + tableH + pagerH + 2;
    const int netH = m_networkPanel ? m_networkPanel->sizeHint().height() : panelH;
    const int sharedH = qMax(panelH, netH);

    m_tablePanel->setGeometry(tableX, 64, panelW, sharedH);
    if (m_networkPanel)
        m_networkPanel->setGeometry(12, 64, m_leftColW, sharedH);
    if (m_jogPanel) {
        m_jogPanel->setGeometry(12, 64 + sharedH + 12, m_leftColW, m_jogPanel->height());
    }
    fitPlcLogPanel();
}

void AdminPanel::fitPlcLogPanel()
{
    if (!m_plcLogPanel || !m_jogPanel)
        return;

    const QRect jog = m_jogPanel->geometry();
    const int x = 12;
    const int y = jog.bottom() + 1 + 12;
    const int w = m_leftColW;
    const int h = qMax(160, m_layoutH - y - 12);
    m_plcLogPanel->setGeometry(x, y, w, h);
    m_plcLogPanel->raise();
}

void AdminPanel::editPoint(const NatsClient::RobotPoint &point)
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Редактировать: %1").arg(point.name));
    dlg.setModal(true);
    dlg.setMinimumWidth(480);
    dlg.setStyleSheet(QStringLiteral("QDialog { background:#FFFFFF; }"));

    auto *xSpin = makeCoordSpin(point.x, &dlg);
    auto *ySpin = makeCoordSpin(point.y, &dlg);
    auto *zSpin = makeCoordSpin(point.z, &dlg);
    auto *rSpin = makeCoordSpin(point.r, &dlg);

    auto *form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);
    form->addRow(QStringLiteral("X"), xSpin);
    form->addRow(QStringLiteral("Y"), ySpin);
    form->addRow(QStringLiteral("Z"), zSpin);
    form->addRow(QStringLiteral("R"), rSpin);

    auto *btnUseCurrent = new QPushButton(QStringLiteral("Заменить на текущую позицию"), &dlg);
    btnUseCurrent->setStyleSheet(QLatin1String(kSecondaryBtn));
    btnUseCurrent->setCursor(Qt::PointingHandCursor);
    btnUseCurrent->setFocusPolicy(Qt::NoFocus);
    btnUseCurrent->setEnabled(m_poseKnown);

    auto *btnSave = new QPushButton(QStringLiteral("Сохранить"), &dlg);
    btnSave->setStyleSheet(QLatin1String(kPrimaryBtn));
    btnSave->setCursor(Qt::PointingHandCursor);
    btnSave->setDefault(true);
    btnSave->setFocusPolicy(Qt::NoFocus);

    auto *buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(8);
    buttons->addWidget(btnUseCurrent);
    buttons->addStretch(1);
    buttons->addWidget(btnSave);

    auto *lay = new QVBoxLayout(&dlg);
    lay->setContentsMargins(16, 16, 16, 16);
    lay->setSpacing(16);
    lay->addLayout(form);
    lay->addLayout(buttons);

    connect(btnUseCurrent, &QPushButton::clicked, &dlg, [this, xSpin, ySpin, zSpin, rSpin]() {
        if (!m_poseKnown) {
            QMessageBox::warning(this, QStringLiteral("Текущая позиция"),
                                 QStringLiteral("Нет данных о текущей позиции робота"));
            return;
        }

        QMessageBox confirm(this);
        confirm.setIcon(QMessageBox::Question);
        confirm.setWindowTitle(QStringLiteral("Текущая позиция"));
        confirm.setText(QStringLiteral("Уверены?"));
        confirm.setInformativeText(
            QStringLiteral("Заменить координаты точки на текущую позицию?\n\n"
                           "X = %1\nY = %2\nZ = %3\nR = %4")
                .arg(m_poseX, 0, 'f', 3)
                .arg(m_poseY, 0, 'f', 3)
                .arg(m_poseZ, 0, 'f', 3)
                .arg(m_poseR, 0, 'f', 3));
        confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        confirm.setDefaultButton(QMessageBox::No);
        confirm.setButtonText(QMessageBox::Yes, QStringLiteral("Да"));
        confirm.setButtonText(QMessageBox::No, QStringLiteral("Нет"));
        confirm.setStyleSheet(QStringLiteral(
            "QMessageBox, QDialog {"
            "  background:#FFFFFF;"
            "}"
            "QLabel {"
            "  color:#1F2126;"
            "  background:transparent;"
            "  font-family:\"Inter\",\"Segoe UI\",sans-serif;"
            "  font-size:13px;"
            "}"
            "QPushButton {"
            "  background:#FFFFFF; color:#1F2126;"
            "  border:1px solid #DBDEE3; border-radius:6px;"
            "  padding:6px 14px; min-height:32px; min-width:72px;"
            "  font-family:\"Inter\",\"Segoe UI\",sans-serif;"
            "  font-size:12px; font-weight:600;"
            "}"
            "QPushButton:pressed { background:#F0F2F5; }"
            "QPushButton:default {"
            "  background:#2F6FED; color:#FFFFFF; border:none;"
            "}"));

        if (confirm.exec() != QMessageBox::Yes)
            return;
        xSpin->setValue(m_poseX);
        ySpin->setValue(m_poseY);
        zSpin->setValue(m_poseZ);
        rSpin->setValue(m_poseR);
    });
    connect(btnSave, &QPushButton::clicked, &dlg, &QDialog::accept);

    if (dlg.exec() != QDialog::Accepted)
        return;

    savePoint(point.kvKey, xSpin->value(), ySpin->value(), zSpin->value(), rSpin->value());
}

void AdminPanel::savePoint(const QString &kvKey, double x, double y, double z, double r)
{
    if (m_loadWatcher->isRunning() || m_saveWatcher->isRunning())
        return;
    if (!m_nats || !m_nats->isConnected()) {
        QMessageBox::warning(this,
                             QStringLiteral("Robot Data Table"),
                             QStringLiteral("Нет соединения с NATS"));
        return;
    }

    setBusy(true);
    m_tableStatus->setText(QStringLiteral("Сохранение…"));

    NatsClient *nats = m_nats;
    m_saveWatcher->setFuture(QtConcurrent::run([nats, kvKey, x, y, z, r]() -> QString {
        QString err;
        if (!nats->openRobotPointsKv(&err))
            return err;
        if (!nats->saveRobotPointPose(kvKey, x, y, z, r, &err))
            return err;
        return QString();
    }));
}

void AdminPanel::onPointSaved()
{
    const QString err = m_saveWatcher->result();
    if (!err.isEmpty()) {
        setBusy(false);
        m_tableStatus->setText(err);
        m_tableStatus->setStyleSheet(QStringLiteral(
            "QLabel { font-family:\"Inter\",\"Segoe UI\",sans-serif;"
            " font-size:11px; color:#C43B3B; background:transparent; border:none; }"));
        QMessageBox::warning(this, QStringLiteral("Robot Data Table"),
                             QStringLiteral("Не удалось сохранить точку:\n%1").arg(err));
        return;
    }

    onRefreshClicked();
}

QPushButton *AdminPanel::makeJogButton(const QString &text, QWidget *parent)
{
    auto *btn = new QPushButton(text, parent);
    btn->setStyleSheet(QLatin1String(kJogBtn));
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setEnabled(false);
    m_jogButtons.push_back(btn);
    return btn;
}

double AdminPanel::currentStep() const
{
    const int i = m_stepSlider ? m_stepSlider->value() : kDefaultStepIndex;
    return kStepValues[qBound(0, i, kStepCount - 1)];
}

void AdminPanel::onStepChanged(int index)
{
    const double step = kStepValues[qBound(0, index, kStepCount - 1)];
    m_stepValue->setText(formatStep(step));
}

void AdminPanel::onPlcStateChanged()
{
    refreshPlcBufferLog();
}

void AdminPanel::refreshPlcBufferLog()
{
    if (!m_plcBufferLog || !m_plcBufferStatus)
        return;

    if (!m_plc) {
        m_plcBufferStatus->setText(QStringLiteral("ПЛК-клиент не подключён"));
        return;
    }

    if (!m_plc->isOk()) {
        m_plcBufferStatus->setText(QStringLiteral("Нет свежего кадра от ПЛК"));
        return;
    }

    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    m_plcBufferStatus->setText(
        QStringLiteral("%1  ok  cam1=%2  cam2=%3")
            .arg(ts)
            .arg(m_plc->vision1Ok() ? QStringLiteral("1") : QStringLiteral("0"))
            .arg(m_plc->vision2Ok() ? QStringLiteral("1") : QStringLiteral("0")));

    QString line = ts;
    line += QLatin1Char(' ');
    for (int i = 0; i < PlcClient::kStatusIntCount; ++i) {
        if (i > 0)
            line += QLatin1Char(' ');
        line += QString::number(m_plc->statusInt16(i));
    }
    m_plcBufferLog->appendPlainText(line);
}

void AdminPanel::updateMotionPowerButton()
{
    if (!m_btnMotionPower)
        return;
    if (m_motionRunning) {
        m_btnMotionPower->setText(QStringLiteral("Стоп"));
        m_btnMotionPower->setStyleSheet(QLatin1String(kPowerOnBtn));
    } else {
        m_btnMotionPower->setText(QStringLiteral("Старт"));
        m_btnMotionPower->setStyleSheet(QLatin1String(kPowerOffBtn));
    }
    for (QPushButton *btn : m_jogButtons)
        btn->setEnabled(m_motionRunning);
}

void AdminPanel::onMotionPose(double x, double y, double z, double r, bool running)
{
    m_poseKnown = true;
    m_poseX = x;
    m_poseY = y;
    m_poseZ = z;
    m_poseR = r;
    m_droX->setText(QStringLiteral("X: %1").arg(x, 0, 'f', 3));
    m_droY->setText(QStringLiteral("Y: %1").arg(y, 0, 'f', 3));
    m_droZ->setText(QStringLiteral("Z: %1").arg(z, 0, 'f', 3));
    m_droR->setText(QStringLiteral("R: %1").arg(r, 0, 'f', 3));
    if (m_motionRunning != running) {
        m_motionRunning = running;
        updateMotionPowerButton();
    }
}

void AdminPanel::onMotionPowerClicked()
{
    if (!m_nats || !m_nats->isConnected()) {
        QMessageBox::warning(this, QStringLiteral("Управление"),
                             QStringLiteral("Нет соединения с NATS"));
        return;
    }

    NatsClient *nats = m_nats;
    const bool running = m_motionRunning;
    m_motionRunning = !running;
    updateMotionPowerButton();
    QThreadPool::globalInstance()->start([nats, running]() {
        QString err;
        if (running)
            nats->publishMotionStop(&err);
        else
            nats->publishMotionStart(&err);
    });
}

void AdminPanel::sendMoveOffset(double x, double y, double z, double r)
{
    if (!m_motionRunning)
        return;
    if (!m_nats || !m_nats->isConnected()) {
        QMessageBox::warning(this, QStringLiteral("Управление"),
                             QStringLiteral("Нет соединения с NATS"));
        return;
    }

    NatsClient *nats = m_nats;
    QThreadPool::globalInstance()->start([nats, x, y, z, r]() {
        QString err;
        nats->publishMoveOffset(x, y, z, r, &err);
    });
}