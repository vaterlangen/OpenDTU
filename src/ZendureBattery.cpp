#include <functional>

#include "Configuration.h"
#include "ZendureBattery.h"
#include "MqttSettings.h"
#include "MessageOutput.h"
#include "Utils.h"

bool ZendureBattery::init(bool verboseLogging)
{
    _verboseLogging = verboseLogging;

    auto const& config = Configuration.get();
    String deviceType;
    String deviceName;

    switch (config.Battery.ZendureDeviceType){
        case 0:
            deviceType = ZENDURE_HUB1200;
            deviceName = String("HUB 1200");
            break;
        case 1:
            deviceType = ZENDURE_HUB2000;
            deviceName = String("HUB 2000");
            break;
        case 2:
            deviceType = ZENDURE_AIO2400;
            deviceName = String("AIO 2400");
            break;
        case 3:
            deviceType = ZENDURE_ACE1500;
            deviceName = String("Ace 1500");
            break;
        case 4:
            deviceType = ZENDURE_HYPER2000;
            deviceName = String("Hyper 2000");
            break;
        default:
            MessageOutput.printf("ZendureBattery: Invalid device type!");
            return false;
    }

    //_baseTopic = "/73bkTV/sU59jtkw/";
    if (strlen(config.Battery.ZendureDeviceSerial) != 8) {
        MessageOutput.printf("ZendureBattery: Invalid device id!");
        return false;
    }

    // store device ID as we will need them for checking when receiving messages
    _deviceId = config.Battery.ZendureDeviceSerial;

    _baseTopic = "/" + deviceType + "/" + _deviceId + "/";

    _logTopic = _baseTopic + "log";
    _reportTopic = _baseTopic + "properties/report";
    _timesyncTopic = "iot" + _baseTopic + "time-sync/reply";
    _readReplyTopic = _baseTopic + "properties/read/reply";

    _readTopic = "iot" + _baseTopic + "properties/read";
    _writeTopic = "iot" + _baseTopic + "properties/write";

    // subscribe for log messages
    MqttSettings.subscribe(_logTopic, 0/*QoS*/,
            std::bind(&ZendureBattery::onMqttMessageLog,
                this, std::placeholders::_1, std::placeholders::_2,
                std::placeholders::_3, std::placeholders::_4,
                std::placeholders::_5, std::placeholders::_6)
            );
    if (_verboseLogging) {
        MessageOutput.printf("ZendureBattery: Subscribed to '%s' for status readings\r\n", _logTopic.c_str());
    }

    // subscribe for report messages
    MqttSettings.subscribe(_reportTopic, 0/*QoS*/,
            std::bind(&ZendureBattery::onMqttMessageReport,
                this, std::placeholders::_1, std::placeholders::_2,
                std::placeholders::_3, std::placeholders::_4,
                std::placeholders::_5, std::placeholders::_6)
            );
    if (_verboseLogging) {
        MessageOutput.printf("ZendureBattery: Subscribed to '%s' for status readings\r\n", _reportTopic.c_str());
    }

    // subscribe for read messages
    MqttSettings.subscribe(_readReplyTopic, 0/*QoS*/,
            std::bind(&ZendureBattery::onMqttMessageReport,
                this, std::placeholders::_1, std::placeholders::_2,
                std::placeholders::_3, std::placeholders::_4,
                std::placeholders::_5, std::placeholders::_6)
            );
    if (_verboseLogging) {
        MessageOutput.printf("ZendureBattery: Subscribed to '%s' for status readings\r\n", _readReplyTopic.c_str());
    }

    _partitialUpdateRateMs = 12 * 1000;
    _fullUpdateRateMs = 120 * 1000;
    _timesyncRateMs = 60 * 60 * 1000;
    _nextPartitialUpdate = 0;
    _nextFullUpdate = 5 * 1000 + millis();
    _nextTimesync = 0;

    // setup some device info
    _stats->setManufacture("Zendure");
    _stats->setDevice(deviceName);

    // pre-generate the settings request
    JsonDocument root;
    JsonVariant prop = root["properties"].to<JsonObject>();
    prop["pvBrand"] = 1;
    prop["autoRecover"] = 1;
    prop["buzzerSwitch"] = 0;
    prop["passMode"] = config.Battery.ZendureBypassMode;
    prop["socSet"] = config.Battery.ZendureMaxSoC * 10;
    prop["minSoc"] = config.Battery.ZendureMinSoC * 10;
    //prop["outputLimit"] = config.Battery.ZendureMaxOutput;
    prop["inverseMaxPower"] = config.Battery.ZendureMaxOutput;
    prop["smartMode"] = 0;
    prop["autoModel"] = 0;
    serializeJson(root, _settingsPayload);

    // pre-generate the partitial update request
    root.clear();
    JsonArray array = root["properties"].to<JsonArray>();
    array.add(ZENDURE_REPORT_MIN_SOC);
    array.add(ZENDURE_REPORT_MAX_SOC);
    array.add(ZENDURE_REPORT_INPUT_LIMIT);
    array.add(ZENDURE_REPORT_OUTPUT_LIMIT);
    array.add(ZENDURE_REPORT_INVERSE_MAX_POWER);
    array.add(ZENDURE_REPORT_PACK_STATE);
    array.add(ZENDURE_REPORT_HEAT_STATE);
    array.add(ZENDURE_REPORT_HUB_STATE);
    array.add(ZENDURE_REPORT_BUZZER_SWITCH);
    array.add(ZENDURE_REPORT_REMAIN_OUT_TIME);
    array.add(ZENDURE_REPORT_REMAIN_IN_TIME);
    serializeJson(root, _updatePayload);

    return true;
}

