#include "PlaceOverlay.h"
#include "Texts.h"

#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>

PlaceOverlay::PlaceOverlay(const QString &imagePath, QWidget *parent)
    : OverlayCard(tx("Выставьте робота вручную"), QSize(1080, 720), parent)
{
    setSubtitle(tx("Робот не в стартовой зоне и не может выехать сам"));
    setStatus(tx("Выставьте робота руками, как на картинке. Окно закроется само, когда робот будет в стартовой зоне"));

    auto *image = new QLabel(m_card);
    image->setAlignment(Qt::AlignCenter);
    image->setMinimumSize(640, 400);
    image->setStyleSheet(QStringLiteral(
        "background:#F3F4F6; border:1px solid #C5C9D0; border-radius:8px; color:#737880;"
        " font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:15px;"));
    const QPixmap pixmap(imagePath);
    if (pixmap.isNull())
        image->setText(tx("Картинка стартового положения не задана (Админ-режим → «Стартовые зоны робота…»)"));
    else
        image->setPixmap(pixmap.scaled(640, 400, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_content->addWidget(image, 1);

    m_zone = addItemRow();
    refresh(0);
}

void PlaceOverlay::refresh(int zone)
{
    const ItemState state = zone > 0 ? ItemState::Done : ItemState::Active;
    m_zone->setText(itemMark(state)
                    + (zone > 0 ? tx("Робот в стартовой зоне %1").arg(zone)
                                : tx("Робот не в стартовой зоне")));
    m_zone->setStyleSheet(itemStyle(state));
}
