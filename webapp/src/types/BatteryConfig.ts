export interface BatteryConfig {
    enabled: boolean;
    verbose_logging: boolean;
    provider: number;
    jkbms_interface: number;
    jkbms_polling_interval: number;
    mqtt_soc_topic: string;
    mqtt_soc_json_path: string;
    mqtt_voltage_topic: string;
    mqtt_voltage_json_path: string;
    mqtt_voltage_unit: number;
    zendure_device_type: number;
    zendure_device_serial: string;
    zendure_soc_min: number;
    zendure_soc_max: number;
    zendure_bypass_mode: number;
    zendure_max_output: number;
}
