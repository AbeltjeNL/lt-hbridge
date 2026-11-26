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
  if (!pin_to_pwm(this->pin_a_, &this->chan_a_) || !pin_to_pwm(this->pin_b_, &this->chan_b_)) {
    ESP_LOGE(TAG, "pin_a/pin_b are not PWM pins");
    this->mark_failed();
    return;
  }
  if (pwm_cw_group_check(this->chan_a_, this->chan_b_) < PWM_GROUP_NUM) {
    ESP_LOGE(TAG, "pin_a and pin_b must be in different PWM groups");
    this->mark_failed();
    return;
  }

  this->period_ = static_cast<uint32_t>(PWM_CLOCK_HZ / this->frequency_);
  this->dead_band_ = static_cast<uint32_t>(static_cast<uint64_t>(PWM_CLOCK_HZ) * this->dead_time_us_ / 1000000ULL);

  // Both outputs low until the light state is written for the first time.
  if (!this->apply_(0, 0, true)) {
    ESP_LOGE(TAG, "Initialising the PWM channels failed");
    this->mark_failed();
    return;
  }
  // Starts both channels with interrupts disabled, so they run in sync.
  if (pwm_cw_start(this->chan_b_, this->chan_a_) != 0) {
    ESP_LOGE(TAG, "Starting the PWM channels failed");
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
                "  PWM channels: A=%u, B=%u",
                this->frequency_, this->dead_time_us_, YESNO(this->constant_brightness_), this->chan_a_,
                this->chan_b_);
  LOG_PIN("  Pin A: ", this->pin_a_);
  LOG_PIN("  Pin B: ", this->pin_b_);
}

void LTHBridgeLightOutput::write_state(light::LightState *state) {
  if (this->is_failed())
    return;

  float cw = 0.0f, ww = 0.0f;
  state->current_values_as_cwww(&cw, &ww, this->constant_brightness_);

  // Both strings share one period: cold white at the start, warm white at the end and a
  // dead band after each pulse. Scale both down if together they don't fit.
  const uint32_t usable = this->period_ - 2 * this->dead_band_;
  const float sum = cw + ww;
  const float scale = sum > 1.0f ? 1.0f / sum : 1.0f;

  uint32_t cw_cycles = static_cast<uint32_t>(cw * scale * usable);
  uint32_t ww_cycles = static_cast<uint32_t>(ww * scale * usable);
  if (cw_cycles + ww_cycles > usable)  // guard against float rounding
    ww_cycles = usable - cw_cycles;

  if (cw_cycles == this->last_cw_ && ww_cycles == this->last_ww_)
    return;

  if (!this->apply_(cw_cycles, ww_cycles, false))
    ESP_LOGW(TAG, "Updating the PWM duty cycle failed");
}

bool LTHBridgeLightOutput::apply_(uint32_t cw_cycles, uint32_t ww_cycles, bool init) {
  pwm_cw_group_param_t param{};
  // p0 = pin_b (high first), p1 = pin_a; channels in different groups so no shared group index
  param.group = GET_GROUP_IDXS_BY_PORT(this->chan_b_, this->chan_a_);
  param.p_t4 = this->period_;

  // pin_b: high from 0 to cw_cycles
  param.p0_t1 = cw_cycles;
  param.p0_init_level = cw_cycles != 0 ? 1 : 0;

  // pin_a: high from (period - dead band - ww_cycles) to (period - dead band)
  if (ww_cycles == 0) {
    param.p1_t1 = 0;
    param.p1_t2 = 0;
    param.p1_init_level = 0;
  } else {
    param.p1_t1 = this->period_ - this->dead_band_ - ww_cycles;
    param.p1_t2 = this->period_ - this->dead_band_;
    param.p1_init_level = 0;
  }

  UINT8 ret;
  __wrap_bk_printf_disable();
  if (init) {
    ret = pwm_cw_init_param(&param);
  } else {
    // Takes effect at the end of the running period (in the PWM ISR), so no glitches.
    ret = pwm_cw_update_param(&param);
  }
  __wrap_bk_printf_enable();

  if (ret != 0) {
    ESP_LOGV(TAG, "pwm_cw_%s_param returned %u", init ? "init" : "update", ret);
    return false;
  }
  this->last_cw_ = cw_cycles;
  this->last_ww_ = ww_cycles;
  return true;
}

}  // namespace esphome::lt_hbridge

#endif  // USE_BK72XX
