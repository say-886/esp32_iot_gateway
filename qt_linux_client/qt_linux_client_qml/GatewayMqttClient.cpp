#include "GatewayMqttClient.h"

#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSet>
#include <QTimer>

#ifdef HAVE_QT_MQTT
#include <QMqttTopicFilter>
#include <QMqttTopicName>
#include <QSslConfiguration>
#endif

namespace {

QString sanitizeClientId(const QString &clientId)
{
    QString value = clientId.trimmed();
    if (value.isEmpty()) {
        value = QStringLiteral("esp32_qt_client");
    }

    value.replace(QChar(' '), QChar('_'));
    return value.left(48);
}

#ifdef HAVE_QT_MQTT
QString mqttErrorText(QMqttClient::ClientError error)
{
    switch (error) {
    case QMqttClient::NoError:
        return QStringLiteral("无错误");
    case QMqttClient::InvalidProtocolVersion:
        return QStringLiteral("MQTT 协议版本无效");
    case QMqttClient::IdRejected:
        return QStringLiteral("客户端 ID 被 broker 拒绝");
    case QMqttClient::ServerUnavailable:
        return QStringLiteral("broker 不可用");
    case QMqttClient::BadUsernameOrPassword:
        return QStringLiteral("MQTT 用户名或密码错误");
    case QMqttClient::NotAuthorized:
        return QStringLiteral("MQTT 连接未授权");
    case QMqttClient::TransportInvalid:
        return QStringLiteral("MQTT 传输层不可用");
    case QMqttClient::ProtocolViolation:
        return QStringLiteral("MQTT 协议错误");
    case QMqttClient::UnknownError:
    default:
        return QStringLiteral("未知 MQTT 错误");
    }
}
#endif

} // namespace

GatewayMqttClient::GatewayMqttClient(QObject *parent)
    : QObject(parent),
      m_port(1883),
      m_useTls(false),
      m_statusText(QStringLiteral("等待 MQTT 参数")),
      m_shouldBeConnected(false),
      m_missingModuleLogged(false),
      m_subscriptionsDirty(true),
      m_reconnectAttempt(0)
{
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this]() { reconnectIfNeeded(); });

#ifdef HAVE_QT_MQTT
    m_client.setProtocolVersion(QMqttClient::MQTT_3_1_1);
    m_client.setKeepAlive(30);
    m_client.setCleanSession(false);

    connect(&m_client, &QMqttClient::connected, this, [this]() {
        resetReconnectBackoff();
        setStatusText(QStringLiteral("已连接"));
        emit logMessage(QStringLiteral("MQTT 已连接到 %1:%2").arg(m_host).arg(m_port));
        subscribeToTopics();
    });

    connect(&m_client, &QMqttClient::disconnected, this, [this]() {
        if (m_shouldBeConnected) {
            scheduleReconnect();
            return;
        }

        setStatusText(QStringLiteral("未连接"));
        emit logMessage(QStringLiteral("MQTT 已断开"));
    });

    connect(&m_client, &QMqttClient::stateChanged, this, [this](QMqttClient::ClientState state) {
        switch (state) {
        case QMqttClient::Disconnected:
            if (!m_shouldBeConnected) {
                setStatusText(QStringLiteral("未连接"));
            }
            break;
        case QMqttClient::Connecting:
            setStatusText(QStringLiteral("正在连接"));
            break;
        case QMqttClient::Connected:
            setStatusText(QStringLiteral("已连接"));
            break;
        }
    });

    connect(&m_client,
            &QMqttClient::errorChanged,
            this,
            [this](QMqttClient::ClientError error) {
                if (error == QMqttClient::NoError) {
                    return;
                }

                const QString text = mqttErrorText(error);
                setStatusText(QStringLiteral("错误: %1").arg(text));
                emit logMessage(QStringLiteral("MQTT 错误: %1").arg(text));
            });

    connect(&m_client,
            &QMqttClient::messageReceived,
            this,
            [this](const QByteArray &message, const QMqttTopicName &topic) {
                emit payloadReceived(topic.name(), message);
            });
