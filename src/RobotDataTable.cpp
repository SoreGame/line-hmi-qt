#include "RobotDataTable.h"
#include "Texts.h"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

namespace {

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

constexpr int kNameW = 140;
constexpr int kCoordW = 64;
constexpr int kActionW = 132;
constexpr int kHeaderBar = 32;
constexpr int kPagerH = 32;
constexpr int kTableHeaderH = 24;
constexpr int kRowH = 26;
constexpr int kTableExtra = 6;

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

void setStatusText(QLabel *label, const QString &text, bool error)
{
    label->setText(text);
    label->setStyleSheet(QStringLiteral(
        "QLabel { font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        " font-size:11px; color:%1; background:transparent; border:none; }")
                             .arg(error ? QStringLiteral("#C43B3B")
                                        : QStringLiteral("#3A3F4A")));
}

} // namespace

RobotDataTable::RobotDataTable(NatsClient *nats, QWidget *parent)
    : QFrame(parent)
    , m_nats(nats)
{
    setObjectName(QStringLiteral("tablePanel"));
    setStyleSheet(QStringLiteral(
        "QFrame#tablePanel {"
        "  background:#FFFFFF;"
        "  border:1px solid #DBDEE3;"
        "  border-radius:8px;"
        "}"));

    auto *tableTitle = new QLabel(QStringLiteral("Robot Data Table"), this);
    tableTitle->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        "  font-size:12px; font-weight:600; color:#1F2126;"
        "  background:transparent; border:none;"
        "}"));

    m_status = new QLabel(this);
    m_status->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        "  font-size:11px; color:#3A3F4A;"
        "  background:transparent; border:none;"
        "}"));

    m_btnRefresh = new QPushButton(tx("Обновить"), this);
    m_btnRefresh->setStyleSheet(QLatin1String(kCompactBtn));
    m_btnRefresh->setCursor(Qt::PointingHandCursor);
    m_btnRefresh->setFocusPolicy(Qt::NoFocus);

    auto *tableHeader = new QWidget(this);
    auto *tableHeaderLay = new QHBoxLayout(tableHeader);
    tableHeaderLay->setContentsMargins(8, 6, 8, 4);
    tableHeaderLay->setSpacing(8);
    tableHeaderLay->addWidget(tableTitle);
    tableHeaderLay->addWidget(m_status, 1);
    tableHeaderLay->addWidget(m_btnRefresh);

    m_table = new QTableWidget(kPageSize, 6, this);
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
    m_table->setFixedHeight(kTableHeaderH + kPageSize * kRowH + kTableExtra);

    auto *vh = m_table->verticalHeader();
    vh->hide();
    vh->setVisible(false);
    vh->setMinimumWidth(0);
    vh->setMaximumWidth(0);
    vh->setFixedWidth(0);
    vh->setDefaultSectionSize(kRowH);

    m_table->horizontalHeader()->setFixedHeight(kTableHeaderH);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_table->setColumnWidth(0, kNameW);
    for (int col = 1; col <= 4; ++col) {
        m_table->horizontalHeader()->setSectionResizeMode(col, QHeaderView::Fixed);
        m_table->setColumnWidth(col, kCoordW);
    }
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    m_table->setColumnWidth(5, kActionW);

    m_pager = new QWidget(this);
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

    auto *tableLay = new QVBoxLayout(this);
    tableLay->setContentsMargins(0, 0, 0, 0);
    tableLay->setSpacing(0);
    tableLay->addWidget(tableHeader);
    tableLay->addWidget(m_table);
    tableLay->addWidget(m_pager);
    tableLay->addStretch(1);

    m_loadWatcher = new QFutureWatcher<PointsLoadResult>(this);
    m_saveWatcher = new QFutureWatcher<QString>(this);

    connect(m_btnRefresh, &QPushButton::clicked, this, &RobotDataTable::reload);
    connect(m_btnPrevPage, &QPushButton::clicked, this, [this]() { goToPage(m_page - 1); });
    connect(m_btnNextPage, &QPushButton::clicked, this, [this]() { goToPage(m_page + 1); });
    connect(m_loadWatcher, &QFutureWatcher<PointsLoadResult>::finished,
            this, &RobotDataTable::onPointsLoaded);
    connect(m_saveWatcher, &QFutureWatcher<QString>::finished,
            this, &RobotDataTable::onPointSaved);
    if (m_nats) {
        connect(m_nats, &NatsClient::motionPoseReceived, this,
                [this](double x, double y, double z, double r, bool) {
                    m_poseKnown = true;
                    m_poseX = x;
                    m_poseY = y;
                    m_poseZ = z;
                    m_poseR = r;
                });
    }

    updatePager();
    QTimer::singleShot(0, this, &RobotDataTable::reload);
}

