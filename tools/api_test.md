# API Test Notes

```text
GET  /api/status
POST /api/control
GET  /api/config
POST /api/config
POST /api/reboot
POST /api/ota
```

`POST /api/control` body:

```json
{
  "led": 1,
  "buzzer": 0,
  "relay": 1
}
```

PowerShell examples:

```powershell
$ip = "10.135.247.46"

Invoke-RestMethod -Method Get -Uri "http://$ip/api/status"
Invoke-RestMethod -Method Post -Uri "http://$ip/api/control" -ContentType "application/json" -Body '{"led":true}'
Invoke-RestMethod -Method Post -Uri "http://$ip/api/control" -ContentType "application/json" -Body '{"led":false}'
Invoke-RestMethod -Method Post -Uri "http://$ip/api/control" -ContentType "application/json" -Body '{"buzzer":true}'
Invoke-RestMethod -Method Post -Uri "http://$ip/api/control" -ContentType "application/json" -Body '{"buzzer":false}'
Invoke-RestMethod -Method Post -Uri "http://$ip/api/control" -ContentType "application/json" -Body '{"relay":true}'
Invoke-RestMethod -Method Post -Uri "http://$ip/api/control" -ContentType "application/json" -Body '{"relay":false}'
```

OTA request shape:

```powershell
Invoke-RestMethod -Method Post -Uri "http://$ip/api/ota" -ContentType "application/json" -Body '{"url":"http://10.135.247.100/esp32_iot_gateway.bin"}'
```
