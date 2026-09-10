# Hardware Acceptance Matrix

## Environment

- ESP32 board and actual AHT20/BH1750/RS485 devices
- Private MQTTS broker with ACL credentials
- Cloud backend running with `IOT_COMMAND_SECRET`
- Serial monitor capture enabled

## Cases

| ID | Scenario | Pass criteria |
| --- | --- | --- |
| H-01 | Wi-Fi and SoftAP provisioning | Invalid or missing station config opens SoftAP; valid config switches to station |
| H-02 | MQTTS TLS connection | Device connects only after SNTP time is valid; public/demo broker is rejected |
| H-03 | Sensor and Modbus polling | Values reach cloud with increasing `boot_id/seq`; Modbus error does not crash tasks |
| H-04 | Remote control | Signed command changes GPIO and returns matching `cmd_id` ACK |
| H-05 | Tampered/expired command | Payload, signature, device mismatch, and expired commands never actuate GPIO |
| H-06 | Offline queue | Disconnecting broker queues telemetry; reconnect replays in order without duplicates |
| H-07 | Power loss recovery | Reboot restores configuration, queue, and recent command history |
| H-08 | OTA success | HTTPS image, digest/signature and target checks pass; new image confirms health |
| H-09 | OTA rollback | Boot failure or health timeout returns to previous image |
| H-10 | 72-hour soak | No uncontrolled reset, heap trend remains stable, task stack watermarks stay above threshold |

Record firmware version, git revision, broker version, start/end time, reset reason, heap minimum,
stack watermarks, MQTT reconnect count, queue high-water mark, and each case's evidence.
