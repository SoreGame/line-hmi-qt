#include "AdminPanel.h"
#include "Texts.h"
#include "ArduinoLink.h"
#include "PlcClient.h"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
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

#if defined(LINE_HMI_HAS_SERIALPORT)
#include <QSerialPortInfo>
#endif

namespace {

const char *kFieldStyle =
    "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {"
    "  background:#FFFFFF; color:#1F2126;"
    "  border:1px solid #DBDEE3; border-radius:6px;"
    "  padding:2px 6px; min-height:26px; max-height:26px;"
    "  font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:12px;"
    "}"
    "QSpinBox::up-button, QSpinBox::down-button,"
    "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width:16px; }"
    "QComboBox::drop-down { width:20px; border:none; }";

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
    button->setText(title + (on ? tx(":  ВКЛ") : tx(":  ВЫКЛ")));
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
        tx("Админ-панель"),
        tx("Пароль:"),
        QLineEdit::Password,
        QString(),
        &ok);
    if (!ok)
        return AuthResult::Cancelled;

    if (password != QLatin1String(kPassword)) {
        QMessageBox::warning(parent,
                             tx("Админ-панель"),
                             tx("Неверный пароль"));
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
    setWindowTitle(tx("Админ-панель"));
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

    auto *btnBack = new QPushButton(tx("Назад"), header);
    btnBack->setStyleSheet(QLatin1String(kSecondaryBtn));
    btnBack->setCursor(Qt::PointingHandCursor);
    btnBack->setFocusPolicy(Qt::NoFocus);

    auto *title = new QLabel(tx("Админ-панель"), header);
    title->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        "  font-size:16px; font-weight:600; color:#1F2126;"
        "  background:transparent; border:none;"
        "}"));

    auto *btnApply = new QPushButton(tx("Применить"), header);
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

    auto *sectionTitle = new QLabel(tx("Сеть"), networkPanel);
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
    m_plcPort->setValue(initial.plcPort > 0 ? initial.plcPort : 2025);
    m_plcPort->setFixedWidth(88);

    m_plcStatusBytes = new QSpinBox(networkPanel);
    m_plcStatusBytes->setRange(PlcClient::kMinStatusSize, PlcClient::kStatusSize);
    m_plcStatusBytes->setSingleStep(2);
    m_plcStatusBytes->setValue(initial.plcStatusBytes > 0 ? initial.plcStatusBytes : 120);
    m_plcStatusBytes->setSuffix(tx(" байт"));
    m_plcStatusBytes->setFixedWidth(120);

    for (QWidget *w : {static_cast<QWidget *>(m_natsHost),
                       static_cast<QWidget *>(m_natsPort),
                       static_cast<QWidget *>(m_plcHost),
                       static_cast<QWidget *>(m_plcPort),
                       static_cast<QWidget *>(m_plcStatusBytes)}) {
        w->setStyleSheet(QLatin1String(kFieldStyle));
    }

    auto *rowNats = new QWidget(networkPanel);
    auto *rowNatsLay = new QHBoxLayout(rowNats);
    rowNatsLay->setContentsMargins(0, 0, 0, 0);
    rowNatsLay->setSpacing(8);
    rowNatsLay->addWidget(makeLabeledField(tx("NATS хост"), m_natsHost, rowNats), 1);
    rowNatsLay->addWidget(makeLabeledField(tx("Порт"), m_natsPort, rowNats), 0);

    auto *rowPlc = new QWidget(networkPanel);
    auto *rowPlcLay = new QHBoxLayout(rowPlc);
    rowPlcLay->setContentsMargins(0, 0, 0, 0);
    rowPlcLay->setSpacing(8);
    rowPlcLay->addWidget(makeLabeledField(tx("ПЛК хост"), m_plcHost, rowPlc), 1);
    rowPlcLay->addWidget(makeLabeledField(tx("Порт"), m_plcPort, rowPlc), 0);

    auto *rowFrame = new QWidget(networkPanel);
    auto *rowFrameLay = new QHBoxLayout(rowFrame);
    rowFrameLay->setContentsMargins(0, 0, 0, 0);
    rowFrameLay->setSpacing(8);
    rowFrameLay->addWidget(makeLabeledField(
        tx("Ожидаемый размер кадра"), m_plcStatusBytes, rowFrame), 1);

    auto *panelLay = new QVBoxLayout(networkPanel);
    panelLay->setContentsMargins(12, 10, 12, 10);
    panelLay->setSpacing(8);
    panelLay->addWidget(sectionTitle);
    panelLay->addWidget(rowNats);
    panelLay->addWidget(rowPlc);
    panelLay->addWidget(rowFrame);
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

    auto *jogTitle = new QLabel(tx("Управление"), m_jogPanel);
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
    auto *stepTitle = new QLabel(tx("Шаг"), stepCol);
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

    m_plcLogTitle = new QLabel(m_plcLogPanel);
    m_plcLogTitle->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        "  font-size:13px; font-weight:600; color:#1F2126;"
        "  background:transparent; border:none;"
        "}"));
    {
        const int bytes = initial.plcStatusBytes > 0 ? initial.plcStatusBytes : 120;
        m_plcLogTitle->setText(tx("Буфер ПЛК (%1 int16, %2 байт)")
                                   .arg(bytes / 2)
                                   .arg(bytes));
    }

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
    plcLogLay->addWidget(m_plcLogTitle);
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

    m_btnRefresh = new QPushButton(tx("Обновить"), m_tablePanel);
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
        tx("Имя"),
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

    m_togglesPanel = new QFrame(this);
    m_togglesPanel->setObjectName(QStringLiteral("togglesPanel"));
    m_togglesPanel->setStyleSheet(QStringLiteral(
        "QFrame#togglesPanel {"
        "  background:#FFFFFF;"
        "  border:1px solid #DBDEE3;"
        "  border-radius:8px;"
        "}"));

    auto *togglesTitle = new QLabel(tx("Переключатели"), m_togglesPanel);
    togglesTitle->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        "  font-size:13px; font-weight:600; color:#1F2126;"
        "  background:transparent; border:none;"
        "}"));

    const auto makeToggle = [](QPushButton *button) {
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        button->setMinimumHeight(44);
        button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    };

    const bool hmiOn = QFile::exists(
        QStringLiteral("/etc/systemd/system/getty@tty1.service.d/autologin.conf"));
    m_btnHmiAutostart = new QPushButton(m_togglesPanel);
    makeToggle(m_btnHmiAutostart);
    m_btnHmiAutostart->setChecked(hmiOn);
    setToggleCaption(m_btnHmiAutostart,
                     tx("Автозапуск пульта на этом компьютере"),
                     hmiOn);

    m_btnRemoteAutostart = new QPushButton(m_togglesPanel);
    makeToggle(m_btnRemoteAutostart);
    m_btnRemoteAutostart->setChecked(initial.autostart);
    setToggleCaption(m_btnRemoteAutostart,
                     tx("Автозапуск системы на NATS-хосте"),
                     initial.autostart);

    m_btnShowCursor = new QPushButton(m_togglesPanel);
    makeToggle(m_btnShowCursor);
    m_btnShowCursor->setChecked(initial.showCursor);
    setToggleCaption(m_btnShowCursor,
                     tx("Курсор мыши"),
                     initial.showCursor);

    m_btnIgnoreLoadCell = new QPushButton(m_togglesPanel);
    makeToggle(m_btnIgnoreLoadCell);
    m_btnIgnoreLoadCell->setChecked(initial.ignoreLoadCell);
    setToggleCaption(m_btnIgnoreLoadCell,
                     tx("Игнорирование тензодатчика"),
                     initial.ignoreLoadCell);

    m_btnIgnoreLaunchLocks = new QPushButton(m_togglesPanel);
    makeToggle(m_btnIgnoreLaunchLocks);
    m_btnIgnoreLaunchLocks->setChecked(initial.ignoreLaunchLocks);
    setToggleCaption(m_btnIgnoreLaunchLocks,
                     tx("Игнорировать блокировки запуска"),
                     initial.ignoreLaunchLocks);

    m_btnResetEstop = new QPushButton(tx("Сбросить e-stop на пульте"), m_togglesPanel);
    m_btnResetEstop->setStyleSheet(QLatin1String(kSecondaryBtn));
    m_btnResetEstop->setCursor(Qt::PointingHandCursor);
    m_btnResetEstop->setFocusPolicy(Qt::NoFocus);
    m_btnResetEstop->setMinimumHeight(44);
    m_btnResetEstop->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    auto *targetHint = new QLabel(
        tx("Пульт включается на этой панели. Автозапуск системы — на компьютере с NATS-хостом. "
                       "Игнорирование тензодатчика: вкл — старт 7 1 1 0, выкл — 7 1 1 1. "
                       "Игнорировать блокировки запуска: можно стартовать цикл без ПЛК, Vision, CTRL и буфера. "
                       "Сбросить e-stop: выход из аварии без восстановления (7 5 на ПЛК) и сброс защёлки, "
                       "после перезапуска пульт не откроет выход из e-stop."),
        m_togglesPanel);
    targetHint->setWordWrap(true);
    targetHint->setStyleSheet(QStringLiteral(
        "QLabel { color:#737880; font-size:11px; background:transparent; border:none; }"));

    auto *togglesLay = new QVBoxLayout(m_togglesPanel);
    togglesLay->setContentsMargins(12, 10, 12, 12);
    togglesLay->setSpacing(8);
    togglesLay->addWidget(togglesTitle);
    togglesLay->addWidget(m_btnHmiAutostart);
    togglesLay->addWidget(m_btnRemoteAutostart);
    togglesLay->addWidget(m_btnShowCursor);
    togglesLay->addWidget(m_btnIgnoreLoadCell);
    togglesLay->addWidget(m_btnIgnoreLaunchLocks);
    togglesLay->addWidget(m_btnResetEstop);
    togglesLay->addWidget(targetHint);

    // --- Программы (.chai) под «Тензодатчиком» -------------------------
    m_programsPanel = new QFrame(this);
    m_programsPanel->setObjectName(QStringLiteral("programsPanel"));
    m_programsPanel->setStyleSheet(QStringLiteral(
        "QFrame#programsPanel {"
        "  background:#FFFFFF;"
        "  border:1px solid #DBDEE3;"
        "  border-radius:8px;"
        "}"));

    auto *programsTitle = new QLabel(
        tx("Программы · имена .chai в KV, как сохранены редактором"), m_programsPanel);
    programsTitle->setStyleSheet(togglesTitle->styleSheet());

    m_initScript1 = new QLineEdit(initial.initScriptDetail1, m_programsPanel);
    m_initScript2 = new QLineEdit(initial.initScriptDetail2, m_programsPanel);
    m_mainScript1 = new QLineEdit(initial.mainScriptDetail1, m_programsPanel);
    m_mainScript2 = new QLineEdit(initial.mainScriptDetail2, m_programsPanel);
    m_estopScript = new QLineEdit(initial.estopScript, m_programsPanel);
    m_startCountdown = new QSpinBox(m_programsPanel);
    m_startCountdown->setRange(0, 60);
    m_startCountdown->setValue(initial.startCountdownSec);
    m_startCountdown->setSuffix(tx(" с"));
    m_startCountdown->setFixedWidth(120);
    for (QWidget *w : {static_cast<QWidget *>(m_initScript1),
                       static_cast<QWidget *>(m_initScript2),
                       static_cast<QWidget *>(m_mainScript1),
                       static_cast<QWidget *>(m_mainScript2),
                       static_cast<QWidget *>(m_estopScript),
                       static_cast<QWidget *>(m_startCountdown)}) {
        w->setStyleSheet(QLatin1String(kFieldStyle));
    }

    const auto makeProgramsRow = [this](QWidget *left, QWidget *right) {
        auto *row = new QWidget(m_programsPanel);
        auto *lay = new QHBoxLayout(row);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(8);
        lay->addWidget(left, 1);
        lay->addWidget(right, 1);
        return row;
    };
    auto *rowInit = makeProgramsRow(
        makeLabeledField(tx("Инициализация · Деталь 1"), m_initScript1, m_programsPanel),
        makeLabeledField(tx("Инициализация · Деталь 2"), m_initScript2, m_programsPanel));
    auto *rowMain = makeProgramsRow(
        makeLabeledField(tx("Основная · Деталь 1"), m_mainScript1, m_programsPanel),
        makeLabeledField(tx("Основная · Деталь 2"), m_mainScript2, m_programsPanel));
    auto *rowEstop = makeProgramsRow(
        makeLabeledField(tx("Выход из E-stop"), m_estopScript, m_programsPanel),
        makeLabeledField(tx("Отсчёт до автозапуска основной"), m_startCountdown,
                         m_programsPanel));

    auto *programsLay = new QVBoxLayout(m_programsPanel);
    programsLay->setContentsMargins(12, 10, 12, 12);
    programsLay->setSpacing(8);
    programsLay->addWidget(programsTitle);
    programsLay->addWidget(rowInit);
    programsLay->addWidget(rowMain);
    programsLay->addWidget(rowEstop);

    // --- Arduino / тензодатчик (низ второго столбца) -------------------
    m_arduinoPanel = new QFrame(this);
    m_arduinoPanel->setObjectName(QStringLiteral("arduinoPanel"));
    m_arduinoPanel->setStyleSheet(QStringLiteral(
        "QFrame#arduinoPanel {"
        "  background:#FFFFFF;"
        "  border:1px solid #DBDEE3;"
        "  border-radius:8px;"
        "}"));

    auto *arduinoTitle = new QLabel(tx("Тензодатчик"), m_arduinoPanel);
    arduinoTitle->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        "  font-size:13px; font-weight:600; color:#1F2126;"
        "  background:transparent; border:none;"
        "}"));

    m_arduinoValue = new QLabel(QStringLiteral("—"), m_arduinoPanel);
    m_arduinoValue->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_arduinoValue->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_arduinoValue->setMinimumHeight(36);
    m_arduinoValue->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  font-family:\"Consolas\",\"DejaVu Sans Mono\",\"Courier New\",monospace;"
        "  font-size:28px; font-weight:700; color:#1F2126;"
        "  background:transparent; border:none;"
        "}"));

    m_arduinoKg = new QLabel(tx("— кг"), m_arduinoPanel);
    m_arduinoKg->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_arduinoKg->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_arduinoKg->setMinimumHeight(30);
    m_arduinoKg->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  font-family:\"Consolas\",\"DejaVu Sans Mono\",\"Courier New\",monospace;"
        "  font-size:22px; font-weight:600; color:#3A3F4A;"
        "  background:transparent; border:none;"
        "}"));

    auto *valueCaption = new QLabel(tx("Сырое / кг"), m_arduinoPanel);
    valueCaption->setStyleSheet(QStringLiteral(
        "QLabel { font-size:11px; color:#737880; background:transparent; border:none; }"));

    m_arduinoStatus = new QLabel(tx("Тензодатчик не подключен"), m_arduinoPanel);
    m_arduinoStatus->setWordWrap(true);
    m_arduinoStatus->setStyleSheet(QStringLiteral(
        "QLabel { font-size:12px; font-weight:600; color:#B42318; background:transparent; border:none; }"));

    auto *valueRow = new QWidget(m_arduinoPanel);
    auto *valueLay = new QVBoxLayout(valueRow);
    valueLay->setContentsMargins(0, 0, 0, 0);
    valueLay->setSpacing(2);
    valueLay->addWidget(m_arduinoValue);
    valueLay->addWidget(m_arduinoKg);
    valueLay->addWidget(m_arduinoStatus);

    m_arduinoPort = new QComboBox(m_arduinoPanel);
    m_arduinoPort->setEditable(true);
    m_arduinoPort->setInsertPolicy(QComboBox::NoInsert);
    m_arduinoPort->setStyleSheet(QLatin1String(kFieldStyle));
    m_arduinoPort->setMinimumHeight(28);

    m_btnArduinoReconnect = new QPushButton(tx("Открыть"), m_arduinoPanel);
    m_btnArduinoReconnect->setStyleSheet(QLatin1String(kCompactBtn));
    m_btnArduinoReconnect->setCursor(Qt::PointingHandCursor);
    m_btnArduinoReconnect->setFocusPolicy(Qt::NoFocus);
    m_btnArduinoReconnect->setFixedHeight(28);

    auto *portRow = new QWidget(m_arduinoPanel);
    auto *portLay = new QHBoxLayout(portRow);
    portLay->setContentsMargins(0, 0, 0, 0);
    portLay->setSpacing(8);
    portLay->addWidget(m_arduinoPort, 1);
    portLay->addWidget(m_btnArduinoReconnect, 0);

    m_arduinoThreshold = new QSpinBox(m_arduinoPanel);
    m_arduinoThreshold->setRange(ArduinoLink::kMinValue, ArduinoLink::kMaxValue);
    m_arduinoThreshold->setValue(ArduinoLink::clampValue(initial.arduinoThreshold));
    m_arduinoThreshold->setStyleSheet(QLatin1String(kFieldStyle));

    m_btnArduinoSend = new QPushButton(tx("Отправить"), m_arduinoPanel);
    m_btnArduinoSend->setStyleSheet(QLatin1String(kPrimaryBtn));
    m_btnArduinoSend->setCursor(Qt::PointingHandCursor);
    m_btnArduinoSend->setFocusPolicy(Qt::NoFocus);
    m_btnArduinoSend->setMinimumHeight(32);

    auto *thresholdRow = new QWidget(m_arduinoPanel);
    auto *thresholdLay = new QHBoxLayout(thresholdRow);
    thresholdLay->setContentsMargins(0, 0, 0, 0);
    thresholdLay->setSpacing(8);
    thresholdLay->addWidget(m_arduinoThreshold, 1);
    thresholdLay->addWidget(m_btnArduinoSend, 0);
    thresholdRow->setMinimumHeight(36);

    m_arduinoUnitsPerKgBaseline =
        initial.arduinoUnitsPerKg > 0.0 ? initial.arduinoUnitsPerKg : 100000.0;
    m_arduinoUnitsPerKg = new QDoubleSpinBox(m_arduinoPanel);
    m_arduinoUnitsPerKg->setDecimals(1);
    m_arduinoUnitsPerKg->setRange(0.1, 1e9);
    m_arduinoUnitsPerKg->setSingleStep(1000.0);
    m_arduinoUnitsPerKg->setValue(m_arduinoUnitsPerKgBaseline);
    m_arduinoUnitsPerKg->setStyleSheet(QLatin1String(kFieldStyle));
    m_arduinoUnitsPerKg->setMinimumWidth(180);
    m_arduinoUnitsPerKg->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_arduinoKnownKg = new QDoubleSpinBox(m_arduinoPanel);
    m_arduinoKnownKg->setDecimals(2);
    m_arduinoKnownKg->setRange(0.01, 1e6);
    m_arduinoKnownKg->setSingleStep(0.5);
    m_arduinoKnownKg->setValue(10.0);
    m_arduinoKnownKg->setSuffix(tx(" кг"));
    m_arduinoKnownKg->setStyleSheet(QLatin1String(kFieldStyle));
    m_arduinoKnownKg->setMinimumWidth(180);
    m_arduinoKnownKg->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_btnArduinoCalibrate = new QPushButton(tx("Калибровать"), m_arduinoPanel);
    m_btnArduinoCalibrate->setStyleSheet(QLatin1String(kPrimaryBtn));
    m_btnArduinoCalibrate->setCursor(Qt::PointingHandCursor);
    m_btnArduinoCalibrate->setFocusPolicy(Qt::NoFocus);
    m_btnArduinoCalibrate->setMinimumHeight(32);

    m_btnArduinoResetCalib = new QPushButton(tx("Сброс калибровки"), m_arduinoPanel);
    m_btnArduinoResetCalib->setStyleSheet(QLatin1String(kSecondaryBtn));
    m_btnArduinoResetCalib->setCursor(Qt::PointingHandCursor);
    m_btnArduinoResetCalib->setFocusPolicy(Qt::NoFocus);
    m_btnArduinoResetCalib->setMinimumHeight(32);

    m_arduinoCalibHint = new QLabel(m_arduinoPanel);
    m_arduinoCalibHint->setWordWrap(true);
    m_arduinoCalibHint->setStyleSheet(QStringLiteral(
        "QLabel { font-size:11px; color:#737880; background:transparent; border:none; }"));

    auto *calibRow = new QWidget(m_arduinoPanel);
    auto *calibLay = new QVBoxLayout(calibRow);
    calibLay->setContentsMargins(0, 0, 0, 0);
    calibLay->setSpacing(8);
    calibLay->addWidget(m_arduinoKnownKg);
    auto *calibBtns = new QWidget(calibRow);
    auto *calibBtnsLay = new QHBoxLayout(calibBtns);
    calibBtnsLay->setContentsMargins(0, 0, 0, 0);
    calibBtnsLay->setSpacing(8);
    calibBtnsLay->addWidget(m_btnArduinoCalibrate, 1);
    calibBtnsLay->addWidget(m_btnArduinoResetCalib, 1);
    calibLay->addWidget(calibBtns);

    auto *leftCol = new QWidget(m_arduinoPanel);
    auto *leftLay = new QVBoxLayout(leftCol);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(6);
    leftLay->addWidget(valueCaption);
    leftLay->addWidget(valueRow);
    leftLay->addWidget(makeLabeledField(tx("Порт"), portRow, leftCol));
    leftLay->addWidget(makeLabeledField(tx("Порог (сырое)"), thresholdRow, leftCol));
    leftLay->addStretch(1);
    {
        QSizePolicy pol(QSizePolicy::Ignored, QSizePolicy::Preferred);
        pol.setHorizontalStretch(1);
        leftCol->setSizePolicy(pol);
    }

    auto *rightCol = new QWidget(m_arduinoPanel);
    auto *rightLay = new QVBoxLayout(rightCol);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(6);
    rightLay->addWidget(makeLabeledField(
        tx("Калибровка: укажите вес на датчике"), calibRow, rightCol));
    rightLay->addWidget(m_arduinoCalibHint);
    rightLay->addWidget(makeLabeledField(tx("Отсчётов на кг"), m_arduinoUnitsPerKg, rightCol));
    rightLay->addStretch(1);
    {
        QSizePolicy pol(QSizePolicy::Ignored, QSizePolicy::Preferred);
        pol.setHorizontalStretch(1);
        rightCol->setSizePolicy(pol);
    }

    auto *body = new QWidget(m_arduinoPanel);
    auto *bodyLay = new QHBoxLayout(body);
    bodyLay->setContentsMargins(0, 0, 0, 0);
    bodyLay->setSpacing(20);
    bodyLay->addWidget(leftCol, 1);
    bodyLay->addWidget(rightCol, 1);

    auto *arduinoLay = new QVBoxLayout(m_arduinoPanel);
    arduinoLay->setContentsMargins(12, 10, 12, 14);
    arduinoLay->setSpacing(8);
    arduinoLay->setSizeConstraint(QLayout::SetMinimumSize);
    arduinoLay->addWidget(arduinoTitle);
    arduinoLay->addWidget(body, 1);

    updateArduinoCalibHint();

    m_arduino = new ArduinoLink(this);
    refreshArduinoPorts(initial.arduinoPort);
    reconnectArduino();

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
    connect(m_btnIgnoreLoadCell, &QPushButton::toggled, this, &AdminPanel::onIgnoreLoadCellToggled);
    connect(m_btnIgnoreLaunchLocks, &QPushButton::toggled, this, &AdminPanel::onIgnoreLaunchLocksToggled);
    connect(m_btnResetEstop, &QPushButton::clicked, this, [this]() {
        const auto answer = QMessageBox::question(
            this, tx("Сбросить e-stop"),
            tx("Выйти из аварии без восстановления и сбросить защёлку e-stop на пульте?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer == QMessageBox::Yes)
            emit estopResetRequested();
    });
    connect(m_btnRefresh, &QPushButton::clicked, this, &AdminPanel::onRefreshClicked);
    connect(m_btnPrevPage, &QPushButton::clicked, this, [this]() { goToPage(m_page - 1); });
    connect(m_btnNextPage, &QPushButton::clicked, this, [this]() { goToPage(m_page + 1); });
    connect(m_loadWatcher, &QFutureWatcher<PointsLoadResult>::finished,
            this, &AdminPanel::onPointsLoaded);
    connect(m_saveWatcher, &QFutureWatcher<QString>::finished,
            this, &AdminPanel::onPointSaved);
    connect(m_arduino, &ArduinoLink::valueChanged, this, &AdminPanel::onArduinoValue);
    connect(m_arduino, &ArduinoLink::linkChanged, this, &AdminPanel::onArduinoLinkChanged);
    connect(m_btnArduinoSend, &QPushButton::clicked, this, &AdminPanel::onArduinoSendThreshold);
    connect(m_btnArduinoReconnect, &QPushButton::clicked, this, &AdminPanel::onArduinoPortEdited);
    connect(m_btnArduinoCalibrate, &QPushButton::clicked, this, &AdminPanel::onArduinoCalibrate);
    connect(m_btnArduinoResetCalib, &QPushButton::clicked, this, &AdminPanel::onArduinoResetCalibration);
    connect(m_arduinoUnitsPerKg, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double) { updateArduinoKgDisplay(); });

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
    c.plcStatusBytes = m_plcStatusBytes ? m_plcStatusBytes->value() : 120;
    c.autostart = m_btnRemoteAutostart && m_btnRemoteAutostart->isChecked();
    c.hmiAutostart = m_btnHmiAutostart && m_btnHmiAutostart->isChecked();
    c.showCursor = m_btnShowCursor && m_btnShowCursor->isChecked();
    c.ignoreLoadCell = m_btnIgnoreLoadCell && m_btnIgnoreLoadCell->isChecked();
    c.ignoreLaunchLocks = m_btnIgnoreLaunchLocks && m_btnIgnoreLaunchLocks->isChecked();
    if (m_initScript1) {
        c.initScriptDetail1 = m_initScript1->text().trimmed();
        c.initScriptDetail2 = m_initScript2->text().trimmed();
        c.mainScriptDetail1 = m_mainScript1->text().trimmed();
        c.mainScriptDetail2 = m_mainScript2->text().trimmed();
        c.estopScript = m_estopScript->text().trimmed();
        c.startCountdownSec = m_startCountdown->value();
    }
    if (m_arduinoPort) {
        QString port;
        const int idx = m_arduinoPort->currentIndex();
        if (idx >= 0) {
            const QVariant data = m_arduinoPort->itemData(idx);
            if (data.isValid() && !data.toString().trimmed().isEmpty()
                && m_arduinoPort->currentText() == m_arduinoPort->itemText(idx)) {
                port = data.toString().trimmed();
            }
        }
        if (port.isEmpty())
            port = m_arduinoPort->currentText().trimmed();
        if (port.contains(QStringLiteral(" — ")))
            port = port.section(QStringLiteral(" — "), 0, 0).trimmed();
        c.arduinoPort = port;
    }
    if (m_arduinoThreshold)
        c.arduinoThreshold = ArduinoLink::clampValue(m_arduinoThreshold->value());
    if (m_arduinoUnitsPerKg) {
        const double scale = m_arduinoUnitsPerKg->value();
        c.arduinoUnitsPerKg = scale > 0.0 ? scale : 100000.0;
    }
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
        *error = tx(
            "NATS-хост указывает на эту панель. Укажите адрес компьютера, где работает NATS.");
        return false;
    }
    for (const QHostAddress &address : QNetworkInterface::allAddresses()) {
        if (address.toString().compare(host.trimmed(), Qt::CaseInsensitive) == 0) {
            *error = tx(
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
        *error = tx("Не удалось запустить ssh");
        return false;
    }
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        *error = tx("Нет ответа от %1").arg(host);
        return false;
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        *error = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        if (error->isEmpty())
            *error = tx("Команда на %1 завершилась с кодом %2").arg(host).arg(proc.exitCode());
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
                         tx("Автозапуск пульта на этом компьютере"),
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
        QMessageBox::warning(this, tx("Автозапуск пульта"),
                             err.isEmpty() ? tx("Не удалось изменить автозапуск на этой панели")
                                           : err);
        return;
    }

    setToggleCaption(m_btnHmiAutostart,
                     tx("Автозапуск пульта на этом компьютере"),
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
                         tx("Автозапуск системы на NATS-хосте"),
                         !enabled);
    };
    if (host.isEmpty()) {
        revert();
        QMessageBox::warning(this, tx("Автозапуск"),
                             tx("Укажите NATS-хост"));
        return;
    }

    QString error;
    const QString action = enabled ? QStringLiteral("enable") : QStringLiteral("disable");
    if (!sshOnNatsHost(host,
                       QStringLiteral("systemctl %1 robot.service").arg(action),
                       20000,
                       &error)) {
        revert();
        QMessageBox::warning(this, tx("Автозапуск"),
                             tx("Не удалось изменить автозапуск на %1:\n%2")
                                 .arg(host, error));
        return;
    }

    setToggleCaption(m_btnRemoteAutostart,
                     tx("Автозапуск системы на NATS-хосте"),
                     enabled);
    ConnectionSettings cfg = networkSettings();
    cfg.save();
}

