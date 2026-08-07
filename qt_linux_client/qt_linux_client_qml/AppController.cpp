#include "AppController.h"

#include <cmath>

#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QUuid>

namespace {

constexpr int kPollIntervalMs = 2000;
constexpr int kRequestTimeoutMs = 5000;
constexpr int kHistoryLimit = 200;
constexpr int kHistoryRetentionDays = 7;
constexpr int kTelemetryDedupMs = 1200;

QString boolText(bool value, const QString &trueText, const QString &falseText)
{
    return value ? trueText : falseText;
}

bool jsonToBool(const QJsonValue &value)
{
    if (value.isBool()) {
        return value.toBool();
    }

    if (value.isDouble()) {
        return value.toInt() != 0;
    }

    const QString text = value.toString().trimmed().toLower();
    return text == QStringLiteral("1") || text == QStringLiteral("true") ||
           text == QStringLiteral("on") || text == QStringLiteral("online") ||
           text == QStringLiteral("connected");
}

void installTimeout(QNetworkReply *reply)
{
    QTimer::singleShot(kRequestTimeoutMs, reply, [reply]() {
        if (reply && !reply->isFinished()) {
            reply->setProperty("requestTimedOut", true);
            reply->abort();
        }
    });
}

QString errorText(QNetworkReply *reply, int httpStatus)
{
    if (reply->property("requestTimedOut").toBool()) {
        return QStringLiteral("请求超时(5 秒)");
    }

    if (httpStatus > 0) {
        return QStringLiteral("HTTP %1: %2").arg(httpStatus).arg(reply->errorString());
    }

    return reply->errorString();
}

QString formatUptime(quint64 seconds)
{
    const quint64 hours = seconds / 3600;
    const quint64 minutes = (seconds % 3600) / 60;
    const quint64 remainingSeconds = seconds % 60;
    return QStringLiteral("%1h %2m %3s")
        .arg(hours)
        .arg(minutes, 2, 10, QChar('0'))
        .arg(remainingSeconds, 2, 10, QChar('0'));
}

QString jsonStringOrDefault(const QJsonObject &obj, const QString &key, const QString &fallback)
{
    const QString value = obj.value(key).toString().trimmed();
    return value.isEmpty() ? fallback : value;
}

} // namespace

AppController::AppController(QObject *parent)
    : QObject(parent),
      m_database(),
      m_mqttClient(this),
      m_port(80),
      m_connectionStatus(QStringLiteral("未连接")),
      m_modbusStatusText(QStringLiteral("Modbus 状态将显示在这里。")),
      m_savedHost(QStringLiteral("10.135.247.46")),
      m_savedPort(80),
      m_mqttPort(1883),
      m_mqttUseTls(false),
      m_errorCode(0),
      m_errorFlags(0),
      m_uptimeSeconds(0),
      m_wifiConnected(false),
      m_deviceMqttOnline(false),
      m_lastTelemetryTemperature(0.0),
      m_lastTelemetryHumidity(0.0),
      m_lastTelemetryLight(0.0),
      m_hasTelemetrySample(false),
      m_ledOn(false),
      m_buzzerOn(false),
      m_relayOn(false)
{
    resetDashboard();

    connect(&m_mqttClient, &GatewayMqttClient::logMessage, this, &AppController::logMessage);
    connect(&m_mqttClient,
            &GatewayMqttClient::payloadReceived,
            this,
            [this](const QString &topic, const QByteArray &payload) {
                handleMqttPayload(topic, payload);
            });
    connect(&m_mqttClient,
            &GatewayMqttClient::connectionStateChanged,
            this,
            [this]() { handleMqttRuntimeChanged(); });

    m_pollTimer.setInterval(kPollIntervalMs);
    m_pollTimer.setSingleShot(false);
    connect(&m_pollTimer, &QTimer::timeout, this, [this]() {
        if (!m_host.isEmpty()) {
            requestStatus();
        }
    });

    loadConnectionSettings();
    initializeDataStore();
    loadTelemetryHistory();
    handleMqttRuntimeChanged();

    QTimer::singleShot(0, this, [this]() {
        emit logMessage(QStringLiteral("已加载 %1 条本地历史记录").arg(m_telemetryPoints.size()));
    });
}

AppController::~AppController()
{
    if (m_database.isValid()) {
        const QString connectionName = m_database.connectionName();
        m_database.close();
        m_database = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
    }
}

QString AppController::connectionStatus() const
{
    return m_connectionStatus;
}

QString AppController::dashboardTemperature() const
{
    return m_dashboardTemperature;
}

QString AppController::dashboardHumidity() const
{
    return m_dashboardHumidity;
}

QString AppController::dashboardLight() const
{
    return m_dashboardLight;
}

QString AppController::dashboardWifi() const
{
    return m_dashboardWifi;
}

QString AppController::dashboardMqtt() const
{
    return m_dashboardMqtt;
}

QString AppController::dashboardState() const
{
    return m_dashboardState;
}

QString AppController::dashboardUptime() const
{
    return m_dashboardUptime;
}

QString AppController::dashboardFirmware() const
{
    return m_dashboardFirmware;
}

QString AppController::dashboardDeviceId() const
{
    return m_dashboardDeviceId;
}

QString AppController::dashboardError() const
{
    return m_dashboardError;
}

QString AppController::dashboardLed() const
{
    return boolText(m_ledOn, QStringLiteral("开启"), QStringLiteral("关闭"));
}

QString AppController::dashboardBuzzer() const
{
    return boolText(m_buzzerOn, QStringLiteral("开启"), QStringLiteral("关闭"));
}

