# ESPHome components for Nibe heat pumps

A ESPHome components that wraps the Arduino based udp gateway `NibeGW` up for use with ESPHome configuration.

## Background

When Modbus adapter support is enabled from the heat pump UI, the heat pump will start to send telegrams every now and then. A telegram contains a maximum of 20 registers. Those 20 registers can be configured via the Nibe ModbusManager application.

A telegram from the heat pump must be acknowledged, otherwise the heat pump will raise an alarm and go into the alarm state. Acknowledgement (ACK or NAK) responses should be sent correctly. This component will ACK/NAK and then forward received data to a configured UDP port on a remote host. It will also accept read/write requests on UDP to request other parameters.

## Parsing

Currently no actual parsing of the payload is performed on the ESPHome device, this must be handled by external application.

* [Nibe MQTT](https://github.com/yozik04/nibe-mqtt)
* [nibepi](https://github.com/anerdins/nibepi)

## Original source of NibeGW

This components is based on the NibeGW code for arduino from [OpenHAB Nibe Addon](https://www.openhab.org/addons/bindings/nibeheatpump/#prerequisites) ([src](https://github.com/openhab/openhab-addons/tree/main/bundles/org.openhab.binding.nibeheatpump/contrib/NibeGW/Arduino/NibeGW))
