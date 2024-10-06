// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <memory>
#include <mutex>
#include <TaskSchedulerDeclarations.h>

#include "BatteryStats.h"

class BatteryProvider {
public:
    // returns true if the provider is ready for use, false otherwise
    virtual bool init(bool verboseLogging) = 0;
    virtual void deinit() = 0;
    virtual void loop() = 0;
    virtual std::shared_ptr<BatteryStats> getStats() const = 0;
    virtual bool isSmartBattery() const = 0;
};

class TraditionalBatteryProvider : public BatteryProvider {
public:
    bool isSmartBattery() const { return false; }
};

class SmartBatteryProvider : public BatteryProvider {
public:
    bool isSmartBattery() const { return true; }

    virtual uint16_t getSolarPower() const = 0;
    virtual uint16_t getChargePower() const = 0;
    virtual uint16_t getDischargePower() const = 0;
    virtual uint16_t getBatteryPowerAvailable() const = 0;
    virtual uint16_t getOutputLimit() const = 0;

    virtual uint16_t increaseOutputLimit(const uint16_t limit) = 0;
    virtual uint16_t decreaseOutputLimit(const uint16_t limit) = 0;

    virtual bool isFull() const = 0;

    virtual uint16_t setOutputLimit(const uint16_t limit) = 0;
    virtual bool setBypass(const bool bypass) = 0;
};

class BatteryClass {
public:
    void init(Scheduler&);
    void updateSettings();

    float getDischargeCurrentLimit();

    std::shared_ptr<BatteryStats const> getStats() const;
    std::shared_ptr<BatteryProvider> getProvider() const;

private:
    void loop();

    Task _loopTask;
    mutable std::mutex _mutex;
    std::shared_ptr<BatteryProvider> _upProvider = nullptr;
};

extern BatteryClass Battery;