void AdminPanel::onShowCursorToggled(bool enabled)
{
    ConnectionSettings::applyShowCursor(enabled);
    setToggleCaption(m_btnShowCursor, tx("Курсор мыши"), enabled);
    ConnectionSettings cfg = networkSettings();
    cfg.save();
}

void AdminPanel::onIgnoreLoadCellToggled(bool enabled)
{
    setToggleCaption(m_btnIgnoreLoadCell,
                     tx("Игнорирование тензодатчика"),
                     enabled);
    ConnectionSettings cfg = networkSettings();
    cfg.save();
}

void AdminPanel::onIgnoreLaunchLocksToggled(bool enabled)
{
    setToggleCaption(m_btnIgnoreLaunchLocks,
                     tx("Игнорировать блокировки запуска"),
                     enabled);
    ConnectionSettings cfg = networkSettings();
    cfg.save();
}

void AdminPanel::onApplyClicked()
{
    const ConnectionSettings cfg = networkSettings();
    if (cfg.natsHost.isEmpty() || cfg.plcHost.isEmpty()) {
        QMessageBox::warning(this,
                             tx("Сеть"),
                             tx("Укажите хост NATS и ПЛК"));
        return;
    }
    if (m_arduino)
        m_arduino->sendThreshold(cfg.arduinoThreshold);
    if (m_plcLogTitle) {
        m_plcLogTitle->setText(tx("Буфер ПЛК (%1 int16, %2 байт)")
                                   .arg(cfg.plcStatusBytes / 2)
                                   .arg(cfg.plcStatusBytes));
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
        m_tableStatus->setText(tx("Нет соединения с NATS"));
        m_tableStatus->setStyleSheet(QStringLiteral(
            "QLabel { font-family:\"Inter\",\"Segoe UI\",sans-serif;"
            " font-size:11px; color:#C43B3B; background:transparent; border:none; }"));
        return;
    }

    setBusy(true);
    m_tableStatus->setText(tx("Загрузка…"));
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
                               ? tx("Нет точек в robot_points")
                               : tx("Точек: %1").arg(m_points.size()));
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

        auto *btn = new QPushButton(tx("Редактировать"));
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
    m_pageLabel->setText(tx("Лист %1 / %2").arg(m_page + 1).arg(pageCount()));
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
    fitTogglesPanel();
    fitPlcLogPanel();
    fitArduinoPanel();
    fitProgramsPanel();
}

