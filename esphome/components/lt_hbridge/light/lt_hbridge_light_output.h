#pragma once

#ifdef USE_BK72XX

#include <cstdint>

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/light/light_output.h"

namespace esphome::lt_hbridge {

/// Drives two anti-parallel LED strings through an H-bridge on a BK7231N.
///
/// Uses the "CW" mode of the Beken PWM driver (BDK >= 3.0.70): two PWM channels in
/// different PWM groups run from one hardware-synchronised counter. pin_b is high at the
/// start of each period (cold white), pin_a at the end (warm white), with a dead band
/// around both pulses so the two outputs are never high at the same time (that would
/// short the supply on this board).
class LTHBridgeLightOutput : public Component, public light::LightOutput {
 public:
  LTHBridgeLightOutput(InternalGPIOPin *pin_a, InternalGPIOPin *pin_b) : pin_a_(pin_a), pin_b_(pin_b) {}

  void set_frequency(float frequency) { this->frequency_ = frequency; }
  void set_dead_time(uint32_t dead_time_us) { this->dead_time_us_ = dead_time_us; }
  void set_constant_brightness(bool constant_brightness) { this->constant_brightness_ = constant_brightness; }

  light::LightTraits get_traits() override;
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }
  void write_state(light::LightState *state) override;

 protected:
  /// Push new pulse lengths (in 26 MHz clock cycles) to the PWM hardware.
  bool apply_(uint32_t cw_cycles, uint32_t ww_cycles, bool init);

  InternalGPIOPin *pin_a_;
  InternalGPIOPin *pin_b_;
  float frequency_{1000.0f};
  uint32_t dead_time_us_{10};
  bool constant_brightness_{false};

  uint8_t chan_a_{0};
  uint8_t chan_b_{0};
  uint32_t period_{0};     // PWM period in 26 MHz cycles
  uint32_t dead_band_{0};  // dead band in 26 MHz cycles
  uint32_t last_cw_{UINT32_MAX};
  uint32_t last_ww_{UINT32_MAX};
};

}  // namespace esphome::lt_hbridge

#endif  // USE_BK72XX
