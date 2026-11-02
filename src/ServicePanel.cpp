#include "ServicePanel.h"
#include "Texts.h"
#include "PlcClient.h"
#include "RobotDataTable.h"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QCloseEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QSizePolicy>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

namespace {

const char *kSecondaryBtn =
    "QPushButton {"
    "  background:#FFFFFF; color:#1F2126; border:1px solid #DBDEE3; border-radius:6px;"
    "  padding:6px 14px; min-height:32px; min-width:68px;"
    "  font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:12px; font-weight:600;"
    "}"
    "QPushButton:pressed { background:#F0F2F5; }"
    "QPushButton:disabled { color:#8A9099; }";

const char *kToggleOffBtn =
    "QPushButton {"
    "  background:#FFFFFF; color:#1F2126; border:1px solid #DBDEE3; border-radius:8px;"
    "  padding:8px 16px; min-height:44px; max-height:44px; min-width:96px;"
    "  font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:13px; font-weight:600;"
    "}"
    "QPushButton:pressed { background:#F0F2F5; }";

const char *kToggleOnBtn =
    "QPushButton {"
    "  background:#2E8C47; color:#FFFFFF; border:1px solid #2E8C47; border-radius:8px;"
    "  padding:8px 16px; min-height:44px; max-height:44px; min-width:96px;"
    "  font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:13px; font-weight:600;"
    "}"
    "QPushButton:pressed { background:#246F38; }";

const char *kDoBtn =
    "QPushButton {"
    "  background:#FFFFFF; color:#1F2126; border:1px solid #DBDEE3; border-radius:6px;"
    "  padding:0px 4px;"
    "  font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:18px; font-weight:700;"
    "}"
    "QPushButton:pressed { background:#F0F2F5; }"
    "QPushButton:checked {"
    "  background:#2E8C47; color:#FFFFFF; border:1px solid #2E8C47;"
    "}"
    "QPushButton:checked:pressed { background:#246F38; }"
    "QPushButton:disabled { background:#F0F2F5; color:#A0A4AB; border:1px solid #E4E7EC; }";

const char *kNoticeStyle =
    "QMessageBox{background:#FFFFFF;}"
    " QLabel{color:#1F2126; background:transparent;"
    " font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:15px;}"
    " QPushButton{background:#FFFFFF; color:#1F2126; border:1px solid #DBDEE3; border-radius:6px;"
    " padding:6px 14px; min-height:32px; min-width:72px; font-weight:600;}";

const char *kTableStyle =
    "QTableWidget {"
    "  background:#FFFFFF; color:#1F2126;"
    "  border:none; gridline-color:#E4E7EC; outline:none;"
    "  font-family:\"Inter\",\"Segoe UI\",sans-serif;"
    "}"
    "QTableWidget::item { padding:0px; }"
    "QTableCornerButton::section { background:#F7F8FA; border:none; }";

constexpr int kBufColW = 72;
constexpr int kBufRowH = 30;
constexpr int kDoBtnW = 112;
constexpr int kDoBtnH = 52;
constexpr int kDoSpacing = 6;
// Внутренний отступ панели и её рамка из kPanelStyle: оба съедают ширину содержимого.
constexpr int kPanelPadding = 10;
constexpr int kPanelBorder = 1;
constexpr int kPanelChromeW = 2 * (kPanelPadding + kPanelBorder);

void paintBufferCell(QLabel *lab, int n, bool hasValue, qint16 value)
{
    if (!lab)
        return;

    QString valueHtml = QStringLiteral("—");
    QString bg = QStringLiteral("#FFFFFF");
    if (hasValue) {
        if (value == 0) {
            bg = QStringLiteral("#F6E27A");
            valueHtml = QStringLiteral(
                "<span style=\"font-size:16px;font-weight:800;color:#1F2126;\">0</span>");
        } else if (value == 1) {
            bg = QStringLiteral("#8FD19A");
            valueHtml = QStringLiteral(
                "<span style=\"font-size:16px;font-weight:800;color:#1F2126;\">1</span>");
        } else {
            valueHtml = QStringLiteral(
                "<span style=\"font-size:12px;font-weight:600;color:#1F2126;\">%1</span>")
                            .arg(value);
        }
    }

    lab->setStyleSheet(QStringLiteral(
        "QLabel { background:%1; border:none; padding:0px; }").arg(bg));
    lab->setText(QStringLiteral(
        "<span style=\"font-size:8px;font-weight:600;color:#737880;\">%1</span> %2")
                     .arg(n)
                     .arg(valueHtml));
}

const char *kPanelStyle =
    "QFrame {"
    "  background:#FFFFFF;"
    "  border:1px solid #DBDEE3;"
    "  border-radius:8px;"
    "}";

void setToggleCaption(QPushButton *button, const QString &title, bool on)
{
    button->setText(title + (on ? tx(":  ВКЛ") : tx(":  ВЫКЛ")));
    button->setStyleSheet(QLatin1String(on ? kToggleOnBtn : kToggleOffBtn));
}

QFrame *makePanel(const QString &objectName, QWidget *parent)
{
    auto *panel = new QFrame(parent);
    panel->setObjectName(objectName);
    panel->setStyleSheet(QLatin1String(kPanelStyle));
    return panel;
}

QLabel *makeTitle(const QString &text, QWidget *parent)
{
    auto *title = new QLabel(text, parent);
    title->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        "  font-size:13px; font-weight:600; color:#1F2126;"
        "  background:transparent; border:none;"
        "}"));
    return title;
}

} // namespace

