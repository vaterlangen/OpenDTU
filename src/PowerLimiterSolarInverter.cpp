#include "MessageOutput.h"
#include "PowerLimiterSolarInverter.h"
#include "inverters/HMS_4CH.h"

PowerLimiterSolarInverter::PowerLimiterSolarInverter(bool verboseLogging, PowerLimiterInverterConfig const& config)
    : PowerLimiterInverter(verboseLogging, config) { }

uint16_t PowerLimiterSolarInverter::getMaxReductionWatts(bool) const
{
    if (!isReachable() || !isSendingCommandsEnabled()) { return 0; }

    auto low = std::min(getCurrentLimitWatts(), getCurrentOutputAcWatts());
    if (low <= _config.LowerPowerLimit) { return 0; }

    return getCurrentOutputAcWatts() - _config.LowerPowerLimit;
}

uint16_t PowerLimiterSolarInverter::getMaxIncreaseWatts() const
{
    if (!isReachable() || !isSendingCommandsEnabled()) { return 0; }

    // TODO(schlimmchen): left for the author of the scaling method: @AndreasBoehm
    return std::min(getConfiguredMaxPowerWatts() - getCurrentOutputAcWatts(), 100);
}

uint16_t PowerLimiterSolarInverter::applyReduction(uint16_t reduction, bool)
{
    if (!isReachable() || !isSendingCommandsEnabled()) { return 0; }

    if (reduction == 0) { return 0; }

    if ((getCurrentOutputAcWatts() - _config.LowerPowerLimit) >= reduction) {
        setAcOutput(getCurrentOutputAcWatts() - reduction);
        return reduction;
    }

    setAcOutput(_config.LowerPowerLimit);
    return getCurrentOutputAcWatts() - _config.LowerPowerLimit;
}

uint16_t PowerLimiterSolarInverter::applyIncrease(uint16_t increase)
{
    if (!isReachable() || !isSendingCommandsEnabled()) { return 0; }

    if (increase == 0) { return 0; }

    // do not wake inverter up if it would produce too much power
    if (!isProducing() && _config.LowerPowerLimit > increase) { return 0; }

    // the limit for solar-powered inverters might be scaled, so we use the
    // current output as the baseline. solar-powered inverters in standby have
    // no output (baseline is zero).
    auto baseline = getCurrentOutputAcWatts();

    auto actualIncrease = std::min(increase, getMaxIncreaseWatts());
    setAcOutput(baseline + actualIncrease);
    return actualIncrease;
}

uint16_t PowerLimiterSolarInverter::standby()
{
    // solar-powered inverters are never actually put into standby (by the
    // DPL), but only set to the configured lower power limit instead.
    setAcOutput(_config.LowerPowerLimit);
    return getCurrentOutputAcWatts() - _config.LowerPowerLimit;
}

uint16_t PowerLimiterSolarInverter::scaleLimit(uint16_t expectedOutputWatts)
{
    // prevent scaling if inverter is not producing, as input channels are not
    // producing energy and hence are detected as not-producing, causing
    // unreasonable scaling.
    if (!isProducing()) { return expectedOutputWatts; }

    auto pStats = _spInverter->Statistics();
    std::list<ChannelNum_t> dcChnls = pStats->getChannelsByType(TYPE_DC);
    std::list<MpptNum_t> dcMppts = pStats->getMppts();
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
            std::list<ChannelNum_t> mpptChnls = pStats->getChannelsByMppt(m);

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
        std::list<ChannelNum_t> mpptChnls = pStats->getChannelsByMppt(m);

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

void PowerLimiterSolarInverter::setAcOutput(uint16_t expectedOutputWatts)
{
    setExpectedOutputAcWatts(expectedOutputWatts);
    setTargetPowerLimitWatts(scaleLimit(expectedOutputWatts));
    setTargetPowerState(true);
}
