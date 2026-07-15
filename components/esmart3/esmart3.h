#pragma once

// SPDX-License-Identifier: GPL-2.0-only
//
// Composant ESPHome pour contrôleur de charge solaire eSmart3 (RS485, 9600 8N1).
// Protocole porté depuis la lib Joba_ESmart3 (Joachim Banzhaf, GPL V2)
// utilisée par le projet Home_Energy_Management
// (trames 0xAA, CRC 8 bits = négation de la somme).

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
#ifdef USE_SELECT
#include "esphome/components/select/select.h"
#endif
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif
#ifdef USE_BUTTON
#include "esphome/components/button/button.h"
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

  // Paramètres batterie (BatParam) - "Factory default" doc Joba_ESmart3 :
  // BulkVolt 14.4V, FloatVolt 13.7V, EqualizeChgVolt 14.6V, EqualizeChgTime 30min
  void set_bulk_voltage(float volts);
  void set_float_voltage(float volts);
  void set_equalize_voltage(float volts);
  void set_equalize_time(uint16_t minutes);
  void set_battery_type(uint8_t type);  // 0=Utilisateur,1=Plomb-acide,2=Gel,3=AGM

  // Paramètres de protection (ProParam) - "Factory default" doc :
  // LoadOvp 16V, LoadUvp 10.5V, BatOvp 16V, BatOvB(recover) 15V, BatUvp 10.5V, BatUvB(recover) 11V
  void set_load_ovp(float volts);
  void set_load_uvp(float volts);
  void set_battery_ovp(float volts);
  void set_battery_ovp_recover(float volts);
  void set_battery_uvp(float volts);
  void set_battery_uvp_recover(float volts);

  // Facteur d'échelle tension système (le contrôleur stocke les tensions de
  // config en base 12V ; ex. bulk_voltage lu=14.4 sur un système 24V => 28.8V
  // réel). mode : 0=Auto (détecté depuis la tension batterie), 1=12V, 2=24V,
  // 3=36V, 4=48V (facteur = mode directement en mode manuel).
  void set_system_voltage_mode(uint8_t mode);

  // Remet à zéro les compteurs d'énergie du contrôleur (item Log : Today/
  // Month/Total générés et consommés). Utile quand ces registres contiennent
  // des valeurs incohérentes (ex. compteurs "Total" non initialisés/corrompus).
  void reset_energy_stats();

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
  void set_system_voltage_sensor(sensor::Sensor *s) { this->system_voltage_sensor_ = s; }
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
  void set_bulk_voltage_number(number::Number *n) { this->bulk_voltage_number_ = n; }
  void set_float_voltage_number(number::Number *n) { this->float_voltage_number_ = n; }
  void set_equalize_voltage_number(number::Number *n) { this->equalize_voltage_number_ = n; }
  void set_equalize_time_number(number::Number *n) { this->equalize_time_number_ = n; }
  void set_load_ovp_number(number::Number *n) { this->load_ovp_number_ = n; }
  void set_load_uvp_number(number::Number *n) { this->load_uvp_number_ = n; }
  void set_battery_ovp_number(number::Number *n) { this->battery_ovp_number_ = n; }
  void set_battery_ovp_recover_number(number::Number *n) { this->battery_ovp_recover_number_ = n; }
  void set_battery_uvp_number(number::Number *n) { this->battery_uvp_number_ = n; }
  void set_battery_uvp_recover_number(number::Number *n) { this->battery_uvp_recover_number_ = n; }
