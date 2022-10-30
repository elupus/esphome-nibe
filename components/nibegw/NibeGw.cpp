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

NibeGw::NibeGw(esphome::uart::UARTDevice* serial, esphome::GPIOPin* RS485DirectionPin)
{
  verbose = 0;
  sendAcknowledge = true;
  state = STATE_WAIT_START;
  connectionState = false;
  RS485 = serial;
  directionPin = RS485DirectionPin;
  setCallback(NULL, NULL);
}

void NibeGw::connect()
{
  if (!connectionState)
  {
    state = STATE_WAIT_START;
    connectionState = true;
  }
}

void NibeGw::disconnect()
{
  if (connectionState)
  {
    connectionState = false;
  }
}

boolean NibeGw::connected()
{
  return connectionState;
}

void NibeGw::setVerboseLevel(byte level)
{
  verbose = level;
}

NibeGw& NibeGw::setCallback(callback_msg_received_type callback_msg_received, callback_msg_token_received_type callback_msg_token_received)
{
  this->callback_msg_received = callback_msg_received;
  this->callback_msg_token_received = callback_msg_token_received;

  return *this;
}

#ifdef ENABLE_NIBE_DEBUG
NibeGw& NibeGw::setDebugCallback(callback_debug_type debug)
{
  this->debug = debug;
  return *this;
}
#endif

void NibeGw::setSendAcknowledge(boolean val)
{
  sendAcknowledge = val;
}

boolean NibeGw::messageStillOnProgress()
{
  if (!connectionState)
    return false;

  if ( RS485->available() > 0)
    return true;

  if (state == STATE_CRC_FAILURE || state == STATE_OK_MESSAGE_RECEIVED)
    return true;

  return false;
}

