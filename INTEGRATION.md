# 🎯 BOTTLING MACHINE API INTEGRATION GUIDE

This document provides the complete JSON API specification for integrating with the Bottling Machine's web interface and control system.

## 📡 API Endpoints Overview

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | Get machine status and network info |
| `/api/settings` | GET | Get current machine settings |
| `/api/settings` | POST | Update multiple settings |
| `/api/settings/{name}` | POST | Update individual setting |
| `/api/control` | POST | Control machine state |
| `/api/wifi` | POST | Configure WiFi connection |

## 🔧 API Reference

### 1. **GET /api/status** - Machine Status
Returns current machine state and network information.

**Response:**
```json
{
  "connected": true,
  "ip": "192.168.1.100",
  "ap": "",
  "hostname": "bottling-machine-A1B2",
  "mdns": "bottling-machine-A1B2.local",
  "machineState": "running"
}
```

**Fields:**
- `connected` (boolean): WiFi connection status
- `ip` (string): IP address when connected, empty when in AP mode
- `ap` (string): AP SSID when in access point mode
- `hostname` (string): Device hostname
- `mdns` (string): mDNS address for local discovery
- `machineState` (string): Current machine state (`"stopped"`, `"paused"`, `"running"`)

### 2. **GET /api/settings** - Current Settings
Returns all current machine configuration settings.

**Response:**
```json
{
  "enableFilling": true,
  "enableCapping": false,
  "pushTime": 3000,
  "fillTime": 32000,
  "capTime": 2000,
  "postPushDelay": 3000,
  "postFillDelay": 1000,
  "bottlePositioningDelay": 1000,
  "thresholdBottleLoaded": 200,
  "thresholdCapLoaded": 160,
  "thresholdCapFull": 160,
  "rollingAverageWindow": 5
}
```

**Settings Fields:**
- `enableFilling` (boolean): Enable/disable bottle filling
- `enableCapping` (boolean): Enable/disable bottle capping
- `pushTime` (integer): Push mechanism duration in milliseconds
- `fillTime` (integer): Fill mechanism duration in milliseconds
- `capTime` (integer): Cap mechanism duration in milliseconds
- `postPushDelay` (integer): Delay after push operation in milliseconds
- `postFillDelay` (integer): Delay after fill operation in milliseconds
- `bottlePositioningDelay` (integer): Conveyor positioning delay in milliseconds
- `thresholdBottleLoaded` (integer): Ultrasonic threshold for bottle detection
- `thresholdCapLoaded` (integer): Ultrasonic threshold for cap availability
- `thresholdCapFull` (integer): Ultrasonic threshold for cap loader full
- `rollingAverageWindow` (integer): Sensor reading averaging window (1-20)

### 3. **POST /api/settings** - Update Settings
Update multiple machine settings at once.

**Request:**
```json
{
  "enableFilling": true,
  "enableCapping": true,
  "pushTime": 2500,
  "fillTime": 30000,
  "capTime": 1800,
  "postPushDelay": 2000,
  "postFillDelay": 800,
  "bottlePositioningDelay": 1200,
  "thresholdBottleLoaded": 180,
  "thresholdCapLoaded": 150,
  "thresholdCapFull": 150,
  "rollingAverageWindow": 8
}
```

**Response:** Returns updated settings (same structure as GET /api/settings)

### 4. **POST /api/settings/{settingName}** - Individual Setting Update
Update a single setting by name.

**Request Body (form-encoded):**
```
value=2500
```

**Response:**
```json
{
  "ok": true,
  "name": "pushTime",
  "value": "2500"
}
```

### 5. **POST /api/control** - Machine Control
Control the machine state (start, pause, stop).

**Request:**
```json
{
  "action": "start"
}
```

**Valid Actions:**
- `"start"` - Start machine operation
- `"pause"` - Pause machine operation
- `"stop"` - Stop machine operation

**Response:**
```json
{
  "machineState": "running"
}
```

### 6. **POST /api/wifi** - WiFi Configuration
Configure WiFi connection settings.

**Request:**
```json
{
  "ssid": "MyNetwork",
  "password": "MyPassword123"
}
```

**Response:**
```json
{
  "connected": true,
  "ip": "192.168.1.100"
}
```

## 🚨 Error Responses

### Invalid JSON
```json
{
  "error": "Invalid JSON"
}
```

### Unknown Setting
```json
{
  "error": "Unknown setting"
}
```

### Not Found
```json
{
  "error": "Not found"
}
```

### Missing Setting Name
```json
{
  "error": "Missing setting name"
}
```

## 🏆 Machine State Values

| State | Description |
|-------|-------------|
| `"stopped"` | Machine is completely stopped |
| `"paused"` | Machine is paused, ready to resume |
| `"running"` | Machine is actively operating |

## 🔧 Integration Examples

### Python Example
```python
import requests
import json

# Get machine status
response = requests.get('http://bottling-machine-A1B2.local/api/status')
status = response.json()
print(f"Machine state: {status['machineState']}")

# Update settings
settings = {
    "enableFilling": True,
    "pushTime": 2500,
    "fillTime": 30000
}
response = requests.post('http://bottling-machine-A1B2.local/api/settings', 
                        json=settings)

# Control machine
control = {"action": "start"}
response = requests.post('http://bottling-machine-A1B2.local/api/control', 
                        json=control)
```

### JavaScript Example
```javascript
// Get machine status
const statusResponse = await fetch('http://bottling-machine-A1B2.local/api/status');
const status = await statusResponse.json();
console.log(`Machine state: ${status.machineState}`);

// Update settings
const settings = {
    enableFilling: true,
    pushTime: 2500,
    fillTime: 30000
};
await fetch('http://bottling-machine-A1B2.local/api/settings', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(settings)
});

// Control machine
await fetch('http://bottling-machine-A1B2.local/api/control', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ action: 'start' })
});
```

## 🛡️ Security Notes

- The device supports both WiFi client mode and access point mode
- When in AP mode, the device creates a network named `BottlingMachine-{CHIP_ID}`
- mDNS is enabled for local discovery as `bottling-machine-{CHIP_ID}.local`
- CORS headers are configured to allow cross-origin requests
- All settings are persisted to non-volatile storage

## 📋 Default Settings

| Setting | Default Value | Range |
|---------|---------------|-------|
| enableFilling | true | boolean |
| enableCapping | false | boolean |
| pushTime | 3000 | 0+ ms |
| fillTime | 32000 | 0+ ms |
| capTime | 2000 | 0+ ms |
| postPushDelay | 3000 | 0+ ms |
| postFillDelay | 1000 | 0+ ms |
| bottlePositioningDelay | 1000 | 0+ ms |
| thresholdBottleLoaded | 200 | 0+ |
| thresholdCapLoaded | 160 | 0+ |
| thresholdCapFull | 160 | 0+ |
| rollingAverageWindow | 5 | 1-20 |
