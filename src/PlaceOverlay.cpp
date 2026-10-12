#include "PlaceOverlay.h"
#include "Texts.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

const QColor kInk(0x1F, 0x23, 0x28);
const QColor kMuted(0x73, 0x78, 0x80);
const QColor kTrack(0xE5, 0xE7, 0xEB);
const QColor kOk(0x16, 0xA3, 0x4A);
const QColor kOkBand(0xBB, 0xF7, 0xD0);
const QColor kBad(0xDC, 0x26, 0x26);
const QColor kBadBand(0xFE, 0xE2, 0xE2);
const QColor kPanel(0xF3, 0xF4, 0xF6);
const QColor kPanelBorder(0xC5, 0xC9, 0xD0);

std::array<double, 2> boundsOf(const StartZone &zone, int axis)
{
    switch (axis) {
    case 0: return {zone.xMin, zone.xMax};
    case 1: return {zone.yMin, zone.yMax};
    case 2: return {zone.zMin, zone.zMax};
    default: return {zone.rMin, zone.rMax};
    }
}

double outside(double value, double lo, double hi)
{
    return value < lo ? lo - value : value > hi ? value - hi : 0.0;
}

// Ближайшая включённая зона: меньше осей вне диапазона, затем меньше суммарный промах
// в долях ширины диапазона. -1 — включённых зон нет.
int nearestZone(const std::array<double, 4> &pose,
                const std::array<StartZone, ConnectionSettings::kZoneCount> &zones)
{
    int best = -1;
    int bestMisses = 0;
    double bestDistance = 0;
    for (int i = 0; i < ConnectionSettings::kZoneCount; ++i) {
        if (!zones[i].enabled)
            continue;
        int misses = 0;
        double distance = 0;
        for (int axis = 0; axis < 4; ++axis) {
            const auto [lo, hi] = boundsOf(zones[i], axis);
            const double miss = outside(pose[axis], lo, hi);
            if (miss > 0) {
                ++misses;
                distance += miss / std::max(hi - lo, 1.0);
            }
        }
        if (best < 0 || misses < bestMisses || (misses == bestMisses && distance < bestDistance)) {
            best = i;
            bestMisses = misses;
            bestDistance = distance;
        }
    }
    return best;
}

QFont uiFont(int px, bool bold)
{
    QFont font(QStringLiteral("Inter"));
    font.setPixelSize(px);
    font.setBold(bold);
    return font;
}

} // namespace

// Шкала одной оси: диапазон зоны (мин слева, макс справа) и метка текущего значения.
class AxisScale : public QWidget
{
public:
    AxisScale(const QString &name, const QString &unit, QWidget *parent)
        : QWidget(parent), m_name(name), m_unit(unit)
    {
        setMinimumHeight(130);
    }

    void set(bool known, double value, double lo, double hi)
    {
        if (known && !m_known)
            m_shown = value;
        // Дрожание позы стоящего робота меньше шага отображения (0.1) не перерисовываем.
        if (known == m_known && std::abs(value - m_value) < 0.01 && lo == m_lo && hi == m_hi)
            return;
        m_known = known;
        m_value = value;
        m_lo = lo;
        m_hi = hi;
        update();
    }

    // Метка догоняет m_value экспоненциально; число над шкалой всегда точное.
    void step(double dtSec)
    {
        if (!m_known || m_shown == m_value)
            return;
        const double span = m_hi > m_lo ? m_hi - m_lo : 1.0;
        m_shown += (m_value - m_shown) * (1.0 - std::exp(-dtSec / kSmoothSec));
        if (std::abs(m_value - m_shown) < span * 1e-4)
            m_shown = m_value;
        update();
    }