void AdminPanel::fitTogglesPanel()
{
    if (!m_togglesPanel || !m_tablePanel)
        return;
    if (m_togglesPanel->layout())
        m_togglesPanel->layout()->activate();

    const QRect table = m_tablePanel->geometry();
    const int togglesH = qMax(m_togglesPanel->minimumSizeHint().height(),
                              m_togglesPanel->sizeHint().height());
    m_togglesPanel->setGeometry(table.x(), table.bottom() + 1 + 12, table.width(), togglesH);
}

void AdminPanel::fitProgramsPanel()
{
    if (!m_programsPanel || !m_arduinoPanel)
        return;
    if (m_programsPanel->layout())
        m_programsPanel->layout()->activate();

    const QRect arduino = m_arduinoPanel->geometry();
    const int h = qMax(m_programsPanel->minimumSizeHint().height(),
                       m_programsPanel->sizeHint().height());
    m_programsPanel->setGeometry(arduino.x(), arduino.bottom() + 1 + 12, arduino.width(), h);
}

void AdminPanel::fitArduinoPanel()
{
    if (!m_arduinoPanel || !m_tablePanel)
        return;
    if (m_arduinoPanel->layout())
        m_arduinoPanel->layout()->activate();

    const QRect table = m_tablePanel->geometry();
    const int x = table.right() + 1 + 12;
    const int y = table.y();
    const int w = table.width();
    const int h = qMax(qMax(m_arduinoPanel->minimumSizeHint().height(),
                            m_arduinoPanel->sizeHint().height()),
                       120);
    m_arduinoPanel->setGeometry(x, y, w, h);
    m_arduinoPanel->raise();
}

