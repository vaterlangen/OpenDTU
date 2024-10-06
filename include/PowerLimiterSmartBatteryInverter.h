// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "PowerLimiterInverter.h"
#include "Battery.h"

class PowerLimiterSmartBatteryInverter : public PowerLimiterInverter {
public:
    PowerLimiterSmartBatteryInverter(bool verboseLogging, PowerLimiterInverterConfig const& config, std::shared_ptr<SmartBatteryProvider> battery);

    uint16_t getMaxReductionWatts(bool allowStandby) const final;
    uint16_t getMaxIncreaseWatts() const final;
    uint16_t applyReduction(uint16_t reduction, bool allowStandby) final;
    uint16_t applyIncrease(uint16_t increase) final;
    uint16_t standby() final;
    bool isSolarPowered() const final { return false; }
    bool isSmartBatteryPowered() const final { return true; }

protected:
    std::pair<uint16_t, uint8_t> getSolarPower() const;
    std::pair<uint16_t, uint8_t> getBatteryPower() const;
    uint16_t getLimitPerMppt() const;

private:
    uint16_t scaleLimit(uint16_t expectedOutputWatts);
    void setAcOutput(uint16_t expectedOutputWatts) final;

    bool isDischargeAllowed() const { return true; } // ToDo: Do some logic here

    std::vector<MpptNum_t> getSolarPoweredMppts() const;
    std::vector<MpptNum_t> getBatteryPoweredMppts() const;

    std::vector<uint16_t> getPowerByMppt(std::vector<MpptNum_t> mppts) const;
    bool isLimited(std::vector<uint16_t> power) const;

    std::shared_ptr<SmartBatteryProvider> _battery;
};
