$body = @{
    clientid = "esp32_gateway_001"
    topic = "esp32/gateway/esp32_gateway_001/sensor"
    payload = @{
        schema = 1
        device_id = "esp32_gateway_001"
        boot_id = 1
        seq = 1
        timestamp = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
        data = @{
            temperature = 27.1
            humidity = 59.2
            light = 401
        }
        edge = @{
            temperature_ema = 27.0
            humidity_ema = 59.0
            light_ema = 400
            anomaly_flags = 0
        }
    }
    timestamp = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
} | ConvertTo-Json -Depth 4

Invoke-RestMethod `
    -Method Post `
    -Uri "http://localhost:3000/api/iot/sensor" `
    -ContentType "application/json" `
    -Body $body

Invoke-RestMethod `
    -Method Get `
    -Uri "http://localhost:3000/api/sensor/latest?device_id=esp32_gateway_001"
