#include "ZonesDialog.h"
#include "NatsClient.h"
#include "Texts.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

const char *kAxes[] = {"X", "Y", "Z", "R"};

std::array<double *, 8> boundsOf(StartZone &zone)
{
    return {&zone.xMin, &zone.xMax, &zone.yMin, &zone.yMax,
            &zone.zMin, &zone.zMax, &zone.rMin, &zone.rMax};
}

} // namespace

ZonesDialog::ZonesDialog(const ConnectionSettings &cfg, NatsClient *nats, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tx("Стартовые зоны робота"));
    setStyleSheet(QStringLiteral(
        "QDialog{background:#FFFFFF;}"
        " QWidget{font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:14px; color:#1F2126;}"
        " QDoubleSpinBox, QLineEdit{min-height:36px; padding:0 6px; background:#FFFFFF;"
        " border:1px solid #DBDEE3; border-radius:4px;}"
        " QDoubleSpinBox:disabled{background:#F0F2F5; color:#A0A4AB;}"
        " QCheckBox{background:transparent;}"
        " QPushButton{min-height:36px; padding:0 14px; background:#FFFFFF;"
        " border:1px solid #DBDEE3; border-radius:6px; font-weight:600;}"
        " QPushButton:pressed{background:#F0F2F5;}"));

    auto *hint = new QLabel(
        tx("Из этих зон init-скрипт сам выводит робота в старт. Робот в зоне, если x, y, z и r "
           "все в границах. Номер зоны уходит на ПЛК в каждом опросе 203."),
        this);
    hint->setWordWrap(true);

    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);
    grid->addWidget(new QLabel(tx("Зона"), this), 0, 0);
    for (int axis = 0; axis < 4; ++axis) {
        grid->addWidget(new QLabel(tx("%1 мин").arg(QLatin1String(kAxes[axis])), this), 0, 1 + axis * 2);
        grid->addWidget(new QLabel(tx("%1 макс").arg(QLatin1String(kAxes[axis])), this), 0, 2 + axis * 2);
    }

    for (int i = 0; i < ConnectionSettings::kZoneCount; ++i) {
        StartZone zone = cfg.zones[i];
        ZoneRow &row = m_rows[i];
        row.enabled = new QCheckBox(tx("Зона %1").arg(i + 1), this);
        row.enabled->setChecked(zone.enabled);
        grid->addWidget(row.enabled, i + 1, 0);
        const auto bounds = boundsOf(zone);
        for (int b = 0; b < 8; ++b) {
            auto *spin = new QDoubleSpinBox(this);
            spin->setRange(-100000.0, 100000.0);
            spin->setDecimals(3);
            spin->setValue(*bounds[b]);
            spin->setFixedWidth(130);
            row.bounds[b] = spin;
            grid->addWidget(spin, i + 1, 1 + b);
        }
        auto *take = new QPushButton(tx("Текущая поза ± допуск"), this);
        connect(take, &QPushButton::clicked, this, [this, i]() { takePose(i); });
        grid->addWidget(take, i + 1, 9);
    }

    m_tolerance = new QDoubleSpinBox(this);
    m_tolerance->setRange(0.0, 10000.0);
    m_tolerance->setDecimals(3);
    m_tolerance->setValue(10.0);
    m_tolerance->setFixedWidth(130);
    m_pose = new QLabel(this);
    auto *toleranceRow = new QHBoxLayout();
    toleranceRow->addWidget(new QLabel(tx("Допуск"), this));
    toleranceRow->addWidget(m_tolerance);
    toleranceRow->addSpacing(24);
    toleranceRow->addWidget(m_pose, 1);

    m_image = new QLineEdit(cfg.placementImage, this);
    m_image->setPlaceholderText(tx("Путь к картинке стартового положения"));
    auto *browse = new QPushButton(tx("Выбрать…"), this);
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, tx("Картинка стартового положения"), m_image->text(),
            QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp)"));
        if (!path.isEmpty())
            m_image->setText(path);
    });
    auto *imageRow = new QHBoxLayout();
    imageRow->addWidget(new QLabel(tx("Картинка для ручной установки"), this));
    imageRow->addWidget(m_image, 1);
    imageRow->addWidget(browse);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);
    layout->addWidget(hint);
    layout->addLayout(grid);
    layout->addLayout(toleranceRow);
    layout->addLayout(imageRow);
    layout->addStretch();
    layout->addWidget(buttons);

    if (nats) {
        connect(nats, &NatsClient::motionPoseReceived, this,
                [this](double x, double y, double z, double r, bool) { onPose(x, y, z, r); });
    }
    updatePoseLabel();
}

std::array<StartZone, ConnectionSettings::kZoneCount> ZonesDialog::zones() const
{
    std::array<StartZone, ConnectionSettings::kZoneCount> out{};
    for (int i = 0; i < ConnectionSettings::kZoneCount; ++i) {
        out[i].enabled = m_rows[i].enabled->isChecked();
        const auto bounds = boundsOf(out[i]);
        for (int b = 0; b < 8; ++b)
            *bounds[b] = m_rows[i].bounds[b]->value();
    }
    return out;
}

QString ZonesDialog::placementImage() const
{
    return m_image->text().trimmed();
}

void ZonesDialog::onPose(double x, double y, double z, double r)
{
    m_current[0] = x;
    m_current[1] = y;
    m_current[2] = z;
    m_current[3] = r;
    m_poseKnown = true;
    updatePoseLabel();
}

void ZonesDialog::takePose(int zone)
{
    if (!m_poseKnown)
        return;
    const double tol = m_tolerance->value();
    for (int axis = 0; axis < 4; ++axis) {
        m_rows[zone].bounds[axis * 2]->setValue(m_current[axis] - tol);
        m_rows[zone].bounds[axis * 2 + 1]->setValue(m_current[axis] + tol);
    }
    m_rows[zone].enabled->setChecked(true);
    updatePoseLabel();
}

void ZonesDialog::updatePoseLabel()
{
    if (!m_poseKnown) {
        m_pose->setText(tx("Поза робота неизвестна (нет motion.status)"));
        return;
    }
    ConnectionSettings probe;
    probe.zones = zones();
    const int zone = probe.zoneOf(m_current[0], m_current[1], m_current[2], m_current[3]);
    m_pose->setText(tx("Поза: X %1  Y %2  Z %3  R %4 · %5")
                        .arg(m_current[0], 0, 'f', 2)
                        .arg(m_current[1], 0, 'f', 2)
                        .arg(m_current[2], 0, 'f', 2)
                        .arg(m_current[3], 0, 'f', 2)
                        .arg(zone > 0 ? tx("в зоне %1").arg(zone) : tx("ни в одной зоне")));
}