QString AppController::dashboardRelay() const
{
    return boolText(m_relayOn, QStringLiteral("开启"), QStringLiteral("关闭"));
}

QString AppController::modbusStatusText() const
{
    return m_modbusStatusText;
}

bool AppController::controlsEnabled() const
{
    return m_connectionStatus == QStringLiteral("已连接") || m_mqttClient.canPublishControl();
}

QVariantList AppController::telemetryPoints() const
{
    return m_telemetryPoints;
}

QString AppController::savedHost() const
{
    return m_savedHost;
}

int AppController::savedPort() const
{
    return m_savedPort;
}

QString AppController::savedToken() const
{
    return m_savedToken;
}

QString AppController::mqttRuntimeStatus() const
{
    return m_mqttClient.statusText();
}

QString AppController::controlHintText() const
{
    if (m_mqttClient.canPublishControl()) {
        return QStringLiteral("控制链路: MQTT 优先，失败回退 HTTP");
    }

    if (!m_mqttClient.compiledWithMqtt()) {
        return QStringLiteral("控制链路: HTTP（当前构建未启用 Qt MQTT）");
    }

    if (!m_mqttHost.isEmpty() && !m_mqttCmdTopic.isEmpty()) {
        return QStringLiteral("控制链路: HTTP（等待 MQTT 连接）");
    }

    return QStringLiteral("控制链路: HTTP");
}

void AppController::connectToDevice(const QString &host, int port, const QString &token)
{
    m_host = host.trimmed();
    m_port = port > 0 ? port : 80;
    m_token = token.trimmed();

    if (m_host.isEmpty()) {
        emit logMessage(QStringLiteral("设备 IP 不能为空"));
        return;
    }

    m_savedHost = m_host;
    m_savedPort = m_port;
    m_savedToken = m_token;
    saveConnectionSettings();
    emit savedConnectionChanged();

    m_connectionStatus = QStringLiteral("正在连接");
    emit connectionStatusChanged();
    emit controlsEnabledChanged();

    emit logMessage(QStringLiteral("开始请求 http://%1:%2/api/status").arg(m_host).arg(m_port));

    requestStatus();
    m_pollTimer.start();

    emit logMessage(QStringLiteral("已启动 2 秒轮询"));
}

void AppController::disconnectFromDevice()
{
    m_pollTimer.stop();
    m_mqttClient.disconnectFromBroker();

    m_host.clear();
    m_token.clear();
    m_port = 80;
    m_mqttHost.clear();
    m_mqttStatusTopic.clear();
    m_mqttSensorTopic.clear();
    m_mqttHeartbeatTopic.clear();
    m_mqttErrorTopic.clear();
    m_mqttCmdTopic.clear();
    m_mqttCmdAckTopic.clear();

    m_connectionStatus = QStringLiteral("未连接");
    m_modbusStatusText = QStringLiteral("已断开连接，暂无 Modbus 数据。");
    resetDashboard();

    emit connectionStatusChanged();
    emit controlsEnabledChanged();
    emit dashboardDataChanged();
    emit modbusStatusChanged();
    emit mqttRuntimeStatusChanged();
    emit logMessage(QStringLiteral("已断开连接"));
}

void AppController::toggleLed()
{
    sendControlCommand(QStringLiteral("led"), !m_ledOn, QStringLiteral("LED"));
}

void AppController::toggleBuzzer()
{
    sendControlCommand(QStringLiteral("buzzer"), !m_buzzerOn, QStringLiteral("蜂鸣器"));
}

void AppController::toggleRelay()
{
    sendControlCommand(QStringLiteral("relay"), !m_relayOn, QStringLiteral("继电器"));
}

void AppController::loadConfig()
{
    if (m_host.isEmpty()) {
        emit logMessage(QStringLiteral("配置读取失败: 尚未连接设备"));
        return;
    }

    if (m_token.isEmpty()) {
        emit logMessage(QStringLiteral("配置读取失败: API Token 不能为空"));
        return;
    }

    const QUrl url(QStringLiteral("http://%1:%2/api/config").arg(m_host).arg(m_port));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QString("Bearer %1").arg(m_token).toUtf8());

    QNetworkReply *reply = m_networkManager.get(request);
    installTimeout(reply);

    emit logMessage(QStringLiteral("开始读取设备配置"));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        const int httpStatus =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->error() != QNetworkReply::NoError) {
            emit logMessage(QStringLiteral("配置读取失败: %1").arg(errorText(reply, httpStatus)));
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            emit logMessage(QStringLiteral("配置读取失败: 返回内容不是有效 JSON 对象"));
            return;
        }

        const QVariantMap configData = doc.object().toVariantMap();
        emit configDataLoaded(configData);
        emit logMessage(QStringLiteral("配置读取成功"));
    });
}