#else
    setStatusText(QStringLiteral("当前构建未启用 Qt MQTT"));
#endif
}

bool GatewayMqttClient::compiledWithMqtt() const
{
#ifdef HAVE_QT_MQTT
    return true;
#else
    return false;
#endif
}

bool GatewayMqttClient::isConnected() const
{
#ifdef HAVE_QT_MQTT
    return m_client.state() == QMqttClient::Connected;
#else
    return false;
#endif
}

bool GatewayMqttClient::canPublishControl() const
{
    return isConnected() && !m_commandTopic.isEmpty();
}

QString GatewayMqttClient::statusText() const
{
    return m_statusText;
}

void GatewayMqttClient::configure(const QString &host,
                                  int port,
                                  bool useTls,
                                  const QString &clientId,
                                  const QStringList &subscribeTopics,
                                  const QString &commandTopic)
{
    const QString trimmedHost = host.trimmed();

    QStringList cleanedTopics;
    cleanedTopics.reserve(subscribeTopics.size());
    QSet<QString> dedupe;
    for (const QString &topic : subscribeTopics) {
        const QString trimmedTopic = topic.trimmed();
        if (trimmedTopic.isEmpty() || dedupe.contains(trimmedTopic)) {
            continue;
        }

        dedupe.insert(trimmedTopic);
        cleanedTopics.append(trimmedTopic);
    }

    const bool configChanged = m_host != trimmedHost || m_port != port || m_useTls != useTls ||
                               m_clientId != sanitizeClientId(clientId) ||
                               m_subscribeTopics != cleanedTopics ||
                               m_commandTopic != commandTopic.trimmed();

    m_host = trimmedHost;
    m_port = port > 0 ? port : (useTls ? 8883 : 1883);
    m_useTls = useTls;
    m_clientId = sanitizeClientId(clientId);
    m_subscribeTopics = cleanedTopics;
    m_commandTopic = commandTopic.trimmed();
    m_subscriptionsDirty = true;
    if (configChanged) {
        resetReconnectBackoff();
    }

    const bool canConnect = !m_host.isEmpty() && !m_subscribeTopics.isEmpty();
    m_shouldBeConnected = canConnect;

#ifndef HAVE_QT_MQTT
    Q_UNUSED(configChanged)
    if (!m_missingModuleLogged && canConnect) {
        m_missingModuleLogged = true;
        emit logMessage(QStringLiteral("当前构建未启用 Qt MQTT，已跳过 MQTT 实时订阅"));
    }

    setStatusText(QStringLiteral("当前构建未启用 Qt MQTT"));
    return;
#else
    if (!canConnect) {
        if (m_client.state() != QMqttClient::Disconnected) {
            m_client.disconnectFromHost();
        }
        setStatusText(QStringLiteral("等待 MQTT 参数"));
        return;
    }

    if (m_client.state() == QMqttClient::Connected && !configChanged) {
        if (m_subscriptionsDirty) {
            subscribeToTopics();
        }
        return;
    }

    if (m_client.state() != QMqttClient::Disconnected) {
        setStatusText(QStringLiteral("正在重新连接"));
        m_client.disconnectFromHost();
        return;
    }

    reconnectIfNeeded();
#endif
}

void GatewayMqttClient::disconnectFromBroker()
{
    m_shouldBeConnected = false;
    resetReconnectBackoff();

#ifdef HAVE_QT_MQTT
    if (m_client.state() != QMqttClient::Disconnected) {
        m_client.disconnectFromHost();
    }
#endif

    setStatusText(compiledWithMqtt() ? QStringLiteral("未连接")
                                     : QStringLiteral("当前构建未启用 Qt MQTT"));
}