void AdminPanel::refreshArduinoPorts(const QString &preferred)
{
    if (!m_arduinoPort)
        return;

    const QString current = preferred.trimmed().isEmpty()
                                ? m_arduinoPort->currentText().trimmed()
                                : preferred.trimmed();
    m_arduinoPort->blockSignals(true);
    m_arduinoPort->clear();

#if defined(LINE_HMI_HAS_SERIALPORT)
    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : ports) {
        QString label = info.portName();
        if (!info.description().isEmpty())
            label += QStringLiteral(" — %1").arg(info.description());
        m_arduinoPort->addItem(label, info.systemLocation());
    }
#endif

    int idx = -1;
    for (int i = 0; i < m_arduinoPort->count(); ++i) {
        const QString sys = m_arduinoPort->itemData(i).toString();
        const QString shortName = QFileInfo(sys).fileName();
        const QString text = m_arduinoPort->itemText(i);
        if (sys == current || shortName == current || text == current
            || text.startsWith(current + QLatin1String(" — "))
            || text.startsWith(shortName + QLatin1String(" — "))) {
            idx = i;
            break;
        }
    }
    if (idx >= 0) {
        m_arduinoPort->setCurrentIndex(idx);
    } else if (!current.isEmpty()) {
        m_arduinoPort->setEditText(current);
    } else {
        m_arduinoPort->setEditText(QStringLiteral("/dev/ttyUSB0"));
    }
    m_arduinoPort->blockSignals(false);
}

