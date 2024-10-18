// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2024 Thomas Basler and others
 */
#include "WebApi_ws_vedirect_live.h"
#include "AsyncJson.h"
#include "Configuration.h"
#include "MessageOutput.h"
#include "Utils.h"
#include "WebApi.h"
#include "defaults.h"
#include "PowerLimiter.h"
#include "VictronMppt.h"

WebApiWsVedirectLiveClass::WebApiWsVedirectLiveClass()
    : _ws("/vedirectlivedata")
{
}

void WebApiWsVedirectLiveClass::init(AsyncWebServer& server, Scheduler& scheduler)
{
    using std::placeholders::_1;
    using std::placeholders::_2;
    using std::placeholders::_3;
    using std::placeholders::_4;
    using std::placeholders::_5;
    using std::placeholders::_6;

    _server = &server;
    _server->on("/api/vedirectlivedata/status", HTTP_GET, std::bind(&WebApiWsVedirectLiveClass::onLivedataStatus, this, _1));

    _server->addHandler(&_ws);
    _ws.onEvent(std::bind(&WebApiWsVedirectLiveClass::onWebsocketEvent, this, _1, _2, _3, _4, _5, _6));


    scheduler.addTask(_wsCleanupTask);
    _wsCleanupTask.setCallback(std::bind(&WebApiWsVedirectLiveClass::wsCleanupTaskCb, this));
    _wsCleanupTask.setIterations(TASK_FOREVER);
    _wsCleanupTask.setInterval(1 * TASK_SECOND);
    _wsCleanupTask.enable();

    scheduler.addTask(_sendDataTask);
    _sendDataTask.setCallback(std::bind(&WebApiWsVedirectLiveClass::sendDataTaskCb, this));
    _sendDataTask.setIterations(TASK_FOREVER);
    _sendDataTask.setInterval(500 * TASK_MILLISECOND);
    _sendDataTask.enable();

    _simpleDigestAuth.setUsername(AUTH_USERNAME);
    _simpleDigestAuth.setRealm("vedirect websocket");

    reload();
}

void WebApiWsVedirectLiveClass::reload()
{
    _ws.removeMiddleware(&_simpleDigestAuth);

    auto const& config = Configuration.get();

    if (config.Security.AllowReadonly) { return; }

    _ws.enable(false);
    _simpleDigestAuth.setPassword(config.Security.Password);
    _ws.addMiddleware(&_simpleDigestAuth);
    _ws.closeAll();
    _ws.enable(true);
}

void WebApiWsVedirectLiveClass::wsCleanupTaskCb()
{
    // see: https://github.com/me-no-dev/ESPAsyncWebServer#limiting-the-number-of-web-socket-clients
    _ws.cleanupClients();
}

bool WebApiWsVedirectLiveClass::hasUpdate(size_t idx)
{
    auto dataAgeMillis = VictronMppt.getDataAgeMillis(idx);
    if (dataAgeMillis == 0) { return false; }
    auto publishAgeMillis = millis() - _lastPublish;
    return dataAgeMillis < publishAgeMillis;
}

uint16_t WebApiWsVedirectLiveClass::responseSize() const
{
    // estimated with ArduinoJson assistant
    return VictronMppt.controllerAmount() * (1024 + 512) + 128/*DPL status and structure*/;
}

void WebApiWsVedirectLiveClass::sendDataTaskCb()
{
    // do nothing if no WS client is connected
    if (_ws.count() == 0) { return; }

    // Update on ve.direct change or at least after 10 seconds
    bool fullUpdate = (millis() - _lastFullPublish > (10 * 1000));
    bool updateAvailable = false;
    if (!fullUpdate) {
        for (size_t idx = 0; idx < VictronMppt.controllerAmount(); ++idx) {
            if (hasUpdate(idx)) {
                updateAvailable = true;
                break;
            }
        }
    }

    if (fullUpdate || updateAvailable) {
        try {
            std::lock_guard<std::mutex> lock(_mutex);
            JsonDocument root;
            JsonVariant var = root;

            generateCommonJsonResponse(var, fullUpdate);

            if (Utils::checkJsonAlloc(root, __FUNCTION__, __LINE__)) {
                String buffer;
                serializeJson(root, buffer);

                _ws.textAll(buffer);;
            }
        } catch (std::bad_alloc& bad_alloc) {
            MessageOutput.printf("Calling /api/vedirectlivedata/status has temporarily run out of resources. Reason: \"%s\".\r\n", bad_alloc.what());
        } catch (const std::exception& exc) {
            MessageOutput.printf("Unknown exception in /api/vedirectlivedata/status. Reason: \"%s\".\r\n", exc.what());
        }
    }

    if (fullUpdate) {
        _lastFullPublish = millis();
    }
}

void WebApiWsVedirectLiveClass::generateCommonJsonResponse(JsonVariant& root, bool fullUpdate)
{
    auto array = root["vedirect"]["instances"].to<JsonObject>();
    root["vedirect"]["full_update"] = fullUpdate;

    for (size_t idx = 0; idx < VictronMppt.controllerAmount(); ++idx) {
        auto optMpptData = VictronMppt.getData(idx);
        if (!optMpptData.has_value()) { continue; }

        if (!fullUpdate && !hasUpdate(idx)) { continue; }

        String serial(optMpptData->serialNr_SER);
        if (serial.isEmpty()) { continue; } // serial required as index

        JsonObject nested = array[serial].to<JsonObject>();
        nested["data_age_ms"] = VictronMppt.getDataAgeMillis(idx);
        populateJson(nested, *optMpptData);
    }

    _lastPublish = millis();

    // power limiter state
    root["dpl"]["PLSTATE"] = -1;
    if (Configuration.get().PowerLimiter.Enabled)
        root["dpl"]["PLSTATE"] = PowerLimiter.getPowerLimiterState();
    root["dpl"]["PLLIMIT"] = PowerLimiter.getLastRequestedPowerLimit();
}

