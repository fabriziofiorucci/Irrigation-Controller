## Configuration

Configuration and monitoring are managed through MQTT and the `esp-garden/control` topic.

Use the appropriate MQTT settings based on your MQTT broker setup

- MQTT_BROKER - MQTT Broker IP Address
- MQTT_USERNAME - MQTT username
- MQTT_PASSWORD - MQTT password

The JSON payload supports the following commands:

- `configure` - sets water lines start/stop time and enable/disable state
- `set` - manually open/close water line
- `dump` - retrieves the current configuration

Configure water lines:

```
$ mosquitto_pub -h MQTT_BROKER -u MQTT_USERNAME -P MQTT_PASSWORD -t "esp-garden/control" -m '{
  "command": "configure",
  "linesConfig": [
    {
      "start": {
        "hour": 12,
        "minute": 5,
        "duration": 2
      },
      "line": 0,
      "enabled": true
    },
    {
      "start": {
        "hour": 4,
        "minute": 48,
        "duration": 4
      },
      "line": 3,
      "enabled": true
    } 
  ]
}'
```

Get current configuration:

```
$ mosquitto_pub -h MQTT_BROKER -u MQTT_USERNAME -P MQTT_PASSWORD -t "esp-garden/control" -m '{
  "command": "dump"
}'

{
  "rtcTime": {
    "hour": 12,
    "minute": 13,
    "second": 22,
    "utc": 1640780002
  },
  "ntpTime": {
    "hour": 12,
    "minute": 12,
    "second": 36
  },
  "networkConfig": {
    "ipAddress": "192.168.1.148"
  },
  "linesConfig": [
    {
      "start": {
        "hour": 0,
        "minute": 0,
        "duration": 0
      },
      "running": {
        "status": "false",
        "offUTCTime": 0
      },
      "line": 0,
      "enabled": "false"
    },
    {
      "start": {
        "hour": 0,
        "minute": 0,
        "duration": 0
      },
      "running": {
        "status": "false",
        "offUTCTime": 0
      },
      "line": 1,
      "enabled": "false"
    },
    {
      "start": {
        "hour": 0,
        "minute": 0,
        "duration": 0
      },
      "running": {
        "status": "false",
        "offUTCTime": 0
      },
      "line": 2,
      "enabled": "false"
    },
    {
      "start": {
        "hour": 0,
        "minute": 0,
        "duration": 0
      },
      "running": {
        "status": "false",
        "offUTCTime": 0
      },
      "line": 3,
      "enabled": "false"
    }
  ],
  "host": "esp-garden"
}

```

Enable water line:

```
$ mosquitto_pub -h MQTT_BROKER -u MQTT_USERNAME -P MQTT_PASSWORD -t "esp-garden/control" -m '{
  "command": "configure",
  "linesConfig": [
    {
      "line": 0,
      "enabled": true
    }
  ]
}'
```

Disable water line:

```
$ mosquitto_pub -h MQTT_BROKER -u MQTT_USERNAME -P MQTT_PASSWORD -t "esp-garden/control" -m '{
  "command": "configure",
  "linesConfig": [
    {
      "line": 0,
      "enabled": false
    }
  ]
}'
```

Manually open a water line:

```
$ mosquitto_pub -h MQTT_BROKER -u MQTT_USERNAME -P MQTT_PASSWORD -t "esp-garden/control" -m '{
  "command": "set",
  "linesConfig": [
    { 
      "line": 0,
      "running": true
    }  
  ]
}'
```

Manually close a water line:

```
$ mosquitto_pub -h MQTT_BROKER -u MQTT_USERNAME -P MQTT_PASSWORD -t "esp-garden/control" -m '{
  "command": "set",
  "linesConfig": [
    { 
      "line": 0,
      "running": false
    }  
  ]
}' 
```
