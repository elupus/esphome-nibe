# ESPHome components for Nibe heat pumps

An ESPHome component that wraps the Arduino based udp gateway `NibeGW` up, for use with ESPHome configuration.

## Background

When Modbus adapter support is enabled from the heat pump UI, the heat pump will start to send telegrams every now and then. A telegram contains a maximum of 20 registers. Those 20 registers can be configured via the Nibe ModbusManager application.

A telegram from the heat pump must be acknowledged, otherwise the heat pump will raise an alarm and go into the alarm state. Acknowledgement (ACK or NAK) responses should be sent correctly. This component will ACK/NAK and then forward received data to a configured UDP port on a remote host. It will also accept read/write requests on UDP to request other parameters.

## Setup

You will need an esp32 with some type of RS485 converter hooked up to a UART. It can either be a MAX485 based chip or a chip with automatic flow control like a MAX3485. If using an automatic flow controlling chip, don't set the `dir_pin`.

An example of such a board is the [LilyGo T-CAN485](https://github.com/Xinyuan-LilyGO/T-CAN485), this board has an integrated RS485 connection that is verified to work with this setup. An example setup can be found in the [examples](./examples) folder.

Another board that should work but isn't tested is the [LILYGO® T-RSC3 ESP32-C3](https://github.com/Xinyuan-LilyGO/T-RSC3)

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
    target:
      - ip: 192.168.16.130

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

## Parsing

Currently no actual parsing of the payload is performed on the ESPHome device, this must be handled by external application.

* [Home Assistant](https://www.home-assistant.io/integrations/nibe_heatpump)
* [OpenHab](https://www.openhab.org/addons/bindings/nibeheatpump)
* [Nibe MQTT](https://github.com/yozik04/nibe-mqtt)
* [nibepi](https://github.com/anerdins/nibepi)

## Original source of NibeGW

This components is based on the NibeGW code for arduino from [OpenHAB Nibe Addon](https://www.openhab.org/addons/bindings/nibeheatpump/#prerequisites) ([src](https://github.com/openhab/openhab-addons/tree/main/bundles/org.openhab.binding.nibeheatpump/contrib/NibeGW/Arduino/NibeGW))