void WebApiWsVedirectLiveClass::populateJson(const JsonObject &root, const VeDirectMpptController::data_t &mpptData) {
    root["product_id"] = mpptData.getPidAsString();
    root["firmware_version"] = mpptData.getFwVersionFormatted();

    const JsonObject values = root["values"].to<JsonObject>();

    const JsonObject device = values["device"].to<JsonObject>();
    device["LOAD"] = mpptData.loadOutputState_LOAD ? "ON" : "OFF";
    device["CS"] = mpptData.getCsAsString();
    device["MPPT"] = mpptData.getMpptAsString();
    device["OR"] = mpptData.getOrAsString();
    device["ERR"] = mpptData.getErrAsString();
    device["HSDS"]["v"] = mpptData.daySequenceNr_HSDS;
    device["HSDS"]["u"] = "d";
    if (mpptData.MpptTemperatureMilliCelsius.first > 0) {
        device["MpptTemperature"]["v"] = mpptData.MpptTemperatureMilliCelsius.second / 1000.0;
        device["MpptTemperature"]["u"] = "°C";
        device["MpptTemperature"]["d"] = "1";
    }

    const JsonObject output = values["output"].to<JsonObject>();
    output["P"]["v"] = mpptData.batteryOutputPower_W;
    output["P"]["u"] = "W";
    output["P"]["d"] = 0;
    output["V"]["v"] = mpptData.batteryVoltage_V_mV / 1000.0;
    output["V"]["u"] = "V";
    output["V"]["d"] = 2;
    output["I"]["v"] = mpptData.batteryCurrent_I_mA / 1000.0;
    output["I"]["u"] = "A";
    output["I"]["d"] = 2;
    output["E"]["v"] = mpptData.mpptEfficiency_Percent;
    output["E"]["u"] = "%";
    output["E"]["d"] = 1;
    if (mpptData.SmartBatterySenseTemperatureMilliCelsius.first > 0) {
        output["SBSTemperature"]["v"] = mpptData.SmartBatterySenseTemperatureMilliCelsius.second / 1000.0;
        output["SBSTemperature"]["u"] = "°C";
        output["SBSTemperature"]["d"] = "0";
    }

    const JsonObject input = values["input"].to<JsonObject>();
    if (mpptData.NetworkTotalDcInputPowerMilliWatts.first > 0) {
        input["NetworkPower"]["v"] = mpptData.NetworkTotalDcInputPowerMilliWatts.second / 1000.0;
        input["NetworkPower"]["u"] = "W";
        input["NetworkPower"]["d"] = "0";
    }
    input["PPV"]["v"] = mpptData.panelPower_PPV_W;
    input["PPV"]["u"] = "W";
    input["PPV"]["d"] = 0;
    input["VPV"]["v"] = mpptData.panelVoltage_VPV_mV / 1000.0;
    input["VPV"]["u"] = "V";
    input["VPV"]["d"] = 2;
    input["IPV"]["v"] = mpptData.panelCurrent_mA / 1000.0;
    input["IPV"]["u"] = "A";
    input["IPV"]["d"] = 2;
    input["YieldToday"]["v"] = mpptData.yieldToday_H20_Wh / 1000.0;
    input["YieldToday"]["u"] = "kWh";
    input["YieldToday"]["d"] = 2;
    input["YieldYesterday"]["v"] = mpptData.yieldYesterday_H22_Wh / 1000.0;
    input["YieldYesterday"]["u"] = "kWh";
    input["YieldYesterday"]["d"] = 2;
    input["YieldTotal"]["v"] = mpptData.yieldTotal_H19_Wh / 1000.0;
    input["YieldTotal"]["u"] = "kWh";
    input["YieldTotal"]["d"] = 2;
    input["MaximumPowerToday"]["v"] = mpptData.maxPowerToday_H21_W;
    input["MaximumPowerToday"]["u"] = "W";
    input["MaximumPowerToday"]["d"] = 0;
    input["MaximumPowerYesterday"]["v"] = mpptData.maxPowerYesterday_H23_W;
    input["MaximumPowerYesterday"]["u"] = "W";
    input["MaximumPowerYesterday"]["d"] = 0;
}

void WebApiWsVedirectLiveClass::onWebsocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len)
{
    if (type == WS_EVT_CONNECT) {
        char str[64];
        snprintf(str, sizeof(str), "Websocket: [%s][%u] connect", server->url(), client->id());
        Serial.println(str);
        MessageOutput.println(str);
    } else if (type == WS_EVT_DISCONNECT) {
        char str[64];
        snprintf(str, sizeof(str), "Websocket: [%s][%u] disconnect", server->url(), client->id());
        Serial.println(str);
        MessageOutput.println(str);
    }
}

void WebApiWsVedirectLiveClass::onLivedataStatus(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentialsReadonly(request)) {
        return;
    }
    try {
        std::lock_guard<std::mutex> lock(_mutex);
        AsyncJsonResponse* response = new AsyncJsonResponse();
        auto& root = response->getRoot();

        generateCommonJsonResponse(root, true/*fullUpdate*/);

        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
    } catch (std::bad_alloc& bad_alloc) {
        MessageOutput.printf("Calling /api/vedirectlivedata/status has temporarily run out of resources. Reason: \"%s\".\r\n", bad_alloc.what());
        WebApi.sendTooManyRequests(request);
    } catch (const std::exception& exc) {
        MessageOutput.printf("Unknown exception in /api/vedirectlivedata/status. Reason: \"%s\".\r\n", exc.what());
        WebApi.sendTooManyRequests(request);
    }
}
