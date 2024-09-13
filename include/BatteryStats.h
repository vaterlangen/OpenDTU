// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>

#include "AsyncJson.h"
#include "Arduino.h"
#include "JkBmsDataPoints.h"
#include "VeDirectShuntController.h"
#include <cfloat>

// mandatory interface for all kinds of batteries
class BatteryStats {
    public:
        String const& getManufacturer() const { return _manufacturer; }

        // the last time *any* data was updated
        uint32_t getAgeSeconds() const { return (millis() - _lastUpdate) / 1000; }
        bool updateAvailable(uint32_t since) const;

        uint8_t getSoC() const { return _soc; }
        uint32_t getSoCAgeSeconds() const { return (millis() - _lastUpdateSoC) / 1000; }
        uint8_t getSoCPrecision() const { return _socPrecision; }

        float getVoltage() const { return _voltage; }
        uint32_t getVoltageAgeSeconds() const { return (millis() - _lastUpdateVoltage) / 1000; }

        float getChargeCurrent() const { return _current; };
        uint8_t getChargeCurrentPrecision() const { return _currentPrecision; }

        // convert stats to JSON for web application live view
        virtual void getLiveViewData(JsonVariant& root) const;

        void mqttLoop();

        // the interval at which all battery data will be re-published, even
        // if they did not change. used to calculate Home Assistent expiration.
        virtual uint32_t getMqttFullPublishIntervalMs() const;

        bool isSoCValid() const { return _lastUpdateSoC > 0; }
        bool isVoltageValid() const { return _lastUpdateVoltage > 0; }
        bool isCurrentValid() const { return _lastUpdateCurrent > 0; }

        // returns true if the battery reached a critically low voltage/SoC,
        // such that it is in need of charging to prevent degredation.
        virtual bool getImmediateChargingRequest() const { return false; };

        virtual float getChargeCurrentLimitation() const { return FLT_MAX; };

    protected:
        virtual void mqttPublish() const;

        void setSoC(float soc, uint8_t precision, uint32_t timestamp) {
            _soc = soc;
            _socPrecision = precision;
            _lastUpdateSoC = _lastUpdate = timestamp;
        }

        void setVoltage(float voltage, uint32_t timestamp) {
            _voltage = voltage;
            _lastUpdateVoltage = _lastUpdate = timestamp;
        }

        void setCurrent(float current, uint8_t precision, uint32_t timestamp) {
            _current = current;
            _currentPrecision = precision;
            _lastUpdateCurrent = _lastUpdate = timestamp;
        }

        String _manufacturer = "unknown";
        String _hwversion = "";
        String _fwversion = "";
        String _serial = "";
        uint32_t _lastUpdate = 0;

    private:
        uint32_t _lastMqttPublish = 0;
        float _soc = 0;
        uint8_t _socPrecision = 0; // decimal places
        uint32_t _lastUpdateSoC = 0;
        float _voltage = 0; // total battery pack voltage
        uint32_t _lastUpdateVoltage = 0;

        // total current into (positive) or from (negative)
        // the battery, i.e., the charging current
        float _current = 0;
        uint8_t _currentPrecision = 0; // decimal places
        uint32_t _lastUpdateCurrent = 0;
};

class PylontechBatteryStats : public BatteryStats {
    friend class PylontechCanReceiver;

    public:
        void getLiveViewData(JsonVariant& root) const final;
        void mqttPublish() const final;
        bool getImmediateChargingRequest() const { return _chargeImmediately; } ;
        float getChargeCurrentLimitation() const { return _chargeCurrentLimitation; } ;

    private:
        void setManufacturer(String&& m) { _manufacturer = std::move(m); }
        void setLastUpdate(uint32_t ts) { _lastUpdate = ts; }

        float _chargeVoltage;
        float _chargeCurrentLimitation;
        float _dischargeCurrentLimitation;
        uint16_t _stateOfHealth;
        float _temperature;

        bool _alarmOverCurrentDischarge;
        bool _alarmOverCurrentCharge;
        bool _alarmUnderTemperature;
        bool _alarmOverTemperature;
        bool _alarmUnderVoltage;
        bool _alarmOverVoltage;
        bool _alarmBmsInternal;

        bool _warningHighCurrentDischarge;
        bool _warningHighCurrentCharge;
        bool _warningLowTemperature;
        bool _warningHighTemperature;
        bool _warningLowVoltage;
        bool _warningHighVoltage;
        bool _warningBmsInternal;

        bool _chargeEnabled;
        bool _dischargeEnabled;
        bool _chargeImmediately;
};

class PytesBatteryStats : public BatteryStats {
    friend class PytesCanReceiver;

    public:
        void getLiveViewData(JsonVariant& root) const final;
        void mqttPublish() const final;
        float getChargeCurrentLimitation() const { return _chargeCurrentLimit; } ;