void AdminPanel::reconnectArduino()
{
    if (!m_arduino || !m_arduinoPort)
        return;

    QString port;
    const int idx = m_arduinoPort->currentIndex();
    if (idx >= 0) {
        const QVariant data = m_arduinoPort->itemData(idx);
        if (data.isValid() && !data.toString().trimmed().isEmpty()
            && m_arduinoPort->currentText() == m_arduinoPort->itemText(idx)) {
            port = data.toString().trimmed();
        }
    }
    if (port.isEmpty())
        port = m_arduinoPort->currentText().trimmed();
    if (port.contains(QStringLiteral(" — ")))
        port = port.section(QStringLiteral(" — "), 0, 0).trimmed();

    m_arduinoValue->setText(QStringLiteral("—"));
    m_arduinoHasValue = false;
    if (m_arduinoKg)
        m_arduinoKg->setText(tx("— кг"));
    m_arduino->open(port);
    if (m_arduinoThreshold)
        m_arduino->sendThreshold(m_arduinoThreshold->value());
}

void AdminPanel::updateArduinoKgDisplay()
{
    if (!m_arduinoKg)
        return;
    if (!m_arduinoHasValue) {
        m_arduinoKg->setText(tx("— кг"));
        return;
    }
    const double scale = m_arduinoUnitsPerKg ? m_arduinoUnitsPerKg->value() : 0.0;
    if (scale <= 0.0) {
        m_arduinoKg->setText(tx("— кг"));
        return;
    }
    const double kg = static_cast<double>(m_arduinoLastRaw) / scale;
    m_arduinoKg->setText(tx("%1 кг").arg(kg, 0, 'f', 2));
}