bool GatewayMqttClient::publishControl(const QJsonObject &command)
{
    if (!canPublishControl()) {
        emit logMessage(QStringLiteral("MQTT 控制发送失败: 命令通道尚未就绪"));
        return false;
    }

#ifndef HAVE_QT_MQTT
    Q_UNUSED(command)
    emit logMessage(QStringLiteral("MQTT 控制发送失败: 当前构建未启用 Qt MQTT"));
    return false;
#else
    const QByteArray payload = QJsonDocument(command).toJson(QJsonDocument::Compact);
    const qint32 messageId =
        m_client.publish(QMqttTopicName(m_commandTopic), payload, 1, false);
    if (messageId < 0) {
        emit logMessage(QStringLiteral("MQTT 控制发送失败: broker 未接受消息"));
        return false;
    }

    QJsonObject redactedCommand = command;
    if (redactedCommand.contains(QStringLiteral("auth"))) {
        redactedCommand.insert(QStringLiteral("auth"), QStringLiteral("***"));
    }
    const QByteArray redactedPayload =
        QJsonDocument(redactedCommand).toJson(QJsonDocument::Compact);
    emit logMessage(QStringLiteral("MQTT 控制已发布到 %1: %2")
                        .arg(m_commandTopic, QString::fromUtf8(redactedPayload)));
    return true;
#endif
}

void GatewayMqttClient::reconnectIfNeeded()
{
#ifdef HAVE_QT_MQTT
    if (!m_shouldBeConnected || m_host.isEmpty() || m_subscribeTopics.isEmpty()) {
        return;
    }

    if (m_client.state() == QMqttClient::Connecting ||
        m_client.state() == QMqttClient::Connected) {
        return;
    }

    m_client.setHostname(m_host);
    m_client.setPort(m_port);
    m_client.setClientId(m_clientId);

    setStatusText(QStringLiteral("正在连接"));

    if (m_useTls) {
        m_client.connectToHostEncrypted(QSslConfiguration::defaultConfiguration());
    } else {
        m_client.connectToHost();
    }
#endif
}

/**
 * @brief 使用指数退避和随机抖动安排下一次 MQTT 重连。
 *
 * 延迟序列约为 1、2、4、8、16、30 秒，抖动用于避免多客户端同时重连形成峰值。
 */
void GatewayMqttClient::scheduleReconnect()
{
#ifdef HAVE_QT_MQTT
    if (!m_shouldBeConnected || m_reconnectTimer.isActive()) {
        return;
    }

    const int shift = qMin(m_reconnectAttempt, 5);
    const int baseDelayMs = qMin(30000, 1000 * (1 << shift));
    const int jitterRange = baseDelayMs / 5;
    const int jitter = jitterRange > 0
                           ? QRandomGenerator::global()->bounded(jitterRange * 2 + 1) - jitterRange
                           : 0;
    const int delayMs = qMax(500, baseDelayMs + jitter);
    ++m_reconnectAttempt;
    setStatusText(QStringLiteral("已断开，%1 秒后重连").arg(delayMs / 1000.0, 0, 'f', 1));
    emit logMessage(QStringLiteral("MQTT 已断开，%1 ms 后执行第 %2 次重连")
                        .arg(delayMs)
                        .arg(m_reconnectAttempt));
    m_reconnectTimer.start(delayMs);
#endif
}

/** @brief 清除重连计数和尚未触发的定时器。 */
void GatewayMqttClient::resetReconnectBackoff()
{
    m_reconnectTimer.stop();
    m_reconnectAttempt = 0;
}

void GatewayMqttClient::subscribeToTopics()
{
#ifdef HAVE_QT_MQTT
    if (m_client.state() != QMqttClient::Connected) {
        return;
    }

    for (const QString &topic : m_subscribeTopics) {
        if (topic.isEmpty()) {
            continue;
        }

        if (m_client.subscribe(QMqttTopicFilter(topic), 1) == nullptr) {
            emit logMessage(QStringLiteral("MQTT 订阅失败: %1").arg(topic));
        } else {
            emit logMessage(QStringLiteral("MQTT 已订阅: %1").arg(topic));
        }
    }

    m_subscriptionsDirty = false;
#endif
}

void GatewayMqttClient::setStatusText(const QString &text)
{
    if (m_statusText == text) {
        return;
    }

    m_statusText = text;
    emit connectionStateChanged();
}
