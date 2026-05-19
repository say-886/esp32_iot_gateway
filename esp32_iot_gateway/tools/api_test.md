# API Test Notes

```text
GET  /api/status
POST /api/control
GET  /api/config
POST /api/config
POST /api/reboot
```

`POST /api/control` body:

```json
{
  "led": 1,
  "buzzer": 0,
  "relay": 1
}
```
