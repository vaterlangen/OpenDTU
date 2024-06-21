// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "PinMapping.h"
#include <cstdint>

#define CONFIG_FILENAME "/config.json"

#define WIFI_MAX_SSID_STRLEN 32
#define WIFI_MAX_PASSWORD_STRLEN 64
#define WIFI_MAX_HOSTNAME_STRLEN 31

#define DEV_MAX_MAPPING_NAME_STRLEN 63

struct CONFIG_T {
    struct {
        uint32_t Version;
        uint32_t SaveCount;
    } Cfg;

    struct {
        char Ssid[WIFI_MAX_SSID_STRLEN + 1];
        char Password[WIFI_MAX_PASSWORD_STRLEN + 1];
        uint8_t Ip[4];
        uint8_t Netmask[4];
        uint8_t Gateway[4];
        uint8_t Dns1[4];
        uint8_t Dns2[4];
        bool Dhcp;
        char Hostname[WIFI_MAX_HOSTNAME_STRLEN + 1];
        uint32_t ApTimeout;
    } WiFi;

    struct {
        char Password[WIFI_MAX_PASSWORD_STRLEN + 1];
        bool AllowReadonly;
    } Security;

    struct {
        uint8_t Brightness;
    } Led_Single[PINMAPPING_LED_COUNT];

    char Dev_PinMapping[DEV_MAX_MAPPING_NAME_STRLEN + 1];
};

class ConfigurationClass {
public:
    void init();
    bool read();
    CONFIG_T& get();
};

extern ConfigurationClass Configuration;
