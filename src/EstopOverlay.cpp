#include "EstopOverlay.h"
#include "ManualCheck.h"
#include "Texts.h"

#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

const char *kItemNames[] = {
    "Выключите вакуум",
    "Убедитесь, что всё нормально",
};

const char *kWeldOff =
    "QPushButton{background:#1D4F9E; color:white; border:none; border-radius:8px;"
    " font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:18px; font-weight:700;"
    " padding:16px 18px; text-align:left;}"
    " QPushButton:pressed{background:#163E7C;}";

const char *kWeldOn =
    "QPushButton{background:#2E8C47; color:white; border:none; border-radius:8px;"
    " font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:18px; font-weight:700;"
    " padding:16px 18px; text-align:left;}"
    " QPushButton:pressed{background:#246F38;}";

const char *kConfirmStyle =
    "QMessageBox, QDialog { background:#FFFFFF; }"
    " QLabel{color:#1F2126; background:transparent;"
    " font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:15px;}"
    " QPushButton{background:#FFFFFF; color:#1F2126; border:1px solid #DBDEE3; border-radius:6px;"
    " padding:6px 14px; min-height:32px; min-width:72px;"
    " font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:13px; font-weight:600;}"
    " QPushButton:pressed{background:#F0F2F5;}"
    " QPushButton:default{background:#1D4F9E; color:white; border:none;}";

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

    auto *vacuum = new ManualCheck(tx(kItemNames[0]), 18, m_card);
    m_content->addWidget(vacuum);
    m_items[0] = vacuum;
    connect(vacuum, &QPushButton::clicked, this, [this, vacuum]() {
        vacuum->setDone(!vacuum->isDone());
        updateItems();
    });

    m_btnWeldSafe = new QPushButton(m_card);
    m_btnWeldSafe->setCursor(Qt::PointingHandCursor);
    m_btnWeldSafe->setFocusPolicy(Qt::NoFocus);
    m_btnWeldSafe->setMinimumHeight(68);
    m_content->addWidget(m_btnWeldSafe);
    restyleWeldButton();
    connect(m_btnWeldSafe, &QPushButton::clicked, this, &EstopOverlay::onWeldSafeClicked);

    auto *allOk = new ManualCheck(tx(kItemNames[1]), 18, m_card);
    m_content->addWidget(allOk);
    m_items[1] = allOk;
    connect(allOk, &QPushButton::clicked, this, [this, allOk]() {
        allOk->setDone(!allOk->isDone());
        updateItems();
    });

    m_content->addStretch();

    m_btnExit = addButton(tx("Выйти из E-STOP"), ButtonKind::Primary);
    m_actions->addStretch();
    connect(m_btnExit, &QPushButton::clicked, this, &EstopOverlay::exitRequested);

    setStatus(tx("Отожмите грибок и/или закройте двери, верните сварку в безопасное положение, "
                 "отметьте остальные пункты и нажмите «Выйти из E-STOP»"));
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

void EstopOverlay::restyleWeldButton()
{
    m_btnWeldSafe->setText(m_weldSafeSent
                               ? tx("Вернуть сварку в безопасное положение\nКоманда отправлена")
                               : tx("Вернуть сварку в безопасное положение"));
    m_btnWeldSafe->setStyleSheet(QLatin1String(m_weldSafeSent ? kWeldOn : kWeldOff));
}

void EstopOverlay::onWeldSafeClicked()
{
    QMessageBox confirm(window());
    confirm.setIcon(QMessageBox::Question);
    confirm.setWindowTitle(tx("Вернуть сварку в безопасное положение"));
    confirm.setText(tx("Уверены?"));
    confirm.setInformativeText(
        tx("На ПЛК уйдёт команда 3 6 0.\nСварка будет возвращена в безопасное положение."));
    auto *yes = confirm.addButton(tx("Да"), QMessageBox::AcceptRole);
    auto *no = confirm.addButton(tx("Нет"), QMessageBox::RejectRole);
    confirm.setDefaultButton(no);
    confirm.setStyleSheet(QLatin1String(kConfirmStyle));
    confirm.exec();
    if (confirm.clickedButton() != yes)
        return;

    m_weldSafeSent = true;
    restyleWeldButton();
    updateItems();
    emit weldSafeRequested();
}

void EstopOverlay::updateItems()
{
    bool all = m_mushroomReleased && m_weldSafeSent;
    for (ManualCheck *item : m_items)
        all = all && item->isDone();
    m_btnExit->setEnabled(all);
}