void ZendureBattery::deinit()
{
    if (!_reportTopic.isEmpty()) {
        MqttSettings.unsubscribe(_reportTopic);
        _reportTopic.clear();
    }
    if (!_logTopic.isEmpty()) {
        MqttSettings.unsubscribe(_logTopic);
        _logTopic.clear();
    }
    if (!_readReplyTopic.isEmpty()){
        MqttSettings.unsubscribe(_readReplyTopic);
        _readReplyTopic.clear();
    }
}

void ZendureBattery::loop()
{
    auto ms = millis();

    if (!_readTopic.isEmpty()) {
        if (ms >= _nextFullUpdate) {
            _nextFullUpdate = ms + _fullUpdateRateMs;
            _nextPartitialUpdate = ms + _partitialUpdateRateMs;
            MqttSettings.publishGeneric(_readTopic, "{\"properties\": [\"getAll\", \"firmwares\", \"modules\", \"logs\"] }", false, 0);
        }
        if (ms >= _nextPartitialUpdate) {
            _nextPartitialUpdate = ms + _partitialUpdateRateMs;
            MqttSettings.publishGeneric(_readTopic, _updatePayload, false, 0);
        }
    }

    if (ms >= _nextTimesync) {
        _nextTimesync = ms + _timesyncRateMs;
        timesync();

        // republish settings - just to be sure
        if (!_writeTopic.isEmpty()) {
            if (!_settingsPayload.isEmpty()){
                MqttSettings.publishGeneric(_writeTopic, _settingsPayload, false, 0);
            }
        }
    }
}
uint16_t ZendureBattery::updateOutputLimit(uint16_t limit)
{
    if (_writeTopic.isEmpty()) {
        return _stats->_output_limit;
    }

    if (_stats->_output_limit != limit){
        if (limit < 100 && limit != 0){
            uint16_t base = limit / 30U;
            uint16_t remain = (limit % 30U) / 15U;
            limit = 30 * base + 30 * remain;
        }
        MqttSettings.publishGeneric(_writeTopic, "{\"properties\": {\"outputLimit\": " + String(limit) + "} }", false, 0);
    }

    return limit;
}

void ZendureBattery::timesync()
{
    if (!_timesyncTopic.isEmpty()) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 5)) {
            char timeStringBuff[50];
            strftime(timeStringBuff, sizeof(timeStringBuff), "%s", &timeinfo);
            MqttSettings.publishGeneric(_timesyncTopic,"{\"zoneOffset\": \"+00:00\", \"messageId\": 123, \"timestamp\": " + String(timeStringBuff) + "}", false, 0);
        }
    }
}

