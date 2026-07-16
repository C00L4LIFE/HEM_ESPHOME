// SPDX-License-Identifier: GPL-2.0-only

#include <cmath>
#include <cstring>

#include "esmart3.h"
#include "esphome/core/log.h"

namespace esphome {
namespace esmart3 {

static const char *const TAG = "esmart3";

// Conversion en dixièmes (registres eSmart3) avec arrondi : un simple cast
// (uint16_t) tronque, et l'imprécision du flottant fait qu'une valeur comme
// 27.9f * 10.0f peut valoir 278.99998 au lieu de 279.0 -> tronqué à 278
// (27.8 V au lieu de 27.9 V). Les valeurs entières (ex. 28.0) tombent
// pile sur un entier flottant et ne sont pas affectées, d'où le symptôme
// "les nombres à virgule ne s'enregistrent pas".
static uint16_t round_deci_(float value) {
  return (uint16_t) lroundf(value);
}

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

void ESmart3Component::send_set_bytes_(uint8_t item, uint8_t start_word, const uint8_t *data, uint8_t data_len) {
  uint8_t payload[2 + 120];
  payload[0] = start_word;
  payload[1] = 0;
  memcpy(&payload[2], data, data_len);
  this->expect_ack_ = true;
  this->expected_item_ = item;
  this->send_frame_(CMD_SET, item, payload, 2 + data_len);
}

void ESmart3Component::enqueue_write_(uint8_t item, uint8_t word_offset, uint16_t raw_value, uint8_t kind) {
  // Remplace une écriture déjà en attente pour le même paramètre (évite l'empilement
  // si l'utilisateur bouge un slider plusieurs fois avant que le bus se libère)
  for (auto &w : this->pending_queue_) {
    if (w.kind == kind) {
      w.item = item;
      w.word_offset = word_offset;
      w.raw_value = raw_value;
      return;
    }
  }
  this->pending_queue_.push_back({item, word_offset, raw_value, kind});
}

bool ESmart3Component::pending_has_(uint8_t kind) const {
  for (const auto &w : this->pending_queue_)
    if (w.kind == kind)
      return true;
  return false;
}

bool ESmart3Component::try_send_pending_() {
  if (this->reset_energy_step_ >= 0) {
    // Écritures séparées, une par dword (4 octets), plutôt qu'un seul SET
    // de 32 octets - offsets dwTodayEng/dwMonthEng/dwTotalEng/
    // dwLoadTodayEng/dwLoadMonthEng/dwLoadTotalEng. Après le reset, les
    // compteurs repartent de 0 et ré-accumulent immédiatement si le
    // contrôleur charge/alimente (comportement normal).
    static const uint8_t RESET_OFFSETS[6] = {6, 10, 14, 16, 18, 20};
    static const uint8_t zero_dword[4] = {0, 0, 0, 0};
    this->pending_write_kind_ = PENDING_RESET_ENERGY;
    this->send_set_bytes_(ITEM_LOG, RESET_OFFSETS[this->reset_energy_step_], zero_dword, sizeof(zero_dword));
    return true;
  }
  if (this->pending_queue_.empty())
    return false;
  const PendingWrite &w = this->pending_queue_.front();
  this->pending_write_kind_ = w.kind;
  this->send_set_word_(w.item, w.word_offset, w.raw_value);
  return true;
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
  if (this->force_proparam_) {
    this->force_proparam_ = false;
    this->send_get_(ITEM_PROPARAM, 0, PROPARAM_WORDS);
    return;
  }
  if (this->force_log_) {
    this->force_log_ = false;
    this->send_get_(ITEM_LOG, 0, LOG_WORDS);
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
    if (this->pending_write_kind_ == PENDING_RESET_ENERGY) {
      if (ok) {
        this->reset_energy_step_++;
        if (this->reset_energy_step_ >= 6) {
          this->reset_energy_step_ = -1;
          this->force_log_ = true;  // relire pour rafraîchir les sensors immédiatement
          ESP_LOGI(TAG, "Statistiques d'énergie réinitialisées");
        }
      } else {
        ESP_LOGW(TAG, "Réinitialisation énergie (étape %d) refusée (NACK), nouvelle tentative au prochain cycle",
                 this->reset_energy_step_);
      }
      this->pending_write_kind_ = 0;
      return;
    }
    if (ok && !this->pending_queue_.empty()) {
      uint8_t kind = this->pending_queue_.front().kind;
      this->pending_queue_.erase(this->pending_queue_.begin());
      switch (kind) {
        case PENDING_MAX_CHARGE_CURRENT:
        case PENDING_MAX_LOAD_CURRENT:
        case PENDING_BULK_VOLTAGE:
        case PENDING_FLOAT_VOLTAGE:
        case PENDING_EQUALIZE_VOLTAGE:
        case PENDING_EQUALIZE_TIME:
        case PENDING_BATTERY_TYPE:
          this->force_batparam_ = true;  // relire pour rafraîchir les sensors/numbers
          break;
        case PENDING_LOAD_OVP:
        case PENDING_LOAD_UVP:
        case PENDING_BATTERY_OVP:
        case PENDING_BATTERY_OVP_RECOVER:
        case PENDING_BATTERY_UVP:
        case PENDING_BATTERY_UVP_RECOVER:
          this->force_proparam_ = true;
          break;
        default:
          break;  // PENDING_LOAD_STATE : LoadParam relu naturellement au prochain cycle
      }
      ESP_LOGI(TAG, "Écriture confirmée (kind=%u)", kind);
    } else if (!ok) {
      ESP_LOGW(TAG, "SET refusé (NACK) pour kind=%u, nouvelle tentative au prochain cycle",
                this->pending_write_kind_);
    }
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

  // Facteur d'échelle tension système : les registres de config BatParam/
  // ProParam sont stockés en base 12V (ex. bulk=14.4 même sur un système
  // 24V/36V/48V, où la tension réelle est 12V-équivalent x facteur).
  if (this->system_voltage_mode_ == 0) {
    // Auto : déduit du dernier facteur connu si la tension batterie sort
    // de toutes les bandes attendues (évite les à-coups sur mesure bruitée)
    float f = this->system_voltage_factor_;
    if (bat_volt >= 10.0f && bat_volt <= 15.0f)
      f = 1.0f;
    else if (bat_volt >= 20.0f && bat_volt <= 30.0f)
      f = 2.0f;
    else if (bat_volt >= 32.0f && bat_volt <= 38.0f)
      f = 3.0f;
    else if (bat_volt >= 40.0f && bat_volt <= 60.0f)
      f = 4.0f;
    this->system_voltage_factor_ = f;
  } else {
    this->system_voltage_factor_ = (float) this->system_voltage_mode_;
  }
#ifdef USE_SENSOR
  if (this->system_voltage_sensor_ != nullptr)
    this->system_voltage_sensor_->publish_state(this->system_voltage_factor_ * 12.0f);
#endif
  float load_volt = word_(d, 5) / 10.0f;
  float load_curr = word_(d, 6) / 10.0f;
  float chg_power = word_(d, 7);
  float load_power = word_(d, 8);
  // Pas de division par 10 : ces deux registres sont déjà en °C entiers
  // (contrairement aux tensions/courants du même message qui sont en dixièmes).
  float bat_temp = sword_(d, 9);
  float inner_temp = sword_(d, 10);
  uint16_t bat_cap = word_(d, 11);
  // dwCO2 documenté "0.1kg" dans le protocole officiel (pas 0.001kg/1000)
  float co2 = dword_(d, 12) / 10.0f;
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

static const char *const BATTERY_TYPE_NAMES[4] = {"Utilisateur", "Plomb-acide", "Gel", "AGM"};

void ESmart3Component::parse_batparam_(const uint8_t *d, size_t len) {
  if (len < BATPARAM_WORDS * 2)
    return;
  uint16_t bat_type_raw = word_(d, 1);
  float f = this->system_voltage_factor_;
  float bulk = word_(d, 3) / 10.0f * f;
  float flt = word_(d, 4) / 10.0f * f;
  float max_chg = word_(d, 5) / 10.0f;
  float max_dis = word_(d, 6) / 10.0f;
  float equalize = word_(d, 7) / 10.0f * f;
  uint16_t equalize_time = word_(d, 8);
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
  if (this->max_charge_current_number_ != nullptr && !this->pending_has_(PENDING_MAX_CHARGE_CURRENT))
    this->max_charge_current_number_->publish_state(max_chg);
  if (this->max_load_current_number_ != nullptr && !this->pending_has_(PENDING_MAX_LOAD_CURRENT))
    this->max_load_current_number_->publish_state(max_dis);
  if (this->bulk_voltage_number_ != nullptr && !this->pending_has_(PENDING_BULK_VOLTAGE))
    this->bulk_voltage_number_->publish_state(bulk);
  if (this->float_voltage_number_ != nullptr && !this->pending_has_(PENDING_FLOAT_VOLTAGE))
    this->float_voltage_number_->publish_state(flt);
  if (this->equalize_voltage_number_ != nullptr && !this->pending_has_(PENDING_EQUALIZE_VOLTAGE))
    this->equalize_voltage_number_->publish_state(equalize);
  if (this->equalize_time_number_ != nullptr && !this->pending_has_(PENDING_EQUALIZE_TIME))
    this->equalize_time_number_->publish_state(equalize_time);
#endif
#ifdef USE_SELECT
  if (this->battery_type_select_ != nullptr && !this->pending_has_(PENDING_BATTERY_TYPE) && bat_type_raw < 4)
    this->battery_type_select_->publish_state(BATTERY_TYPE_NAMES[bat_type_raw]);
#endif
}

void ESmart3Component::parse_log_(const uint8_t *d, size_t len) {
  if (len < LOG_WORDS * 2)
    return;
  // Doc protocole officielle (esmart3-serial-comm.txt, §7.1.3 Run log) :
  // dwTodayEng/dwMonthEng/dwTotalEng/dwLoadTodayEng/dwLoadMonthEng/
  // dwLoadTotalEng sont TOUS documentés "1wh" - valeur brute directement en
  // Wh, sans aucune division. Le firmware d'origine (mppt_manager.cpp)
  // divisait month/total par 1000 en les qualifiant de kWh : erreur de
  // calcul (pas seulement d'unité) reproduite puis seulement à moitié
  // corrigée ici précédemment (libellé changé sans retirer la division).
#ifdef USE_SENSOR
  if (this->energy_today_sensor_ != nullptr)
    this->energy_today_sensor_->publish_state(dword_(d, 6));  // Wh
  if (this->energy_month_sensor_ != nullptr)
    this->energy_month_sensor_->publish_state(dword_(d, 10));  // Wh
  if (this->energy_total_sensor_ != nullptr)
    this->energy_total_sensor_->publish_state(dword_(d, 14));  // Wh
  if (this->load_energy_today_sensor_ != nullptr)
    this->load_energy_today_sensor_->publish_state(dword_(d, 16));  // Wh
  if (this->load_energy_month_sensor_ != nullptr)
    this->load_energy_month_sensor_->publish_state(dword_(d, 18));  // Wh
  if (this->load_energy_total_sensor_ != nullptr)
    this->load_energy_total_sensor_->publish_state(dword_(d, 20));  // Wh
#endif
}

void ESmart3Component::parse_proparam_(const uint8_t *d, size_t len) {
  if (len < PROPARAM_WORDS * 2)
    return;
  float f = this->system_voltage_factor_;
  float load_ovp = word_(d, 1) / 10.0f * f;
  float load_uvp = word_(d, 2) / 10.0f * f;
  float bat_ovp = word_(d, 3) / 10.0f * f;
  float bat_ovp_recover = word_(d, 4) / 10.0f * f;
  float bat_uvp = word_(d, 5) / 10.0f * f;
  float bat_uvp_recover = word_(d, 6) / 10.0f * f;
#ifdef USE_SENSOR
  if (this->battery_ovp_sensor_ != nullptr)
    this->battery_ovp_sensor_->publish_state(bat_ovp);
  if (this->battery_uvp_sensor_ != nullptr)
    this->battery_uvp_sensor_->publish_state(bat_uvp);
#endif
#ifdef USE_NUMBER
  if (this->load_ovp_number_ != nullptr && !this->pending_has_(PENDING_LOAD_OVP))
    this->load_ovp_number_->publish_state(load_ovp);
  if (this->load_uvp_number_ != nullptr && !this->pending_has_(PENDING_LOAD_UVP))
    this->load_uvp_number_->publish_state(load_uvp);
  if (this->battery_ovp_number_ != nullptr && !this->pending_has_(PENDING_BATTERY_OVP))
    this->battery_ovp_number_->publish_state(bat_ovp);
  if (this->battery_ovp_recover_number_ != nullptr && !this->pending_has_(PENDING_BATTERY_OVP_RECOVER))
    this->battery_ovp_recover_number_->publish_state(bat_ovp_recover);
  if (this->battery_uvp_number_ != nullptr && !this->pending_has_(PENDING_BATTERY_UVP))
    this->battery_uvp_number_->publish_state(bat_uvp);
  if (this->battery_uvp_recover_number_ != nullptr && !this->pending_has_(PENDING_BATTERY_UVP_RECOVER))
    this->battery_uvp_recover_number_->publish_state(bat_uvp_recover);
#endif
}

void ESmart3Component::parse_loadparam_(const uint8_t *d, size_t len) {
  if (len < LOADPARAM_WORDS * 2)
    return;
  bool load_on = word_(d, 15) != 0;  // wLoadSts
#ifdef USE_SWITCH
  if (this->load_switch_ != nullptr && !this->pending_has_(PENDING_LOAD_STATE))
    this->load_switch_->publish_state(load_on);
#endif
}

void ESmart3Component::parse_information_(const uint8_t *d, size_t len) {
  if (len < INFORMATION_WORDS * 2)
    return;
  this->info_read_ = true;
  // Chaque mot contient 2 caractères ASCII dans l'ordre naturel de lecture
  // (octet de poids faible = 1er caractère, octet de poids fort = 2e).
  auto words_to_string = [&](size_t first_word, size_t n_words) {
    std::string s;
    for (size_t i = 0; i < n_words; i++) {
      uint16_t w = word_(d, first_word + i);
      char first = (char) (w & 0xff), second = (char) (w >> 8);
      if (first)
        s += first;
      if (second)
        s += second;
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
  this->enqueue_write_(ITEM_BATPARAM, 0x05, round_deci_(amps * 10.0f), PENDING_MAX_CHARGE_CURRENT);
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
  this->enqueue_write_(ITEM_BATPARAM, 0x06, round_deci_(amps * 10.0f), PENDING_MAX_LOAD_CURRENT);
#ifdef USE_NUMBER
  if (this->max_load_current_number_ != nullptr)
    this->max_load_current_number_->publish_state(amps);
#endif
}

void ESmart3Component::set_load_state(bool on) {
  this->enqueue_write_(ITEM_LOADPARAM, 0x01, on ? 5117 : 5118, PENDING_LOAD_STATE);
#ifdef USE_SWITCH
  if (this->load_switch_ != nullptr)
    this->load_switch_->publish_state(on);
#endif
}

void ESmart3Component::set_bulk_voltage(float volts) {
  float f = this->system_voltage_factor_ > 0 ? this->system_voltage_factor_ : 1.0f;
  this->enqueue_write_(ITEM_BATPARAM, 0x03, round_deci_(volts / f * 10.0f), PENDING_BULK_VOLTAGE);
#ifdef USE_NUMBER
  if (this->bulk_voltage_number_ != nullptr)
    this->bulk_voltage_number_->publish_state(volts);
#endif
}

void ESmart3Component::set_float_voltage(float volts) {
  float f = this->system_voltage_factor_ > 0 ? this->system_voltage_factor_ : 1.0f;
  this->enqueue_write_(ITEM_BATPARAM, 0x04, round_deci_(volts / f * 10.0f), PENDING_FLOAT_VOLTAGE);
#ifdef USE_NUMBER
  if (this->float_voltage_number_ != nullptr)
    this->float_voltage_number_->publish_state(volts);
#endif
}

void ESmart3Component::set_equalize_voltage(float volts) {
  float f = this->system_voltage_factor_ > 0 ? this->system_voltage_factor_ : 1.0f;
  this->enqueue_write_(ITEM_BATPARAM, 0x07, round_deci_(volts / f * 10.0f), PENDING_EQUALIZE_VOLTAGE);
#ifdef USE_NUMBER
  if (this->equalize_voltage_number_ != nullptr)
    this->equalize_voltage_number_->publish_state(volts);
#endif
}

void ESmart3Component::set_equalize_time(uint16_t minutes) {
  this->enqueue_write_(ITEM_BATPARAM, 0x08, minutes, PENDING_EQUALIZE_TIME);
#ifdef USE_NUMBER
  if (this->equalize_time_number_ != nullptr)
    this->equalize_time_number_->publish_state(minutes);
#endif
}

void ESmart3Component::set_system_voltage_mode(uint8_t mode) {
  if (mode > 4)
    mode = 4;
  this->system_voltage_mode_ = mode;
  if (mode != 0)
    this->system_voltage_factor_ = (float) mode;  // appliqué immédiatement ; mode Auto attend le prochain ChgSts
  // Relire BatParam/ProParam pour republier les tensions déjà connues avec le nouveau facteur
  this->force_batparam_ = true;
  this->force_proparam_ = true;
}

void ESmart3Component::reset_energy_stats() {
  this->reset_energy_step_ = 0;
  ESP_LOGD(TAG, "Réinitialisation des statistiques d'énergie demandée");
}

void ESmart3Component::set_battery_type(uint8_t type) {
  if (type > 3)
    type = 3;
  this->enqueue_write_(ITEM_BATPARAM, 0x01, type, PENDING_BATTERY_TYPE);
#ifdef USE_SELECT
  if (this->battery_type_select_ != nullptr)
    this->battery_type_select_->publish_state(BATTERY_TYPE_NAMES[type]);
#endif
}

void ESmart3Component::set_load_ovp(float volts) {
  float f = this->system_voltage_factor_ > 0 ? this->system_voltage_factor_ : 1.0f;
  this->enqueue_write_(ITEM_PROPARAM, 0x01, round_deci_(volts / f * 10.0f), PENDING_LOAD_OVP);
#ifdef USE_NUMBER
  if (this->load_ovp_number_ != nullptr)
    this->load_ovp_number_->publish_state(volts);
#endif
}

void ESmart3Component::set_load_uvp(float volts) {
  float f = this->system_voltage_factor_ > 0 ? this->system_voltage_factor_ : 1.0f;
  this->enqueue_write_(ITEM_PROPARAM, 0x02, round_deci_(volts / f * 10.0f), PENDING_LOAD_UVP);
#ifdef USE_NUMBER
  if (this->load_uvp_number_ != nullptr)
    this->load_uvp_number_->publish_state(volts);
#endif
}

void ESmart3Component::set_battery_ovp(float volts) {
  float f = this->system_voltage_factor_ > 0 ? this->system_voltage_factor_ : 1.0f;
  this->enqueue_write_(ITEM_PROPARAM, 0x03, round_deci_(volts / f * 10.0f), PENDING_BATTERY_OVP);
#ifdef USE_NUMBER
  if (this->battery_ovp_number_ != nullptr)
    this->battery_ovp_number_->publish_state(volts);
#endif
}

void ESmart3Component::set_battery_ovp_recover(float volts) {
  float f = this->system_voltage_factor_ > 0 ? this->system_voltage_factor_ : 1.0f;
  this->enqueue_write_(ITEM_PROPARAM, 0x04, round_deci_(volts / f * 10.0f), PENDING_BATTERY_OVP_RECOVER);
#ifdef USE_NUMBER
  if (this->battery_ovp_recover_number_ != nullptr)
    this->battery_ovp_recover_number_->publish_state(volts);
#endif
}

void ESmart3Component::set_battery_uvp(float volts) {
  float f = this->system_voltage_factor_ > 0 ? this->system_voltage_factor_ : 1.0f;
  this->enqueue_write_(ITEM_PROPARAM, 0x05, round_deci_(volts / f * 10.0f), PENDING_BATTERY_UVP);
#ifdef USE_NUMBER
  if (this->battery_uvp_number_ != nullptr)
    this->battery_uvp_number_->publish_state(volts);
#endif
}

void ESmart3Component::set_battery_uvp_recover(float volts) {
  float f = this->system_voltage_factor_ > 0 ? this->system_voltage_factor_ : 1.0f;
  this->enqueue_write_(ITEM_PROPARAM, 0x06, round_deci_(volts / f * 10.0f), PENDING_BATTERY_UVP_RECOVER);
#ifdef USE_NUMBER
  if (this->battery_uvp_recover_number_ != nullptr)
    this->battery_uvp_recover_number_->publish_state(volts);
#endif
}

#ifdef USE_NUMBER
void ESmart3Number::control(float value) {
  // purpose_ correspond aux valeurs de ESmart3Component::PendingKind
  // (3=LOAD_STATE et 8=BATTERY_TYPE ne sont pas utilisés ici : switch/select)
  switch (this->purpose_) {
    case 2:
      this->parent_->set_max_load_current(value);
      break;
    case 4:
      this->parent_->set_bulk_voltage(value);
      break;
    case 5:
      this->parent_->set_float_voltage(value);
      break;
    case 6:
      this->parent_->set_equalize_voltage(value);
      break;
    case 7:
      this->parent_->set_equalize_time((uint16_t) value);
      break;
    case 9:
      this->parent_->set_load_ovp(value);
      break;
    case 10:
      this->parent_->set_load_uvp(value);
      break;
    case 11:
      this->parent_->set_battery_ovp(value);
      break;
    case 12:
      this->parent_->set_battery_ovp_recover(value);
      break;
    case 13:
      this->parent_->set_battery_uvp(value);
      break;
    case 14:
      this->parent_->set_battery_uvp_recover(value);
      break;
    default:
      this->parent_->set_max_charge_current(value);
      break;
  }
  this->publish_state(value);
}
#endif

#ifdef USE_SELECT
void ESmart3BatteryTypeSelect::control(const std::string &value) {
  auto index = this->index_of(value);
  if (index.has_value()) {
    this->parent_->set_battery_type((uint8_t) *index);
    this->publish_state(value);
  }
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