ServicePanel::ServicePanel(PlcClient *plc, NatsClient *nats, QWidget *parent)
    : QDialog(parent)
    , m_plc(plc)
{
    setWindowTitle(tx("Сервисная панель"));
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

    auto *title = new QLabel(tx("Сервисная панель"), header);
    title->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        "  font-size:16px; font-weight:600; color:#1F2126;"
        "  background:transparent; border:none;"
        "}"));

    auto *headerLay = new QHBoxLayout(header);
    headerLay->setContentsMargins(12, 0, 12, 0);
    headerLay->setSpacing(12);
    headerLay->addWidget(btnBack, 0);
    headerLay->addWidget(title, 0);
    headerLay->addStretch(1);

    const int bodyX = 12;
    const int bodyY = 64;
    const int gap = 12;
    const int bufTableW = kBufCols * kBufColW;
    const int bufTableH = kBufRows * kBufRowH;
    const int bufferW = bufTableW + kPanelChromeW;
    const int bufferH = 28 + 18 + bufTableH + 16;
    const int doGridW = kDoCols * kDoBtnW + (kDoCols - 1) * kDoSpacing;

    auto *bufferPanel = makePanel(QStringLiteral("bufferPanel"), this);
    bufferPanel->setGeometry(bodyX, bodyY, bufferW, bufferH);

    auto *bufferTitle =
        makeTitle(QStringLiteral("DI 0..%1").arg(kBufCellCount - 1), bufferPanel);
    m_bufferStatus = new QLabel(bufferPanel);
    m_bufferStatus->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        "  font-size:11px; color:#3A3F4A;"
        "  background:transparent; border:none;"
        "}"));

    m_bufferTable = new QTableWidget(kBufRows, kBufCols, bufferPanel);
    m_bufferTable->setStyleSheet(QLatin1String(kTableStyle));
    m_bufferTable->setFrameShape(QFrame::NoFrame);
    m_bufferTable->setShowGrid(true);
    m_bufferTable->setAlternatingRowColors(false);
    m_bufferTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_bufferTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_bufferTable->setFocusPolicy(Qt::NoFocus);
    m_bufferTable->setCornerButtonEnabled(false);
    m_bufferTable->horizontalHeader()->setVisible(false);
    m_bufferTable->verticalHeader()->setVisible(false);
    m_bufferTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_bufferTable->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_bufferTable->verticalHeader()->setMinimumSectionSize(kBufRowH);
    m_bufferTable->verticalHeader()->setDefaultSectionSize(kBufRowH);
    m_bufferTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_bufferTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_bufferTable->setFixedSize(bufTableW, bufTableH);
    for (int col = 0; col < kBufCols; ++col)
        m_bufferTable->setColumnWidth(col, kBufColW);

    for (int row = 0; row < kBufRows; ++row) {
        m_bufferTable->setRowHeight(row, kBufRowH);
        for (int col = 0; col < kBufCols; ++col) {
            const int n = row * kBufCols + col;
            auto *lab = new QLabel(m_bufferTable);
            lab->setAlignment(Qt::AlignCenter);
            lab->setTextFormat(Qt::RichText);
            lab->setMargin(0);
            paintBufferCell(lab, n, false, 0);
            m_bufferTable->setCellWidget(row, col, lab);
        }
    }

    auto *bufferLay = new QVBoxLayout(bufferPanel);
    bufferLay->setContentsMargins(kPanelPadding, 8, kPanelPadding, 8);
    bufferLay->setSpacing(4);
    bufferLay->addWidget(bufferTitle);
    bufferLay->addWidget(m_bufferStatus);
    bufferLay->addWidget(m_bufferTable, 0, Qt::AlignLeft | Qt::AlignTop);
    bufferLay->addStretch(1);
    bufferLay->activate();

    auto *doPanel = makePanel(QStringLiteral("doPanel"), this);

    auto *doTitle = makeTitle(QStringLiteral("DO 0..%1").arg(kDoCount - 1), doPanel);
    m_doTitle = doTitle;
    auto *doGrid = new QWidget(doPanel);
    auto *doLay = new QGridLayout(doGrid);
    doLay->setContentsMargins(0, 0, 0, 0);
    doLay->setHorizontalSpacing(kDoSpacing);
    doLay->setVerticalSpacing(kDoSpacing);
    m_doButtons.resize(kDoCount);
    for (int n = 0; n < kDoCount; ++n) {
        auto *btn = new QPushButton(QString::number(n), doGrid);
        btn->setCheckable(true);
        btn->setChecked(false);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setMinimumSize(kDoBtnW, kDoBtnH);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        btn->setStyleSheet(QLatin1String(kDoBtn));
        m_doButtons[n] = btn;
        doLay->addWidget(btn, n / kDoCols, n % kDoCols);
        connect(btn, &QPushButton::toggled, this, [this, n](bool on) { onDoToggled(n, on); });
    }
    for (int r = 0; r < kDoRows; ++r)
        doLay->setRowStretch(r, 1);
    for (int c = 0; c < kDoCols; ++c)
        doLay->setColumnStretch(c, 1);

    auto *doPanelLay = new QVBoxLayout(doPanel);
    doPanelLay->setContentsMargins(kPanelPadding, 8, kPanelPadding, 8);
    doPanelLay->setSpacing(4);
    doPanelLay->addWidget(doTitle);
    doPanelLay->addWidget(doGrid, 1);
    doPanelLay->activate();

    const int doW = qMax(doGridW + kPanelChromeW, doPanel->minimumSizeHint().width());

    const int tablesH = qMax(qMax(bufferH, bufferPanel->sizeHint().height()),
                             doPanel->sizeHint().height());
    bufferPanel->setGeometry(bodyX, bodyY, bufferW, tablesH);
    doPanel->setGeometry(bodyX + bufferW + gap, bodyY, doW, tablesH);

    auto *togglesPanel = new QFrame(this);
    togglesPanel->setObjectName(QStringLiteral("togglesPanel"));
    togglesPanel->setStyleSheet(QStringLiteral(
        "QFrame#togglesPanel {"
        "  background:#FFFFFF;"
        "  border:1px solid #DBDEE3;"
        "  border-radius:8px;"
        "}"));

    auto *togglesTitle = new QLabel(tx("Переключатели"), togglesPanel);
    togglesTitle->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        "  font-size:13px; font-weight:600; color:#1F2126;"
        "  background:transparent; border:none;"
        "}"));

    m_btnServiceMode = new QPushButton(togglesPanel);
    m_btnServiceMode->setCheckable(true);
    m_btnServiceMode->setCursor(Qt::PointingHandCursor);
    m_btnServiceMode->setFocusPolicy(Qt::NoFocus);
    m_btnServiceMode->setMinimumHeight(44);
    m_btnServiceMode->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setToggleCaption(m_btnServiceMode, tx("Перевести ПЛК в сервисный режим"), false);

    auto *togglesLay = new QVBoxLayout(togglesPanel);
    togglesLay->setContentsMargins(12, 10, 12, 12);
    togglesLay->setSpacing(8);
    togglesLay->addWidget(togglesTitle);
    togglesLay->addWidget(m_btnServiceMode);
    togglesLay->activate();

    const int togglesW = bufferW + gap + doW;
    const int togglesH = qMax(togglesPanel->sizeHint().height(), 90);
    togglesPanel->setGeometry(bodyX, bodyY + tablesH + gap, togglesW, togglesH);

    m_robotTable = new RobotDataTable(nats, this);
    const int tableW = m_robotTable->preferredWidth();
    const int tableH = m_robotTable->contentHeight();
    const int rightX = bodyX + bufferW + gap + doW + gap;
    int tableX = rightX;
    int tableY = bodyY;
    if (rightX + tableW > m_layoutW - 12) {
        tableX = bodyX;
        tableY = bodyY + tablesH + gap + togglesH + gap;
    }
    m_robotTable->setGeometry(tableX, tableY, tableW, tableH);
    connect(m_robotTable, &RobotDataTable::contentHeightChanged, this, [this]() {
        const QRect g = m_robotTable->geometry();
        m_robotTable->setGeometry(g.x(), g.y(), m_robotTable->preferredWidth(),
                                  m_robotTable->contentHeight());
    });

    connect(btnBack, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_btnServiceMode, &QPushButton::toggled, this, &ServicePanel::onServiceModeToggled);
    if (m_plc)
        connect(m_plc, &PlcClient::stateChanged, this, &ServicePanel::onPlcStateChanged);

    refreshBufferTable();
    syncServiceUiFromPlc();
}

