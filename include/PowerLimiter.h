// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "Configuration.h"
#include "PowerLimiterInverter.h"
#include <espMqttClient.h>
#include <Arduino.h>
#include <deque>
#include <memory>
#include <functional>
#include <optional>
#include <TaskSchedulerDeclarations.h>
#include <frozen/string.h>

#define PL_UI_STATE_INACTIVE 0
#define PL_UI_STATE_CHARGING 1
#define PL_UI_STATE_USE_SOLAR_ONLY 2
#define PL_UI_STATE_USE_SOLAR_AND_BATTERY 3

class PowerLimiterClass {
public:
    PowerLimiterClass() = default;

    enum class Status : unsigned {
        Initializing,
        DisabledByConfig,
        DisabledByMqtt,
        WaitingForValidTimestamp,
        PowerMeterPending,
        InverterInvalid,
        SingleSolarPoweredInverter,
        InverterCmdPending,
        InverterRemoval,
        InverterStatsPending,
        FullSolarPassthrough,
        UnconditionalSolarPassthrough,
        NoVeDirect,
        NoEnergy,
        HuaweiPsu,
        Stable,
    };

    void init(Scheduler& scheduler);
    uint8_t getInverterUpdateTimeouts() const;
    uint8_t getPowerLimiterState();
    int32_t getInverterOutput() { return _lastExpectedInverterOutput; }
    bool getFullSolarPassThroughEnabled() const { return _fullSolarPassThroughEnabled; }

    enum class Mode : unsigned {
        Normal = 0,
        Disabled = 1,
        UnconditionalFullSolarPassthrough = 2
    };

    void setMode(Mode m) { _mode = m; }
    Mode getMode() const { return _mode; }
    bool usesBatteryPoweredInverter();
    bool isManagedInverterProducing();
    void calcNextInverterRestart();

private:
    void loop();

    Task _loopTask;

    uint16_t _lastExpectedInverterOutput = 0;
    Status _lastStatus = Status::Initializing;
    uint32_t _lastStatusPrinted = 0;
    uint32_t _lastCalculation = 0;
    static constexpr uint32_t _calculationBackoffMsDefault = 128;
    uint32_t _calculationBackoffMs = _calculationBackoffMsDefault;
    Mode _mode = Mode::Normal;

    std::deque<PowerLimiterInverter> _inverters;
    bool _batteryDischargeEnabled = false;
    bool _nighttimeDischarging = false;
    uint32_t _nextInverterRestart = 0; // Values: 0->not calculated / 1->no restart configured / >1->time of next inverter restart in millis()
    uint32_t _nextCalculateCheck = 5000; // time in millis for next NTP check to calulate restart
    bool _fullSolarPassThroughEnabled = false;
    bool _verboseLogging = true;

    frozen::string const& getStatusText(Status status);
    void announceStatus(Status status);
    bool shutdown(Status status);
    float getBatteryVoltage(bool log = false);
    uint16_t solarDcToInverterAc(uint16_t dcPower);
    void fullSolarPassthrough(PowerLimiterClass::Status reason);
    int16_t calcHouseholdConsumption();
    using inverter_filter_t = std::function<bool(PowerLimiterInverter const&)>;
    uint16_t updateInverterLimits(uint16_t powerRequested, inverter_filter_t filter, std::string const& filterExpression);
    uint16_t calcBatteryAllowance(uint16_t powerRequested);
    bool updateInverters();
    uint16_t getSolarPassthroughPower();
    float getBatteryInvertersOutputAcWatts();
    int32_t getBatteryDischargeLimit();
    float getLoadCorrectedVoltage();
    bool testThreshold(float socThreshold, float voltThreshold,
            std::function<bool(float, float)> compare);
    bool isStartThresholdReached();
    bool isStopThresholdReached();
    bool isBelowStopThreshold();
    bool isFullSolarPassthroughActive();
};

extern PowerLimiterClass PowerLimiter;