void AppController::saveConfig(const QVariantMap &configData)
{
    if (m_host.isEmpty()) {
        emit logMessage(QStringLiteral("配置保存失败: 尚未连接设备"));
        return;
    }

    if (m_token.isEmpty()) {
        emit logMessage(QStringLiteral("配置保存失败: API Token 不能为空"));
        return;
    }

    const QUrl url(QStringLiteral("http://%1:%2/api/config").arg(m_host).arg(m_port));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QString("Bearer %1").arg(m_token).toUtf8());

    QJsonObject obj;
    const auto addString = [&obj](const QString &key, const QVariantMap &source) {
        const QString value = source.value(key).toString().trimmed();
        if (!value.isEmpty()) {
            obj.insert(key, value);
        }
    };
    const auto addInt = [&obj](const QString &key, const QVariantMap &source) {
        if (source.contains(key)) {
            obj.insert(key, source.value(key).toInt());
        }
    };
    const auto addBool = [&obj](const QString &key, const QVariantMap &source) {
        if (source.contains(key)) {
            obj.insert(key, source.value(key).toBool());
        }
    };

    addString(QStringLiteral("wifi_ssid"), configData);
    addString(QStringLiteral("wifi_password"), configData);
    addString(QStringLiteral("mqtt_host"), configData);
    addInt(QStringLiteral("mqtt_port"), configData);
    addBool(QStringLiteral("mqtt_use_tls"), configData);
    addString(QStringLiteral("mqtt_username"), configData);
    addString(QStringLiteral("mqtt_password"), configData);
    addString(QStringLiteral("device_id"), configData);
    addString(QStringLiteral("api_token"), configData);
    addInt(QStringLiteral("sample_period_ms"), configData);
    addBool(QStringLiteral("modbus_enabled"), configData);
    addInt(QStringLiteral("modbus_slave_addr"), configData);
    addInt(QStringLiteral("modbus_baud_rate"), configData);
    addInt(QStringLiteral("modbus_start_register"), configData);
    addInt(QStringLiteral("modbus_register_count"), configData);
    addInt(QStringLiteral("modbus_poll_period_ms"), configData);

    QNetworkReply *reply =
        m_networkManager.post(request, QJsonDocument(obj).toJson(QJsonDocument::Compact));
    installTimeout(reply);

    emit logMessage(QStringLiteral("开始保存设备配置"));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        const int httpStatus =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->error() != QNetworkReply::NoError) {
            emit logMessage(QStringLiteral("配置保存失败: %1").arg(errorText(reply, httpStatus)));
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            emit logMessage(QStringLiteral("配置保存失败: 返回内容不是有效 JSON 对象"));
            return;
        }

        const QJsonObject obj = doc.object();
        if (obj.value(QStringLiteral("ok")).toBool()) {
            emit logMessage(QStringLiteral("配置保存成功"));
            if (obj.value(QStringLiteral("restart_required")).toBool()) {
                emit logMessage(QStringLiteral("设备提示: 配置已保存，重启后生效"));
            }
            loadConfig();
            return;
        }

        emit logMessage(QStringLiteral("配置保存失败: 设备返回异常"));
    });
}

void AppController::startOta(const QString &otaUrl)
{
    if (m_host.isEmpty()) {
        emit logMessage(QStringLiteral("OTA 启动失败: 尚未连接设备"));
        return;
    }

    if (m_token.isEmpty()) {
        emit logMessage(QStringLiteral("OTA 启动失败: API Token 不能为空"));
        return;
    }

    const QString trimmedUrl = otaUrl.trimmed();
    if (trimmedUrl.isEmpty()) {
        emit logMessage(QStringLiteral("OTA 启动失败: 固件 URL 不能为空"));
        return;
    }

    if (!trimmedUrl.startsWith(QStringLiteral("https://"))) {
        emit logMessage(QStringLiteral("OTA 启动失败: 固件 URL 必须以 https:// 开头"));
        return;
    }

    const QUrl url(QStringLiteral("http://%1:%2/api/ota").arg(m_host).arg(m_port));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QString("Bearer %1").arg(m_token).toUtf8());

    QJsonObject obj;
    obj.insert(QStringLiteral("url"), trimmedUrl);

    QNetworkReply *reply =
        m_networkManager.post(request, QJsonDocument(obj).toJson(QJsonDocument::Compact));
    installTimeout(reply);

    emit logMessage(QStringLiteral("开始发送 OTA 请求: %1").arg(trimmedUrl));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        const int httpStatus =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const QString bodyText = QString::fromUtf8(body).trimmed();

        if (reply->error() != QNetworkReply::NoError) {
            if (!bodyText.isEmpty() && !reply->property("requestTimedOut").toBool()) {
                emit logMessage(QStringLiteral("OTA 启动失败: HTTP %1: %2")
                                    .arg(httpStatus)
                                    .arg(bodyText));
            } else {
                emit logMessage(QStringLiteral("OTA 启动失败: %1").arg(errorText(reply, httpStatus)));
            }
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject() &&
            doc.object().value(QStringLiteral("ok")).toBool() &&
            doc.object().value(QStringLiteral("ota_started")).toBool()) {
            emit logMessage(QStringLiteral("OTA 请求已提交，请保持设备供电稳定"));
            return;
        }

        emit logMessage(bodyText.isEmpty()
                            ? QStringLiteral("OTA 返回异常")
                            : QStringLiteral("OTA 返回异常: %1").arg(bodyText));
    });
}

void AppController::rebootDevice()
{
    if (m_host.isEmpty()) {
        emit logMessage(QStringLiteral("设备重启失败: 尚未连接设备"));
        return;
    }

    if (m_token.isEmpty()) {
        emit logMessage(QStringLiteral("设备重启失败: API Token 不能为空"));
        return;
    }

    const QUrl url(QStringLiteral("http://%1:%2/api/reboot").arg(m_host).arg(m_port));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QString("Bearer %1").arg(m_token).toUtf8());

    QNetworkReply *reply = m_networkManager.post(request, QByteArray());
    installTimeout(reply);

    emit logMessage(QStringLiteral("开始发送设备重启请求"));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        const int httpStatus =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const QString bodyText = QString::fromUtf8(body).trimmed();

        if (reply->error() != QNetworkReply::NoError) {
            if (!bodyText.isEmpty() && !reply->property("requestTimedOut").toBool()) {
                emit logMessage(QStringLiteral("设备重启失败: HTTP %1: %2")
                                    .arg(httpStatus)
                                    .arg(bodyText));
            } else {
                emit logMessage(QStringLiteral("设备重启失败: %1").arg(errorText(reply, httpStatus)));
            }
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject() &&
            doc.object().value(QStringLiteral("ok")).toBool() &&
            doc.object().value(QStringLiteral("rebooting")).toBool()) {
            m_connectionStatus = QStringLiteral("重启中");
            emit connectionStatusChanged();
            emit controlsEnabledChanged();
            emit logMessage(QStringLiteral("设备重启请求已提交，请等待设备重新上线"));
            return;
        }

        emit logMessage(bodyText.isEmpty()
                            ? QStringLiteral("设备重启返回异常")
                            : QStringLiteral("设备重启返回异常: %1").arg(bodyText));
    });
}

