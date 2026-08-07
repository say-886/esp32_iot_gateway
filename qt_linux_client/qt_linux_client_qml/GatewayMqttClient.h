#ifndef GATEWAYMQTTCLIENT_H
#define GATEWAYMQTTCLIENT_H

#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QTimer>

#ifdef HAVE_QT_MQTT
#include <QMqttClient>
#endif

class GatewayMqttClient : public QObject
{
    Q_OBJECT

public:
    explicit GatewayMqttClient(QObject *parent = nullptr);

    bool compiledWithMqtt() const;
    bool isConnected() const;
    bool canPublishControl() const;
    QString statusText() const;

    void configure(const QString &host,
                   int port,
                   bool useTls,
                   const QString &clientId,
                   const QStringList &subscribeTopics,
                   const QString &commandTopic);
    void disconnectFromBroker();
    bool publishControl(const QJsonObject &command);

signals:
    void connectionStateChanged();
    void payloadReceived(const QString &topic, const QByteArray &payload);
    void logMessage(const QString &message);

private:
    void reconnectIfNeeded();
    void scheduleReconnect();
    void resetReconnectBackoff();
    void subscribeToTopics();
    void setStatusText(const QString &text);

    QString m_host;
    int m_port;
    bool m_useTls;
    QString m_clientId;
    QStringList m_subscribeTopics;
    QString m_commandTopic;
    QString m_statusText;
    bool m_shouldBeConnected;
    bool m_missingModuleLogged;
    bool m_subscriptionsDirty;
    int m_reconnectAttempt;
    QTimer m_reconnectTimer;

#ifdef HAVE_QT_MQTT
    QMqttClient m_client;
#endif
};

#endif