void ZendureBattery::onMqttMessageRead(espMqttClientTypes::MessageProperties const& properties,
        char const* topic, uint8_t const* payload, size_t len, size_t index, size_t total)
{
}

void ZendureBattery::onMqttMessageReport(espMqttClientTypes::MessageProperties const& properties,
        char const* topic, uint8_t const* payload, size_t len, size_t index, size_t total)
{
    std::string const src = std::string(reinterpret_cast<const char*>(payload), len);
    std::string logValue = src.substr(0, 64);
    if (src.length() > logValue.length()) { logValue += "..."; }

    auto log = [_verboseLogging=_verboseLogging](char const* format, auto&&... args) -> void {
        if (_verboseLogging) {
            MessageOutput.printf("ZendureBattery (Report): ");
            MessageOutput.printf(format, args...);
            MessageOutput.println();
        }
        return;
    };

    JsonDocument json;

    const DeserializationError error = deserializeJson(json, src);
    if (error) {
        return log("cannot parse payload '%s' as JSON", logValue.c_str());
    }

    if (json.overflowed()) {
        return log("payload too large to process as JSON");
    }

    auto obj = json.as<JsonObjectConst>();

    // validate input data
    // messageId has to be set to "123"
    // deviceId has to be set to the configured deviceId
    // product has to be set to "solarFlow"
    if (!json["messageId"].as<String>().equals("123")){
        return log("Invalid or missing 'messageId' in '%s'", logValue.c_str());
    }
    if (!json["deviceId"].as<String>().equals(_deviceId)){
        return log("Invalid or missing 'deviceId' in '%s'", logValue.c_str());
    }/*
    if (!json["product"].as<String>().equals("solarFlow")){
        return log("Invalid or missing 'product' in '%s'", logValue.c_str());
    }*/

    auto props = Utils::getJsonElement<JsonObjectConst>(obj, "properties", 1);
    if (props.has_value()){
        auto sw_version = Utils::getJsonElement<uint32_t>(*props, ZENDURE_REPORT_FW_VERSION);
        if (sw_version.has_value()){
            _stats->setFwVersion(parseVersion(*sw_version));
        }

        auto hw_version = Utils::getJsonElement<uint32_t>(*props, "masterhaerVersion");
        if (hw_version.has_value()){
            _stats->setHwVersion(parseVersion(*hw_version));
        } else {
            hw_version = Utils::getJsonElement<uint32_t>(*props, "masterHaerVersion");
            if (hw_version.has_value()){
                _stats->setHwVersion(parseVersion(*hw_version));
            }
        }

        auto soc_max = Utils::getJsonElement<float>(*props, ZENDURE_REPORT_MAX_SOC);
        if (soc_max.has_value()){
            *soc_max /= 10;
            if (*soc_max >= 40 && *soc_max <= 100){
                _stats->_soc_max = *soc_max;
            }
        }

        auto soc_min = Utils::getJsonElement<float>(*props, ZENDURE_REPORT_MIN_SOC);
        if (soc_min.has_value()){
            *soc_min /= 10;
            if (*soc_min >= 0 && *soc_min <= 60){
                _stats->_soc_min = *soc_min;
            }
        }

        auto input_limit = Utils::getJsonElement<uint16_t>(*props, ZENDURE_REPORT_INPUT_LIMIT);
        if (input_limit.has_value()){
            _stats->_input_limit = *input_limit;
        }

        // auto output_limit = Utils::getJsonElement<uint16_t>(*props, ZENDURE_REPORT_OUTPUT_LIMIT);
        // if (output_limit.has_value()){
        //     _stats->_output_limit = *output_limit;
        // }

        auto inverse_max = Utils::getJsonElement<uint16_t>(*props, ZENDURE_REPORT_INVERSE_MAX_POWER);
        if (inverse_max.has_value()){
            _stats->_inverse_max = *inverse_max;
        }

        auto state = Utils::getJsonElement<uint8_t>(*props, ZENDURE_REPORT_PACK_STATE);
        if (state.has_value() && *state <= 2){
            _stats->_state = static_cast<ZendureBatteryStats::State>(*state);
        }

        auto heat_state = Utils::getJsonElement<uint8_t>(*props, ZENDURE_REPORT_HEAT_STATE);
        if (heat_state.has_value()){
            _stats->_heat_state = static_cast<bool>(*heat_state);
        }

        auto auto_shutdown = Utils::getJsonElement<uint8_t>(*props, ZENDURE_REPORT_HUB_STATE);
        if (auto_shutdown.has_value()){
            _stats->_auto_shutdown = static_cast<bool>(*auto_shutdown);
        }

        auto buzzer = Utils::getJsonElement<uint8_t>(*props, ZENDURE_REPORT_BUZZER_SWITCH);
        if (buzzer.has_value()){
            _stats->_buzzer = static_cast<bool>(*buzzer);
        }

        auto outtime = Utils::getJsonElement<uint16_t>(*props, ZENDURE_REPORT_REMAIN_OUT_TIME);
        if (outtime.has_value()){
            _stats->_remain_out_time = *outtime >= ZENDURE_REMAINING_TIME_OVERFLOW ? -1 : *outtime;
        }

        auto intime = Utils::getJsonElement<uint16_t>(*props, ZENDURE_REPORT_REMAIN_IN_TIME);
        if (intime.has_value()){
            _stats->_remain_in_time = *intime >= ZENDURE_REMAINING_TIME_OVERFLOW ? -1 : *intime;
        }

    }

    // stop processing here, if no pack data found in message
    auto packData = Utils::getJsonElement<JsonArrayConst>(obj, "packData", 2);
    if (!packData.has_value()){
        return;
    }

    // get serial number related to index only if all packs given in message
    if (_stats->_num_batteries != 0 && (*packData).size() == _stats->_num_batteries){
        for (size_t i = 0 ; i < _stats->_num_batteries ; i++){
            auto serial = Utils::getJsonElement<String>((*packData)[i], ZENDURE_REPORT_SERIAL);
            if (serial.has_value()){
                if (!_stats->addPackData(i+1, *serial).has_value()){
                    log("Invalid or unkown serial '%s' in '%s'", (*serial).c_str(), logValue.c_str());
                }
            }else{
                log("Missing serial of battery pack in '%s'", logValue.c_str());
            }
        }
    }

    // get additional data only if all packs were identified
    if (_stats->_packData.size() == _stats->_num_batteries){
        for (auto packDataJson : *packData){
            auto serial = Utils::getJsonElement<String>(packDataJson, ZENDURE_REPORT_SERIAL);
            auto state = Utils::getJsonElement<uint8_t>(packDataJson, "state");
            auto version = Utils::getJsonElement<uint32_t>(packDataJson, ZENDURE_REPORT_FW_VERSION);

            // do not waste processing time if nothing to do
            if (!serial.has_value() || !(state.has_value() || version.has_value())){
                continue;
            }

            // find pack data related to serial number
            for (auto [index, entry] : _stats->_packData){
                auto pack = _stats->getPackData(index);
                if (pack.has_value() && (*pack)->_serial == serial){
                    if (state.has_value()){
                        (*pack)->_state = static_cast<ZendureBatteryStats::State>(*state);
                    }

                    if (version.has_value()){
                        (*pack)->_fwversion = parseVersion(*version);
                    }

                    // we found the pack we searched for, so terminate loop here
                    break;
                }
            }
        }
    }
}

