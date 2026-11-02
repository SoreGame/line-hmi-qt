#include "PowerOffOverlay.h"
#include "ManualCheck.h"
#include "Texts.h"

#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

const char *kCmdOff =
    "QPushButton{background:#1D4F9E; color:white; border:none; border-radius:8px;"
    " font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:18px; font-weight:700;"
    " padding:16px 18px; text-align:left;}"
    " QPushButton:pressed{background:#163E7C;}"
    " QPushButton:disabled{background:#A9B0B8; color:white;}";

const char *kCmdOn =
    "QPushButton{background:#2E8C47; color:white; border:none; border-radius:8px;"
    " font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:18px; font-weight:700;"
    " padding:16px 18px; text-align:left;}"
    " QPushButton:pressed{background:#246F38;}"
    " QPushButton:disabled{background:#2E8C47; color:white;}";

const char *kConfirmStyle =
    "QMessageBox, QDialog { background:#FFFFFF; }"
    " QLabel{color:#1F2126; background:transparent;"
    " font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:15px;}"
    " QPushButton{background:#FFFFFF; color:#1F2126; border:1px solid #DBDEE3; border-radius:6px;"
    " padding:6px 14px; min-height:32px; min-width:72px;"
    " font-family:\"Inter\",\"Segoe UI\",sans-serif; font-size:13px; font-weight:600;}"
    " QPushButton:pressed{background:#F0F2F5;}"
    " QPushButton:default{background:#1D4F9E; color:white; border:none;}";

} // namespace

PowerOffOverlay::PowerOffOverlay(QWidget *parent)
    : OverlayCard(tx("Выключение питания"), QSize(1080, 820), parent)
{
    setStatus(tx("Выполните пункты по порядку. Кнопки команд открываются после предыдущих шагов. "
                 "Отмены нет — завершите чек-лист кнопкой «Выключить»."));

    m_robotSafe = new ManualCheck(tx("Выведите робота в безопасное положение"), 18, m_card);
    m_content->addWidget(m_robotSafe);
    connect(m_robotSafe, &QPushButton::clicked, this, [this]() {
        m_robotSafe->setDone(!m_robotSafe->isDone());
        updateGates();
    });

    m_noObjects = new ManualCheck(tx("Нет посторонних предметов"), 18, m_card);
    m_content->addWidget(m_noObjects);
    connect(m_noObjects, &QPushButton::clicked, this, [this]() {
        m_noObjects->setDone(!m_noObjects->isDone());
        updateGates();
    });

    m_btnWeldSafe = new QPushButton(m_card);
    m_btnWeldSafe->setCursor(Qt::PointingHandCursor);
    m_btnWeldSafe->setFocusPolicy(Qt::NoFocus);
    m_btnWeldSafe->setMinimumHeight(68);
    m_content->addWidget(m_btnWeldSafe);
    connect(m_btnWeldSafe, &QPushButton::clicked, this, &PowerOffOverlay::onWeldSafeClicked);

    m_btnWelderOff = new QPushButton(m_card);
    m_btnWelderOff->setCursor(Qt::PointingHandCursor);
    m_btnWelderOff->setFocusPolicy(Qt::NoFocus);
    m_btnWelderOff->setMinimumHeight(68);
    m_content->addWidget(m_btnWelderOff);
    connect(m_btnWelderOff, &QPushButton::clicked, this, &PowerOffOverlay::onWelderOffClicked);

    m_airOff = new ManualCheck(tx("Отключите подачу воздуха"), 18, m_card);
    m_content->addWidget(m_airOff);
    connect(m_airOff, &QPushButton::clicked, this, [this]() {
        m_airOff->setDone(!m_airOff->isDone());
        updateGates();
    });

    m_content->addStretch();

    m_btnPowerOff = addButton(tx("Выключить"), ButtonKind::Stop);
    m_actions->addStretch();
    connect(m_btnPowerOff, &QPushButton::clicked, this, &PowerOffOverlay::powerOffConfirmed);

    updateGates();
}

void PowerOffOverlay::restyleCommandButton(QPushButton *btn, const QString &idle,
                                           const QString &done, bool sent, bool enabled)
{
    btn->setText(sent ? done : idle);
    btn->setEnabled(enabled || sent);
    btn->setStyleSheet(QLatin1String(sent ? kCmdOn : kCmdOff));
}

void PowerOffOverlay::updateGates()
{
    const bool prepDone = m_robotSafe->isDone() && m_noObjects->isDone();
    restyleCommandButton(m_btnWeldSafe,
                         tx("Вернуть сварку в безопасное положение"),
                         tx("Вернуть сварку в безопасное положение\nКоманда отправлена"),
                         m_weldSafeSent, prepDone && !m_weldSafeSent);
    restyleCommandButton(m_btnWelderOff,
                         tx("Выключить сварочный аппарат"),
                         tx("Выключить сварочный аппарат\nКоманда отправлена"),
                         m_welderOffSent, m_weldSafeSent && !m_welderOffSent);

    m_airOff->setEnabled(m_welderOffSent || m_airOff->isDone());
    if (!m_welderOffSent && m_airOff->isDone())
        m_airOff->setDone(false);

    const bool all = prepDone && m_weldSafeSent && m_welderOffSent && m_airOff->isDone();
    m_btnPowerOff->setEnabled(all);
}

void PowerOffOverlay::onWeldSafeClicked()
{
    if (m_weldSafeSent || !(m_robotSafe->isDone() && m_noObjects->isDone()))
        return;

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
    updateGates();
    emit weldSafeRequested();
}

void PowerOffOverlay::onWelderOffClicked()
{
    if (m_welderOffSent || !m_weldSafeSent)
        return;

    m_welderOffSent = true;
    updateGates();
    emit welderOffRequested();
}
