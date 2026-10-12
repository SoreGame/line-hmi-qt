#ifndef SERVICEPANEL_H
#define SERVICEPANEL_H

#include "PlcClient.h"

#include <QDialog>
#include <QVector>

class QCloseEvent;
class QFrame;
class QLabel;
class QPushButton;
class QShowEvent;
class QTableWidget;
class QWidget;

class ServicePanel : public QDialog
{
    Q_OBJECT

public:
    explicit ServicePanel(PlcClient *plc, QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void reject() override;

private slots:
    void onPlcStateChanged();
    void onServiceModeToggled(bool on);
    void onDoToggled(int n, bool on);

private:
    void leaveServiceIfNeeded();
    void refreshBufferTable();
    // DO шлются только в режиме 5 «Service»; в остальных кнопки неактивны и сброшены.
    void updateDoAvailability();
    bool plcInMode(PlcClient::Mode mode) const;
    void sendServiceMode(bool on);
    void sendDo(int n, bool on);

    static constexpr int kWidth = 1920;
    static constexpr int kHeight = 1080;
    static constexpr int kBufRows = 16;
    static constexpr int kBufCols = 5;
    static constexpr int kBufCellCount = kBufRows * kBufCols;
    static constexpr int kDoRows = 10;
    static constexpr int kDoCols = 5;
    static constexpr int kDoCount = kDoRows * kDoCols;

    PlcClient *m_plc = nullptr;
    QTableWidget *m_bufferTable = nullptr;
    QLabel *m_bufferStatus = nullptr;
    QPushButton *m_btnServiceMode = nullptr;
    QLabel *m_doTitle = nullptr;
    QVector<QPushButton *> m_doButtons;
    bool m_fullScreenApplied = false;
    bool m_serviceRequested = false;
    int m_layoutW = kWidth;
    int m_layoutH = kHeight;
};

#endif // SERVICEPANEL_H
