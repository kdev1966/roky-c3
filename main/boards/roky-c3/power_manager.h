#pragma once

#include <algorithm>
#include <functional>
#include <vector>

#include <esp_adc/adc_oneshot.h>
#include <esp_log.h>
#include <esp_timer.h>

// Niveau de batterie de la WHA103, mesure sur ADC1 canal 2 (GPIO2).
//
// Reprend le comportement du firmware d'origine : attenuation 12 dB, table
// brute 2450 (0 %) .. 3080 (100 %), et une valeur lissee egale a
// (moyenne sans extremes + 3 x mediane) / 4 pour ignorer les pics de lecture
// pendant que l'ampli tire du courant.
//
// La carte n'a pas de broche de charge distincte : l'original lisait l'etat
// numerique de GPIO2, la broche meme de la mesure, ce qui ne renseigne pas.
// On ne signale donc ni charge ni decharge.
class PowerManager {
private:
    esp_timer_handle_t timer_handle_ = nullptr;
    adc_oneshot_unit_handle_t adc_handle_ = nullptr;
    adc_channel_t channel_;
    std::function<void(bool)> on_low_battery_status_changed_;

    std::vector<uint16_t> adc_values_;
    int battery_level_ = 0;
    bool is_low_battery_ = false;
    int ticks_ = 0;

    static constexpr size_t kBatteryAdcDataCount = 5;
    static constexpr int kBatteryAdcInterval = 60;  // secondes, une fois la serie pleine
    static constexpr int kLowBatteryLevel = 20;

    void CheckBatteryStatus() {
        // Remplit d'abord la serie a une mesure par seconde, puis espace
        if (adc_values_.size() < kBatteryAdcDataCount || ++ticks_ % kBatteryAdcInterval == 0) {
            ReadBatteryAdcData();
        }
    }

    static int Smooth(std::vector<uint16_t> values) {
        std::sort(values.begin(), values.end());
        int median = values[values.size() / 2];
        if (values.size() < 3) {
            return median;
        }
        int sum = 0;
        for (size_t i = 1; i + 1 < values.size(); i++) {
            sum += values[i];
        }
        int trimmed_mean = sum / (int)(values.size() - 2);
        return (trimmed_mean + 3 * median) / 4;
    }

    static int LevelFromAdc(int adc) {
        static const struct {
            uint16_t adc;
            uint8_t level;
        } levels[] = {
            {2450, 0}, {2520, 10}, {2580, 20}, {2650, 30}, {2720, 40},
            {2800, 60}, {2900, 70}, {2980, 80}, {3080, 90},
        };
        constexpr int n = sizeof(levels) / sizeof(levels[0]);
        if (adc < levels[0].adc) {
            return 0;
        }
        if (adc >= levels[n - 1].adc) {
            return 100;
        }
        for (int i = 0; i < n - 1; i++) {
            if (adc < levels[i + 1].adc) {
                return levels[i].level + (adc - levels[i].adc) * (levels[i + 1].level - levels[i].level) /
                                             (levels[i + 1].adc - levels[i].adc);
            }
        }
        return 100;
    }

    void ReadBatteryAdcData() {
        int adc_value = 0;
        if (adc_oneshot_read(adc_handle_, channel_, &adc_value) != ESP_OK) {
            return;
        }
        adc_values_.push_back(adc_value);
        if (adc_values_.size() > kBatteryAdcDataCount) {
            adc_values_.erase(adc_values_.begin());
        }

        int smoothed = Smooth(adc_values_);
        battery_level_ = LevelFromAdc(smoothed);

        if (adc_values_.size() >= kBatteryAdcDataCount) {
            bool low = battery_level_ <= kLowBatteryLevel;
            if (low != is_low_battery_) {
                is_low_battery_ = low;
                if (on_low_battery_status_changed_) {
                    on_low_battery_status_changed_(is_low_battery_);
                }
            }
        }
        ESP_LOGI("PowerManager", "ADC %d lisse %d niveau %d%%", adc_value, smoothed, battery_level_);
    }

public:
    explicit PowerManager(adc_channel_t channel) : channel_(channel) {
        adc_oneshot_unit_init_cfg_t init_config = {
            .unit_id = ADC_UNIT_1,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle_));

        adc_oneshot_chan_cfg_t chan_config = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
        };
        ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle_, channel_, &chan_config));

        esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) { static_cast<PowerManager*>(arg)->CheckBatteryStatus(); },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "battery_check_timer",
            .skip_unhandled_events = true,
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer_handle_));
        ESP_ERROR_CHECK(esp_timer_start_periodic(timer_handle_, 1000000));
    }

    ~PowerManager() {
        if (timer_handle_) {
            esp_timer_stop(timer_handle_);
            esp_timer_delete(timer_handle_);
        }
        if (adc_handle_) {
            adc_oneshot_del_unit(adc_handle_);
        }
    }

    int GetBatteryLevel() const { return battery_level_; }
    bool HasReading() const { return !adc_values_.empty(); }

    void OnLowBatteryStatusChanged(std::function<void(bool)> callback) {
        on_low_battery_status_changed_ = callback;
    }
};
