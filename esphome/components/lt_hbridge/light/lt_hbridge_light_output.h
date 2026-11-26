#pragma once

#ifdef USE_BK72XX

#include <cstdint>

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/light/light_output.h"

namespace esphome::lt_hbridge {

/// Drives two anti-parallel LED strings through an H-bridge on a BK7231N.
///
/// Uses the "CW" modes of the Beken PWM driver (BDK >= 3.0.70), so both outputs run from
/// one hardware-synchronised counter:
///  - pins in different PWM groups: CW mode, pin_b pulses at the start of each period;
///  - pins in the same PWM group: CW group mode, the even PWM channel pulses first.
/// pin_a carries warm white, pin_b cold white. A dead band after both pulses makes sure
/// the two outputs are never high at the same time (that would short the supply on the
/// fairy light board).
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
  bool apply_(uint32_t first_cycles, uint32_t second_cycles, bool init);

  InternalGPIOPin *pin_a_;
  InternalGPIOPin *pin_b_;
  float frequency_{1000.0f};
  uint32_t dead_time_us_{10};
  bool constant_brightness_{false};

  uint8_t first_chan_{0};   // PWM channel that is high at the start of the period
  uint8_t second_chan_{0};  // PWM channel that is high at the end of the period
  uint8_t group_{0};        // PWM group, only valid in group mode
  bool group_mode_{false};  // both pins in the same PWM group
  bool first_is_a_{false};  // first_chan_ belongs to pin_a
  uint32_t period_{0};     // PWM period in 26 MHz cycles
  uint32_t dead_band_{0};  // dead band in 26 MHz cycles
  uint32_t last_first_{UINT32_MAX};
  uint32_t last_second_{UINT32_MAX};
};

}  // namespace esphome::lt_hbridge

#endif  // USE_BK72XX