void ZendureBattery::onMqttMessageLog(espMqttClientTypes::MessageProperties const& properties,
        char const* topic, uint8_t const* payload, size_t len, size_t index, size_t total)
{
    auto ms = millis();

    std::string const src = std::string(reinterpret_cast<const char*>(payload), len);
    std::string logValue = src.substr(0, 64);
    if (src.length() > logValue.length()) { logValue += "..."; }

    auto log = [_verboseLogging=_verboseLogging](char const* format, auto&&... args) -> void {
        if (_verboseLogging) {
            MessageOutput.printf("ZendureBattery (Log): ");
            MessageOutput.printf(format, args...);
            MessageOutput.println();
        }
        return;
    };

    JsonDocument json;

    const DeserializationError error = deserializeJson(json, src);
    if (error) {
        return log("cannot parse payload '%s' as JSON", logValue.c_str());
    }

    if (json.overflowed()) {
        return log("payload too large to process as JSON");
    }

    auto obj = json.as<JsonObjectConst>();

    // validate input data
    // deviceId has to be set to the configured deviceId
    // logType has to be set to "2"
    if (!json["deviceId"].as<String>().equals(_deviceId)){
        return log("Invalid or missing 'deviceId' in '%s'", logValue.c_str());
    }
    if (!json["logType"].as<String>().equals("2")){
        return log("Invalid or missing 'v' in '%s'", logValue.c_str());
    }

    auto data = Utils::getJsonElement<JsonObjectConst>(obj, "log", 2);
    if (!data.has_value()){
        return log("Unable to find 'log' in '%s'", logValue.c_str());
    }

    _stats->setSerial(Utils::getJsonElement<String>(*data, ZENDURE_REPORT_SERIAL));

    auto params = Utils::getJsonElement<JsonArrayConst>(*data, "params", 1);
    if (!params.has_value()){
        return log("Unable to find 'params' in '%s'", logValue.c_str());
    }

    auto v = *params;

    uint8_t num = v[ZENDURE_LOG_OFFSET_PACKNUM].as<uint8_t>();
    if (num > 0 && num <= ZENDURE_MAX_PACKS){
        uint16_t soc = 0;
        uint16_t voltage = 0;
        int16_t power = 0;
        int16_t current = 0;
        uint32_t cellMin = UINT32_MAX;
        uint32_t cellMax = 0;
        uint32_t cellAvg = 0;
        uint32_t cellDelta = 0;
        int32_t cellTemp = 0;
        uint16_t capacity = 0;

        for (size_t i = 1 ; i <= num ; i++){
            auto pvol = v[ZENDURE_LOG_OFFSET_PACK_VOLTAGE(i)].as<uint16_t>() * 10;
            auto pcur = v[ZENDURE_LOG_OFFSET_PACK_CURRENT(i)].as<int16_t>();
            auto psoc = v[ZENDURE_LOG_OFFSET_PACK_SOC(i)].as<uint16_t>();

            auto ctmp = v[ZENDURE_LOG_OFFSET_PACK_TEMPERATURE(i)].as<int32_t>();
            auto cmin = v[ZENDURE_LOG_OFFSET_PACK_CELL_MIN(i)].as<uint32_t>() * 10;
            auto cmax = v[ZENDURE_LOG_OFFSET_PACK_CELL_MAX(i)].as<uint32_t>() * 10;
            auto cdel = cmax - cmin;

            auto pack = _stats->getPackData(i);
            if (pack.has_value()){
                auto cavg = pvol / (*pack)->getCellCount();

                (*pack)->_cell_voltage_min = static_cast<uint16_t>(cmin);
                (*pack)->_cell_voltage_max = static_cast<uint16_t>(cmax);
                (*pack)->_cell_voltage_avg = static_cast<uint16_t>(cavg);
                (*pack)->_cell_voltage_spread = static_cast<uint16_t>(cdel);
                (*pack)->_cell_temperature_max = static_cast<float>(ctmp);
                (*pack)->_current = static_cast<float>(pcur) / 10.0;
                (*pack)->_voltage_total = static_cast<float>(pvol) / 1000.0;
                (*pack)->_soc_level = static_cast<float>(psoc) / 10.0;
                (*pack)->_power = static_cast<int16_t>((*pack)->_current * (*pack)->_voltage_total);
                (*pack)->_lastUpdateTimestamp = ms;

                capacity += (*pack)->getCapacity();
                cellAvg += cavg;
                power += (*pack)->_power;
            }

            cellMin = min(cmin, cellMin);
            cellMax = max(cmax, cellMax);
            cellDelta = max(cdel, cellDelta);
            cellTemp = max(ctmp, cellTemp);

            soc += psoc;
            voltage += pvol;
            current += pcur;
        }

        _stats->_num_batteries = num;
        _stats->setSoC(static_cast<float>(soc) / 10.0 / num, 2, ms);
        //_stats->setVoltage(static_cast<float>(voltage) / 1000 / num, ms);
        _stats->setVoltage(v[ZENDURE_LOG_OFFSET_VOLTAGE].as<float>() / 10.0, ms);
        _stats->setCurrent(static_cast<float>(current) / 10.0, 1, ms);
        if (capacity){
            _stats->_capacity = capacity;
        }

        _stats->_auto_recover = static_cast<bool>(v[ZENDURE_LOG_OFFSET_AUTO_RECOVER].as<uint8_t>());
        _stats->_bypass_mode = static_cast<ZendureBatteryStats::BypassMode>(v[ZENDURE_LOG_OFFSET_BYPASS_MODE].as<uint8_t>());
        _stats->_soc_min = v[ZENDURE_LOG_OFFSET_MIN_SOC].as<float>();

        _stats->_solar_power_1 = v[ZENDURE_LOG_OFFSET_SOLAR_POWER_MPPT_1].as<uint16_t>();
        _stats->_solar_power_2 = v[ZENDURE_LOG_OFFSET_SOLAR_POWER_MPPT_2].as<uint16_t>();
        _stats->_input_power = _stats->_solar_power_1 + _stats->_solar_power_2;

        _stats->_output_limit = static_cast<uint16_t>(v[ZENDURE_LOG_OFFSET_OUTPUT_POWER_LIMIT].as<uint32_t>() / 100);
        //_stats->_input_power = v[ZENDURE_LOG_OFFSET_INPUT_POWER].as<uint16_t>();
        _stats->_output_power = v[ZENDURE_LOG_OFFSET_OUTPUT_POWER].as<uint16_t>();
        _stats->_charge_power = v[ZENDURE_LOG_OFFSET_CHARGE_POWER].as<uint16_t>();
        _stats->_discharge_power = v[ZENDURE_LOG_OFFSET_DISCHARGE_POWER].as<uint16_t>();

        _stats->_cellMinMilliVolt = static_cast<uint16_t>(cellMin);
        _stats->_cellMaxMilliVolt = static_cast<uint16_t>(cellMax);
        _stats->_cellAvgMilliVolt = static_cast<uint16_t>(cellAvg) / num;
        _stats->_cellDeltaMilliVolt = static_cast<uint16_t>(cellDelta);
        _stats->_cellTemperature = static_cast<float>(cellTemp);

        _stats->_lastUpdate = ms;

        calculateEfficiency();
    }
}