void AppController::loadModbusStatus()
{
    if (m_host.isEmpty()) {
        m_modbusStatusText = QStringLiteral("尚未连接设备。");
        emit modbusStatusChanged();
        emit logMessage(QStringLiteral("Modbus 状态读取失败: 尚未连接设备"));
        return;
    }

    if (m_token.isEmpty()) {
        m_modbusStatusText = QStringLiteral("请先输入 API Token，再读取 Modbus 状态。");
        emit modbusStatusChanged();
        emit logMessage(QStringLiteral("Modbus 状态读取失败: API Token 不能为空"));
        return;
    }

    const QUrl url(QStringLiteral("http://%1:%2/api/modbus").arg(m_host).arg(m_port));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QString("Bearer %1").arg(m_token).toUtf8());

    m_modbusStatusText = QStringLiteral("正在读取 Modbus 状态...");
    emit modbusStatusChanged();
    emit logMessage(QStringLiteral("开始读取 Modbus 状态"));

    QNetworkReply *reply = m_networkManager.get(request);
    installTimeout(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        const int httpStatus =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();

        if (reply->error() != QNetworkReply::NoError) {
            m_modbusStatusText = QStringLiteral("读取失败\n%1").arg(errorText(reply, httpStatus));
            emit modbusStatusChanged();
            emit logMessage(QStringLiteral("Modbus 状态读取失败: %1").arg(errorText(reply, httpStatus)));
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (!doc.isObject()) {
            m_modbusStatusText = QStringLiteral("返回内容不是有效 JSON 对象。");
            emit modbusStatusChanged();
            emit logMessage(QStringLiteral("Modbus 状态读取失败: 返回内容不是有效 JSON 对象"));
            return;
        }

        const QJsonObject obj = doc.object();
        const QJsonArray registers = obj.value(QStringLiteral("registers")).toArray();
        QStringList registerTexts;
        for (const QJsonValue &value : registers) {
            registerTexts.append(QString::number(value.toInt()));
        }

        const QString enabledText =
            boolText(obj.value(QStringLiteral("enabled")).toBool(),
                     QStringLiteral("启用"),
                     QStringLiteral("禁用"));
        const QString onlineText =
            boolText(obj.value(QStringLiteral("online")).toBool(),
                     QStringLiteral("在线"),
                     QStringLiteral("离线"));
        const QString registersText =
            registerTexts.isEmpty() ? QStringLiteral("--") : registerTexts.join(QStringLiteral(", "));
        const qint64 lastError =
            static_cast<qint64>(obj.value(QStringLiteral("last_error")).toDouble());

        m_modbusStatusText =
            QStringLiteral("启用状态: %1\n"
                           "在线状态: %2\n"
                           "从站地址: %3\n"
                           "起始寄存器: %4\n"
                           "寄存器数量: %5\n"
                           "成功次数: %6\n"
                           "失败次数: %7\n"
                           "连续失败次数: %8\n"
                           "最近错误码: %9\n"
                           "寄存器值: %10")
                .arg(enabledText)
                .arg(onlineText)
                .arg(obj.value(QStringLiteral("slave_addr")).toInt())
                .arg(obj.value(QStringLiteral("start_register")).toInt())
                .arg(obj.value(QStringLiteral("register_count")).toInt())
                .arg(static_cast<qulonglong>(obj.value(QStringLiteral("success_count")).toDouble()))
                .arg(static_cast<qulonglong>(obj.value(QStringLiteral("error_count")).toDouble()))
                .arg(static_cast<qulonglong>(obj.value(QStringLiteral("consecutive_failures")).toDouble()))
                .arg(lastError)
                .arg(registersText);

        emit modbusStatusChanged();
        emit logMessage(QStringLiteral("Modbus 状态读取成功"));
    });
}

void AppController::requestStatus()
{
    const QUrl url(QStringLiteral("http://%1:%2/api/status").arg(m_host).arg(m_port));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply *reply = m_networkManager.get(request);
    installTimeout(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        const int httpStatus =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->error() != QNetworkReply::NoError) {
            m_connectionStatus = QStringLiteral("连接失败");
            emit connectionStatusChanged();
            emit controlsEnabledChanged();

            if (!m_mqttClient.isConnected()) {
                resetDashboard();
                emit dashboardDataChanged();
                emit logMessage(QStringLiteral("状态请求失败: %1").arg(errorText(reply, httpStatus)));
            } else {
                emit logMessage(QStringLiteral("状态请求失败: %1，已保留 MQTT 实时数据")
                                    .arg(errorText(reply, httpStatus)));
            }
            return;
        }

        const QByteArray body = reply->readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (!doc.isObject()) {
            m_connectionStatus = QStringLiteral("数据异常");
            emit connectionStatusChanged();
            emit controlsEnabledChanged();

            if (!m_mqttClient.isConnected()) {
                resetDashboard();
                emit dashboardDataChanged();
                emit logMessage(QStringLiteral("状态读取失败: 返回内容不是有效 JSON 对象"));
            } else {
                emit logMessage(QStringLiteral("状态读取失败: 返回内容不是有效 JSON 对象，已保留 MQTT 实时数据"));
            }
            return;
        }

        m_connectionStatus = QStringLiteral("已连接");
        applyStatusPayload(doc.object(), true);

        emit connectionStatusChanged();
        emit controlsEnabledChanged();
        emit logMessage(QStringLiteral("状态读取成功"));
    });
}

