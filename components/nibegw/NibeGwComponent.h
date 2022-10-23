#pragma once

#include <set>
#include <queue>
#include <vector>
#include <cstddef>

#include "esphome.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/number/number.h"

#include "NibeGw.h"


#ifdef USE_ESP32
#include <WiFi.h>
#endif

#ifdef USE_ESP8266
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#endif



typedef std::tuple<byte, byte>  request_key_type;
typedef std::vector<byte>       request_data_type;
typedef std::tuple<esphome::network::IPAddress, int> target_type;


class NibeGwNumber : public esphome::number::Number, public esphome::Component {
    float initial_value_{NAN};
 public:
    NibeGwNumber(float initial_value) { this->initial_value_ = initial_value; };
    void setup() override { this->publish_state(this->initial_value_); }
 protected:
    void control(float value) override { this->publish_state(value); }
};


class NibeGwComponent: public esphome::Component, public esphome::uart::UARTDevice {

    float get_setup_priority() const override { return esphome::setup_priority::BEFORE_CONNECTION; }
    const char* TAG = "nibegw";
    const int requests_queue_max = 3;
    int udp_read_port_  = 9999;
    int udp_write_port_ = 10000;
    std::set<esphome::network::IPAddress> udp_source_ip_;
    bool is_connected_ = false;

    std::vector<target_type> udp_targets_;
    std::map<request_key_type, std::queue<request_data_type>> requests_; 
    std::map<request_key_type, request_data_type>             requests_const_; 

    NibeGw* gw_;

    WiFiUDP udp_read_;
    WiFiUDP udp_write_;

    void callback_msg_received(const byte* const data, int len);
    int callback_msg_token_received(eTokenType token, byte* data);
    void callback_debug(byte verbose, char* data);

    void token_request_cache(WiFiUDP& udp, byte address, byte token);

    public:

    void set_read_port(int port) { udp_read_port_ = port; };
    void set_write_port(int port) { udp_write_port_ = port; };

    void add_target(const esphome::network::IPAddress& ip, int port)
    {
        auto target = target_type(ip, port);
        udp_targets_.push_back(target);
    }

    void add_source_ip(const esphome::network::IPAddress& ip){
        udp_source_ip_.insert(ip);
    };

    void set_const_request(int address, int token, request_data_type request)
    {
        requests_const_[request_key_type(address, token)] = std::move(request);
    }

    void add_queued_request(int address, int token, request_data_type request)
    {
        auto& queue = requests_[request_key_type(address, token)];
        if (queue.size() >= requests_queue_max) {
            queue.pop();
        }
        queue.push(std::move(request));
    }

    void add_rmu_temperature(int address, NibeGwNumber* number);

    NibeGw& gw() { return *gw_; }

    NibeGwComponent(esphome::GPIOPin* dir_pin);

    void setup();
    void dump_config();
    void loop();
};
