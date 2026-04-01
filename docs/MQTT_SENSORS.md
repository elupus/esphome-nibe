# NibeGW Direct Sensor Polling via MQTT

Publish Nibe heat pump coil values directly from the ESP32 to an MQTT broker, with Home Assistant auto-discovery. This eliminates the need for an external gateway like [nibe-mqtt](https://github.com/yozik04/nibe-mqtt).

## Why use this?

The traditional setup requires a separate service (e.g. nibe-mqtt in Docker) to bridge UDP to MQTT. With direct sensor polling:

- **No external dependencies** - the ESP32 handles everything
- **Network resilience** - RS485 communication continues even when MQTT/network is down
- **Buffering** - coil values are stored during MQTT outages and flushed on reconnect
- **Simpler deployment** - one device, one config file

## Prerequisites

- ESPHome 2026.3.0 or later
- An MQTT broker (e.g. Mosquitto) reachable from the ESP32
- Your Nibe heat pump model's coil addresses (see [Finding coil addresses](#finding-coil-addresses))

## Configuration

The `nibegw` component has two independently enabled features:

| Feature | Purpose |
|---|---|
| `udp` | Forward raw data to external tools (classic NibeGW mode) |
| `sensors` | Poll specific coils and publish directly to MQTT |

Both can be enabled simultaneously, or just one.

### Minimal example (sensors only)

```yaml
mqtt:
  broker: 192.168.1.2
  username: user
  password: pass
  discovery: true

nibegw:
  acknowledge:
    - MODBUS40
  constants:
    - address: MODBUS40
      token: ACCESSORY
      data: [0x0A, 0x00, 0x01]

  udp:
    enabled: false

  sensors:
    enabled: true
    poll_interval: 30s
    poll_groups:
      - id: fast
        interval: 10s
      - id: slow
        interval: 120s
    coils:
      - name: "BT1 Outdoor Temperature"
        address: 40004
        size: s16
        factor: 10
        unit_of_measurement: "°C"
        device_class: temperature
        accuracy_decimals: 1
        poll_group: fast
      - name: "Energy Heating"
        address: 44300
        size: u32
        factor: 10
        unit_of_measurement: "kWh"
        device_class: energy
        accuracy_decimals: 1
        poll_group: slow
```

### Dual mode (UDP + sensors)

```yaml
nibegw:
  acknowledge:
    - MODBUS40
  constants:
    - address: MODBUS40
      token: ACCESSORY
      data: [0x0A, 0x00, 0x01]

  udp:
    enabled: true
    target:
      - ip: 192.168.1.10
        port: 9999
    source:
      - 192.168.1.10
    read_port: 9999
    write_port: 10000

  sensors:
    enabled: true
    poll_interval: 30s
    coils:
      - name: "BT1 Outdoor Temperature"
        address: 40004
        size: s16
        factor: 10
        unit_of_measurement: "°C"
        device_class: temperature
        accuracy_decimals: 1
```

### UDP only (classic mode)

```yaml
nibegw:
  acknowledge:
    - MODBUS40
  constants:
    - address: MODBUS40
      token: ACCESSORY
      data: [0x0A, 0x00, 0x01]

  udp:
    enabled: true
    target:
      - ip: 192.168.1.10
        port: 9999
    read_port: 9999
    write_port: 10000

  sensors:
    enabled: false
```

## Configuration reference

### `sensors:` block

| Option | Default | Description |
|---|---|---|
| `enabled` | `false` | Enable direct coil polling |
| `poll_interval` | `30s` | Default interval for poll groups not explicitly defined |
| `poll_groups` | `[]` | Named poll groups with custom intervals (see below) |
| `buffer_size` | `4096` | Circular buffer size in bytes for MQTT-offline storage (0 = disabled) |
| `buffer_mode` | `latest_only` | `latest_only`, `history`, or `off` (see [Buffer behavior](#buffer-behavior)) |
| `coils` | `[]` | List of read-only coil sensors |
| `writable` | `[]` | List of writable coil number entities |

### `poll_groups[]` entries

| Option | Required | Description |
|---|---|---|
| `id` | yes | Group name (referenced by `poll_group` on coils) |
| `interval` | yes | Poll interval for this group (e.g. `10s`, `30s`, `120s`) |

Coils without an explicit `poll_group` are assigned to `"default"`, which uses `poll_interval`.
Groups referenced by coils but not defined in `poll_groups` are auto-created with `poll_interval`.

### `coils[]` entries (read-only sensors)

| Option | Required | Description |
|---|---|---|
| `name` | yes | Sensor name (appears in HA) |
| `address` | yes | Nibe coil address (e.g. `40004`) |
| `size` | yes | Data type: `u8`, `s8`, `u16`, `s16`, `u32`, `s32` |
| `factor` | no (default: 1) | Raw value is divided by this (e.g. factor 10 means raw 215 = 21.5) |
| `poll_group` | no (default: `"default"`) | Which poll group this coil belongs to |
| `unit_of_measurement` | no | Unit string for HA (e.g. `"°C"`, `"Hz"`) |
| `device_class` | no | HA device class (e.g. `temperature`, `frequency`, `power`) |
| `accuracy_decimals` | no | Decimal places shown in HA |

### `writable[]` entries (number entities)

Same as `coils[]` plus:

| Option | Required | Description |
|---|---|---|
| `min_value` | yes | Minimum allowed value |
| `max_value` | yes | Maximum allowed value |
| `step` | no (default: 1.0) | Step size for the number entity |

### Write behavior

Writable coils appear as `number` entities in HA, controllable via MQTT command topic or the web dashboard.

**Write flow:**
1. Value is sent to the heat pump as a MODBUS write request (0x6B)
2. The pump confirms or denies the write (0x6C response)
3. On success: a read-back request is queued to verify the value from the pump
4. On denial: a warning is logged (the pump rejects writes to read-only or out-of-range values)
5. Write timeout: 10 seconds - if no response, the write is considered failed

**Initial value on boot:**
Writable coils queue an initial read request 5 seconds after boot to fetch the current value from the pump. This means the number entity shows the actual pump setting shortly after startup, without needing to write first.

**Queue behavior:**
Read and write requests share the RS485 bus with regular sensor polling. The Nibe protocol processes one request per token cycle (~2 seconds). Requests are served FIFO with a queue depth of 3 per token type. In practice:
- Write requests use WRITE_TOKEN (0x6B) - separate queue from reads
- Read-after-write uses READ_TOKEN (0x69) - shares queue with sensor polling
- With many sensors polling, a read-back may take a few seconds to be served

**Important:** Only configure coils marked with `"write": true` in the coil database. Writing to read-only coils will be denied by the pump.

## Finding coil addresses

Coil definitions for each Nibe model are maintained in the [nibe](https://github.com/yozik04/nibe) library under [`nibe/data/`](https://github.com/yozik04/nibe/tree/master/nibe/data).

### Step 1: Find your model's JSON file

| Model | File |
|---|---|
| F1155 / F1255 | `f1155_f1255.json` |
| F1145 / F1245 | `f1145_f1245.json` |
| F730 | `f730.json` |
| F750 | `f750.json` |
| S1156 / S1256 | `s1156_s1256.json` |
| SMO40 | `smo40.json` |

### Step 2: Look up a coil and map to YAML

Each JSON entry looks like:

```json
"40004": {
  "title": "BT1 Outdoor Temperature",
  "info": "Current outdoor temperature",
  "unit": "°C",
  "size": "s16",
  "factor": 10,
  "name": "bt1-outdoor-temperature-40004"
}
```

Map it to YAML:

```yaml
- name: "BT1 Outdoor Temperature"    # ← title
  address: 40004                       # ← JSON key
  size: s16                            # ← size
  factor: 10                           # ← factor (raw value ÷ factor = displayed value)
  unit_of_measurement: "°C"            # ← unit
  device_class: temperature            # ← you choose based on the unit
  accuracy_decimals: 1                 # ← you choose (factor 10 → 1 decimal)
```

### Step 3: Writable coils

Writable coils have `"write": true` and `min`/`max` fields in the JSON:

```json
"47011": {
  "title": "Heat Offset S1",
  "size": "s8",
  "factor": 1,
  "min": -10.0,
  "max": 10.0,
  "write": true,
  "name": "heat-offset-s1-47011"
}
```

Map to YAML under `writable:`:

```yaml
writable:
  - name: "Heat Offset S1"
    address: 47011
    size: s8
    factor: 1
    min_value: -10
    max_value: 10
    step: 1
```

## Buffer behavior

When MQTT is disconnected (e.g. broker down, network loss), the ESP32 continues polling the heat pump. Coil values are stored in a configurable buffer and published when MQTT reconnects.

### `buffer_mode: latest_only` (default)

Stores only the most recent value per coil. On reconnect, each coil publishes its last known value. Uses minimal RAM (~8 bytes per coil).

### `buffer_mode: history`

Stores timestamped values in a circular ring buffer. On reconnect, all buffered entries are published oldest-first. Buffer capacity depends on `buffer_size`:

| `buffer_size` | Entries (~12 bytes each) | Duration (20 coils @ 30s) |
|---|---|---|
| `1024` | ~85 | ~2 min |
| `4096` | ~341 | ~8.5 min |
| `16384` | ~1365 | ~34 min |
| `65536` | ~5461 | ~2.3 hours |

### `buffer_mode: off`

No buffering. Values during MQTT outage are lost.

## Migrating from nibe-mqtt

If you currently use [nibe-mqtt](https://github.com/yozik04/nibe-mqtt), here's how to migrate:

### 1. Map your poll coils

Your nibe-mqtt `config.yaml`:

```yaml
nibe:
  model: F1155
  poll:
    coils:
      - bt1-outdoor-temperature-40004
      - bt7-hw-top-40013
      - compressor-frequency-actual-43136
```

The `name` field in the JSON database (e.g. `bt1-outdoor-temperature-40004`) is the same identifier nibe-mqtt uses. Look up each one in the JSON file to get the address, size, and factor.

### 2. Add them to your ESPHome config

```yaml
sensors:
  enabled: true
  coils:
    - name: "BT1 Outdoor Temperature"
      address: 40004
      size: s16
      factor: 10
      unit_of_measurement: "°C"
      device_class: temperature
      accuracy_decimals: 1
    - name: "BT7 HW Top"
      address: 40013
      size: s16
      factor: 10
      unit_of_measurement: "°C"
      device_class: temperature
      accuracy_decimals: 1
    - name: "Compressor Frequency"
      address: 43136
      size: u16
      factor: 10
      unit_of_measurement: "Hz"
      accuracy_decimals: 1
```

### 3. Add MQTT config

Replace `api:` with `mqtt:` in your ESPHome config:

```yaml
mqtt:
  broker: your_mqtt_broker
  username: your_user
  password: your_pass
  discovery: true
```

### 4. Disable UDP if no longer needed

Set `udp.enabled: false` if you're no longer running nibe-mqtt.

### 5. Stop the nibe-mqtt container

Once verified, stop and remove the Docker container.

## Optional: Web dashboard and diagnostics

ESPHome has a built-in web server that shows live sensor values - useful for quick checks without HA:

```yaml
web_server:
  port: 80
```

Access it at `http://<esp32-ip>/`. All coil sensors appear automatically.

To monitor memory usage (useful for tuning `buffer_size`):

```yaml
debug:
  update_interval: 30s

sensor:
  - platform: debug
    free:
      name: "Free Heap Memory"
    loop_time:
      name: "Loop Time"
```

These show up on both the web dashboard and in MQTT/HA.

## Troubleshooting

### No sensor data appearing

- Check ESPHome logs for `"Coil xxxx = ..."` messages (set log level to DEBUG)
- Verify `acknowledge: [MODBUS40]` is set
- Verify the MODBUS40 accessory constant is configured
- Check that the coil address exists for your heat pump model

### Wrong values / garbage data

- Check the `size` field matches the JSON database (e.g. `s16` vs `u16`)
- Check the `factor` field - wrong factor gives values 10x or 100x off
- Some coils use `s8` which is padded to 2 bytes in the protocol

### MQTT not connecting

- Verify broker IP is reachable from the ESP32
- Check credentials
- ESPHome's `mqtt:` and `api:` cannot be used simultaneously - remove/comment out `api:`

### Buffer not flushing on reconnect

- Check logs for `"MQTT reconnected, flushing buffer"`
- Verify `buffer_mode` is not `off`
- `buffer_size: 0` also disables buffering