void AppController::resetDashboard()
{
    m_dashboardTemperature = QStringLiteral("-- °C");
    m_dashboardHumidity = QStringLiteral("-- %");
    m_dashboardLight = QStringLiteral("-- Lux");
    m_dashboardState = QStringLiteral("--");
    m_dashboardUptime = QStringLiteral("--");
    m_dashboardFirmware = QStringLiteral("--");
    m_dashboardDeviceId = QStringLiteral("--");

    m_wifiConnected = false;
    m_deviceMqttOnline = false;
    m_errorCode = 0;
    m_errorFlags = 0;
    m_uptimeSeconds = 0;
    updateDashboardNetworkTexts();
    updateDashboardErrorText();

    m_ledOn = false;
    m_buzzerOn = false;
    m_relayOn = false;
}

void AppController::updateMqttRuntime(const QJsonObject &statusObject)
{
    m_mqttHost = statusObject.value(QStringLiteral("mqtt_host")).toString().trimmed();
    m_mqttUseTls = statusObject.value(QStringLiteral("mqtt_use_tls")).toBool(m_mqttUseTls);
    m_mqttPort = statusObject.value(QStringLiteral("mqtt_port")).toInt(m_mqttUseTls ? 8883 : 1883);
    m_mqttStatusTopic = statusObject.value(QStringLiteral("mqtt_status_topic")).toString().trimmed();
    m_mqttSensorTopic = statusObject.value(QStringLiteral("mqtt_sensor_topic")).toString().trimmed();
    m_mqttHeartbeatTopic =
        statusObject.value(QStringLiteral("mqtt_heartbeat_topic")).toString().trimmed();
    m_mqttErrorTopic = statusObject.value(QStringLiteral("mqtt_error_topic")).toString().trimmed();
    m_mqttCmdTopic = statusObject.value(QStringLiteral("mqtt_cmd_topic")).toString().trimmed();
    m_mqttCmdAckTopic =
        statusObject.value(QStringLiteral("mqtt_cmd_ack_topic")).toString().trimmed();

    QString clientId = statusObject.value(QStringLiteral("device_id")).toString().trimmed();
    if (clientId.isEmpty()) {
        clientId = m_dashboardDeviceId == QStringLiteral("--") ? m_savedHost : m_dashboardDeviceId;
    }
    clientId = QStringLiteral("%1_qt_client").arg(clientId);

    QStringList topics;
    if (!m_mqttStatusTopic.isEmpty()) {
        topics.append(m_mqttStatusTopic);
    }
    if (!m_mqttSensorTopic.isEmpty()) {
        topics.append(m_mqttSensorTopic);
    }
    if (!m_mqttHeartbeatTopic.isEmpty()) {
        topics.append(m_mqttHeartbeatTopic);
    }
    if (!m_mqttErrorTopic.isEmpty()) {
        topics.append(m_mqttErrorTopic);
    }
    if (!m_mqttCmdAckTopic.isEmpty()) {
        topics.append(m_mqttCmdAckTopic);
    }

    m_mqttClient.configure(m_mqttHost,
                           m_mqttPort,
                           m_mqttUseTls,
                           clientId,
                           topics,
                           m_mqttCmdTopic);

    handleMqttRuntimeChanged();
}

void AppController::applyStatusPayload(const QJsonObject &obj, bool fromHttp)
{
    const double temperature = obj.value(QStringLiteral("temperature")).toDouble();
    const double humidity = obj.value(QStringLiteral("humidity")).toDouble();
    const double light = obj.value(QStringLiteral("light")).toDouble();

    m_dashboardTemperature = QString::number(temperature, 'f', 1) + QStringLiteral(" °C");
    m_dashboardHumidity = QString::number(humidity, 'f', 1) + QStringLiteral(" %");
    m_dashboardLight = QString::number(light, 'f', 0) + QStringLiteral(" Lux");

    applyCommonStateFields(obj);
    appendTelemetryPoint(temperature, humidity, light);

    if (fromHttp) {
        updateMqttRuntime(obj);
    }

    emit dashboardDataChanged();
}

void AppController::applySensorPayload(const QJsonObject &obj)
{
    /* 新固件使用 data 嵌套结构；没有 data 时继续兼容旧版顶层字段。 */
    const QJsonObject data = obj.value(QStringLiteral("data")).isObject()
                                 ? obj.value(QStringLiteral("data")).toObject()
                                 : obj;
    const double temperature = data.value(QStringLiteral("temperature")).toDouble();
    const double humidity = data.value(QStringLiteral("humidity")).toDouble();
    const double light = data.value(QStringLiteral("light")).toDouble();

    m_dashboardTemperature = QString::number(temperature, 'f', 1) + QStringLiteral(" °C");
    m_dashboardHumidity = QString::number(humidity, 'f', 1) + QStringLiteral(" %");
    m_dashboardLight = QString::number(light, 'f', 0) + QStringLiteral(" Lux");

    appendTelemetryPoint(temperature,
                         humidity,
                         light,
                         obj.value(QStringLiteral("device_id")).toString(),
                         static_cast<quint32>(obj.value(QStringLiteral("boot_id")).toDouble()),
                         static_cast<quint32>(obj.value(QStringLiteral("seq")).toDouble()),
                         obj.value(QStringLiteral("replayed")).toBool());
    emit dashboardDataChanged();
}

