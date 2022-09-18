#pragma once

#include <set>

#include "esphome.h"
#include "esphome/core/component.h"

#include "NibeGw.h"
#include <HardwareSerial.h>
#include <WiFiUdp.h>

using namespace esphome;

class NibeGwComponent: public Component {
    float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }
    const char* TAG = "nibegw";
    int udp_read_port_  = 9999;
    int udp_write_port_ = 10000;
    int udp_target_port_;
    IPAddress udp_target_ip_;
    std::set<IPAddress> udp_source_ip_;

    NibeGw* gw_;

    WiFiUDP udp_read_;
    WiFiUDP udp_write_;

    void callback_msg_received(const byte* const data, int len);
    int callback_msg_token_received(eTokenType token, byte* data);
    void callback_debug(byte verbose, char* data);

    int token_request(WiFiUDP& udp, byte* data);

    public:

    void set_target_port(int port) { udp_target_port_ = port; };
    void set_read_port(int port) { udp_read_port_ = port; };
    void set_write_port(int port) { udp_write_port_ = port; };
    void set_target_ip(std::string ip) { udp_target_ip_.fromString(ip.c_str()); };
    void add_source_ip(std::string ip) { udp_source_ip_.insert(IPAddress().fromString(ip.c_str())); };

    NibeGw& gw() { return *gw_; }

    NibeGwComponent(int uart_no, int dir_pin, int rx_pin, int tx_pin);

    void setup();

    void loop();
};