    private:
        void setManufacturer(String&& m) { _manufacturer = std::move(m); }
        void setLastUpdate(uint32_t ts) { _lastUpdate = ts; }
        void updateSerial() {
            if (!_serialPart1.isEmpty() && !_serialPart2.isEmpty()) {
                _serial = _serialPart1 + _serialPart2;
            }
        }

        String _serialPart1 = "";
        String _serialPart2 = "";

        float _chargeVoltageLimit;
        float _chargeCurrentLimit;
        float _dischargeVoltageLimit;
        float _dischargeCurrentLimit;

        uint16_t _stateOfHealth;

        float _temperature;

        uint16_t _cellMinMilliVolt;
        uint16_t _cellMaxMilliVolt;
        float _cellMinTemperature;
        float _cellMaxTemperature;

        String _cellMinVoltageName;
        String _cellMaxVoltageName;
        String _cellMinTemperatureName;
        String _cellMaxTemperatureName;

        uint8_t _moduleCountOnline;
        uint8_t _moduleCountOffline;

        uint8_t _moduleCountBlockingCharge;
        uint8_t _moduleCountBlockingDischarge;

        uint16_t _totalCapacity;
        uint16_t _availableCapacity;

        float _chargedEnergy = -1;
        float _dischargedEnergy = -1;

        bool _alarmUnderVoltage;
        bool _alarmOverVoltage;
        bool _alarmOverCurrentCharge;
        bool _alarmOverCurrentDischarge;
        bool _alarmUnderTemperature;
        bool _alarmOverTemperature;
        bool _alarmUnderTemperatureCharge;
        bool _alarmOverTemperatureCharge;
        bool _alarmInternalFailure;
        bool _alarmCellImbalance;

        bool _warningLowVoltage;
        bool _warningHighVoltage;
        bool _warningHighChargeCurrent;
        bool _warningHighDischargeCurrent;
        bool _warningLowTemperature;
        bool _warningHighTemperature;
        bool _warningLowTemperatureCharge;
        bool _warningHighTemperatureCharge;
        bool _warningInternalFailure;
        bool _warningCellImbalance;
};

class JkBmsBatteryStats : public BatteryStats {
    public:
        void getLiveViewData(JsonVariant& root) const final {
            getJsonData(root, false);
        }

        void getInfoViewData(JsonVariant& root) const {
            getJsonData(root, true);
        }

        void mqttPublish() const final;

        uint32_t getMqttFullPublishIntervalMs() const final { return 60 * 1000; }

        void updateFrom(JkBms::DataPointContainer const& dp);

    private:
        void getJsonData(JsonVariant& root, bool verbose) const;

        JkBms::DataPointContainer _dataPoints;
        mutable uint32_t _lastMqttPublish = 0;
        mutable uint32_t _lastFullMqttPublish = 0;

        uint16_t _cellMinMilliVolt = 0;
        uint16_t _cellAvgMilliVolt = 0;
        uint16_t _cellMaxMilliVolt = 0;
        uint32_t _cellVoltageTimestamp = 0;
};

class VictronSmartShuntStats : public BatteryStats {
    public:
        void getLiveViewData(JsonVariant& root) const final;
        void mqttPublish() const final;

        void updateFrom(VeDirectShuntController::data_t const& shuntData);

    private:
        float _temperature;
        bool _tempPresent;
        uint8_t _chargeCycles;
        uint32_t _timeToGo;
        float _chargedEnergy;
        float _dischargedEnergy;
        int32_t _instantaneousPower;
        float _midpointVoltage;
        float _midpointDeviation;
        float _consumedAmpHours;
        int32_t _lastFullCharge;

        bool _alarmLowVoltage;
        bool _alarmHighVoltage;
        bool _alarmLowSOC;
        bool _alarmLowTemperature;
        bool _alarmHighTemperature;
};

class MqttBatteryStats : public BatteryStats {
    friend class MqttBattery;

    public:
        // since the source of information was MQTT in the first place,
        // we do NOT publish the same data under a different topic.
        void mqttPublish() const final { }

        // we don't need a card in the liveview, since the SoC and
        // voltage (if available) is already displayed at the top.
        void getLiveViewData(JsonVariant& root) const final { }
};

class ZendureBatteryStats : public BatteryStats {
    friend class ZendureBattery;

    enum class State : uint8_t {
        Idle        = 0,
        Charging    = 1,
        Discharging = 2
    };

    enum class BypassMode : uint8_t {
        Automatic   = 0,
        AlwaysOff   = 1,
        AlwaysOn    = 2
    };

