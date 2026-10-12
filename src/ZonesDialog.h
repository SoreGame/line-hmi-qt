#ifndef ZONESDIALOG_H
#define ZONESDIALOG_H

#include "ConnectionSettings.h"

#include <QDialog>

class NatsClient;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;

// Стартовые зоны робота и картинка для ручной установки.
class ZonesDialog : public QDialog
{
    Q_OBJECT
public:
    ZonesDialog(const ConnectionSettings &cfg, NatsClient *nats, QWidget *parent = nullptr);

    std::array<StartZone, ConnectionSettings::kZoneCount> zones() const;
    QString placementImage() const;

private:
    void onPose(double x, double y, double z, double r);
    void takePose(int zone);
    void updatePoseLabel();

    struct ZoneRow {
        QCheckBox *enabled = nullptr;
        QDoubleSpinBox *bounds[8] = {};
    };
    ZoneRow m_rows[ConnectionSettings::kZoneCount];
    QDoubleSpinBox *m_tolerance = nullptr;
    QLineEdit *m_image = nullptr;
    QLabel *m_pose = nullptr;
    bool m_poseKnown = false;
    double m_current[4] = {0, 0, 0, 0};
};

#endif // ZONESDIALOG_H
