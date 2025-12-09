/**
 * Copyright (c) 2010-2022 Contributors to the openHAB project
 *
 * See the NOTICE file(s) distributed with this work for additional
 * information.
 *
 * This program and the accompanying materials are made available under the
 * terms of the Eclipse Public License 2.0 which is available at
 * http://www.eclipse.org/legal/epl-2.0
 *
 * SPDX-License-Identifier: EPL-2.0
 *
 * ----------------------------------------------------------------------------
 *
 * Author: pauli.anttila@gmail.com
 *
 */

#include "NibeGw.h"
#include "esphome/core/gpio.h"
#include "esphome/components/uart/uart.h"

using namespace esphome;

NibeGw::NibeGw(esphome::uart::UARTDevice *serial, esphome::GPIOPin *RS485DirectionPin) {
  state = STATE_WAIT_START;
  connectionState = false;
  RS485 = serial;
  directionPin = RS485DirectionPin;
  setCallback(NULL, NULL);
}

void NibeGw::connect() {
  if (!connectionState) {
    state = STATE_WAIT_START;
    connectionState = true;
    if (directionPin)
      directionPin->setup();
  }
}

void NibeGw::disconnect() {
  if (connectionState) {
    connectionState = false;
  }
}

bool NibeGw::connected() {
  return connectionState;
}

NibeGw &NibeGw::setCallback(callback_msg_received_type callback_msg_received,
                            callback_msg_token_received_type callback_msg_token_received) {
  this->callback_msg_received = callback_msg_received;
  this->callback_msg_token_received = callback_msg_token_received;

  return *this;
}

bool NibeGw::messageStillOnProgress() {
  if (!connectionState)
    return false;

  if (RS485->available() > 0)
    return true;

  if (state == STATE_WAIT_DATA)
    return true;

  return false;
}

void NibeGw::handleDataReceived(uint8_t b) {
  if (index >= MAX_DATA_LEN) {
    // too long message
    handleInvalidData(b);
    return;
  }

  switch (state) {
    case STATE_WAIT_START:

      buffer[0] = buffer[1];
      buffer[1] = b;

      if (buffer[0] == STARTBYTE_MASTER) {
        if (buffer[1] == STARTBYTE_MASTER) {
          buffer[1] = 0x00;
          ESP_LOGD(TAG, "Ignore double start");
        } else {
          index = 2;
          state = STATE_WAIT_DATA;
          ESP_LOGVV(TAG, "Frame start found");
        }
      } else {
        if (buffer[1] != STARTBYTE_MASTER) {
          ESP_LOGD(TAG, "Ignoring byte %02X", b);
        }
      }
      break;

    case STATE_WAIT_START_SLAVE:
      if (b == STARTBYTE_SLAVE) {
        indexSlave = index;
        buffer[index++] = b;
        state = STATE_WAIT_DATA_SLAVE;
      } else {
        handleExpectedAck(b);
      }
      break;

    case STATE_WAIT_ACK:
      handleExpectedAck(b);
      break;

    case STATE_WAIT_DATA_SLAVE: {
      buffer[index++] = b;

      eParse check = checkSlaveData(&buffer[indexSlave], index - indexSlave);
      if (check == PACKET_PENDING) {
        break;
      }

      if (check == PACKET_OK) {
        ESP_LOGV(TAG, "Received token %02X and response", buffer[3]);
        state = STATE_WAIT_ACK;
        break;
      }

      stateComplete(0);
      break;
    }

    case STATE_WAIT_DATA:
      buffer[index++] = b;

      eParse check = checkMasterData(buffer, index);
      if (check == PACKET_PENDING) {
        break;
      }

      if (check == PACKET_OK) {
        handleMsgReceived();
        break;
      }

      handleCrcFailure();
      break;
  }
}

void NibeGw::handleExpectedAck(uint8_t b) {
  buffer[index++] = b;
  ESP_LOGV(TAG, "Recv: %02X", b);
  if (b == STARTBYTE_ACK || b == STARTBYTE_NACK) {
    /* Complete */
  } else if (b == STARTBYTE_MASTER) {
    /* Next message */
    index--;
  } else {
    ESP_LOGW(TAG, "Unexpected Ack/Nack: %02X", b);
  }
  stateComplete(b);
}

void NibeGw::stateComplete(uint8_t data) {
  if (index) {
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
    for (uint8_t i = 0; i < index && i < DEBUG_BUFFER_LEN / 3; i++) {
      sprintf(debug_buf + i * 3, "%02X ", buffer[i]);
    }
    ESP_LOGV(TAG, "Recv: %s", debug_buf);
#endif
  }

  callback_msg_received(buffer, index);
  state = STATE_WAIT_START;
  index = 0;
  buffer[1] = data;  // reset second byte
}

