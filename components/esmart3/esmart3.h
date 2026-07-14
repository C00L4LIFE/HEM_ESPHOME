#pragma once

// Composant ESPHome pour contrôleur de charge solaire eSmart3 (RS485, 9600 8N1).
// Protocole porté depuis la lib Joba_ESmart3 utilisée par le projet
// Home_Energy_Management (trames 0xAA, CRC 8 bits = négation de la somme).

#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/components/uart/uart.h"

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#ifdef USE_NUMBER
#include "esphome/components/number/number.h"
#endif
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif

namespace esphome {
namespace esmart3 {

class ESmart3Component : public PollingComponent, public uart::UARTDevice {
 public:
  void setup() override;
  void update() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_flow_control_pin(GPIOPin *pin) { this->flow_control_pin_ = pin; }

  // API publique (utilisée par la protection batterie et les entités number/switch)
  void set_max_charge_current(float amps);
  void set_max_load_current(float amps);
  void set_load_state(bool on);
  bool is_online() const { return this->online_; }

#ifdef USE_SENSOR
  void set_pv_voltage_sensor(sensor::Sensor *s) { this->pv_voltage_sensor_ = s; }
  void set_pv_current_sensor(sensor::Sensor *s) { this->pv_current_sensor_ = s; }
  void set_pv_power_sensor(sensor::Sensor *s) { this->pv_power_sensor_ = s; }
  void set_battery_voltage_sensor(sensor::Sensor *s) { this->battery_voltage_sensor_ = s; }
  void set_charge_current_sensor(sensor::Sensor *s) { this->charge_current_sensor_ = s; }
  void set_charge_power_sensor(sensor::Sensor *s) { this->charge_power_sensor_ = s; }
  void set_battery_soc_sensor(sensor::Sensor *s) { this->battery_soc_sensor_ = s; }
  void set_battery_temperature_sensor(sensor::Sensor *s) { this->battery_temperature_sensor_ = s; }
  void set_controller_temperature_sensor(sensor::Sensor *s) { this->controller_temperature_sensor_ = s; }
  void set_load_voltage_sensor(sensor::Sensor *s) { this->load_voltage_sensor_ = s; }
  void set_load_current_sensor(sensor::Sensor *s) { this->load_current_sensor_ = s; }
  void set_load_power_sensor(sensor::Sensor *s) { this->load_power_sensor_ = s; }
  void set_co2_saved_sensor(sensor::Sensor *s) { this->co2_saved_sensor_ = s; }
  void set_fault_count_sensor(sensor::Sensor *s) { this->fault_count_sensor_ = s; }
  void set_fault_bitmask_sensor(sensor::Sensor *s) { this->fault_bitmask_sensor_ = s; }
  void set_energy_today_sensor(sensor::Sensor *s) { this->energy_today_sensor_ = s; }
  void set_energy_month_sensor(sensor::Sensor *s) { this->energy_month_sensor_ = s; }
  void set_energy_total_sensor(sensor::Sensor *s) { this->energy_total_sensor_ = s; }
  void set_load_energy_today_sensor(sensor::Sensor *s) { this->load_energy_today_sensor_ = s; }
  void set_load_energy_month_sensor(sensor::Sensor *s) { this->load_energy_month_sensor_ = s; }
  void set_load_energy_total_sensor(sensor::Sensor *s) { this->load_energy_total_sensor_ = s; }
  void set_bulk_voltage_sensor(sensor::Sensor *s) { this->bulk_voltage_sensor_ = s; }
  void set_float_voltage_sensor(sensor::Sensor *s) { this->float_voltage_sensor_ = s; }
  void set_equalize_voltage_sensor(sensor::Sensor *s) { this->equalize_voltage_sensor_ = s; }
  void set_max_charge_current_sensor(sensor::Sensor *s) { this->max_charge_current_sensor_ = s; }
  void set_max_discharge_current_sensor(sensor::Sensor *s) { this->max_discharge_current_sensor_ = s; }
  void set_battery_ovp_sensor(sensor::Sensor *s) { this->battery_ovp_sensor_ = s; }
  void set_battery_uvp_sensor(sensor::Sensor *s) { this->battery_uvp_sensor_ = s; }
#endif
#ifdef USE_BINARY_SENSOR
  void set_online_status_binary_sensor(binary_sensor::BinarySensor *b) { this->online_binary_sensor_ = b; }
  void set_fault_active_binary_sensor(binary_sensor::BinarySensor *b) { this->fault_active_binary_sensor_ = b; }
#endif
#ifdef USE_TEXT_SENSOR
  void set_charge_mode_text_sensor(text_sensor::TextSensor *t) { this->charge_mode_text_sensor_ = t; }
  void set_errors_text_sensor(text_sensor::TextSensor *t) { this->errors_text_sensor_ = t; }
  void set_model_text_sensor(text_sensor::TextSensor *t) { this->model_text_sensor_ = t; }
  void set_serial_number_text_sensor(text_sensor::TextSensor *t) { this->serial_number_text_sensor_ = t; }
  void set_firmware_text_sensor(text_sensor::TextSensor *t) { this->firmware_text_sensor_ = t; }
#endif
#ifdef USE_NUMBER
  void set_max_charge_current_number(number::Number *n) { this->max_charge_current_number_ = n; }
  void set_max_load_current_number(number::Number *n) { this->max_load_current_number_ = n; }
#endif
#ifdef USE_SWITCH
  void set_load_switch(switch_::Switch *s) { this->load_switch_ = s; }
#endif