bool ServicePanel::plcInMode(PlcClient::Mode mode) const
{
    return m_plc && m_plc->isOk() && m_plc->mode() == mode;
}

bool ServicePanel::plcServiceActive() const
{
    return m_plc && m_plc->serviceActive();
}

void ServicePanel::updateDoAvailability()
{
    const bool inService = plcServiceActive();
    for (QPushButton *btn : std::as_const(m_doButtons)) {
        if (!inService && btn->isChecked()) {
            const QSignalBlocker blocker(btn);
            btn->setChecked(false);
        }
        btn->setEnabled(inService);
    }
    if (m_doTitle)
        m_doTitle->setText(inService ? QStringLiteral("DO 0..%1").arg(kDoCount - 1)
                                     : tx("DO 0..%1 · только в сервисном режиме").arg(kDoCount - 1));
}

void ServicePanel::syncServiceUiFromPlc()
{
    const bool active = plcServiceActive();
    if (m_btnServiceMode) {
        const QSignalBlocker blocker(m_btnServiceMode);
        m_btnServiceMode->setChecked(active);
        setToggleCaption(m_btnServiceMode, tx("Перевести ПЛК в сервисный режим"), active);
    }
    // После 99 1 ячейка 68 ещё может быть 0 — запрос держим, пока ПЛК не ответит.
    // Как только 68 с 1 ушёл в 0, сервис для пульта закончен.
    if (m_prevServiceActive && !active)
        m_serviceRequested = false;
    if (active)
        m_serviceRequested = true;
    m_prevServiceActive = active;
    updateDoAvailability();
}