    bool inRange() const { return m_known && outside(m_value, m_lo, m_hi) == 0; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF area = rect().adjusted(18, 0, -18, 0);
        const bool ok = inRange();
        const QColor accent = !m_known ? kMuted : ok ? kOk : kBad;

        p.setFont(uiFont(30, true));
        p.setPen(kInk);
        p.drawText(QRectF(area.left(), 0, 60, 40), Qt::AlignLeft | Qt::AlignVCenter, m_name);

        QString hint;
        if (!m_known)
            hint = tx("нет позы");
        else if (ok)
            hint = tx("✓ в диапазоне");
        else if (m_value < m_lo)
            hint = tx("увеличить на %1").arg(m_lo - m_value, 0, 'f', 1);
        else
            hint = tx("уменьшить на %1").arg(m_value - m_hi, 0, 'f', 1);
        p.setFont(uiFont(24, true));
        p.setPen(accent);
        p.drawText(QRectF(area.left() + 60, 0, area.width() - 60, 40), Qt::AlignRight | Qt::AlignVCenter,
                   m_known ? tx("%1 %2   ·   %3").arg(m_value, 0, 'f', 1).arg(m_unit, hint) : hint);

        const double span = m_hi > m_lo ? m_hi - m_lo : 1.0;
        const double viewLo = m_lo - span * 0.6;
        const double viewHi = m_hi + span * 0.6;
        const auto xOf = [&](double v) {
            return area.left() + (v - viewLo) / (viewHi - viewLo) * area.width();
        };

        const qreal trackY = 66;
        const qreal trackH = 22;
        p.setPen(Qt::NoPen);
        p.setBrush(kTrack);
        p.drawRoundedRect(QRectF(area.left(), trackY, area.width(), trackH), trackH / 2, trackH / 2);
        const qreal bandL = xOf(m_lo);
        const qreal bandR = xOf(m_hi);
        p.setBrush(ok ? kOkBand : kBadBand);
        p.drawRect(QRectF(bandL, trackY, bandR - bandL, trackH));
        p.setPen(QPen(accent, 3));
        p.drawLine(QPointF(bandL, trackY - 6), QPointF(bandL, trackY + trackH + 6));
        p.drawLine(QPointF(bandR, trackY - 6), QPointF(bandR, trackY + trackH + 6));

        p.setFont(uiFont(20, false));
        p.setPen(kMuted);
        p.drawText(QRectF(bandL - 100, trackY + trackH + 8, 200, 28), Qt::AlignCenter,
                   QString::number(m_lo, 'f', 1));
        p.drawText(QRectF(bandR - 100, trackY + trackH + 8, 200, 28), Qt::AlignCenter,
                   QString::number(m_hi, 'f', 1));

        if (!m_known)
            return;
        // За пределами шкалы метка прижимается к краю и становится стрелкой наружу.
        const bool offLeft = m_shown < viewLo;
        const bool offRight = m_shown > viewHi;
        const qreal x = std::clamp(xOf(m_shown), area.left(), area.right());
        const qreal midY = trackY + trackH / 2;
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        QPainterPath marker;
        if (offLeft) {
            marker.moveTo(x - 12, midY);
            marker.lineTo(x + 12, trackY - 8);
            marker.lineTo(x + 12, trackY + trackH + 8);
        } else if (offRight) {
            marker.moveTo(x + 12, midY);
            marker.lineTo(x - 12, trackY - 8);
            marker.lineTo(x - 12, trackY + trackH + 8);
        } else {
            marker.moveTo(x, trackY + 2);
            marker.lineTo(x - 13, trackY - 18);
            marker.lineTo(x + 13, trackY - 18);
            p.drawRect(QRectF(x - 2, trackY, 4, trackH));
        }
        marker.closeSubpath();
        p.drawPath(marker);
    }

private:
    static constexpr double kSmoothSec = 0.08;

    QString m_name;
    QString m_unit;
    bool m_known = false;
    double m_value = 0;
    double m_shown = 0;
    double m_lo = 0;
    double m_hi = 0;
};

