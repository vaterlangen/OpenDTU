// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "WebApi_errors.h"
#include "WebApi_firmware.h"
#include "WebApi_maintenance.h"
#include "WebApi_sysstatus.h"
#include "WebApi_webapp.h"
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <TaskSchedulerDeclarations.h>

class WebApiClass {
public:
    WebApiClass();
    void init(Scheduler& scheduler);

    static bool checkCredentials(AsyncWebServerRequest* request);
    static bool checkCredentialsReadonly(AsyncWebServerRequest* request);

    static void sendTooManyRequests(AsyncWebServerRequest* request);

    static void writeConfig(JsonVariant& retMsg, const WebApiError code = WebApiError::GenericSuccess, const String& message = "Settings saved!");

    static bool parseRequestData(AsyncWebServerRequest* request, AsyncJsonResponse* response, JsonDocument& json_document);
    static uint64_t parseSerialFromRequest(AsyncWebServerRequest* request, String param_name = "inv");
    static bool sendJsonResponse(AsyncWebServerRequest* request, AsyncJsonResponse* response, const char* function, const uint16_t line);

private:
    AsyncWebServer _server;

    WebApiFirmwareClass _webApiFirmware;
    WebApiMaintenanceClass _webApiMaintenance;
    WebApiSysstatusClass _webApiSysstatus;
    WebApiWebappClass _webApiWebapp;
};

extern WebApiClass WebApi;
