# ESPHome components for Nibe heat pumps

An ESPHome component that wraps the Arduino based udp gateway `NibeGW` up, for use with ESPHome configuration.

NibeGW is known to work also on Nibe clone pumps. For example, Jämä STAR RST 6 corresponds to Nibe F1245 and works without modifications.

## Background

When Modbus adapter support is enabled from the heat pump UI, the heat pump will start to send telegrams every now and then. A telegram contains a maximum of 20 registers. Those 20 registers can be configured via the Nibe ModbusManager application.

A telegram from the heat pump must be acknowledged, otherwise the heat pump will raise an alarm and go into the alarm state. Acknowledgement (ACK or NAK) responses should be sent correctly. This component will ACK/NAK and then forward received data to a configured UDP port on a remote host. It will also accept read/write requests on UDP to request other parameters.

## Setup

You will need an esp32 with some type of RS485 converter hooked up to a UART. It can either be a MAX485 based chip or a chip with automatic flow control like a MAX3485. If using an automatic flow controlling chip, don't set the `dir_pin`.

An example of such a board is the [LilyGo T-CAN485](https://github.com/Xinyuan-LilyGO/T-CAN485), this board has an integrated RS485 connection that is verified to work with this setup. An example setup can be found in the [examples](./examples) folder.

Another board that should work but isn't tested is the [LILYGO® T-RSC3 ESP32-C3](https://github.com/Xinyuan-LilyGO/T-RSC3)

If your heat pump has very old software, consider updating it first: [myUplink Firmware Downloads](https://myuplink.com/update-firmware)

> [!NOTE]
> esp8266 boards are currently not supported due to lack of UDP socket abstraction support in esphome.

### Wifi power save mode
It is recommended to disable powersave mode on wifi, to make sure the device does not miss UDP requests sent.

```yaml
wifi:
  power_save_mode: none
```

### Sharing pins with logger
If you are using the same uart as used for the normal logger component, make sure to disable the logger's output to uart.

```yaml
logger:
  baudrate: 0
```

### Configuration example

Add the following to a ESPHome configuration to enable the udp gateway feature to the device.

Minimal Config

```yaml
external_components:
  - source: 
      type: git
      url: https://github.com/elupus/esphome-nibe.git
    components: [ nibegw ]

uart:
  rx_pin: GPIO16
  tx_pin: GPIO17
  baud_rate: 9600

nibegw:
  udp:
    source:
      - 192.168.16.130

  acknowledge:
    - MODBUS40
```

Complete Config

```yaml
external_components:
  - source: 
      type: git
      url: https://github.com/elupus/esphome-nibe.git
    components: [ nibegw ]

logger:
  level: WARNING

uart:
  id: my_uart
  rx_pin: GPIO16
  tx_pin: GPIO17
  baud_rate: 9600

nibegw:
  dir_pin:
    number: GPIO4
    inverted: false

  # If you have a named uart instance, you can specify this here.
  uart_id: my_uart

  udp:
    # The target address(s) to send data to. May also be multicast addresses.
    # the gateway will automatically populate this based on valid requests
    # entering on the read and write port and will remain valid for one
    # minute after last request.
    #
    # If you want a passive listener that never requests data, you can add
    # an explicit target address here.
    target:
      - ip: 192.168.16.130
        port: 9999

    # List of source address to accept read/write from, may be empty for no filter, but
    # this is not recommended.
    source:
      - 192.168.16.130

    # Optional port this device will listen to to receive read requests. Defaults to 9999
    read_port: 9999

    # Optional port this device will listen to to receive write request. Defaults to 10000
    write_port: 10000

    # Optional command ports for specific requests.
    # ports:
    #  - address: RMU40_S3
    #    token: RMU_WRITE
    #    port: 10001

  acknowledge:
    - MODBUS40

    # Enable a dummy RMU40 accessory to receive updates
    # to certain registers faster. This should not be
    # enabled if you have an actual RMU40.
    - RMU40_S4

  # Constant replies to certain requests can be made
  constants:
    - address: MODBUS40
      token: ACCESSORY
      data: [
            0x0A, # MODBUS version low
            0x00, # MODBUS version high
            0x02, # MODBUS address?
      ]

    # Accessory version response
    - address: RMU40_S4
      token: ACCESSORY
      data: [
            0xEE, # RMU ?
            0x03, # RMU version low
            0x01, # RMU version high
      ]

    # Unknown response that nibepi uses
    - address: RMU40_S4
      token: RMU_DATA
      command: RMU_WRITE
      data: [
            0x63,
            0x00,
      ]

    # Constant fixed temperature to avoid pump going into alarm.
    - address: RMU40_S4
      token: RMU_WRITE
      data: [
            0x06, # Temperature
            0x14, # degrees low
            0x00, # degrees high
      ]

# Add a virtual RMU on S3
climate:
  - platform: nibegw
    name: s3
    address: RMU40_S3
    sensor: current_temperature_s3

# Add a temperature sensor taken from home assistant to use for virtual RMU
sensor:
  - platform: homeassistant
    id: current_temperature_s3
    entity_id: sensor.current_temperature_s3
  
```

## Encrypted UDP (`udp_noise`)

As an alternative to the plaintext `udp` block, the gateway can use an encrypted
transport on a single UDP port. It uses the same Noise handshake and libraries
as the ESPHome native API (`Noise_NNpsk0_25519_ChaChaPoly_SHA256`) with a
pre-shared key, so only clients knowing the key can read telegrams or send
requests. `udp` and `udp_noise` can not be used at the same time.

```yaml
nibegw:
  udp_noise:
    # Pre-shared key, generate with: openssl rand -base64 32
    key: !secret nibegw_noise_key

    # Optional port to listen on. Defaults to 9999
    port: 9999

    # Optional list of requests clients may queue. Defaults to MODBUS40
    # read and write requests. When given, only the listed ones are allowed.
    requests:
      - address: MODBUS40
        token: MODBUS_READ
      - address: MODBUS40
        token: MODBUS_WRITE
      - address: RMU40_S3
        token: RMU_WRITE

  acknowledge:
    - MODBUS40
```

Notes:

* Sessions are always initiated by the client. Static `target`s and multicast are
  not available; a client that only listens must keep its session alive.
* Client source addresses are not filtered, the key is the authentication.
* Requires an ESPHome version that includes the `noise` component.

### Protocol

The packet layout follows the [WireGuard](https://www.wireguard.com/protocol/)
data plane. All integers are little endian, every packet starts with a type
byte followed by three zero bytes.

| Packet                 | Direction        | Content                                                  |
|------------------------|------------------|----------------------------------------------------------|
| `HANDSHAKE_INITIATION` | client → gateway | `1`, sender index (u32), noise handshake message 1       |
| `HANDSHAKE_RESPONSE`   | gateway → client | `2`, sender index (u32), receiver index (u32), message 2 |
| `TRANSPORT_DATA`       | both             | `4`, receiver index (u32), counter (u64), ciphertext     |

The handshake is `Noise_NNpsk0_25519_ChaChaPoly_SHA256` with the prologue
`nibegw-udp-noise-v1`. Each side picks a random index for the session and the
peer addresses its transport data to that index.

Transport data is encrypted with ChaCha20-Poly1305 (IETF), nonce `00 00 00 00`
followed by the counter, without associated data. Counters must increase,
older or repeated ones are dropped. Each direction uses its own key, derived
from the corresponding Noise transport key `k` as the first 32 bytes of
`ENCRYPT(k, n=2^64-2, ad="", 32 zero bytes)`.

The plaintext is:

* empty for a keepalive, which the gateway echoes,
* a request from the client: address (u16), token (u8), request frame,
* a telegram from the gateway: the frame as received from the heat pump.

The client confirms a new session by sending transport data, normally a
keepalive, directly after the handshake. Only confirmed sessions receive
telegrams, and confirming a session drops older sessions from the same client
address. Sessions without traffic from the client expire after 2 minutes. At
most 4 sessions are active, the least recently active is dropped.

Invalid handshakes are dropped and logged by the gateway. Unlike WireGuard the
handshake uses a pre-shared key instead of static keys, and there are no
cookies or periodic rekeying.

## Parsing

Currently no actual parsing of the payload is performed on the ESPHome device, this must be handled by external application.

* [Home Assistant](https://www.home-assistant.io/integrations/nibe_heatpump)
* [OpenHab](https://www.openhab.org/addons/bindings/nibeheatpump)
* [Nibe MQTT](https://github.com/yozik04/nibe-mqtt)
* [nibepi](https://github.com/anerdins/nibepi)

## Original source of NibeGW

This components is based on the NibeGW code for arduino from [OpenHAB Nibe Addon](https://www.openhab.org/addons/bindings/nibeheatpump/#prerequisites) ([src](https://github.com/openhab/openhab-addons/tree/main/bundles/org.openhab.binding.nibeheatpump/contrib/NibeGW/Arduino/NibeGW))
