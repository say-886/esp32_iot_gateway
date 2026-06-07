# Security, OTA and Modbus Extension

## API authentication

`GET /api/status` remains public for the local dashboard. The following APIs
require an `Authorization: Bearer <token>` header:

- `POST /api/control`
- `GET/POST /api/config`
- `POST /api/reboot`
- `POST /api/ota`
- `GET /api/modbus`

Set a unique `api_token` before deployment. The default
`CHANGE_ME_BEFORE_DEPLOYMENT` value is only intended for initial bring-up.
`GET /api/config` never returns WiFi passwords, MQTT passwords, or the API
token.

Example:

```bash
curl -H "Authorization: Bearer YOUR_TOKEN" http://DEVICE_IP/api/modbus
```

## MQTTS

Set `mqtt_use_tls` to `true`, use the broker TLS port (commonly `8883`), and
provide `mqtt_username` and `mqtt_password` when the broker requires them.
Server certificates are verified with the ESP-IDF certificate bundle.

## OTA rollback

OTA accepts HTTPS URLs only. Before writing the new image, the service reads
its application descriptor and rejects an image with the same version as the
running firmware. It also verifies that the complete image was received.

Rollback support is enabled in `sdkconfig.defaults`. A newly installed image
is confirmed only after the application initializes its main services and
tasks. If the device resets before confirmation, the bootloader can roll back
to the previous image.

## RS485 / Modbus RTU

The initial extension implements a Modbus RTU master polling function for
holding registers (`0x03`). It is disabled by default so an absent RS485
transceiver does not affect existing functions.

Default pins:

| Signal | ESP32 pin |
| --- | --- |
| UART2 TX | GPIO17 |
| UART2 RX | GPIO16 |
| RS485 DE/RTS | GPIO4 |

Configuration fields:

```json
{
  "modbus_enabled": true,
  "modbus_slave_addr": 1,
  "modbus_baud_rate": 9600,
  "modbus_start_register": 0,
  "modbus_register_count": 4,
  "modbus_poll_period_ms": 5000
}
```

Restart the device after changing Modbus or MQTT connection settings.
Three consecutive polling failures set error code `6001`; a successful poll
clears the error.