String ZendureBattery::parseVersion(uint32_t version) {
    uint8_t major = (version >> 12) & 0xF;
    uint8_t minor = (version >> 8) & 0xF;
    uint8_t bugfix = version & 0xFF;

    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%d.%d.%d", major, minor, bugfix);
    return String(buffer);
}

void ZendureBattery::calculateEfficiency() {
    float in = static_cast<float>(_stats->_input_power);
    float out = static_cast<float>(_stats->_output_power);
    float charge = static_cast<float>(_stats->_charge_power);
    float discharge = static_cast<float>(_stats->_discharge_power);
    float efficiency = 0.0;

    // Variante A
    // in += efficiency ? discharge / efficiency : 0.0;
    // out += charge * efficiency;

    // Variante B
    //auto _battery_power = charge ? charge : -discharge
    // if (_battery_power){
    //     efficiency = (charge - discharge) / static_cast<float>(_battery_power);
    // }

    // in += _battery_power < 0 ? static_cast<float>(-_battery_power) : 0.0;
    // out += _battery_power > 0 ?static_cast<float>( _battery_power) : 0.0;

    // Variante C
    in += discharge;
    out += charge;

    efficiency = in ? out / in : 0.0;

    if (efficiency <= 1 && efficiency >= 0){
        _stats->_efficiency = efficiency * 100;
    }
}
