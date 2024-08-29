#pragma once

#include <optional>
#include "Battery.h"
#include <espMqttClient.h>


#define ZENDURE_HUB1200     "73bkTV"
#define ZENDURE_HUB2000     "A8yh63"
#define ZENDURE_AIO2400     "yWF7hV)"
#define ZENDURE_ACE1500     "8bM93H"
#define ZENDURE_HYPER2000   "ja72U0ha)"


class ZendureBattery : public BatteryProvider {
public:
    ZendureBattery() = default;

    bool init(bool verboseLogging) final;
    void deinit() final;
    void loop() final;
    std::shared_ptr<BatteryStats> getStats() const final { return _stats; }

    uint16_t updateOutputLimit(uint16_t limit);

protected:
    void timesync();

private:
    bool _verboseLogging = false;

    uint32_t _updateRateMs;
    unsigned long _nextUpdate;

    uint32_t _timesyncRateMs;
    unsigned long _nextTimesync;

    String _deviceId;

    String _baseTopic;
    String _reportTopic;
    String _readTopic;
    String _writeTopic;
    String _timesyncTopic;
    String _settingsPayload;
    std::shared_ptr<ZendureBatteryStats> _stats = std::make_shared<ZendureBatteryStats>();

    void onMqttMessageReport(espMqttClientTypes::MessageProperties const& properties,
            char const* topic, uint8_t const* payload, size_t len, size_t index, size_t total);
};
