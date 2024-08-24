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
  sendAcknowledge = true;
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

boolean NibeGw::connected() {
  return connectionState;
}

NibeGw &NibeGw::setCallback(callback_msg_received_type callback_msg_received,
                            callback_msg_token_received_type callback_msg_token_received) {
  this->callback_msg_received = callback_msg_received;
  this->callback_msg_token_received = callback_msg_token_received;

  return *this;
}

void NibeGw::setSendAcknowledge(boolean val) {
  sendAcknowledge = val;
}

boolean NibeGw::messageStillOnProgress() {
  if (!connectionState)
    return false;

  if (RS485->available() > 0)
    return true;

  if (state == STATE_CRC_FAILURE || state == STATE_OK_MESSAGE_RECEIVED || state == STATE_WAIT_DATA)
    return true;

  return false;
}

void NibeGw::loop() {
  if (!connectionState)
    return;

  switch (state) {
    case STATE_WAIT_START:
      if (RS485->available() > 0) {
        byte b = RS485->read();
        ESP_LOGVV(TAG, "%02X", b);

        buffer[0] = buffer[1];
        buffer[1] = b;

        if (buffer[0] == STARTBYTE_MASTER) {
          if (buffer[1] == STARTBYTE_MASTER) {
            buffer[1] = 0x00;
            state = STATE_WAIT_START;
            ESP_LOGVV(TAG, "Ignore double start");
          } else {
            index = 2;
            state = STATE_WAIT_DATA;
            ESP_LOGVV(TAG, "Frame start found");
          }
        }
      }
      break;

    case STATE_WAIT_ACK:
      if (RS485->available() > 0) {
        byte b = RS485->read();
        ESP_LOGVV(TAG, "%02X", b);

        if (b == STARTBYTE_ACK) {
          ESP_LOGV(TAG, "Ack seen");
        } else if (b == STARTBYTE_NACK) {
          ESP_LOGV(TAG, "Nack seen");
        } else {
          ESP_LOGW(TAG, "Unexpected Ack/Nack: %02X", b);
        }

        state = STATE_WAIT_START;
        buffer[1] = b;
      }
      break;

    case STATE_WAIT_DATA:
      if (RS485->available() > 0) {
        byte b = RS485->read();
        ESP_LOGVV(TAG, "%02X", b);

        if (index >= MAX_DATA_LEN) {
          // too long message
          state = STATE_WAIT_START;
        } else {
          buffer[index++] = b;
          int msglen = checkNibeMessage(buffer, index);
          ESP_LOGVV(TAG, "checkMsg=%d", msglen);

          switch (msglen) {
            case 0:
              break;  // Ok, but not ready
            case -1:
              state = STATE_WAIT_START;
              break;  // Invalid message
            case -2:
              state = STATE_CRC_FAILURE;
              break;  // Checksum error
            default:
              state = STATE_OK_MESSAGE_RECEIVED;
              break;
          }

          if (msglen) {
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERY_VERBOSE
            for (byte i = 0; i < msglen && i < DEBUG_BUFFER_LEN / 3; i++) {
              sprintf(debug_buf + i * 3, "%02X ", buffer[i]);
            }
            ESP_LOGVV(TAG, "Message of %d bytes received from heat pump: %s", msglen, debug_buf);
#endif

            callback_msg_received(buffer, index);
          }
        }
      }
      break;

    case STATE_CRC_FAILURE:
      if (shouldAckNakSend(buffer[2]))
        sendNak();
      ESP_LOGW(TAG, "Had CRC failure");
      state = STATE_WAIT_START;
      break;

    case STATE_OK_MESSAGE_RECEIVED:
      if (!shouldAckNakSend(buffer[2])) {
        state = STATE_WAIT_START;
        break;
      }

      state = STATE_WAIT_START;
      if (buffer[0] == STARTBYTE_MASTER && buffer[4] == 0x00) {
        int msglen = callback_msg_token_received((eTokenType) (buffer[3]), buffer);
        if (msglen > 0) {
          sendData(buffer, (byte) msglen);
          state = STATE_WAIT_ACK;
          ESP_LOGVV(TAG, "Responded to token %02X", buffer[3]);
        } else {
          sendAck();
          ESP_LOGVV(TAG, "Had no response to token %02X ", buffer[3]);
        }
      } else {
        sendAck();
      }
      break;
  }
}

/*
   Return:
    >0 if valid message received (return message len)
     0 if ok, but message not ready
    -1 if invalid message
    -2 if checksum fails
*/
int NibeGw::checkNibeMessage(const byte *const data, byte len) {
  if (len <= 0)
    return 0;

  if (len >= 1) {
    if (data[0] != STARTBYTE_MASTER)
      return -1;

    if (len >= 6) {
      int datalen = data[4];

      if (len < datalen + 6)
        return 0;

      byte checksum = 0;

      // calculate XOR checksum
      for (int i = 1; i < (datalen + 5); i++)
        checksum ^= data[i];

      byte msg_checksum = data[datalen + 5];

      ESP_LOGVV(TAG, "checksum=%02X, msg_checksum=%02X", checksum, msg_checksum);

      if (checksum != msg_checksum) {
        // if checksum is 0x5C (start character),
        // heat pump seems to send 0xC5 checksum
        if (checksum != 0x5C && msg_checksum != 0xC5)
          return -2;
      }

      return datalen + 6;
    }
  }

  return 0;
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

void NibeGw::sendData(const byte *const data, byte len) {
  sendBegin();
  RS485->write_array(data, len);
  sendEnd();

#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERY_VERBOSE
  for (byte i = 0; i < len && i < DEBUG_BUFFER_LEN / 3; i++) {
    sprintf(debug_buf + i * 3, "%02X ", data[i]);
  }
  ESP_LOGVV(TAG, "Sent message of %d bytes to heat pump: %s", len, debug_buf);
#endif
}

void NibeGw::sendAck() {
  sendBegin();
  RS485->write_byte(STARTBYTE_ACK);
  sendEnd();
  ESP_LOGVV(TAG, "Sent ACK");
}

void NibeGw::sendNak() {
  sendBegin();
  RS485->write_byte(STARTBYTE_NACK);
  sendEnd();
  ESP_LOGVV(TAG, "Sent NACK");
}

boolean NibeGw::shouldAckNakSend(byte address) {
  return addressAcknowledge.count(address) != 0;
}
