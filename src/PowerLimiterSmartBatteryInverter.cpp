#include "MessageOutput.h"
#include "PowerLimiterSmartBatteryInverter.h"
#include "Battery.h"

PowerLimiterSmartBatteryInverter::PowerLimiterSmartBatteryInverter(bool verboseLogging, PowerLimiterInverterConfig const& config, std::shared_ptr<SmartBatteryProvider> battery)
    : PowerLimiterInverter(verboseLogging, config), _battery(battery) { }

uint16_t PowerLimiterSmartBatteryInverter::getMaxReductionWatts(bool) const
{
    if (!isReachable() || !isSendingCommandsEnabled()) { return 0; }

    auto low = std::min(getCurrentLimitWatts(), getCurrentOutputAcWatts());
    if (low <= _config.LowerPowerLimit) { return 0; }

    return getCurrentOutputAcWatts() - _config.LowerPowerLimit;
}

uint16_t PowerLimiterSmartBatteryInverter::getMaxIncreaseWatts() const
{
    if (!isReachable() || !isSendingCommandsEnabled()) { return 0; }

    // check if we could deliver more but are limited
    if (isLimited(getPowerByMppt(_spInverter->getMppts()))){
        return std::min(getConfiguredMaxPowerWatts() - getCurrentOutputAcWatts(), 100);
    }

    // if solar power is already exausted, try to redirect charge current from battery
    if (_battery->getChargePower())
    {
        return _battery->getChargePower();
    }

    // if this also does not fit, we try to discharge battery
    if (isDischargeAllowed())
    {
        return _battery->getBatteryPowerAvailable();
    }

    return 0;
}

uint16_t PowerLimiterSmartBatteryInverter::applyReduction(uint16_t reduction, bool)
{
    if (!isReachable() || !isSendingCommandsEnabled()) { return 0; }

    if (reduction == 0) { return 0; }

    auto baseline = getCurrentOutputAcWatts();
    auto direct_solar = getSolarPower().first;
    auto solar_power = direct_solar + _battery->getSolarPower();
    auto discharge_power = _battery->getDischargePower();

    if ((baseline - _config.LowerPowerLimit) >= reduction) {
        // discharging while harvesting solar power - reduce battery output
        if (discharge_power && solar_power > 10){
            _battery->decreaseOutputLimit(reduction);
        }

        setAcOutput(baseline - reduction);
        return reduction;
    }

    // can the request covered by direct panels?
    if (direct_solar >= _config.LowerPowerLimit){
        _battery->setOutputLimit(0);
    }

    setAcOutput(_config.LowerPowerLimit);
    return getCurrentOutputAcWatts() - _config.LowerPowerLimit;
}

uint16_t PowerLimiterSmartBatteryInverter::applyIncrease(uint16_t increase)
{
    if (!isReachable() || !isSendingCommandsEnabled()) { return 0; }

    if (increase == 0) { return 0; }

    // do not wake inverter up if it would produce too much power
    if (!isProducing() && _config.LowerPowerLimit > increase) { return 0; }

    // the limit for solar-powered inverters might be scaled, so we use the
    // current output as the baseline. solar-powered inverters in standby have
    // no output (baseline is zero).
    auto baseline = getCurrentOutputAcWatts();
    auto actualIncrease = 0;

    auto solar_power = getSolarPower().first + _battery->getSolarPower();

    // check if we could deliver more but are limited
    if (isLimited(getPowerByMppt(_spInverter->getMppts()))){
        actualIncrease = std::min(increase, getMaxIncreaseWatts());

        // discharge allowed and no solar power (maybe night or thunderstorm)
        // set battery to maximum output power
        if (isDischargeAllowed() && solar_power < 10) {
            _battery->setOutputLimit(1200);
        }
    } else {
        auto charge_power = _battery->getChargePower();

        // if solar power is already exausted, try to get power from battery
        if (charge_power >= increase || isDischargeAllowed()){
            actualIncrease = _battery->increaseOutputLimit(increase);
        }else{
            actualIncrease = _battery->increaseOutputLimit(charge_power);
        }
    }

    setAcOutput(baseline + actualIncrease);
    return actualIncrease;
}