// Картинка стартового положения, вписанная в доступное место с сохранением пропорций.
class PlacementImage : public QWidget
{
public:
    PlacementImage(const QString &path, QWidget *parent) : QWidget(parent), m_pixmap(path) {}

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setPen(kPanelBorder);
        p.setBrush(kPanel);
        p.drawRoundedRect(frame, 10, 10);
        if (m_pixmap.isNull()) {
            p.setFont(uiFont(20, false));
            p.setPen(kMuted);
            p.drawText(rect().adjusted(40, 0, -40, 0), Qt::AlignCenter | Qt::TextWordWrap,
                       tx("Картинка стартового положения не задана (Админ-режим → «Стартовые зоны робота…»)"));
            return;
        }
        const QSize fitted = m_pixmap.size().scaled(rect().adjusted(12, 12, -12, -12).size(), Qt::KeepAspectRatio);
        QRect target(QPoint(0, 0), fitted);
        target.moveCenter(rect().center());
        p.drawPixmap(target, m_pixmap);
    }

private:
    QPixmap m_pixmap;
};

PlaceOverlay::PlaceOverlay(const QString &imagePath, QWidget *parent)
    : OverlayCard(tx("Робот не в стартовой зоне"), QSize(), parent)
{
    setSubtitle(tx("Программу нельзя запустить, пока робот не вернётся в стартовую зону"));
    setStatus(tx("Выставьте робота руками, как на картинке справа: каждая ось должна попасть в свой диапазон. "
                 "Окно закроется само."));

    auto *body = new QHBoxLayout;
    body->setSpacing(36);

    auto *scales = new QVBoxLayout;
    scales->setSpacing(12);
    m_target = new QLabel(m_card);
    m_target->setWordWrap(true);
    m_target->setStyleSheet(QStringLiteral(
        "color:#1F2328; font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:24px; font-weight:700;"));
    scales->addWidget(m_target);
    scales->addSpacing(12);
    const QString names[4] = {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z"), QStringLiteral("R")};
    const QString units[4] = {tx("мм"), tx("мм"), tx("мм"), tx("°")};
    for (int axis = 0; axis < 4; ++axis) {
        m_axes[axis] = new AxisScale(names[axis], units[axis], m_card);
        scales->addWidget(m_axes[axis], 1);
    }
    scales->addStretch(0);
    body->addLayout(scales, 1);

    body->addWidget(new PlacementImage(imagePath, m_card), 1);
    m_content->addLayout(body, 1);

    m_animClock.start();
    m_animTimer = new QTimer(this);
    m_animTimer->setTimerType(Qt::PreciseTimer);
    connect(m_animTimer, &QTimer::timeout, this, [this]() {
        const double dt = m_animClock.restart() / 1000.0;
        if (m_poseDirty) {
            m_poseDirty = false;
            apply();
        }
        for (AxisScale *axis : m_axes)
            axis->step(dt);
    });
    m_animTimer->start(kAnimIntervalMs);

    apply();
}

void PlaceOverlay::refresh(int zone, bool poseKnown, const std::array<double, 4> &pose,
                           const std::array<StartZone, ConnectionSettings::kZoneCount> &zones)
{
    m_zone = zone;
    m_poseKnown = poseKnown;
    m_pose = pose;
    m_zones = zones;
    apply();
}

void PlaceOverlay::setPose(double x, double y, double z, double r)
{
    m_poseKnown = true;
    m_pose = {x, y, z, r};
    m_poseDirty = true;
}

void PlaceOverlay::apply()
{
    const int target = m_zone > 0 ? m_zone - 1 : nearestZone(m_pose, m_zones);
    for (AxisScale *axis : m_axes)
        axis->setVisible(target >= 0);
    QString text;
    if (target < 0) {
        text = tx("Стартовые зоны не заданы (Админ-режим → «Стартовые зоны робота…»)");
    } else {
        int hit = 0;
        for (int axis = 0; axis < 4; ++axis) {
            const auto [lo, hi] = boundsOf(m_zones[target], axis);
            m_axes[axis]->set(m_poseKnown, m_pose[axis], lo, hi);
            if (m_axes[axis]->inRange())
                ++hit;
        }
        text = m_poseKnown ? tx("Стартовая зона %1 · в диапазоне %2 из 4").arg(target + 1).arg(hit)
                           : tx("Стартовая зона %1 · поза робота неизвестна").arg(target + 1);
    }
    if (m_target->text() != text)
        m_target->setText(text);
}