void ServicePanel::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (!m_fullScreenApplied) {
        m_fullScreenApplied = true;
        showFullScreen();
    }
}

void ServicePanel::closeEvent(QCloseEvent *event)
{
    leaveServiceIfNeeded();
    QDialog::closeEvent(event);
}

void ServicePanel::reject()
{
    leaveServiceIfNeeded();
    QDialog::reject();
}

void ServicePanel::leaveServiceIfNeeded()
{
    if (!m_serviceRequested && !plcServiceActive())
        return;
    sendServiceMode(false);
    m_serviceRequested = false;
    syncServiceUiFromPlc();
}

void ServicePanel::onPlcStateChanged()
{
    refreshBufferTable();
    syncServiceUiFromPlc();
}

void ServicePanel::refreshBufferTable()
{
    if (!m_bufferTable || !m_bufferStatus)
        return;

    if (!m_plc) {
        m_bufferStatus->setText(tx("ПЛК-клиент не подключён"));
        for (int n = 0; n < kBufCellCount; ++n) {
            auto *lab =
                qobject_cast<QLabel *>(m_bufferTable->cellWidget(n / kBufCols, n % kBufCols));
            paintBufferCell(lab, n, false, 0);
        }
        return;
    }

    if (!m_plc->isOk()) {
        m_bufferStatus->setText(tx("Нет свежего кадра от ПЛК"));
        return;
    }

    // Ячейки за пределами кадра ПЛК показываем прочерком, а не нулём.
    const int received = m_plc->statusIntCount();
    m_bufferStatus->setText(tx("Кадр ок, %1 int16").arg(received));
    for (int n = 0; n < kBufCellCount; ++n) {
        const int row = n / kBufCols;
        const int col = n % kBufCols;
        auto *lab = qobject_cast<QLabel *>(m_bufferTable->cellWidget(row, col));
        paintBufferCell(lab, n, n < received, m_plc->statusInt16(n));
    }
}