#endif
#ifdef USE_SELECT
  void set_battery_type_select(select::Select *s) { this->battery_type_select_ = s; }
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

  // Identifie la donnée locale à rafraîchir/relire une fois l'écriture confirmée (ACK)
  enum PendingKind : uint8_t {
    PENDING_MAX_CHARGE_CURRENT = 1,
    PENDING_MAX_LOAD_CURRENT = 2,
    PENDING_LOAD_STATE = 3,
    PENDING_BULK_VOLTAGE = 4,
    PENDING_FLOAT_VOLTAGE = 5,
    PENDING_EQUALIZE_VOLTAGE = 6,
    PENDING_EQUALIZE_TIME = 7,
    PENDING_BATTERY_TYPE = 8,
    PENDING_LOAD_OVP = 9,
    PENDING_LOAD_UVP = 10,
    PENDING_BATTERY_OVP = 11,
    PENDING_BATTERY_OVP_RECOVER = 12,
    PENDING_BATTERY_UVP = 13,
    PENDING_BATTERY_UVP_RECOVER = 14,
    PENDING_RESET_ENERGY = 15,
  };
  struct PendingWrite {
    uint8_t item;
    uint8_t word_offset;
    uint16_t raw_value;
    uint8_t kind;
  };

  void send_frame_(uint8_t cmd, uint8_t item, const uint8_t *payload, size_t payload_len);
  void send_get_(uint8_t item, uint8_t start_word, uint8_t end_word);
  void send_set_word_(uint8_t item, uint8_t start_word, uint16_t value);
  void send_set_bytes_(uint8_t item, uint8_t start_word, const uint8_t *data, uint8_t data_len);
  void enqueue_write_(uint8_t item, uint8_t word_offset, uint16_t raw_value, uint8_t kind);
  bool pending_has_(uint8_t kind) const;
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
  uint8_t pending_write_kind_{0};
  uint32_t last_tx_{0};
  uint32_t last_frame_{0};
  uint8_t cycle_{0};
  uint8_t error_count_{0};
  bool online_{false};
  bool info_read_{false};
  bool force_batparam_{false};
  bool force_proparam_{false};
  bool force_log_{false};
  // Étape courante du reset énergie (-1 = inactif). Écritures séparées, une
  // par dword (voir try_send_pending_) : un unique SET de 32 octets laisse
  // les mots hauts de certains champs non modifiés sur ce contrôleur
  // (observé : Load*Eng affichant 0x10000 au lieu de 0 après un reset en bloc).
  int8_t reset_energy_step_{-1};
  uint8_t system_voltage_mode_{0};       // 0=Auto, 1..4 = 12V/24V/36V/48V
  float system_voltage_factor_{1.0f};    // multiplicateur appliqué aux tensions BatParam/ProParam

  // File des écritures en attente (appliquées dès que le bus est libre, une à la fois)
  std::vector<PendingWrite> pending_queue_;

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
  sensor::Sensor *system_voltage_sensor_{nullptr};
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
  number::Number *bulk_voltage_number_{nullptr};
  number::Number *float_voltage_number_{nullptr};
  number::Number *equalize_voltage_number_{nullptr};
  number::Number *equalize_time_number_{nullptr};
  number::Number *load_ovp_number_{nullptr};
  number::Number *load_uvp_number_{nullptr};
  number::Number *battery_ovp_number_{nullptr};
  number::Number *battery_ovp_recover_number_{nullptr};
  number::Number *battery_uvp_number_{nullptr};
  number::Number *battery_uvp_recover_number_{nullptr};
#endif
#ifdef USE_SELECT
  select::Select *battery_type_select_{nullptr};
#endif
#ifdef USE_SWITCH
  switch_::Switch *load_switch_{nullptr};
#endif
};

#ifdef USE_BUTTON
class ESmart3ResetEnergyButton : public button::Button, public Parented<ESmart3Component> {
 protected:
  void press_action() override { this->parent_->reset_energy_stats(); }
};
#endif

#ifdef USE_NUMBER
class ESmart3Number : public number::Number, public Parented<ESmart3Component> {
 public:
  // Correspond à ESmart3Component::PendingKind (1-14, hors LOAD_STATE géré par le switch)
  void set_purpose(uint8_t purpose) { this->purpose_ = purpose; }

 protected:
  void control(float value) override;
  uint8_t purpose_{1};
};
#endif

#ifdef USE_SELECT
class ESmart3BatteryTypeSelect : public select::Select, public Parented<ESmart3Component> {
 protected:
  void control(const std::string &value) override;
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
