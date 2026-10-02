#ifndef ADMINPANEL_H
#define ADMINPANEL_H

#include "ConnectionSettings.h"
#include "NatsClient.h"

#include <QDialog>
#include <QFutureWatcher>
#include <QVector>

class ArduinoLink;
class PlcClient;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QTableWidget;
class QWidget;

class AdminPanel : public QDialog
{
    Q_OBJECT

public:
    explicit AdminPanel(const ConnectionSettings &initial, NatsClient *nats,
                        PlcClient *plc = nullptr, QWidget *parent = nullptr);
    ~AdminPanel() override;

    ConnectionSettings networkSettings() const;

    enum class AuthResult { Cancelled, Denied, Ok };

    /// Запрос пароля. Без сброса/смены — только проверка.
    static AuthResult authenticate(QWidget *parent);

signals:
    void applyRequested(const ConnectionSettings &settings);

private slots:
    void onApplyClicked();
    void onRefreshClicked();
    void onPointsLoaded();
    void onPointSaved();
    void onMotionPose(double x, double y, double z, double r, bool running);
    void onMotionPowerClicked();
    void onHmiAutostartToggled(bool enabled);
    void onRemoteAutostartToggled(bool enabled);
    void onShowCursorToggled(bool enabled);
    void onIgnoreLoadCellToggled(bool enabled);
    void onStepChanged(int index);
    void onPlcStateChanged();
    void onArduinoValue(qint32 value);
    void onArduinoLinkChanged(bool linked, const QString &message);
    void onArduinoSendThreshold();
    void onArduinoPortEdited();

private:
    struct PointsLoadResult {
        QString error;
        QVector<NatsClient::RobotPoint> points;
    };

    void setBusy(bool busy);
    void fillTable();
    void fitTablePanel();
    int pageCount() const;
    void clampPage();
    void goToPage(int page);
    void updatePager();
    void editPoint(const NatsClient::RobotPoint &point);
    void savePoint(const QString &kvKey, double x, double y, double z, double r);
    void sendMoveOffset(double x, double y, double z, double r);
    void updateMotionPowerButton();
    void refreshPlcBufferLog();
    void fitPlcLogPanel();
    void fitTogglesPanel();
    void fitArduinoPanel();
    void refreshArduinoPorts(const QString &preferred);
    void reconnectArduino();
    void updateArduinoKgDisplay();
    double currentStep() const;
    QPushButton *makeJogButton(const QString &text, QWidget *parent);
    void showEvent(QShowEvent *event) override;

    static constexpr const char *kPassword = "scara";
    static constexpr int kWidth = 1920;
    static constexpr int kHeight = 1080;
    static constexpr int kPageSize = 10;
    static constexpr int kPlcLogMaxLines = 120;

    NatsClient *m_nats = nullptr;
    PlcClient *m_plc = nullptr;
    ArduinoLink *m_arduino = nullptr;
    QLineEdit *m_natsHost = nullptr;
    QSpinBox *m_natsPort = nullptr;
    QLineEdit *m_plcHost = nullptr;
    QSpinBox *m_plcPort = nullptr;
    QPlainTextEdit *m_plcBufferLog = nullptr;
    QLabel *m_plcBufferStatus = nullptr;
    QFrame *m_plcLogPanel = nullptr;

    QLabel *m_tableStatus = nullptr;
    QPushButton *m_btnRefresh = nullptr;
    QFrame *m_tablePanel = nullptr;
    QTableWidget *m_table = nullptr;
    QWidget *m_pager = nullptr;
    QPushButton *m_btnPrevPage = nullptr;
    QPushButton *m_btnNextPage = nullptr;
    QLabel *m_pageLabel = nullptr;
    QVector<NatsClient::RobotPoint> m_points;
    int m_page = 0;

    QFrame *m_networkPanel = nullptr;
    QFrame *m_togglesPanel = nullptr;
    QFrame *m_arduinoPanel = nullptr;
    QFrame *m_jogPanel = nullptr;
    int m_leftColW = 420;
    int m_layoutW = kWidth;
    int m_layoutH = kHeight;
    bool m_fullScreenApplied = false;

    QLabel *m_arduinoValue = nullptr;
    QLabel *m_arduinoKg = nullptr;
    QLabel *m_arduinoStatus = nullptr;
    QComboBox *m_arduinoPort = nullptr;
    QSpinBox *m_arduinoThreshold = nullptr;
    QDoubleSpinBox *m_arduinoUnitsPerKg = nullptr;
    QPushButton *m_btnArduinoSend = nullptr;
    QPushButton *m_btnArduinoReconnect = nullptr;
    qint32 m_arduinoLastRaw = 0;
    bool m_arduinoHasValue = false;

    QFutureWatcher<PointsLoadResult> *m_loadWatcher = nullptr;
    QFutureWatcher<QString> *m_saveWatcher = nullptr;

    QPushButton *m_btnMotionPower = nullptr;
    QPushButton *m_btnHmiAutostart = nullptr;
    QPushButton *m_btnRemoteAutostart = nullptr;
    QPushButton *m_btnShowCursor = nullptr;
    QPushButton *m_btnIgnoreLoadCell = nullptr;
    QLabel *m_droX = nullptr;
    QLabel *m_droY = nullptr;
    QLabel *m_droZ = nullptr;
    QLabel *m_droR = nullptr;
    QSlider *m_stepSlider = nullptr;
    QLabel *m_stepValue = nullptr;
    QVector<QPushButton *> m_jogButtons;
    bool m_motionRunning = false;
    bool m_poseKnown = false;
    double m_poseX = 0;
    double m_poseY = 0;
    double m_poseZ = 0;
    double m_poseR = 0;
};

#endif // ADMINPANEL_H