void AppController::applyHeartbeatPayload(const QJsonObject &obj)
{
    applyCommonStateFields(obj);
    emit dashboardDataChanged();
}

void AppController::applyErrorPayload(const QJsonObject &obj)
{
    applyCommonStateFields(obj);
    emit dashboardDataChanged();

    if (m_errorCode != 0 || m_errorFlags != 0) {
        emit logMessage(QStringLiteral("MQTT 错误上报: error_code=%1, error_flags=%2")
                            .arg(m_errorCode)
                            .arg(m_errorFlags));
    }
}

void AppController::applyCommandAckPayload(const QJsonObject &obj)
{
    const QString cmdId = obj.value(QStringLiteral("cmd_id")).toString();
    const QString status = obj.value(QStringLiteral("status")).toString();
    const int code = obj.value(QStringLiteral("code")).toInt();
    const QJsonObject reported = obj.value(QStringLiteral("reported")).toObject();
    if (!reported.isEmpty()) {
        applyCommonStateFields(reported);
        emit dashboardDataChanged();
    }
    emit logMessage(QStringLiteral("MQTT 命令确认: cmd_id=%1 status=%2 code=%3")
                        .arg(cmdId, status)
                        .arg(code));
}

void AppController::applyCommonStateFields(const QJsonObject &obj)
{
    if (obj.contains(QStringLiteral("wifi"))) {
        m_wifiConnected = jsonToBool(obj.value(QStringLiteral("wifi")));
    }

    if (obj.contains(QStringLiteral("mqtt"))) {
        m_deviceMqttOnline = jsonToBool(obj.value(QStringLiteral("mqtt")));
    }

    if (obj.contains(QStringLiteral("state"))) {
        m_dashboardState = jsonStringOrDefault(obj, QStringLiteral("state"), QStringLiteral("--"));
    }

    if (obj.contains(QStringLiteral("uptime"))) {
        m_uptimeSeconds = static_cast<quint64>(obj.value(QStringLiteral("uptime")).toDouble());
        m_dashboardUptime = formatUptime(m_uptimeSeconds);
    }

    if (obj.contains(QStringLiteral("firmware"))) {
        m_dashboardFirmware =
            jsonStringOrDefault(obj, QStringLiteral("firmware"), QStringLiteral("--"));
    }

    if (obj.contains(QStringLiteral("device_id"))) {
        m_dashboardDeviceId =
            jsonStringOrDefault(obj, QStringLiteral("device_id"), QStringLiteral("--"));
    }

    if (obj.contains(QStringLiteral("error_code"))) {
        m_errorCode = static_cast<quint32>(obj.value(QStringLiteral("error_code")).toDouble());
    }

    if (obj.contains(QStringLiteral("error_flags"))) {
        m_errorFlags = static_cast<quint32>(obj.value(QStringLiteral("error_flags")).toDouble());
    }

    if (obj.contains(QStringLiteral("led"))) {
        m_ledOn = jsonToBool(obj.value(QStringLiteral("led")));
    }

    if (obj.contains(QStringLiteral("buzzer"))) {
        m_buzzerOn = jsonToBool(obj.value(QStringLiteral("buzzer")));
    }

    if (obj.contains(QStringLiteral("relay"))) {
        m_relayOn = jsonToBool(obj.value(QStringLiteral("relay")));
    }

    updateDashboardNetworkTexts();
    updateDashboardErrorText();
}

void AppController::updateDashboardNetworkTexts()
{
    m_dashboardWifi = boolText(m_wifiConnected, QStringLiteral("已连接"), QStringLiteral("未连接"));

    const QString brokerText =
        boolText(m_deviceMqttOnline, QStringLiteral("设备在线"), QStringLiteral("设备离线"));
    m_dashboardMqtt = QStringLiteral("%1 / 客户端:%2")
                          .arg(brokerText, m_mqttClient.statusText());
}

void AppController::updateDashboardErrorText()
{
    if (m_errorCode == 0 && m_errorFlags == 0) {
        m_dashboardError = QStringLiteral("正常");
        return;
    }

    m_dashboardError =
        QStringLiteral("码:%1 标志:%2").arg(m_errorCode).arg(m_errorFlags);
}

void AppController::handleMqttPayload(const QString &topic, const QByteArray &payload)
{
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject()) {
        emit logMessage(QStringLiteral("MQTT 消息解析失败: topic=%1 不是有效 JSON 对象").arg(topic));
        return;
    }

    const QJsonObject obj = doc.object();

    if (topic == m_mqttStatusTopic) {
        applyStatusPayload(obj, false);
        return;
    }

    if (topic == m_mqttSensorTopic) {
        applySensorPayload(obj);
        return;
    }

    if (topic == m_mqttHeartbeatTopic) {
        applyHeartbeatPayload(obj);
        return;
    }

    if (topic == m_mqttErrorTopic) {
        applyErrorPayload(obj);
        return;
    }

    if (topic == m_mqttCmdAckTopic) {
        applyCommandAckPayload(obj);
        return;
    }

    emit logMessage(QStringLiteral("收到未识别的 MQTT topic: %1").arg(topic));
}

