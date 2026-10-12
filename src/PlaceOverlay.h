#ifndef PLACEOVERLAY_H
#define PLACEOVERLAY_H

#include "ConnectionSettings.h"
#include "OverlayCard.h"

#include <QElapsedTimer>

#include <array>

class QLabel;
class QTimer;
class AxisScale;

// Робот вне стартовых зон, ПЛК готов: на весь экран слева шкалы x, y, z, r ближайшей зоны,
// справа картинка стартового положения. Закрывает его пульт, когда робот попадает в зону.
class PlaceOverlay : public OverlayCard
{
    Q_OBJECT
public:
    PlaceOverlay(const QString &imagePath, QWidget *parent);

    void refresh(int zone, bool poseKnown, const std::array<double, 4> &pose,
                 const std::array<StartZone, ConnectionSettings::kZoneCount> &zones);
    // Каждая поза из motion.status: шкалы не ждут общего обновления экрана (~1 раз в секунду без ПЛК).
    void setPose(double x, double y, double z, double r);

private:
    static constexpr int kAnimIntervalMs = 16;

    void apply();

    QLabel *m_target = nullptr;
    std::array<AxisScale *, 4> m_axes{};
    QTimer *m_animTimer = nullptr;
    QElapsedTimer m_animClock;
    int m_zone = 0;
    bool m_poseKnown = false;
    // Поза приходит ~185 раз в секунду; применяется не чаще кадра анимации.
    bool m_poseDirty = false;
    std::array<double, 4> m_pose{};
    std::array<StartZone, ConnectionSettings::kZoneCount> m_zones{};
};

#endif // PLACEOVERLAY_H