uint16_t PowerLimiterSmartBatteryInverter::standby()
{
    // solar-powered inverters are never actually put into standby (by the
    // DPL), but only set to the configured lower power limit instead.
    setAcOutput(_config.LowerPowerLimit);
    return getCurrentOutputAcWatts() - _config.LowerPowerLimit;
}

uint16_t PowerLimiterSmartBatteryInverter::scaleLimit(uint16_t expectedOutputWatts)
{
    // prevent scaling if inverter is not producing, as input channels are not
    // producing energy and hence are detected as not-producing, causing
    // unreasonable scaling.
    if (!isProducing()) { return expectedOutputWatts; }

    auto pStats = _spInverter->Statistics();
    std::vector<ChannelNum_t> dcChnls = _spInverter->getChannels();
    std::vector<MpptNum_t> dcMppts = _spInverter->getMppts();
    size_t dcTotalChnls = dcChnls.size();
    size_t dcTotalMppts = dcMppts.size();

    // if there is only one MPPT available, there is nothing we can do
    if (dcTotalMppts <= 1) { return expectedOutputWatts; }

    // test for a reasonable power limit that allows us to assume that an input
    // channel with little energy is actually not producing, rather than
    // producing very little due to the very low limit.
    if (getCurrentLimitWatts() < dcTotalChnls * 10) { return expectedOutputWatts; }

    // overscalling allows us to compensate for shaded panels by increasing the
    // total power limit, if the inverter is solar powered.
    if (_config.UseOverscalingToCompensateShading) {
        auto inverterOutputAC = pStats->getChannelFieldValue(TYPE_AC, CH0, FLD_PAC);

        float inverterEfficiencyFactor = pStats->getChannelFieldValue(TYPE_INV, CH0, FLD_EFF);

        // fall back to hoymiles peak efficiency as per datasheet if inverter
        // is currently not producing (efficiency is zero in that case)
        inverterEfficiencyFactor = (inverterEfficiencyFactor > 0) ? inverterEfficiencyFactor/100 : 0.967;

        // 98% of the expected power is good enough
        auto expectedAcPowerPerMppt = (getCurrentLimitWatts() / dcTotalMppts) * 0.98;

        if (_verboseLogging) {
            MessageOutput.printf("%s expected AC power per mppt %f W\r\n",
                    _logPrefix, expectedAcPowerPerMppt);
        }

        size_t dcShadedMppts = 0;
        auto shadedChannelACPowerSum = 0.0;

        for (auto& m : dcMppts) {
            float mpptPowerAC = 0.0;
            std::vector<ChannelNum_t> mpptChnls = _spInverter->getChannelsByMppt(m);

            for (auto& c : mpptChnls) {
                mpptPowerAC += pStats->getChannelFieldValue(TYPE_DC, c, FLD_PDC) * inverterEfficiencyFactor;
            }

            if (mpptPowerAC < expectedAcPowerPerMppt) {
                dcShadedMppts++;
                shadedChannelACPowerSum += mpptPowerAC;
            }

            if (_verboseLogging) {
                MessageOutput.printf("%s mppt-%c AC power %f W\r\n",
                        _logPrefix, m + 'a', mpptPowerAC);
            }
        }

        // no shading or the shaded channels provide more power than what
        // we currently need.
        if (dcShadedMppts == 0 || shadedChannelACPowerSum >= expectedOutputWatts) {
            return expectedOutputWatts;
        }

        if (dcShadedMppts == dcTotalMppts) {
            // keep the currentLimit when:
            // - all channels are shaded
            // - currentLimit >= expectedOutputWatts
            // - we get the expected AC power or less and
            if (getCurrentLimitWatts() >= expectedOutputWatts &&
                    inverterOutputAC <= expectedOutputWatts) {
                if (_verboseLogging) {
                    MessageOutput.printf("%s all mppts are shaded, "
                            "keeping the current limit of %d W\r\n",
                            _logPrefix, getCurrentLimitWatts());
                }

                return getCurrentLimitWatts();

            } else {
                return expectedOutputWatts;
            }
        }

        size_t dcNonShadedMppts = dcTotalMppts - dcShadedMppts;
        uint16_t overScaledLimit = (expectedOutputWatts - shadedChannelACPowerSum) / dcNonShadedMppts * dcTotalMppts;

        if (overScaledLimit <= expectedOutputWatts) { return expectedOutputWatts; }

        if (_verboseLogging) {
            MessageOutput.printf("%s %d/%d mppts are shaded, scaling %d W\r\n",
                    _logPrefix, dcShadedMppts, dcTotalMppts, overScaledLimit);
        }

        return overScaledLimit;
    }

    size_t dcProdMppts = 0;
    for (auto& m : dcMppts) {
        float dcPowerMppt = 0.0;
        std::vector<ChannelNum_t> mpptChnls = _spInverter->getChannelsByMppt(m);

        for (auto& c : mpptChnls) {
            dcPowerMppt += pStats->getChannelFieldValue(TYPE_DC, c, FLD_PDC);
        }

        if (dcPowerMppt > 2.0 * mpptChnls.size()) {
            dcProdMppts++;
        }
    }

    if (dcProdMppts == 0 || dcProdMppts == dcTotalMppts) { return expectedOutputWatts; }

    uint16_t scaled = expectedOutputWatts / dcProdMppts * dcTotalMppts;
    MessageOutput.printf("%s %d/%d mppts are producing, scaling from %d to "
            "%d W\r\n", _logPrefix, dcProdMppts, dcTotalMppts,
            expectedOutputWatts, scaled);

    return scaled;
}