void AppController::handleMqttRuntimeChanged()
{
    updateDashboardNetworkTexts();
    emit dashboardDataChanged();
    emit mqttRuntimeStatusChanged();
    emit controlsEnabledChanged();
}

void AppController::sendControlCommand(const QString &target, bool nextValue, const QString &label)
{
    const qint64 createdAt = QDateTime::currentMSecsSinceEpoch();
    QJsonObject control;
    control.insert(target, nextValue ? 1 : 0);
    QJsonObject command;
    command.insert(QStringLiteral("schema"), 1);
    command.insert(QStringLiteral("cmd_id"),
                   QUuid::createUuid().toString(QUuid::WithoutBraces));
    command.insert(QStringLiteral("type"), QStringLiteral("control"));
    command.insert(QStringLiteral("created_at"), static_cast<double>(createdAt));
    command.insert(QStringLiteral("expires_at"), static_cast<double>(createdAt + 30000));
    command.insert(QStringLiteral("payload"), control);

    if (m_mqttClient.canPublishControl()) {
        if (m_mqttClient.publishControl(command)) {
            if (m_connectionStatus == QStringLiteral("已连接")) {
                requestStatus();
            }
            return;
        }

        emit logMessage(QStringLiteral("%1 MQTT 控制失败，正在回退到 HTTP").arg(label));
    }

    sendControlRequest(target, nextValue, label);
}

void AppController::sendControlRequest(const QString &target, bool nextValue, const QString &label)
{
    if (m_host.isEmpty()) {
        emit logMessage(QStringLiteral("%1 控制失败: 尚未连接设备").arg(label));
        return;
    }

    if (m_token.isEmpty()) {
        emit logMessage(QStringLiteral("%1 控制失败: API Token 不能为空").arg(label));
        return;
    }

    const QUrl url(QStringLiteral("http://%1:%2/api/control").arg(m_host).arg(m_port));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QString("Bearer %1").arg(m_token).toUtf8());

    QJsonObject obj;
    obj.insert(target, nextValue ? 1 : 0);

    const QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = m_networkManager.post(request, body);
    installTimeout(reply);

    emit logMessage(QStringLiteral("发送 %1 控制请求: %2")
                        .arg(label)
                        .arg(nextValue ? QStringLiteral("开启") : QStringLiteral("关闭")));

    connect(reply, &QNetworkReply::finished, this, [this, reply, label]() {
        reply->deleteLater();

        const int httpStatus =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();

        if (reply->error() != QNetworkReply::NoError) {
            emit logMessage(QStringLiteral("%1 控制失败: %2").arg(label, errorText(reply, httpStatus)));
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject() && doc.object().value(QStringLiteral("ok")).toBool()) {
            emit logMessage(QStringLiteral("%1 控制成功").arg(label));
            requestStatus();
            return;
        }

        emit logMessage(QStringLiteral("%1 控制返回异常").arg(label));
    });
}

void AppController::initializeDataStore()
{
    const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir appDataDir(appDataPath);
    if (!appDataDir.exists() && !appDataDir.mkpath(QStringLiteral("."))) {
        QTimer::singleShot(0, this, [this, appDataPath]() {
            emit logMessage(QStringLiteral("本地历史库初始化失败: 无法创建目录 %1").arg(appDataPath));
        });
        return;
    }

    m_databaseConnectionName =
        QStringLiteral("telemetry_connection_%1").arg(reinterpret_cast<quintptr>(this), 0, 16);
    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_databaseConnectionName);
    m_database.setDatabaseName(appDataDir.filePath(QStringLiteral("telemetry.db")));

    if (!m_database.open()) {
        const QString message = m_database.lastError().text();
        QTimer::singleShot(0, this, [this, message]() {
            emit logMessage(QStringLiteral("本地历史库初始化失败: %1").arg(message));
        });
        return;
    }

    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS telemetry ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "timestamp TEXT NOT NULL,"
            "temperature REAL NOT NULL,"
            "humidity REAL NOT NULL,"
            "light REAL NOT NULL,"
            "device_id TEXT NOT NULL DEFAULT '',"
            "boot_id INTEGER NOT NULL DEFAULT 0,"
            "seq INTEGER NOT NULL DEFAULT 0,"
            "replayed INTEGER NOT NULL DEFAULT 0)"))) {
        const QString message = query.lastError().text();
        QTimer::singleShot(0, this, [this, message]() {
            emit logMessage(QStringLiteral("本地历史表创建失败: %1").arg(message));
        });
        return;
    }

    /* 对已有数据库执行向前兼容迁移，不删除用户的历史遥测。 */
    QSet<QString> columns;
    if (query.exec(QStringLiteral("PRAGMA table_info(telemetry)"))) {
        while (query.next()) {
            columns.insert(query.value(1).toString());
        }
    }
    const QList<QPair<QString, QString>> migrations = {
        {QStringLiteral("device_id"), QStringLiteral("TEXT NOT NULL DEFAULT ''")},
        {QStringLiteral("boot_id"), QStringLiteral("INTEGER NOT NULL DEFAULT 0")},
        {QStringLiteral("seq"), QStringLiteral("INTEGER NOT NULL DEFAULT 0")},
        {QStringLiteral("replayed"), QStringLiteral("INTEGER NOT NULL DEFAULT 0")}
    };
    for (const auto &migration : migrations) {
        if (columns.contains(migration.first)) {
            continue;
        }
        if (!query.exec(QStringLiteral("ALTER TABLE telemetry ADD COLUMN %1 %2")
                            .arg(migration.first, migration.second))) {
            emit logMessage(QStringLiteral("历史表迁移失败(%1): %2")
                                .arg(migration.first, query.lastError().text()));
        }
    }
    if (!query.exec(QStringLiteral(
            "CREATE UNIQUE INDEX IF NOT EXISTS idx_telemetry_identity "
            "ON telemetry(device_id, boot_id, seq) "
            "WHERE device_id <> '' AND seq > 0"))) {
        emit logMessage(QStringLiteral("遥测幂等索引创建失败: %1").arg(query.lastError().text()));
    }
}

