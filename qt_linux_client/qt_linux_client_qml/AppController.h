#ifndef APPCONTROLLER_H
#define APPCONTROLLER_H

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QTimer>
#include <QVariant>

#include "GatewayMqttClient.h"

class AppController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString connectionStatus READ connectionStatus NOTIFY connectionStatusChanged)
    Q_PROPERTY(QString dashboardTemperature READ dashboardTemperature NOTIFY dashboardDataChanged)
    Q_PROPERTY(QString dashboardHumidity READ dashboardHumidity NOTIFY dashboardDataChanged)
    Q_PROPERTY(QString dashboardLight READ dashboardLight NOTIFY dashboardDataChanged)
    Q_PROPERTY(QString dashboardWifi READ dashboardWifi NOTIFY dashboardDataChanged)
    Q_PROPERTY(QString dashboardMqtt READ dashboardMqtt NOTIFY dashboardDataChanged)
    Q_PROPERTY(QString dashboardState READ dashboardState NOTIFY dashboardDataChanged)
    Q_PROPERTY(QString dashboardUptime READ dashboardUptime NOTIFY dashboardDataChanged)
    Q_PROPERTY(QString dashboardFirmware READ dashboardFirmware NOTIFY dashboardDataChanged)
    Q_PROPERTY(QString dashboardDeviceId READ dashboardDeviceId NOTIFY dashboardDataChanged)
    Q_PROPERTY(QString dashboardError READ dashboardError NOTIFY dashboardDataChanged)
    Q_PROPERTY(QString dashboardLed READ dashboardLed NOTIFY dashboardDataChanged)
    Q_PROPERTY(QString dashboardBuzzer READ dashboardBuzzer NOTIFY dashboardDataChanged)
    Q_PROPERTY(QString dashboardRelay READ dashboardRelay NOTIFY dashboardDataChanged)
    Q_PROPERTY(QString modbusStatusText READ modbusStatusText NOTIFY modbusStatusChanged)
    Q_PROPERTY(bool controlsEnabled READ controlsEnabled NOTIFY controlsEnabledChanged)
    Q_PROPERTY(QVariantList telemetryPoints READ telemetryPoints NOTIFY telemetryPointsChanged)
    Q_PROPERTY(QString savedHost READ savedHost NOTIFY savedConnectionChanged)
    Q_PROPERTY(int savedPort READ savedPort NOTIFY savedConnectionChanged)
    Q_PROPERTY(QString savedToken READ savedToken NOTIFY savedConnectionChanged)
    Q_PROPERTY(QString mqttRuntimeStatus READ mqttRuntimeStatus NOTIFY mqttRuntimeStatusChanged)
    Q_PROPERTY(QString controlHintText READ controlHintText NOTIFY mqttRuntimeStatusChanged)

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    QString connectionStatus() const;
    QString dashboardTemperature() const;
    QString dashboardHumidity() const;
    QString dashboardLight() const;
    QString dashboardWifi() const;
    QString dashboardMqtt() const;
    QString dashboardState() const;
    QString dashboardUptime() const;
    QString dashboardFirmware() const;
    QString dashboardDeviceId() const;
    QString dashboardError() const;
    QString dashboardLed() const;
    QString dashboardBuzzer() const;
    QString dashboardRelay() const;
    QString modbusStatusText() const;
    bool controlsEnabled() const;
    QVariantList telemetryPoints() const;
    QString savedHost() const;
    int savedPort() const;
    QString savedToken() const;
    QString mqttRuntimeStatus() const;
    QString controlHintText() const;

    Q_INVOKABLE void connectToDevice(const QString &host, int port, const QString &token);
    Q_INVOKABLE void disconnectFromDevice();

    Q_INVOKABLE void toggleLed();
    Q_INVOKABLE void toggleBuzzer();
    Q_INVOKABLE void toggleRelay();

    Q_INVOKABLE void loadConfig();
    Q_INVOKABLE void saveConfig(const QVariantMap &configData);

    Q_INVOKABLE void startOta(const QString &otaUrl);
    Q_INVOKABLE void rebootDevice();
    Q_INVOKABLE void loadModbusStatus();

signals:
    void connectionStatusChanged();
    void dashboardDataChanged();
    void modbusStatusChanged();
    void controlsEnabledChanged();
    void telemetryPointsChanged();
    void savedConnectionChanged();
    void mqttRuntimeStatusChanged();
    void logMessage(const QString &message);

    void configDataLoaded(const QVariantMap &configData);

private:
    void requestStatus();
    void resetDashboard();
    void updateMqttRuntime(const QJsonObject &statusObject);
    void applyStatusPayload(const QJsonObject &obj, bool fromHttp);
    void applySensorPayload(const QJsonObject &obj);
    void applyHeartbeatPayload(const QJsonObject &obj);
    void applyErrorPayload(const QJsonObject &obj);
    void applyCommandAckPayload(const QJsonObject &obj);
    void applyCommonStateFields(const QJsonObject &obj);
    void updateDashboardNetworkTexts();
    void updateDashboardErrorText();
    void handleMqttPayload(const QString &topic, const QByteArray &payload);
    void handleMqttRuntimeChanged();
    void sendControlCommand(const QString &target, bool nextValue, const QString &label);
    void sendControlRequest(const QString &target, bool nextValue, const QString &label);

    void initializeDataStore();
    void loadTelemetryHistory();
    void appendTelemetryPoint(double temperature,
                              double humidity,
                              double light,
                              const QString &deviceId = QString(),
                              quint32 bootId = 0,
                              quint32 sequence = 0,
                              bool replayed = false);
    void saveConnectionSettings();
    void loadConnectionSettings();
    QVariantMap makeTelemetryPoint(const QString &timestamp,
                                   double temperature,
                                   double humidity,
                                   double light) const;
    bool shouldSkipTelemetryPoint(double temperature, double humidity, double light) const;

    QNetworkAccessManager m_networkManager;
    QTimer m_pollTimer;
    QSqlDatabase m_database;
    GatewayMqttClient m_mqttClient;

    QString m_host;
    int m_port;
    QString m_token;

    QString m_connectionStatus;
    QString m_dashboardTemperature;
    QString m_dashboardHumidity;
    QString m_dashboardLight;
    QString m_dashboardWifi;
    QString m_dashboardMqtt;
    QString m_dashboardState;
    QString m_dashboardUptime;
    QString m_dashboardFirmware;
    QString m_dashboardDeviceId;
    QString m_dashboardError;
    QString m_modbusStatusText;

    QVariantList m_telemetryPoints;
    QString m_savedHost;
    int m_savedPort;
    QString m_savedToken;
    QString m_databaseConnectionName;
    QString m_mqttStatusTopic;
    QString m_mqttSensorTopic;
    QString m_mqttHeartbeatTopic;
    QString m_mqttErrorTopic;
    QString m_mqttCmdTopic;
    QString m_mqttCmdAckTopic;
    QString m_mqttHost;
    int m_mqttPort;
    bool m_mqttUseTls;
    quint32 m_errorCode;
    quint32 m_errorFlags;
    quint64 m_uptimeSeconds;
    bool m_wifiConnected;
    bool m_deviceMqttOnline;
    QDateTime m_lastTelemetryRecordedAt;
    double m_lastTelemetryTemperature;
    double m_lastTelemetryHumidity;
    double m_lastTelemetryLight;
    bool m_hasTelemetrySample;

    bool m_ledOn;
    bool m_buzzerOn;
    bool m_relayOn;
};

#endif