void AdminPanel::updateArduinoCalibHint()
{
    if (!m_arduinoCalibHint)
        return;
    if (m_arduinoCalibPoints <= 0) {
        m_arduinoCalibHint->setText(
            tx("Укажите известный вес, нагрузите датчик и нажмите «Калибровать». "
                           "Повторите с другим весом для уточнения."));
        return;
    }
    m_arduinoCalibHint->setText(
        tx("Точек калибровки: %1 · среднее «отсчётов на кг» подставляется выше. "
                       "«Сброс» вернёт значение до калибровки в этой сессии.")
            .arg(m_arduinoCalibPoints));
}

void AdminPanel::onArduinoCalibrate()
{
    if (!m_arduinoKnownKg || !m_arduinoUnitsPerKg) {
        return;
    }
    if (!m_arduinoHasValue) {
        QMessageBox::warning(this,
                             tx("Калибровка"),
                             tx("Нет текущего значения с тензодатчика."));
        return;
    }
    const double knownKg = m_arduinoKnownKg->value();
    if (knownKg <= 0.0) {
        QMessageBox::warning(this,
                             tx("Калибровка"),
                             tx("Укажите вес больше нуля."));
        return;
    }
    if (m_arduinoLastRaw == 0) {
        QMessageBox::warning(this,
                             tx("Калибровка"),
                             tx("Сырое значение равно 0 — нагрузите датчик."));
        return;
    }

    const double sample = static_cast<double>(m_arduinoLastRaw) / knownKg;
    if (!(sample > 0.0) || !qIsFinite(sample)) {
        QMessageBox::warning(this,
                             tx("Калибровка"),
                             tx("Не удалось вычислить масштаб по этой точке."));
        return;
    }

    m_arduinoCalibScaleSum += sample;
    ++m_arduinoCalibPoints;
    const double averaged = m_arduinoCalibScaleSum / static_cast<double>(m_arduinoCalibPoints);
    m_arduinoUnitsPerKg->setValue(averaged);
    updateArduinoCalibHint();
    updateArduinoKgDisplay();
}