void AppController::loadTelemetryHistory()
{
    m_telemetryPoints.clear();

    if (!m_database.isValid() || !m_database.isOpen()) {
        emit telemetryPointsChanged();
        return;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT timestamp, temperature, humidity, light "
        "FROM telemetry "
        "ORDER BY timestamp DESC "
        "LIMIT ?"));
    query.addBindValue(kHistoryLimit);

    if (!query.exec()) {
        emit logMessage(QStringLiteral("历史数据读取失败: %1").arg(query.lastError().text()));
        emit telemetryPointsChanged();
        return;
    }

    QVariantList points;
    while (query.next()) {
        points.prepend(makeTelemetryPoint(query.value(0).toString(),
                                          query.value(1).toDouble(),
                                          query.value(2).toDouble(),
                                          query.value(3).toDouble()));
    }

    m_telemetryPoints = points;
    emit telemetryPointsChanged();
}

void AppController::appendTelemetryPoint(double temperature,
                                         double humidity,
                                         double light,
                                         const QString &deviceId,
                                         quint32 bootId,
                                         quint32 sequence,
                                         bool replayed)
{
    const bool hasIdentity = !deviceId.trimmed().isEmpty() && sequence > 0;
    if (!hasIdentity && shouldSkipTelemetryPoint(temperature, humidity, light)) {
        return;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QString timestamp = now.toString(Qt::ISODateWithMs);
    const QVariantMap point = makeTelemetryPoint(timestamp, temperature, humidity, light);

    if (m_database.isValid() && m_database.isOpen()) {
        QSqlQuery insertQuery(m_database);
        insertQuery.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO telemetry "
            "(timestamp, temperature, humidity, light, device_id, boot_id, seq, replayed) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
        insertQuery.addBindValue(timestamp);
        insertQuery.addBindValue(temperature);
        insertQuery.addBindValue(humidity);
        insertQuery.addBindValue(light);
        insertQuery.addBindValue(deviceId.trimmed());
        insertQuery.addBindValue(bootId);
        insertQuery.addBindValue(sequence);
        insertQuery.addBindValue(replayed ? 1 : 0);

        if (!insertQuery.exec()) {
            emit logMessage(QStringLiteral("历史数据写入失败: %1").arg(insertQuery.lastError().text()));
        } else if (hasIdentity && insertQuery.numRowsAffected() == 0) {
            return;
        }

        QSqlQuery cleanupQuery(m_database);
        cleanupQuery.prepare(QStringLiteral("DELETE FROM telemetry WHERE timestamp < ?"));
        cleanupQuery.addBindValue(now.addDays(-kHistoryRetentionDays).toString(Qt::ISODateWithMs));
        if (!cleanupQuery.exec()) {
            emit logMessage(QStringLiteral("历史数据清理失败: %1").arg(cleanupQuery.lastError().text()));
        }
    }

    m_telemetryPoints.append(point);
    while (m_telemetryPoints.size() > kHistoryLimit) {
        m_telemetryPoints.removeFirst();
    }

    m_lastTelemetryRecordedAt = now;
    m_lastTelemetryTemperature = temperature;
    m_lastTelemetryHumidity = humidity;
    m_lastTelemetryLight = light;
    m_hasTelemetrySample = true;

    emit telemetryPointsChanged();
}

void AppController::saveConnectionSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("connection"));
    settings.setValue(QStringLiteral("host"), m_savedHost);
    settings.setValue(QStringLiteral("port"), m_savedPort);
    settings.setValue(QStringLiteral("token"), m_savedToken);
    settings.endGroup();
    settings.sync();
}

void AppController::loadConnectionSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("connection"));
    m_savedHost =
        settings.value(QStringLiteral("host"), QStringLiteral("10.135.247.46")).toString().trimmed();
    m_savedPort = settings.value(QStringLiteral("port"), 80).toInt();
    m_savedToken = settings.value(QStringLiteral("token"), QString()).toString();
    settings.endGroup();

    if (m_savedHost.isEmpty()) {
        m_savedHost = QStringLiteral("10.135.247.46");
    }

    if (m_savedPort <= 0 || m_savedPort > 65535) {
        m_savedPort = 80;
    }
}

QVariantMap AppController::makeTelemetryPoint(const QString &timestamp,
                                              double temperature,
                                              double humidity,
                                              double light) const
{
    QVariantMap point;
    point.insert(QStringLiteral("timestamp"), timestamp);
    point.insert(QStringLiteral("temperature"), temperature);
    point.insert(QStringLiteral("humidity"), humidity);
    point.insert(QStringLiteral("light"), light);
    return point;
}

bool AppController::shouldSkipTelemetryPoint(double temperature, double humidity, double light) const
{
    if (!m_hasTelemetrySample) {
        return false;
    }

    if (m_lastTelemetryRecordedAt.msecsTo(QDateTime::currentDateTimeUtc()) > kTelemetryDedupMs) {
        return false;
    }

    return std::abs(m_lastTelemetryTemperature - temperature) < 0.01 &&
           std::abs(m_lastTelemetryHumidity - humidity) < 0.01 &&
           std::abs(m_lastTelemetryLight - light) < 0.01;
}
