// SPDX-License-Identifier: GPL-2.0-or-later
#include <solarcharger/smartbufferbatteries/Provider.h>
#include <battery/Controller.h>
#include <battery/zendure/Stats.h>

namespace SolarChargers::SmartBufferBatteries {

bool Provider::init(bool verboseLogging)
{
    _verboseLogging = verboseLogging;
    return true;
}

} // namespace SolarChargers::SmartBufferBatteries
