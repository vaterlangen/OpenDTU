// SPDX-License-Identifier: GPL-2.0-or-later
#include <solarcharger/smartbufferbatteries/Stats.h>
#include <battery/Controller.h>
#include <battery/zendure/Stats.h>

namespace SolarChargers::SmartBufferBatteries {

std::optional<float> Stats::getOutputPowerWatts() const
{
    float sum = 0;
    for (const auto& [key, device] : _deviceData) {
        for (const auto& [num, mppt] : device->_mpptData) {
            sum += mppt->_power;
        }
    }
    return getValueIfNotOutdated(_lastUpdateOutputPowerWatts, sum);
}

std::optional<float> Stats::getOutputVoltage() const
{
    float minimum = INFINITY;
    for (const auto& [key, device] : _deviceData) {
        for (const auto& [num, mppt] : device->_mpptData) {
            minimum = min(minimum, mppt->_voltage);
        }
    }

    if (minimum == INFINITY) {
        return std::nullopt;
    }

    return getValueIfNotOutdated(_lastUpdateOutputVoltage, minimum);
}

std::optional<float> Stats::getValueIfNotOutdated(const uint32_t lastUpdate, const float value) const {
    // never updated or older than 60 seconds
    if (lastUpdate == 0
        || millis() - lastUpdate > 60 * 1000) {
        return std::nullopt;
    }

    return value;
}

void Stats::getLiveViewData(JsonVariant& root, const boolean fullUpdate, const uint32_t lastPublish) const
{
    ::SolarChargers::Stats::getLiveViewData(root, fullUpdate, lastPublish);

    auto age = millis() - _lastUpdate;

    auto hasUpdate = _lastUpdate > 0 && age < millis() - lastPublish;
    if (!fullUpdate && !hasUpdate) { return; }

    for (const auto& [device, deviceData] : _deviceData) {
        auto dev = deviceData->_manufacture + " " + deviceData->_device;
        auto devage = millis() - deviceData->_lastUpdate;

        const JsonObject instance = root["solarcharger"]["instances"][dev.c_str()].to<JsonObject>();
        instance["data_age_ms"] = devage;
        instance["hide_serial"] = true;
        instance["product_id"] = dev;

        for (const auto& [mppt, mpptData] : deviceData->_mpptData) {
            auto name = String("mppt" + String(mppt));
            const JsonObject output = instance["values"][name.c_str()].to<JsonObject>();
            output["Power"]["v"] = mpptData->_power;
            output["Power"]["u"] = "W";
            output["Power"]["d"] = 1;
            output["Voltage"]["v"] = mpptData->_voltage;
            output["Voltage"]["u"] = "V";
            output["Voltage"]["d"] = 1;
        }
    }
}

void Stats::setMpptVoltage(const uint32_t id, const size_t num, const float voltage, const uint32_t lastUpdate) {
    std::shared_ptr<DeviceData> device;
    try
    {
        device = _deviceData.at(id);
        device->setMpptData(num, lastUpdate, std::nullopt, voltage);
        _lastUpdate = lastUpdate;
        _lastUpdateOutputVoltage = lastUpdate;
    }
    catch(const std::out_of_range& ex)
    {
        return;
    }
}

void Stats::setMpptPower(const uint32_t id, const size_t num, const float power, const uint32_t lastUpdate) {
    std::shared_ptr<DeviceData> device;
    try
    {
        device = _deviceData.at(id);
        device->setMpptData(num, lastUpdate, power, std::nullopt);
        _lastUpdate = lastUpdate;
        _lastUpdateOutputPowerWatts = lastUpdate;
    }
    catch(const std::out_of_range& ex)
    {
        return;
    }
}

DeviceData::DeviceData(const String& manufacture, const String& device, const size_t numMppts /* = 0 */)
    : _manufacture(manufacture)
    , _device(device)
    , _numMppts(numMppts) { }


void DeviceData::setMpptData(const size_t num, const uint32_t lastUpdate, const std::optional<float> power, std::optional<float> voltage) {
    if (num == 0 || num > _numMppts) {
        return;
    }

    if (!power && !voltage) {
        return;
    }

    std::shared_ptr<MpptData> mppt;
    try
    {
        mppt = _mpptData.at(num);
    }
    catch(const std::out_of_range& ex)
    {
        mppt = std::make_shared<MpptData>();
        _mpptData[num] = mppt;
    }

    _lastUpdate = lastUpdate;
    mppt->_lastUpdate = lastUpdate;

    if (power.has_value()) {
        mppt->_power = *power;
    }

    if (voltage.has_value()) {
        mppt->_voltage = *voltage;
    }

}

uint32_t Stats::addDevice(const String& manufacture, const String& name, const size_t numMppts) {
    _deviceData[_nextIndex] = std::make_shared<DeviceData>(manufacture, name, numMppts);

    return _nextIndex++;
}

}; // namespace SolarChargers::SmartBufferBatteries