void ServicePanel::onServiceModeToggled(bool on)
{
    // Запрос 99 1 только из «Готов»; факт сервиса — по ячейке 68, не по кнопке.
    if (on && !plcInMode(PlcClient::Mode::Ready) && !plcServiceActive()) {
        const QString now = !m_plc || !m_plc->isOk() ? tx("нет связи с ПЛК")
                                                     : tx("сейчас: %1").arg(PlcClient::modeText(m_plc->mode()));
        QMessageBox box(QMessageBox::Warning, tx("Сервисный режим"),
                        tx("ПЛК ещё не в стартовом положении (%1).\n"
                           "Сервисный режим включается, только когда ПЛК в режиме «Готов».")
                            .arg(now),
                        QMessageBox::Ok, this);
        box.setStyleSheet(QString::fromUtf8(kNoticeStyle));
        box.exec();
        syncServiceUiFromPlc();
        return;
    }
    if (on) {
        m_serviceRequested = true;
        sendServiceMode(true);
    } else {
        m_serviceRequested = false;
        sendServiceMode(false);
    }
    // Кнопка и DO следуют за ячейкой 68: ВКЛ только при 1, ВЫКЛ — как только 0.
    syncServiceUiFromPlc();
}

void ServicePanel::onDoToggled(int n, bool on)
{
    sendDo(n, on);
}

void ServicePanel::sendServiceMode(bool on)
{
    if (!m_plc)
        return;
    m_plc->sendCommand(PlcClient::serviceModeCommand(on));
}

void ServicePanel::sendDo(int n, bool on)
{
    if (!m_plc)
        return;
    m_plc->sendCommand(PlcClient::doCommand(n, on));
}