 protected:
  // Items du protocole (ordre du document "MPPT Solar Controller communication protocol")
  enum Item : uint8_t {
    ITEM_CHGSTS = 0,
    ITEM_BATPARAM = 1,
    ITEM_LOG = 2,
    ITEM_PARAMETERS = 3,
    ITEM_LOADPARAM = 4,
    ITEM_CHGDEBUG = 5,
    ITEM_REMOTECONTROL = 6,
    ITEM_PROPARAM = 7,
    ITEM_INFORMATION = 8,
    ITEM_TEMPPARAM = 9,
    ITEM_ENGSAVE = 10,
  };
  enum Cmd : uint8_t { CMD_ACK = 0, CMD_GET = 1, CMD_SET = 2, CMD_SET_NO_RESP = 3, CMD_NACK = 4, CMD_EXEC = 5 };

  void send_frame_(uint8_t cmd, uint8_t item, const uint8_t *payload, size_t payload_len);
  void send_get_(uint8_t item, uint8_t start_word, uint8_t end_word);
  void send_set_word_(uint8_t item, uint8_t start_word, uint16_t value);
  bool try_send_pending_();
  void handle_frame_(const uint8_t *frame, size_t data_len);
  void on_success_();
  void on_failure_();

  void parse_chgsts_(const uint8_t *d, size_t len);
  void parse_batparam_(const uint8_t *d, size_t len);
  void parse_log_(const uint8_t *d, size_t len);
  void parse_proparam_(const uint8_t *d, size_t len);
  void parse_loadparam_(const uint8_t *d, size_t len);
  void parse_information_(const uint8_t *d, size_t len);

  static uint16_t word_(const uint8_t *d, size_t idx) {
    return (uint16_t) d[idx * 2] | ((uint16_t) d[idx * 2 + 1] << 8);
  }
  static int16_t sword_(const uint8_t *d, size_t idx) { return (int16_t) word_(d, idx); }
  static uint32_t dword_(const uint8_t *d, size_t idx) {
    return (uint32_t) word_(d, idx) | ((uint32_t) word_(d, idx + 1) << 16);
  }

