#include "EstopOverlay.h"
#include "Texts.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

const char *kItemNames[] = {
    "Выключите вакуум",
    "Разожмите сварку руками",
};

QLabel *sectionTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setStyleSheet(QStringLiteral(
        "color:#737880; font-family:\"Inter\",\"Segoe UI\",sans-serif;"
        " font-size:14px; font-weight:600; background:transparent;"));
    return label;
}

} // namespace

EstopOverlay::EstopOverlay(QWidget *parent)
    : OverlayCard(tx("Авария"), QSize(1080, 760), parent)
{
    m_content->addWidget(sectionTitle(tx("Причины"), m_card));
    m_causes = addItemRow();
    m_zone = addItemRow();

    m_content->addWidget(sectionTitle(tx("Перед выходом"), m_card));
    for (int i = 0; i < kItemCount; ++i) {
        auto *item = new QPushButton(m_card);
        item->setFocusPolicy(Qt::NoFocus);
        item->setMinimumHeight(64);
        item->setCursor(Qt::PointingHandCursor);
        m_content->addWidget(item);
        m_items[i] = item;
        connect(item, &QPushButton::clicked, this, [this, i]() {
            m_done[i] = !m_done[i];
            updateItems();
        });
    }
    m_content->addStretch();

    m_btnExit = addButton(tx("Выйти из E-STOP"), ButtonKind::Primary);
    m_actions->addStretch();
    connect(m_btnExit, &QPushButton::clicked, this, &EstopOverlay::exitRequested);

    setStatus(tx("Отожмите грибок и/или закройте двери, выполните пункты и нажмите «Выйти из E-STOP»"));
    refresh({}, false, 0);
}

void EstopOverlay::refresh(const QStringList &causes, bool mushroomReleased, int zone)
{
    m_mushroomReleased = mushroomReleased;

    if (causes.isEmpty()) {
        m_causes->setText(itemMark(ItemState::Done) + tx("Причин в кадре ПЛК нет"));
        m_causes->setStyleSheet(itemStyle(ItemState::Done));
    } else {
        QStringList lines;
        for (const QString &cause : causes)
            lines << itemMark(ItemState::Error) + cause;
        m_causes->setText(lines.join(QLatin1Char('\n')));
        m_causes->setStyleSheet(itemStyle(ItemState::Error));
    }

    const ItemState zoneState = zone > 0 ? ItemState::Done : ItemState::Active;
    m_zone->setText(itemMark(zoneState)
                    + (zone > 0 ? tx("Робот в стартовой зоне %1").arg(zone)
                                : tx("Робот не в стартовой зоне · после выхода выставить вручную")));
    m_zone->setStyleSheet(itemStyle(zoneState));

    updateItems();
}

void EstopOverlay::updateItems()
{
    bool all = m_mushroomReleased;
    for (int i = 0; i < kItemCount; ++i) {
        const ItemState state = m_done[i] ? ItemState::Done : ItemState::Active;
        m_items[i]->setText(itemMark(state) + tx(kItemNames[i]));
        m_items[i]->setStyleSheet(QStringLiteral("QPushButton{%1 text-align:left;}").arg(itemStyle(state)));
        all = all && m_done[i];
    }
    m_btnExit->setEnabled(all);
}
