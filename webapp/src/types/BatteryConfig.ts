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
    enable_discharge_current_limit: boolean;
    discharge_current_limit: number;
    use_battery_reported_discharge_current_limit: boolean;
    mqtt_discharge_current_topic: string;
    mqtt_discharge_current_json_path: string;
    mqtt_amperage_unit: number;
    zendure_device_type: number;
    zendure_device_id: string;
    zendure_polling_interval : number;
    zendure_soc_min: number;
    zendure_soc_max: number;
    zendure_bypass_mode: number;
    zendure_max_output: number;
    zendure_auto_shutdown : boolean;
    zendure_force_limit : boolean;
}