  GPIOPin *flow_control_pin_{nullptr};
  std::vector<uint8_t> rx_;
  bool waiting_{false};
  bool expect_ack_{false};
  uint8_t expected_item_{0xff};
  uint8_t pending_write_kind_{0};  // 1=chg current, 2=load current, 3=load on/off
  uint32_t last_tx_{0};
  uint32_t last_frame_{0};
  uint8_t cycle_{0};
  uint8_t error_count_{0};
  bool online_{false};
  bool info_read_{false};
  bool force_batparam_{false};

  // Commandes en attente (appliquées dès que le bus est libre)
  bool pending_chg_{false};
  float pending_chg_val_{0};
  bool pending_loadcur_{false};
  float pending_loadcur_val_{0};
  bool pending_load_{false};
  bool pending_load_val_{false};

#ifdef USE_SENSOR
  sensor::Sensor *pv_voltage_sensor_{nullptr};
  sensor::Sensor *pv_current_sensor_{nullptr};
  sensor::Sensor *pv_power_sensor_{nullptr};
  sensor::Sensor *battery_voltage_sensor_{nullptr};
  sensor::Sensor *charge_current_sensor_{nullptr};
  sensor::Sensor *charge_power_sensor_{nullptr};
  sensor::Sensor *battery_soc_sensor_{nullptr};
  sensor::Sensor *battery_temperature_sensor_{nullptr};
  sensor::Sensor *controller_temperature_sensor_{nullptr};
  sensor::Sensor *load_voltage_sensor_{nullptr};
  sensor::Sensor *load_current_sensor_{nullptr};
  sensor::Sensor *load_power_sensor_{nullptr};
  sensor::Sensor *co2_saved_sensor_{nullptr};
  sensor::Sensor *fault_count_sensor_{nullptr};
  sensor::Sensor *fault_bitmask_sensor_{nullptr};
  sensor::Sensor *energy_today_sensor_{nullptr};
  sensor::Sensor *energy_month_sensor_{nullptr};
  sensor::Sensor *energy_total_sensor_{nullptr};
  sensor::Sensor *load_energy_today_sensor_{nullptr};
  sensor::Sensor *load_energy_month_sensor_{nullptr};
  sensor::Sensor *load_energy_total_sensor_{nullptr};
  sensor::Sensor *bulk_voltage_sensor_{nullptr};
  sensor::Sensor *float_voltage_sensor_{nullptr};
  sensor::Sensor *equalize_voltage_sensor_{nullptr};
  sensor::Sensor *max_charge_current_sensor_{nullptr};
  sensor::Sensor *max_discharge_current_sensor_{nullptr};
  sensor::Sensor *battery_ovp_sensor_{nullptr};
  sensor::Sensor *battery_uvp_sensor_{nullptr};
#endif
#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *online_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *fault_active_binary_sensor_{nullptr};
#endif
#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *charge_mode_text_sensor_{nullptr};
  text_sensor::TextSensor *errors_text_sensor_{nullptr};
  text_sensor::TextSensor *model_text_sensor_{nullptr};
  text_sensor::TextSensor *serial_number_text_sensor_{nullptr};
  text_sensor::TextSensor *firmware_text_sensor_{nullptr};
#endif
#ifdef USE_NUMBER
  number::Number *max_charge_current_number_{nullptr};
  number::Number *max_load_current_number_{nullptr};
#endif
#ifdef USE_SWITCH
  switch_::Switch *load_switch_{nullptr};
#endif
};

#ifdef USE_NUMBER
class ESmart3Number : public number::Number, public Parented<ESmart3Component> {
 public:
  // 1 = courant de charge max (wMaxChgCurr), 2 = courant load max (wMaxDisChgCurr)
  void set_purpose(uint8_t purpose) { this->purpose_ = purpose; }

 protected:
  void control(float value) override;
  uint8_t purpose_{1};
};
#endif

#ifdef USE_SWITCH
class ESmart3LoadSwitch : public switch_::Switch, public Parented<ESmart3Component> {
 protected:
  void write_state(bool state) override;
};
#endif

}  // namespace esmart3
}  // namespace esphome
