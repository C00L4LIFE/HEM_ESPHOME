#include "esmart3.h"
#include "esphome/core/log.h"

namespace esphome {
namespace esmart3 {

static const char *const TAG = "esmart3";

// Tailles des structures en mots de 16 bits (protocole Feb 2018)
static const uint8_t CHGSTS_WORDS = 16;
static const uint8_t BATPARAM_WORDS = 11;
static const uint8_t LOG_WORDS = 25;
static const uint8_t PROPARAM_WORDS = 8;
static const uint8_t LOADPARAM_WORDS = 18;
static const uint8_t INFORMATION_WORDS = 20;

static const uint32_t RESPONSE_TIMEOUT_MS = 300;
static const uint32_t INTER_FRAME_DELAY_MS = 15;  // la lib d'origine utilise 12 ms
static const uint8_t MAX_ERRORS_BEFORE_OFFLINE = 5;

// CRC 8 bits du protocole : négation de la somme de tous les octets
static uint8_t crc_sum(const uint8_t *data, size_t len, uint8_t crc = 0) {
  while (len--)
    crc -= *(data++);
  return crc;
}

void ESmart3Component::setup() {
  if (this->flow_control_pin_ != nullptr) {
    this->flow_control_pin_->setup();
    this->flow_control_pin_->digital_write(false);  // réception par défaut
  }
  this->rx_.reserve(160);
}

void ESmart3Component::dump_config() {
  ESP_LOGCONFIG(TAG, "eSmart3:");
  LOG_PIN("  Flow control pin: ", this->flow_control_pin_);
  LOG_UPDATE_INTERVAL(this);
  this->check_uart_settings(9600);
}

//=============================================================================
// ENVOI DES TRAMES
//=============================================================================

void ESmart3Component::send_frame_(uint8_t cmd, uint8_t item, const uint8_t *payload, size_t payload_len) {
  uint8_t header[6] = {0xaa, 0x01 /*MPPT*/, 0x00 /*BROADCAST*/, cmd, item, (uint8_t) payload_len};
  uint8_t crc = crc_sum(header, sizeof(header));
  crc = crc_sum(payload, payload_len, crc);

  // Vider le buffer de réception avant d'émettre
  uint8_t discard;
  while (this->available())
    this->read_byte(&discard);
  this->rx_.clear();

  if (this->flow_control_pin_ != nullptr)
    this->flow_control_pin_->digital_write(true);  // émission

  this->write_array(header, sizeof(header));
  this->write_array(payload, payload_len);
  this->write_array(&crc, 1);
  this->flush();

  if (this->flow_control_pin_ != nullptr)
    this->flow_control_pin_->digital_write(false);  // réception

  this->waiting_ = true;
  this->last_tx_ = millis();
}

void ESmart3Component::send_get_(uint8_t item, uint8_t start_word, uint8_t end_word) {
  uint8_t payload[3] = {start_word, 0, (uint8_t) ((end_word - start_word) * 2)};
  this->expect_ack_ = false;
  this->expected_item_ = item;
  this->send_frame_(CMD_GET, item, payload, sizeof(payload));
}

void ESmart3Component::send_set_word_(uint8_t item, uint8_t start_word, uint16_t value) {
  uint8_t payload[4] = {start_word, 0, (uint8_t) (value & 0xff), (uint8_t) (value >> 8)};
  this->expect_ack_ = true;
  this->expected_item_ = item;
  this->send_frame_(CMD_SET, item, payload, sizeof(payload));
}

bool ESmart3Component::try_send_pending_() {
  if (this->pending_chg_) {
    uint16_t deci = (uint16_t) (this->pending_chg_val_ * 10.0f);
    this->pending_write_kind_ = 1;
    this->send_set_word_(ITEM_BATPARAM, 0x05, deci);  // wMaxChgCurr
    return true;
  }
  if (this->pending_loadcur_) {
    uint16_t deci = (uint16_t) (this->pending_loadcur_val_ * 10.0f);
    this->pending_write_kind_ = 2;
    this->send_set_word_(ITEM_BATPARAM, 0x06, deci);  // wMaxDisChgCurr
    return true;
  }
  if (this->pending_load_) {
    this->pending_write_kind_ = 3;
    this->send_set_word_(ITEM_LOADPARAM, 0x01, this->pending_load_val_ ? 5117 : 5118);
    return true;
  }
  return false;
}

//=============================================================================
// POLLING (rotation identique au projet d'origine : ChgSts un cycle sur deux)
//=============================================================================

void ESmart3Component::update() {
  if (this->waiting_)
    return;  // transaction en cours, le timeout est géré dans loop()

  if (!this->info_read_) {
    this->send_get_(ITEM_INFORMATION, 0, INFORMATION_WORDS);
    return;
  }
  if (this->try_send_pending_())
    return;
  if (this->force_batparam_) {
    this->force_batparam_ = false;
    this->send_get_(ITEM_BATPARAM, 0, BATPARAM_WORDS);
    return;
  }

  if ((this->cycle_ % 2) == 0) {
    this->send_get_(ITEM_CHGSTS, 0, CHGSTS_WORDS);
  } else {
    switch (this->cycle_) {
      case 1:
        this->send_get_(ITEM_LOG, 0, LOG_WORDS);
        break;
      case 3:
        this->send_get_(ITEM_BATPARAM, 0, BATPARAM_WORDS);
        break;
      case 5:
        this->send_get_(ITEM_PROPARAM, 0, PROPARAM_WORDS);
        break;
      case 7:
        this->send_get_(ITEM_LOADPARAM, 0, LOADPARAM_WORDS);
        break;
    }
  }
  this->cycle_ = (this->cycle_ + 1) % 8;
}

void ESmart3Component::loop() {
  // Envoi rapide des écritures en attente (sans attendre le prochain update)
  if (!this->waiting_ && (millis() - this->last_frame_) >= INTER_FRAME_DELAY_MS) {
    if (this->info_read_ && this->try_send_pending_())
      return;
  }

  if (!this->waiting_)
    return;

  while (this->available()) {
    uint8_t b;
    this->read_byte(&b);
    if (this->rx_.empty() && b != 0xaa)
      continue;  // resynchronisation sur l'octet de start
    this->rx_.push_back(b);
  }

  if (this->rx_.size() >= 6) {
    uint8_t len = this->rx_[5];
    if (len > 120) {
      ESP_LOGW(TAG, "Longueur invalide (%u), trame ignorée", len);
      this->on_failure_();
      return;
    }
    size_t total = 6 + len + 1;
    if (this->rx_.size() >= total) {
      uint8_t crc = crc_sum(this->rx_.data(), 6 + len);
      if (crc == this->rx_[6 + len]) {
        this->handle_frame_(this->rx_.data(), len);
        this->on_success_();
      } else {
        ESP_LOGW(TAG, "CRC invalide (calculé 0x%02X, reçu 0x%02X)", crc, this->rx_[6 + len]);
        this->on_failure_();
      }
      return;
    }
  }

  if (millis() - this->last_tx_ > RESPONSE_TIMEOUT_MS) {
    ESP_LOGW(TAG, "Timeout item %u (cmd %s)", this->expected_item_, this->expect_ack_ ? "SET" : "GET");
    this->on_failure_();
  }
}

void ESmart3Component::on_success_() {
  this->waiting_ = false;
  this->last_frame_ = millis();
  this->error_count_ = 0;
  if (!this->online_) {
    this->online_ = true;
#ifdef USE_BINARY_SENSOR
    if (this->online_binary_sensor_ != nullptr)
      this->online_binary_sensor_->publish_state(true);
#endif
  }
}

void ESmart3Component::on_failure_() {
  this->waiting_ = false;
  this->last_frame_ = millis();
  this->rx_.clear();
  if (this->error_count_ < 255)
    this->error_count_++;
  if (this->error_count_ >= MAX_ERRORS_BEFORE_OFFLINE && this->online_) {
    this->online_ = false;
    ESP_LOGE(TAG, "eSmart3 injoignable (%u erreurs consécutives)", this->error_count_);
#ifdef USE_BINARY_SENSOR
    if (this->online_binary_sensor_ != nullptr)
      this->online_binary_sensor_->publish_state(false);
#endif
  }
}

//=============================================================================
// TRAITEMENT DES RÉPONSES
//=============================================================================

void ESmart3Component::handle_frame_(const uint8_t *frame, size_t len) {
  uint8_t command = frame[3];
  uint8_t item = frame[4];

  if (this->expect_ack_) {
    bool ok = (command == CMD_ACK);
    switch (this->pending_write_kind_) {
      case 1:
        if (ok) {
          this->pending_chg_ = false;
          this->force_batparam_ = true;
          ESP_LOGI(TAG, "Courant de charge max appliqué: %.1f A", this->pending_chg_val_);
        }
        break;
      case 2:
        if (ok) {
          this->pending_loadcur_ = false;
          this->force_batparam_ = true;
          ESP_LOGI(TAG, "Courant load max appliqué: %.1f A", this->pending_loadcur_val_);
        }
        break;
      case 3:
        if (ok) {
          this->pending_load_ = false;
          ESP_LOGI(TAG, "Load %s", this->pending_load_val_ ? "ON" : "OFF");
        }
        break;
    }
    if (!ok)
      ESP_LOGW(TAG, "SET refusé (NACK), nouvelle tentative au prochain cycle");
    this->pending_write_kind_ = 0;
    return;
  }

  if (len < 2)
    return;  // pas de données
  const uint8_t *data = frame + 8;  // 6 octets header + 2 octets offset
  size_t data_len = len - 2;

  switch (item) {
    case ITEM_CHGSTS:
      this->parse_chgsts_(data, data_len);
      break;
    case ITEM_BATPARAM:
      this->parse_batparam_(data, data_len);
      break;
    case ITEM_LOG:
      this->parse_log_(data, data_len);
      break;
    case ITEM_PROPARAM:
      this->parse_proparam_(data, data_len);
      break;
    case ITEM_LOADPARAM:
      this->parse_loadparam_(data, data_len);
      break;
    case ITEM_INFORMATION:
      this->parse_information_(data, data_len);
      break;
    default:
      ESP_LOGD(TAG, "Item %u non géré", item);
      break;
  }
}

void ESmart3Component::parse_chgsts_(const uint8_t *d, size_t len) {
  if (len < CHGSTS_WORDS * 2) {
    ESP_LOGW(TAG, "ChgSts trop court (%u octets)", (unsigned) len);
    return;
  }
  uint16_t chg_mode = word_(d, 0);
  float pv_volt = word_(d, 1) / 10.0f;
  float bat_volt = word_(d, 2) / 10.0f;
  float chg_curr = word_(d, 3) / 10.0f;
  float load_volt = word_(d, 5) / 10.0f;
  float load_curr = word_(d, 6) / 10.0f;
  float chg_power = word_(d, 7);
  float load_power = word_(d, 8);
  float bat_temp = sword_(d, 9) / 10.0f;
  float inner_temp = sword_(d, 10) / 10.0f;
  uint16_t bat_cap = word_(d, 11);
  float co2 = dword_(d, 12) / 1000.0f;
  uint16_t fault = word_(d, 14);

#ifdef USE_SENSOR
  if (this->pv_voltage_sensor_ != nullptr)
    this->pv_voltage_sensor_->publish_state(pv_volt);
  if (this->pv_current_sensor_ != nullptr)
    this->pv_current_sensor_->publish_state(pv_volt > 0 ? chg_power / pv_volt : 0.0f);
  if (this->pv_power_sensor_ != nullptr)
    this->pv_power_sensor_->publish_state(chg_power);
  if (this->battery_voltage_sensor_ != nullptr)
    this->battery_voltage_sensor_->publish_state(bat_volt);
  if (this->charge_current_sensor_ != nullptr)
    this->charge_current_sensor_->publish_state(chg_curr);
  if (this->charge_power_sensor_ != nullptr)
    this->charge_power_sensor_->publish_state(bat_volt * chg_curr);
  if (this->battery_soc_sensor_ != nullptr)
    this->battery_soc_sensor_->publish_state(bat_cap);
  if (this->battery_temperature_sensor_ != nullptr)
    this->battery_temperature_sensor_->publish_state(bat_temp);
  if (this->controller_temperature_sensor_ != nullptr)
    this->controller_temperature_sensor_->publish_state(inner_temp);
  if (this->load_voltage_sensor_ != nullptr)
    this->load_voltage_sensor_->publish_state(load_volt);
  if (this->load_current_sensor_ != nullptr)
    this->load_current_sensor_->publish_state(load_curr);
  if (this->load_power_sensor_ != nullptr)
    this->load_power_sensor_->publish_state(load_power);
  if (this->co2_saved_sensor_ != nullptr)
    this->co2_saved_sensor_->publish_state(co2);
  if (this->fault_bitmask_sensor_ != nullptr)
    this->fault_bitmask_sensor_->publish_state(fault);
#endif

  // Faults (mêmes bits que la lib Joba_ESmart3)
  static const char *const FAULT_NAMES[10] = {
      "Surtension batterie", "Surtension PV",         "Surcourant charge",   "Surcourant décharge",
      "Température batterie", "Température interne",  "Tension PV basse",    "Tension batterie basse",
      "Protection trip-zéro", "Contrôle manuel",
  };
  uint8_t fault_count = 0;
  std::string errors;
  for (uint8_t i = 0; i < 9; i++) {  // le bit 9 (contrôle manuel) n'est pas compté comme fault (comme l'origine)
    if (fault & (1 << i)) {
      fault_count++;
      if (!errors.empty())
        errors += "; ";
      errors += FAULT_NAMES[i];
    }
  }
#ifdef USE_SENSOR
  if (this->fault_count_sensor_ != nullptr)
    this->fault_count_sensor_->publish_state(fault_count);
#endif
#ifdef USE_BINARY_SENSOR
  if (this->fault_active_binary_sensor_ != nullptr)
    this->fault_active_binary_sensor_->publish_state(fault_count > 0);
#endif
#ifdef USE_TEXT_SENSOR
  if (this->errors_text_sensor_ != nullptr)
    this->errors_text_sensor_->publish_state(errors.empty() ? "OK" : errors);
  if (this->charge_mode_text_sensor_ != nullptr) {
    const char *mode;
    switch (chg_mode) {
      case 0:
        mode = "Off";
        break;
      case 1:
        mode = "MPPT";
        break;
      case 2:
        mode = "Boost";
        break;
      case 3:
        mode = "Float";
        break;
      case 4:
        mode = "Equalizing";
        break;
      default:
        mode = "Unknown";
        break;
    }
    this->charge_mode_text_sensor_->publish_state(mode);
  }
#endif
}

void ESmart3Component::parse_batparam_(const uint8_t *d, size_t len) {
  if (len < BATPARAM_WORDS * 2)
    return;
  float bulk = word_(d, 3) / 10.0f;
  float flt = word_(d, 4) / 10.0f;
  float max_chg = word_(d, 5) / 10.0f;
  float max_dis = word_(d, 6) / 10.0f;
  float equalize = word_(d, 7) / 10.0f;
#ifdef USE_SENSOR
  if (this->bulk_voltage_sensor_ != nullptr)
    this->bulk_voltage_sensor_->publish_state(bulk);
  if (this->float_voltage_sensor_ != nullptr)
    this->float_voltage_sensor_->publish_state(flt);
  if (this->equalize_voltage_sensor_ != nullptr)
    this->equalize_voltage_sensor_->publish_state(equalize);
  if (this->max_charge_current_sensor_ != nullptr)
    this->max_charge_current_sensor_->publish_state(max_chg);
  if (this->max_discharge_current_sensor_ != nullptr)
    this->max_discharge_current_sensor_->publish_state(max_dis);
#endif
#ifdef USE_NUMBER
  if (this->max_charge_current_number_ != nullptr && !this->pending_chg_)
    this->max_charge_current_number_->publish_state(max_chg);
  if (this->max_load_current_number_ != nullptr && !this->pending_loadcur_)
    this->max_load_current_number_->publish_state(max_dis);
#endif
}

void ESmart3Component::parse_log_(const uint8_t *d, size_t len) {
  if (len < LOG_WORDS * 2)
    return;
#ifdef USE_SENSOR
  if (this->energy_today_sensor_ != nullptr)
    this->energy_today_sensor_->publish_state(dword_(d, 6));  // Wh
  if (this->energy_month_sensor_ != nullptr)
    this->energy_month_sensor_->publish_state(dword_(d, 10) / 1000.0f);  // kWh
  if (this->energy_total_sensor_ != nullptr)
    this->energy_total_sensor_->publish_state(dword_(d, 14) / 1000.0f);  // kWh
  if (this->load_energy_today_sensor_ != nullptr)
    this->load_energy_today_sensor_->publish_state(dword_(d, 16));  // Wh
  if (this->load_energy_month_sensor_ != nullptr)
    this->load_energy_month_sensor_->publish_state(dword_(d, 18) / 1000.0f);  // kWh
  if (this->load_energy_total_sensor_ != nullptr)
    this->load_energy_total_sensor_->publish_state(dword_(d, 20) / 1000.0f);  // kWh
#endif
}

void ESmart3Component::parse_proparam_(const uint8_t *d, size_t len) {
  if (len < PROPARAM_WORDS * 2)
    return;
#ifdef USE_SENSOR
  if (this->battery_ovp_sensor_ != nullptr)
    this->battery_ovp_sensor_->publish_state(word_(d, 3) / 10.0f);
  if (this->battery_uvp_sensor_ != nullptr)
    this->battery_uvp_sensor_->publish_state(word_(d, 5) / 10.0f);
#endif
}

void ESmart3Component::parse_loadparam_(const uint8_t *d, size_t len) {
  if (len < LOADPARAM_WORDS * 2)
    return;
  bool load_on = word_(d, 15) != 0;  // wLoadSts
#ifdef USE_SWITCH
  if (this->load_switch_ != nullptr && !this->pending_load_)
    this->load_switch_->publish_state(load_on);
#endif
}

void ESmart3Component::parse_information_(const uint8_t *d, size_t len) {
  if (len < INFORMATION_WORDS * 2)
    return;
  this->info_read_ = true;
  // Les chaînes sont stockées en gros-boutiste dans les mots (comme l'origine)
  auto words_to_string = [&](size_t first_word, size_t n_words) {
    std::string s;
    for (size_t i = 0; i < n_words; i++) {
      uint16_t w = word_(d, first_word + i);
      char hi = (char) (w >> 8), lo = (char) (w & 0xff);
      if (hi)
        s += hi;
      if (lo)
        s += lo;
    }
    return s;
  };
#ifdef USE_TEXT_SENSOR
  if (this->serial_number_text_sensor_ != nullptr)
    this->serial_number_text_sensor_->publish_state(words_to_string(1, 8));
  if (this->firmware_text_sensor_ != nullptr)
    this->firmware_text_sensor_->publish_state(words_to_string(9, 2));
  if (this->model_text_sensor_ != nullptr)
    this->model_text_sensor_->publish_state(words_to_string(11, 8));
#endif
  ESP_LOGI(TAG, "eSmart3 détecté: %s", words_to_string(11, 8).c_str());
}

//=============================================================================
// API PUBLIQUE
//=============================================================================

void ESmart3Component::set_max_charge_current(float amps) {
  if (amps < 0)
    amps = 0;
  if (amps > 60)
    amps = 60;
  this->pending_chg_ = true;
  this->pending_chg_val_ = amps;
  ESP_LOGD(TAG, "Courant de charge max demandé: %.1f A", amps);
#ifdef USE_NUMBER
  if (this->max_charge_current_number_ != nullptr)
    this->max_charge_current_number_->publish_state(amps);
#endif
}

void ESmart3Component::set_max_load_current(float amps) {
  if (amps < 0)
    amps = 0;
  if (amps > 40)
    amps = 40;
  this->pending_loadcur_ = true;
  this->pending_loadcur_val_ = amps;
#ifdef USE_NUMBER
  if (this->max_load_current_number_ != nullptr)
    this->max_load_current_number_->publish_state(amps);
#endif
}

void ESmart3Component::set_load_state(bool on) {
  this->pending_load_ = true;
  this->pending_load_val_ = on;
#ifdef USE_SWITCH
  if (this->load_switch_ != nullptr)
    this->load_switch_->publish_state(on);
#endif
}

#ifdef USE_NUMBER
void ESmart3Number::control(float value) {
  if (this->purpose_ == 2) {
    this->parent_->set_max_load_current(value);
  } else {
    this->parent_->set_max_charge_current(value);
  }
  this->publish_state(value);
}
#endif

#ifdef USE_SWITCH
void ESmart3LoadSwitch::write_state(bool state) {
  this->parent_->set_load_state(state);
  this->publish_state(state);
}
#endif

}  // namespace esmart3
}  // namespace esphome