void AdminPanel::onArduinoResetCalibration()
{
    m_arduinoCalibScaleSum = 0.0;
    m_arduinoCalibPoints = 0;
    if (m_arduinoUnitsPerKg)
        m_arduinoUnitsPerKg->setValue(m_arduinoUnitsPerKgBaseline > 0.0
                                          ? m_arduinoUnitsPerKgBaseline
                                          : 100000.0);
    updateArduinoCalibHint();
    updateArduinoKgDisplay();
}

void AdminPanel::onArduinoValue(qint32 value)
{
    m_arduinoLastRaw = value;
    m_arduinoHasValue = true;
    if (m_arduinoValue)
        m_arduinoValue->setText(QString::number(value));
    updateArduinoKgDisplay();
}

void AdminPanel::onArduinoLinkChanged(bool linked, const QString &message)
{
    if (!m_arduinoStatus)
        return;
    m_arduinoStatus->setText(message);
    m_arduinoStatus->setStyleSheet(linked
                                       ? QStringLiteral(
                                             "QLabel { font-size:12px; font-weight:600; color:#067647; "
                                             "background:transparent; border:none; }")
                                       : QStringLiteral(
                                             "QLabel { font-size:12px; font-weight:600; color:#B42318; "
                                             "background:transparent; border:none; }"));
    if (!linked) {
        m_arduinoHasValue = false;
        if (m_arduinoValue)
            m_arduinoValue->setText(QStringLiteral("—"));
        if (m_arduinoKg)
            m_arduinoKg->setText(tx("— кг"));
    }
}