void PowerLimiterSmartBatteryInverter::setAcOutput(uint16_t expectedOutputWatts)
{
    setExpectedOutputAcWatts(expectedOutputWatts);
    setTargetPowerLimitWatts(scaleLimit(expectedOutputWatts));
    setTargetPowerState(true);
}

std::vector<MpptNum_t> PowerLimiterSmartBatteryInverter::getSolarPoweredMppts() const
{
    return { MPPT_A }; // ToDo: Add config
}

std::vector<MpptNum_t> PowerLimiterSmartBatteryInverter::getBatteryPoweredMppts() const
{
    return { MPPT_B, MPPT_C }; // ToDo: Add config
}

std::vector<uint16_t> PowerLimiterSmartBatteryInverter::getPowerByMppt(std::vector<MpptNum_t> mppts) const
{
    std::vector<uint16_t> l;
    auto inverterEfficiencyFactor = getInverterEfficiencyFactor();
    for (auto m : mppts)
    {
        uint16_t power = 0;
        for (auto c : _spInverter->getChannelsByMppt(m))
        {
            power += _spInverter->Statistics()->getChannelFieldValue(TYPE_DC, c, FLD_PDC) * inverterEfficiencyFactor;
        }
        l.push_back(power);
    }
    return l;
}

bool PowerLimiterSmartBatteryInverter::isLimited(std::vector<uint16_t> power) const
{
    auto limit = getLimitPerMppt() * 0.98;
    for (auto p : power)
    {
        if (p >= limit)
        {
            return true;
        }
    }
    return false;
}

std::pair<uint16_t, uint8_t> PowerLimiterSmartBatteryInverter::getSolarPower() const
{
    auto l = getPowerByMppt(getSolarPoweredMppts());
    uint16_t power = 0;
    for (auto p : l)
    {
        power += p;
    }
    return { power, l.size() };
}

std::pair<uint16_t, uint8_t> PowerLimiterSmartBatteryInverter::getBatteryPower() const
{
    auto l = getPowerByMppt(getBatteryPoweredMppts());
    uint16_t power = 0;
    for (auto p : l)
    {
        power += p;
    }
    return { power, l.size() };
}

uint16_t PowerLimiterSmartBatteryInverter::getLimitPerMppt() const
{
    return getCurrentLimitWatts() / _spInverter->getMppts().size();
}