void NibeGw::handleMsgReceived() {
  const uint16_t address = buffer[2] | (buffer[1] << 8);
  const uint8_t command = buffer[3];
  const uint8_t len = buffer[4];
  if (shouldAckNakSend(address)) {
    if (len == 0) {
      int msglen = callback_msg_token_received(address, command, &buffer[index]);
      if (msglen > 0) {
        sendData(&buffer[index], msglen);
        index += msglen;
        state = STATE_WAIT_ACK;
      } else {
        stateCompleteAck();
      }
    } else {
      stateCompleteAck();
    }
  } else {
    if (len == 0) {
      state = STATE_WAIT_START_SLAVE;
    } else {
      state = STATE_WAIT_ACK;
    }
  }
}

void NibeGw::handleCrcFailure() {
  ESP_LOGW(TAG, "Had crc failure");
  if (shouldAckNakSend(buffer[2] | (buffer[1] << 8))) {
    stateCompleteNak();
  } else {
    stateComplete(0);
  }
}

void NibeGw::handleInvalidData(uint8_t data) {
  ESP_LOGW(TAG, "Had invalid message");
  stateComplete(data);
}

void NibeGw::loop() {
  if (!connectionState)
    return;

  if (RS485->available() > 0) {
    uint8_t b = RS485->read();
    ESP_LOGVV(TAG, "%02X", b);
    handleDataReceived(b);
  }
}

uint8_t NibeGw::calculateChecksum(const uint8_t *const data, uint8_t len) {
  uint8_t checksum = 0;
  for (uint8_t i = 0; i < len; i++) {
    checksum ^= data[i];
  }

  if (checksum == STARTBYTE_MASTER) {
    // if checksum is 0x5C (start character),
    // heat pump seems to send 0xC5 checksum
    checksum = 0xC5;
  }
  return checksum;
}

void NibeGw::sendBegin() {
  if (directionPin) {
    directionPin->digital_write(true);
    esphome::delay(1);
  }
}

void NibeGw::sendEnd() {
  RS485->flush();
  if (directionPin) {
    esphome::delay(1);
    directionPin->digital_write(false);
  }
}

void NibeGw::sendData(const uint8_t *const data, uint8_t len) {
  sendBegin();
  RS485->write_array(data, len);
  sendEnd();

#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
  for (uint8_t i = 0; i < len && i < DEBUG_BUFFER_LEN / 3; i++) {
    sprintf(debug_buf + i * 3, "%02X ", data[i]);
  }
  ESP_LOGV(TAG, "Sent: %s", debug_buf);
#endif
}

void NibeGw::stateCompleteAck() {
  sendBegin();
  RS485->write_byte(STARTBYTE_ACK);
  sendEnd();
  ESP_LOGV(TAG, "Sent: %02X", STARTBYTE_ACK);

  buffer[index++] = STARTBYTE_ACK;
  stateComplete(0);
}

void NibeGw::stateCompleteNak() {
  sendBegin();
  RS485->write_byte(STARTBYTE_NACK);
  sendEnd();
  ESP_LOGV(TAG, "Sent: %02X", STARTBYTE_NACK);

  buffer[index++] = STARTBYTE_NACK;
  stateComplete(0);
}

bool NibeGw::shouldAckNakSend(uint16_t address) {
  return addressAcknowledge.count(address) != 0;
}

eParse NibeGw::checkSlaveData(const uint8_t *data, size_t len) {
  /* start, cmd, len, data[len], checksum */
  if (len < 4) {
    return PACKET_PENDING;
  }

  if (data[0] != STARTBYTE_SLAVE) {
    ESP_LOGD(TAG, "Slave start byte is invalid");
    return PACKET_ERR;
  }

  if (len - 4 < data[2]) {
    return PACKET_PENDING;
  }

  if (len - 4 != data[2]) {
    ESP_LOGD(TAG, "Slave packet has invalid size");
    return PACKET_ERR;
  }

  const uint8_t checksum = calculateChecksum(data, len - 1);
  if (data[len - 1] != checksum) {
    ESP_LOGD(TAG, "Slave data checksum is invalid");
    return PACKET_ERR;
  }
  return PACKET_OK;
}

eParse NibeGw::checkMasterData(const uint8_t *data, size_t len) {
  /* start, address1, address2, cmd, len, data[len], checksum */
  if (len < 6) {
    return PACKET_PENDING;
  }
  if (data[0] != STARTBYTE_MASTER) {
    ESP_LOGD(TAG, "Master start byte is invalid");
    return PACKET_ERR;
  }

  if (len - 6 < data[4]) {
    return PACKET_PENDING;
  }

  if (len - 6 != data[4]) {
    ESP_LOGD(TAG, "Master packet too large");
    return PACKET_ERR;
  }

  const uint8_t checksum = calculateChecksum(&data[1], len - 2);
  if (data[len - 1] != checksum) {
    ESP_LOGD(TAG, "Master data checksum is invalid");
    return PACKET_ERR;
  }
  return PACKET_OK;
}