    public:
        template <typename T>
        static T stateToString(State state){
            switch (state) {
                case State::Idle:
                    return "idle";
                case State::Charging:
                    return "charging";
                case State::Discharging:
                    return "discharging";
                default:
                    return "invalid";
            }
        }
        template <typename T>
        static T bypassModeToString(BypassMode state){
            switch (state) {
                case BypassMode::Automatic:
                    return "automatic";
                case BypassMode::AlwaysOff:
                    return "alwaysoff";
                case BypassMode::AlwaysOn:
                    return "alwayson";
                default:
                    return "invalid";
            }
        }
        inline static bool isDischarging(State state){
            return state == State::Discharging;
        }
        inline static bool isCharging(State state){
            return state == State::Charging;
        }

    class PackStats {
        friend class ZendureBatteryStats;
        friend class ZendureBattery;

        public:
            explicit PackStats(){ }
            explicit PackStats(String serial){ setSerial(serial); }
            virtual ~PackStats(){ }

            inline String getSerial() const { return _serial; }
            inline uint8_t getCellCount() const { return 15; }
            virtual uint16_t getCapacity() const { return 0; }
            virtual String getName() const { return "UNKOWN"; }

            static PackStats* fromSerial(String serial){
                if (serial.length() == 15) {
                    if (serial.startsWith("AO4H")){
                        return new AB1000(serial);
                    }
                    if (serial.startsWith("CO4H")){
                        return new AB2000(serial);
                    }
                    return new PackStats(serial);
                }
                return nullptr;
            };

        protected:
            void setSerial(String serial) { _serial = serial; }
            void setHwVersion(String version) { _hwversion = version; }
            void setFwVersion(String version) { _fwversion = version; }

        private:
            String _serial;

            String _fwversion;
            String _hwversion;

            uint16_t _cell_voltage_min;
            uint16_t _cell_voltage_max;
            uint16_t _cell_voltage_spread;
            uint16_t _cell_voltage_avg;
            float _cell_temperature_max;

            float _voltage_total;
            float _current;
            int16_t _power;
            float _soc_level;
            State _state;

            uint32_t _lastUpdateTimestamp = 0;

    };

    class AB1000 : public PackStats {
        public:
            explicit AB1000(String serial){ setSerial(serial); }
            virtual ~AB1000(){ }
            inline uint16_t getCapacity() const override { return 960; }
            inline String getName() const override { return "AB1000"; }
    };

    class AB2000 : public PackStats {
        public:
            explicit AB2000(String serial){ setSerial(serial); }
            virtual ~AB2000(){ }
            inline uint16_t getCapacity() const override { return 1920; }
            inline String getName() const override { return "AB2000"; }
    };

    public:
        virtual ~ZendureBatteryStats(){
            for (const auto [index, item] : _packData){
                delete item;
            }
            _packData.clear();
        }
        void mqttPublish() const;
        void getLiveViewData(JsonVariant& root) const;

    protected:
        std::optional<ZendureBatteryStats::PackStats*> getPackData(size_t index) const;
        std::optional<ZendureBatteryStats::PackStats*> addPackData(size_t index, String serial);

        uint16_t getCapacity() const { return _capacity; };
        uint16_t getAvailableCapacity() const { return getCapacity() * (static_cast<float>(_soc_max - _soc_min) / 100.0); };

    private:
        void setHwVersion(String version) { _hwversion = version; }
        void setFwVersion(String version) { _fwversion = version; }

        void setManufacture(const char* manufacture) {
            _manufacturer = String(manufacture);
            if (_device){
                _manufacturer += " " + _device;
            }
        }

        void setSerial(const char* serial) {
            _serial = String(serial);
        }
        void setSerial(String serial) {
            _serial = serial;
        }
        void setSerial(std::optional<String> serial) {
            if (serial.has_value()){
                setSerial(*serial);
            }
        }

        void setDevice(const char* device) {
            _device = String(device);
        }
        void setDevice(String device) {
            _device = device;
        }

        String _device;

        std::map<size_t, PackStats*> _packData = std::map<size_t, PackStats*>();

        float _cellTemperature;
        uint16_t _cellMinMilliVolt;
        uint16_t _cellMaxMilliVolt;
        uint16_t _cellDeltaMilliVolt;
        uint16_t _cellAvgMilliVolt;

        float _electricLevel;

        float _soc_max;
        float _soc_min;

        uint16_t _inverse_max;
        uint16_t _input_limit;
        uint16_t _output_limit;

        float _efficiency = 0.0;
        uint16_t _capacity;

        uint16_t _charge_power;
        uint16_t _discharge_power;
        uint16_t _output_power;
        uint16_t _input_power;
        uint16_t _solar_power_1;
        uint16_t _solar_power_2;

        int16_t _remain_out_time;
        int16_t _remain_in_time;

        uint32_t _swVersion;
        uint32_t _hwVersion;

        State _state;
        uint8_t _num_batteries;
        BypassMode _bypass_mode;
        bool _bypass_state;
        bool _auto_recover;
        bool _heat_state;
        bool _auto_shutdown;
        bool _buzzer;
};