void AdminPanel::onArduinoSendThreshold()
{
    if (!m_arduino || !m_arduinoThreshold)
        return;
    m_arduino->sendThreshold(m_arduinoThreshold->value());
}

void AdminPanel::onArduinoPortEdited()
{
    refreshArduinoPorts(m_arduinoPort ? m_arduinoPort->currentText() : QString());
    reconnectArduino();
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
    dlg.setWindowTitle(tx("Редактировать: %1").arg(point.name));
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

    auto *btnUseCurrent = new QPushButton(tx("Заменить на текущую позицию"), &dlg);
    btnUseCurrent->setStyleSheet(QLatin1String(kSecondaryBtn));
    btnUseCurrent->setCursor(Qt::PointingHandCursor);
    btnUseCurrent->setFocusPolicy(Qt::NoFocus);
    btnUseCurrent->setEnabled(m_poseKnown);

    auto *btnSave = new QPushButton(tx("Сохранить"), &dlg);
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
            QMessageBox::warning(this, tx("Текущая позиция"),
                                 tx("Нет данных о текущей позиции робота"));
            return;
        }

        QMessageBox confirm(this);
        confirm.setIcon(QMessageBox::Question);
        confirm.setWindowTitle(tx("Текущая позиция"));
        confirm.setText(tx("Уверены?"));
        confirm.setInformativeText(
            tx("Заменить координаты точки на текущую позицию?\n\n"
                           "X = %1\nY = %2\nZ = %3\nR = %4")
                .arg(m_poseX, 0, 'f', 3)
                .arg(m_poseY, 0, 'f', 3)
                .arg(m_poseZ, 0, 'f', 3)
                .arg(m_poseR, 0, 'f', 3));
        confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        confirm.setDefaultButton(QMessageBox::No);
        confirm.setButtonText(QMessageBox::Yes, tx("Да"));
        confirm.setButtonText(QMessageBox::No, tx("Нет"));
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
                             tx("Нет соединения с NATS"));
        return;
    }

    setBusy(true);
    m_tableStatus->setText(tx("Сохранение…"));

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
                             tx("Не удалось сохранить точку:\n%1").arg(err));
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
        m_plcBufferStatus->setText(tx("ПЛК-клиент не подключён"));
        return;
    }

    if (!m_plc->isOk()) {
        m_plcBufferStatus->setText(tx("Нет свежего кадра от ПЛК"));
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
    for (int i = 0; i < m_plc->statusIntCount(); ++i) {
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
        m_btnMotionPower->setText(tx("Стоп"));
        m_btnMotionPower->setStyleSheet(QLatin1String(kPowerOnBtn));
    } else {
        m_btnMotionPower->setText(tx("Старт"));
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
        QMessageBox::warning(this, tx("Управление"),
                             tx("Нет соединения с NATS"));
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
        QMessageBox::warning(this, tx("Управление"),
                             tx("Нет соединения с NATS"));
        return;
    }

    NatsClient *nats = m_nats;
    QThreadPool::globalInstance()->start([nats, x, y, z, r]() {
        QString err;
        nats->publishMoveOffset(x, y, z, r, &err);
    });
}