RobotDataTable::~RobotDataTable()
{
    if (m_loadWatcher && m_loadWatcher->isRunning())
        m_loadWatcher->waitForFinished();
    if (m_saveWatcher && m_saveWatcher->isRunning())
        m_saveWatcher->waitForFinished();
}

int RobotDataTable::preferredWidth() const
{
    return kNameW + kCoordW * 4 + kActionW + 2;
}

int RobotDataTable::contentHeight() const
{
    const int tableH = kTableHeaderH + kPageSize * kRowH + kTableExtra;
    const int pagerH = m_pagerOn ? kPagerH : 0;
    return kHeaderBar + tableH + pagerH + 2;
}

void RobotDataTable::setBusy(bool busy)
{
    m_btnRefresh->setEnabled(!busy);
    m_table->setEnabled(!busy);
    updatePager();
    if (busy) {
        m_btnPrevPage->setEnabled(false);
        m_btnNextPage->setEnabled(false);
    }
}

void RobotDataTable::reload()
{
    if (m_loadWatcher->isRunning() || m_saveWatcher->isRunning())
        return;

    if (!m_nats || !m_nats->isConnected()) {
        m_points.clear();
        fillTable();
        setStatusText(m_status, tx("Нет соединения с NATS"), true);
        return;
    }

    setBusy(true);
    setStatusText(m_status, tx("Загрузка…"), false);

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

void RobotDataTable::onPointsLoaded()
{
    const PointsLoadResult result = m_loadWatcher->result();
    setBusy(false);

    if (!result.error.isEmpty()) {
        m_points.clear();
        fillTable();
        setStatusText(m_status, result.error, true);
        return;
    }

    m_points = result.points;
    clampPage();
    fillTable();
    setStatusText(m_status,
                  m_points.isEmpty() ? tx("Нет точек в robot_points")
                                     : tx("Точек: %1").arg(m_points.size()),
                  false);
}

void RobotDataTable::fillTable()
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
        m_table->setRowHeight(i, kRowH);
    }
    for (int i = visible; i < kPageSize; ++i)
        m_table->setRowHeight(i, kRowH);

    updatePager();
}

int RobotDataTable::pageCount() const
{
    if (m_points.isEmpty())
        return 1;
    return (m_points.size() + kPageSize - 1) / kPageSize;
}

void RobotDataTable::clampPage()
{
    m_page = qBound(0, m_page, pageCount() - 1);
}

void RobotDataTable::goToPage(int page)
{
    m_page = page;
    clampPage();
    fillTable();
}

void RobotDataTable::updatePager()
{
    const bool multi = m_points.size() > kPageSize;
    const bool changed = m_pagerOn != multi;
    m_pagerOn = multi;
    m_pager->setVisible(multi);
    m_pageLabel->setText(tx("Лист %1 / %2").arg(m_page + 1).arg(pageCount()));
    m_btnPrevPage->setEnabled(multi && m_page > 0);
    m_btnNextPage->setEnabled(multi && m_page < pageCount() - 1);
    if (changed)
        emit contentHeightChanged();
}

void RobotDataTable::editPoint(const NatsClient::RobotPoint &point)
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

    auto *btnCancel = new QPushButton(tx("Отмена"), &dlg);
    btnCancel->setStyleSheet(QLatin1String(kSecondaryBtn));
    btnCancel->setCursor(Qt::PointingHandCursor);
    btnCancel->setFocusPolicy(Qt::NoFocus);

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
    buttons->addWidget(btnCancel);
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
    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(btnSave, &QPushButton::clicked, &dlg, &QDialog::accept);

    if (dlg.exec() != QDialog::Accepted)
        return;

    savePoint(point.kvKey, xSpin->value(), ySpin->value(), zSpin->value(), rSpin->value());
}

void RobotDataTable::savePoint(const QString &kvKey, double x, double y, double z, double r)
{
    if (m_loadWatcher->isRunning() || m_saveWatcher->isRunning())
        return;
    if (!m_nats || !m_nats->isConnected()) {
        QMessageBox::warning(this, QStringLiteral("Robot Data Table"),
                             tx("Нет соединения с NATS"));
        return;
    }

    setBusy(true);
    setStatusText(m_status, tx("Сохранение…"), false);

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

void RobotDataTable::onPointSaved()
{
    const QString err = m_saveWatcher->result();
    if (!err.isEmpty()) {
        setBusy(false);
        setStatusText(m_status, err, true);
        QMessageBox::warning(this, QStringLiteral("Robot Data Table"),
                             tx("Не удалось сохранить точку:\n%1").arg(err));
        return;
    }

    reload();
}
