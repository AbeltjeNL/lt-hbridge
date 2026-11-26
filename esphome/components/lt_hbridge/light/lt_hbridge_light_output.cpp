#ifdef USE_BK72XX

#include "lt_hbridge_light_output.h"

#include "esphome/core/log.h"

// LibreTiny pin info + Beken SDK headers (pwm_pub.h, CFG_BDK_USE_NEW_PWM_DRIVER)
#include <wiring_private.h>

#if !CFG_BDK_USE_NEW_PWM_DRIVER
#error "lt_hbridge needs the new Beken PWM driver (BK7231N with BDK >= 3.0.70, the default since ESPHome 2026.x)."
#endif

namespace esphome::lt_hbridge {

static const char *const TAG = "lt_hbridge.light";

static constexpr uint32_t PWM_CLOCK_HZ = 26000000;  // PWM_CLK_26M

// BK7231N: PWM channel per GPIO (same table as LibreTiny's wiring_analog.c)
static constexpr uint8_t PWM_TO_GPIO[] = {6, 7, 8, 9, 24, 26};

static bool gpio_to_pwm(uint8_t gpio, uint8_t *channel) {
  for (uint8_t i = 0; i < sizeof(PWM_TO_GPIO); i++) {
    if (PWM_TO_GPIO[i] == gpio) {
      *channel = i;
      return true;
    }
  }
  return false;
}

static bool pin_to_pwm(InternalGPIOPin *pin, uint8_t *channel) {
  PinInfo *info = pinInfo(pin->get_pin());
  if (info == nullptr || !pinSupported(info, PIN_PWM) || !gpio_to_pwm(info->gpio, channel))
    return false;
  // Tell LibreTiny the pin is now a PWM output (GPIO and PWM can't be used together).
  pinRemoveMode(info, PIN_GPIO | PIN_IRQ);
  pinEnable(info, PIN_PWM);
  return true;
}

light::LightTraits LTHBridgeLightOutput::get_traits() {
  auto traits = light::LightTraits();
  traits.set_supported_color_modes({light::ColorMode::COLD_WARM_WHITE});
  traits.set_min_mireds(170);  // chosen so the color temperature slider is divided roughly 50/50
  traits.set_max_mireds(200);
  return traits;
}

void LTHBridgeLightOutput::setup() {
  uint8_t chan_a, chan_b;
  if (!pin_to_pwm(this->pin_a_, &chan_a) || !pin_to_pwm(this->pin_b_, &chan_b) || chan_a == chan_b) {
    ESP_LOGE(TAG, "pin_a/pin_b must be two different PWM pins");
    this->mark_failed();
    return;
  }

  // Same PWM group: hardware group mode, where the even channel of the group is always the one that
  // is high at the start of the period. Different groups: two synchronised channels, pin_b first.
  this->group_ = pwm_cw_group_check(chan_a, chan_b);
  this->group_mode_ = this->group_ < PWM_GROUP_NUM;
  this->first_is_a_ = this->group_mode_ && (chan_a % 2 == 0);
  this->first_chan_ = this->first_is_a_ ? chan_a : chan_b;
  this->second_chan_ = this->first_is_a_ ? chan_b : chan_a;

  this->period_ = static_cast<uint32_t>(PWM_CLOCK_HZ / this->frequency_);
  this->dead_band_ = static_cast<uint32_t>(static_cast<uint64_t>(PWM_CLOCK_HZ) * this->dead_time_us_ / 1000000ULL);

  // Both outputs low until the light state is written for the first time.
  if (!this->apply_(0, 0, true)) {
    ESP_LOGE(TAG, "Initialising the PWM channels failed");
    this->mark_failed();
    return;
  }
  // Starts both channels at once, so they run in sync.
  __wrap_bk_printf_disable();
  UINT8 ret = this->group_mode_ ? pwm_cw_group_start(this->group_) : pwm_cw_start(this->first_chan_, this->second_chan_);
  __wrap_bk_printf_enable();
  if (ret != 0) {
    ESP_LOGE(TAG, "Starting the PWM channels failed (%u)", ret);
    this->mark_failed();
    return;
  }
}

void LTHBridgeLightOutput::dump_config() {
  ESP_LOGCONFIG(TAG,
                "LibreTiny H-Bridge Light:\n"
                "  Frequency: %.0f Hz\n"
                "  Dead time: %" PRIu32 " us\n"
                "  Constant brightness: %s\n"
                "  PWM mode: %s\n"
                "  PWM channels: first=%u (pin_%c), second=%u (pin_%c)",
                this->frequency_, this->dead_time_us_, YESNO(this->constant_brightness_),
                this->group_mode_ ? "group (same PWM group)" : "synchronised (different PWM groups)",
                this->first_chan_, this->first_is_a_ ? 'a' : 'b', this->second_chan_, this->first_is_a_ ? 'b' : 'a');
  LOG_PIN("  Pin A (warm white): ", this->pin_a_);
  LOG_PIN("  Pin B (cold white): ", this->pin_b_);
}

void LTHBridgeLightOutput::write_state(light::LightState *state) {
  if (this->is_failed())
    return;

  float cw = 0.0f, ww = 0.0f;
  state->current_values_as_cwww(&cw, &ww, this->constant_brightness_);

  // Both strings share one period: one pulse at the start, the other at the end and a dead band
  // after each pulse. Scale both down if together they don't fit.
  const uint32_t usable = this->period_ - 2 * this->dead_band_;
  const float sum = cw + ww;
  const float scale = sum > 1.0f ? 1.0f / sum : 1.0f;

  uint32_t cw_cycles = static_cast<uint32_t>(cw * scale * usable);
  uint32_t ww_cycles = static_cast<uint32_t>(ww * scale * usable);
  if (cw_cycles + ww_cycles > usable)  // guard against float rounding
    ww_cycles = usable - cw_cycles;

  // pin_a carries warm white, pin_b cold white
  const uint32_t first = this->first_is_a_ ? ww_cycles : cw_cycles;
  const uint32_t second = this->first_is_a_ ? cw_cycles : ww_cycles;

  if (first == this->last_first_ && second == this->last_second_)
    return;

  if (!this->apply_(first, second, false))
    ESP_LOGW(TAG, "Updating the PWM duty cycle failed");
}

bool LTHBridgeLightOutput::apply_(uint32_t first_cycles, uint32_t second_cycles, bool init) {
  pwm_cw_group_param_t param{};
  param.group = this->group_mode_ ? this->group_ : GET_GROUP_IDXS_BY_PORT(this->first_chan_, this->second_chan_);
  param.p_t4 = this->period_;

  // first channel (p0): high from 0 to first_cycles
  param.p0_t1 = first_cycles;
  param.p0_init_level = first_cycles != 0 ? 1 : 0;

  // second channel (p1): high from (period - dead band - second_cycles) to (period - dead band)
  if (second_cycles == 0) {
    param.p1_t1 = 0;
    param.p1_t2 = 0;
    param.p1_init_level = 0;
  } else {
    param.p1_t1 = this->period_ - this->dead_band_ - second_cycles;
    param.p1_t2 = this->period_ - this->dead_band_;
    param.p1_init_level = 0;
  }

  UINT8 ret;
  __wrap_bk_printf_disable();
  if (init) {
    ret = this->group_mode_ ? pwm_cw_group_init_param(&param) : pwm_cw_init_param(&param);
  } else {
    // Takes effect at the end of the running period (in the PWM ISR), so no glitches.
    ret = this->group_mode_ ? pwm_cw_group_update_param(&param) : pwm_cw_update_param(&param);
  }
  __wrap_bk_printf_enable();

  if (ret != 0) {
    ESP_LOGV(TAG, "PWM %s returned %u", init ? "init" : "update", ret);
    return false;
  }
  this->last_first_ = first_cycles;
  this->last_second_ = second_cycles;
  return true;
}

}  // namespace esphome::lt_hbridge

#endif  // USE_BK72XX
