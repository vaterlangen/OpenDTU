// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2024 Thomas Basler and others
 */
#include "WebApi.h"
#include "Configuration.h"
#include "MessageOutput.h"
#include "defaults.h"
#include <AsyncJson.h>

WebApiClass::WebApiClass()
    : _server(HTTP_PORT)
{
}

void WebApiClass::init(Scheduler& scheduler)
{
    _webApiFirmware.init(_server, scheduler);
    _webApiMaintenance.init(_server, scheduler);
    _webApiSysstatus.init(_server, scheduler);
    _webApiWebapp.init(_server, scheduler);

    _server.begin();
}

bool WebApiClass::checkCredentials(AsyncWebServerRequest* request)
{
    CONFIG_T& config = Configuration.get();
    if (request->authenticate(AUTH_USERNAME, config.Security.Password)) {
        return true;
    }

    AsyncWebServerResponse* r = request->beginResponse(401);

    // WebAPI should set the X-Requested-With to prevent browser internal auth dialogs
    if (!request->hasHeader("X-Requested-With")) {
        r->addHeader("WWW-Authenticate", "Basic realm=\"Login Required\"");
    }
    request->send(r);

    return false;
}

bool WebApiClass::checkCredentialsReadonly(AsyncWebServerRequest* request)
{
    CONFIG_T& config = Configuration.get();
    if (config.Security.AllowReadonly) {
        return true;
    } else {
        return checkCredentials(request);
    }
}

void WebApiClass::sendTooManyRequests(AsyncWebServerRequest* request)
{
    auto response = request->beginResponse(429, "text/plain", "Too Many Requests");
    response->addHeader("Retry-After", "60");
    request->send(response);
}

bool WebApiClass::parseRequestData(AsyncWebServerRequest* request, AsyncJsonResponse* response, JsonDocument& json_document)
{
    auto& retMsg = response->getRoot();
    retMsg["type"] = "warning";

    if (!request->hasParam("data", true)) {
        retMsg["message"] = "No values found!";
        retMsg["code"] = WebApiError::GenericNoValueFound;
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
        return false;
    }

    const String json = request->getParam("data", true)->value();
    const DeserializationError error = deserializeJson(json_document, json);
    if (error) {
        retMsg["message"] = "Failed to parse data!";
        retMsg["code"] = WebApiError::GenericParseError;
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
        return false;
    }

    return true;
}

bool WebApiClass::sendJsonResponse(AsyncWebServerRequest* request, AsyncJsonResponse* response, const char* function, const uint16_t line)
{
    bool ret_val = true;
    if (response->overflowed()) {
        auto& root = response->getRoot();

        root.clear();
        root["message"] = String("500 Internal Server Error: ") + function + ", " + line;
        root["code"] = WebApiError::GenericInternalServerError;
        root["type"] = "danger";
        response->setCode(500);
        MessageOutput.printf("WebResponse failed: %s, %d\r\n", function, line);
        ret_val = false;
    }

    response->setLength();
    request->send(response);
    return ret_val;
}

WebApiClass WebApi;