void NibeGw::loop()
{
  if (!connectionState)
    return;

  switch (state)
  {
    case STATE_WAIT_START:
      if (RS485->available() > 0)
      {
        byte b = RS485->read();

#ifdef ENABLE_NIBE_DEBUG
        if (debug)
        {
          sprintf(debug_buf, "%02X ", b);
          debug(3, debug_buf);
        }
#endif

        if (b == 0x5C)
        {
          buffer[0] = b;
          index = 1;
          state = STATE_WAIT_DATA;

#ifdef ENABLE_NIBE_DEBUG
          if (debug)
            debug(4, "\nFrame start found\n");
#endif
        }
      }
      break;

    case STATE_WAIT_DATA:
      if (RS485->available() > 0)
      {
        byte b = RS485->read();

#ifdef ENABLE_NIBE_DEBUG
        if (debug)
        {
          sprintf(debug_buf, "%02X", b);
          debug(3, debug_buf);
        }
#endif

        if (index >= MAX_DATA_LEN)
        {
          // too long message
          state = STATE_WAIT_START;
        }
        else
        {
          buffer[index++] = b;
          int msglen = checkNibeMessage(buffer, index);

#ifdef ENABLE_NIBE_DEBUG
          if (debug)
          {
            sprintf(debug_buf, "\ncheckMsg=%d\n", msglen);
            debug(5, debug_buf);
          }
#endif

          switch (msglen)
          {
            case 0:   break; // Ok, but not ready
            case -1:  state = STATE_WAIT_START; break; // Invalid message
            case -2:  state = STATE_CRC_FAILURE; break; // Checksum error
            default:  state = STATE_OK_MESSAGE_RECEIVED; break;
          }

          if (msglen) {
            callback_msg_received(buffer, index);
          }
        }
      }
      break;

    case STATE_CRC_FAILURE:
#ifdef ENABLE_NIBE_DEBUG
      if (debug)
        debug(1, "CRC failure\n");
#endif
      if (shouldAckNakSend(buffer[2]))
        sendNak();
      state = STATE_WAIT_START;
      break;

    case STATE_OK_MESSAGE_RECEIVED:
      if (!shouldAckNakSend(buffer[2])) {
        state = STATE_WAIT_START;
        break;
      }

      if (buffer[0] == 0x5C && buffer[1] == 0x00 && buffer[4] == 0x00)
      {
#ifdef ENABLE_NIBE_DEBUG
        if (debug)
          debug(1, "Token received\n");
#endif

        int msglen = callback_msg_token_received((eTokenType)(buffer[3]), buffer);
        if (msglen > 0)
        {
          int msgnok = checkNibeRequest(buffer, msglen);
          if (msgnok == 0)
            sendData(buffer, (byte) msglen);
          else
            sendAck();
        }
        else
        {
#ifdef ENABLE_NIBE_DEBUG
          if (debug)
            debug(2, "No message to send\n");
#endif
          sendAck();
        }
      }
      else
      {
#ifdef ENABLE_NIBE_DEBUG
        if (debug)
        {
          debug(1, "Message received\n");
        }
#endif
        sendAck();
      }
      state = STATE_WAIT_START;
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
int NibeGw::checkNibeMessage(const byte* const data, byte len)
{
  if (len <= 0)
    return 0;

  if (len >= 1)
  {
    if (data[0] != 0x5C)
      return -1;

    if (len >= 2)
    {
      if (data[1] != 0x00)
        return -1;
    }

    if (len >= 6)
    {
      int datalen = data[4];

      if (len < datalen + 6)
        return 0;

      byte checksum = calcChecksum(&data[2], datalen + 3);
      byte msg_checksum = data[datalen + 5];

#ifdef ENABLE_NIBE_DEBUG
      if (debug) {
        sprintf(debug_buf, "\nchecksum=%02X, msg_checksum=%02X\n", checksum, msg_checksum);
        debug(4, debug_buf);
      }
#endif

      if (checksum != msg_checksum)
      {
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

/*
   Return:
     0 if ok
    -1 if invalid message
    -2 if checksum fails
*/
int NibeGw::checkNibeRequest(const byte* const data, byte len)
{
  if (len < 3)
    return -1;

  byte checksum = calcChecksum(data, len - 1);
  byte msg_checksum = data[len - 1];

#ifdef ENABLE_NIBE_DEBUG
  if (debug) {
    sprintf(debug_buf, "\nchecksum=%02X, msg_checksum=%02X\n", checksum, msg_checksum);
    debug(4, debug_buf);
  }
#endif

  if (checksum != msg_checksum)
  {
    if (checksum != 0x5C && msg_checksum != 0xC5)
      return -2;
  }
  return 0;
}

int NibeGw::calcChecksum(const byte* const data, byte len)
{
  byte checksum = 0;
  for (int i = 0; i < len; i++)
    checksum ^= data[i];
  return checksum;
}


void NibeGw::sendData(const byte* const data, byte len)
{
#ifdef ENABLE_NIBE_DEBUG
  if (debug)
  {
    debug(1, "Send message to heat pump: ");
    for (int i = 0; i < len; i++)
    {
      sprintf(debug_buf, "%02X", data[i]);
      debug(1, debug_buf);
    }
    debug(1, "\n");
  }
#endif

  if(directionPin)
    directionPin->digital_write(true);
  RS485->write_array(data, len);
  RS485->flush();
  delay(1);
  if(directionPin)
    directionPin->digital_write(false);
}

void NibeGw::sendAck()
{
#ifdef ENABLE_NIBE_DEBUG
  if (debug)
    debug(1, "Send ACK\n");
#endif

  if(directionPin)
    directionPin->digital_write(true);
  delay(1);
  RS485->write_byte(0x06);
  RS485->flush();
  delay(1);
  if(directionPin)
    directionPin->digital_write(false);
}

void NibeGw::sendNak()
{
#ifdef ENABLE_NIBE_DEBUG
  if (debug)
    debug(1, "Send NACK\n");
#endif

  if(directionPin)
    directionPin->digital_write(true);
  delay(1);
  RS485->write(0x15);
  RS485->flush();
  delay(1);
  if(directionPin)
    directionPin->digital_write(false);
}

boolean NibeGw::shouldAckNakSend(byte address)
{
  return addressAcknowledge.count(address) != 0;
}
