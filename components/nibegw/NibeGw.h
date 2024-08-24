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
 * Frame format:
 * +----+----+------+-----+-----+----+----+-----+
 * | 5C | 00 | ADDR | CMD | LEN |  DATA   | CHK |
 * +----+----+------+-----+-----+----+----+-----+
 *           |------------ CHK -----------|
 *
 *  Address:
 *    0x16 = SMS40
 *    0x19 = RMU40
 *    0x20 = MODBUS40
 *
 *  Checksum: XOR
 *
 * When valid data is received (checksum ok),
 *  ACK (0x06) should be sent to the heat pump.
 * When checksum mismatch,
 *  NAK (0x15) should be sent to the heat pump.
 *
 * Author: pauli.anttila@gmail.com
 *
 */
#ifndef NibeGw_h
#define NibeGw_h

#include <Arduino.h>
#include "esphome/components/uart/uart.h"
#include "esphome/core/gpio.h"
#include <functional>
#include <set>

using namespace esphome;

// #define HARDWARE_SERIAL_WITH_PINS
// #define HARDWARE_SERIAL

// state machine states
enum eState {
  STATE_WAIT_START,
  STATE_WAIT_DATA,
  STATE_WAIT_ACK,
  STATE_OK_MESSAGE_RECEIVED,
  STATE_CRC_FAILURE,
};

enum eTokenType {
  READ_TOKEN = 0x69,
  WRITE_TOKEN = 0x6B,
  RMU_WRITE_TOKEN = 0x60,
  RMU_DATA_MSG = 0x62,
  RMU_DATA_TOKEN = 0x63,
  ACCESSORY_TOKEN = 0xEE,
};

enum eStartByte {
  STARTBYTE_MASTER = 0x5c,
  STARTBYTE_SLAVE = 0xc0,
  STARTBYTE_ACK = 0x06,
  STARTBYTE_NACK = 0x15,
};

// message buffer for RS-485 communication. Max message length is 80 bytes + 6 bytes header
#define MAX_DATA_LEN 128

typedef std::function<void(const byte *const data, int len)> callback_msg_received_type;
typedef std::function<int(eTokenType token, byte *data)> callback_msg_token_received_type;

#define SMS40 0x16
#define RMU40 0x19
#define RMU40_S1 0x19
#define RMU40_S2 0x1A
#define RMU40_S3 0x1B
#define RMU40_S4 0x1C
#define MODBUS40 0x20

class NibeGw {
 private:
  eState state;
  boolean connectionState;
  esphome::GPIOPin *directionPin;
  byte buffer[MAX_DATA_LEN];
  byte index;
  esphome::uart::UARTDevice *RS485;
  callback_msg_received_type callback_msg_received;
  callback_msg_token_received_type callback_msg_token_received;
  std::set<byte> addressAcknowledge;
  boolean sendAcknowledge;

  int checkNibeMessage(const byte *const data, byte len);
  void sendData(const byte *const data, byte len);
  void sendAck();
  void sendNak();
  void sendBegin();
  void sendEnd();
  boolean shouldAckNakSend(byte address);

  const char *TAG = "nibeGW";
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
#define DEBUG_BUFFER_LEN 300
  char debug_buf[DEBUG_BUFFER_LEN];
#endif

 public:
  NibeGw(esphome::uart::UARTDevice *serial, esphome::GPIOPin *RS485DirectionPin);
  NibeGw &setCallback(callback_msg_received_type callback_msg_received,
                      callback_msg_token_received_type callback_msg_token_received);

  void connect();
  void disconnect();
  boolean connected();
  boolean messageStillOnProgress();
  void loop();

  void setAcknowledge(byte address, boolean val) {
    if (val)
      addressAcknowledge.insert(address);
    else
      addressAcknowledge.erase(address);
  }

  void setAckModbus40Address(boolean val) {
    setAcknowledge(MODBUS40, val);
  }
  void setAckSms40Address(boolean val) {
    setAcknowledge(SMS40, val);
  }
  void setAckRmu40Address(boolean val) {
    setAcknowledge(RMU40, val);
  }
  void setSendAcknowledge(boolean val);
};

#endif
