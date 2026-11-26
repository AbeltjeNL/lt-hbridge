# LibreTiny H-Bridge Light Component

Light output for cheap two-wire fairy light strings with anti-parallel LEDs, driven by two
half-bridges on a Beken **BK7231N**.

## ESPHome 2026.11 update

ESPHome 2026.11 builds LibreTiny 1.14 with Beken BDK 3.0.78. From BDK 3.0.70 on, the
BK7231N uses a new PWM driver: the old `pwm_param_t` / `sddev_control(PWM_DEV_NAME, ...)`
interface the first version of this component used no longer works.

This version uses the CW mode of the new driver (`pwm_cw_init_param`, `pwm_cw_start`,
`pwm_cw_update_param`). It drives two PWM channels in **different** PWM groups from one
hardware-synchronised counter, so the old trick of starting both channels right after
each other is no longer needed:

```
period:  |<------------------------------- 1/frequency ------------------------------->|
pin_b:   ‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾|________________________________________________________________   cold white
pin_a:   ______________________________________________|‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾|____   warm white
                        <-- >= dead_time -->                                        <-->
                                                                                 dead_time
```

The two outputs are never high at the same time (on this board that would short the supply).
New duty cycles take effect at the end of the running PWM period, so there are no glitches.

## Configuration

```yaml
light:
  - platform: lt_hbridge
    name: "Light"
    pin_a: P7               # warm white pulse
    pin_b: P8               # cold white pulse, must be in a different PWM group than pin_a
    frequency: 1kHz         # optional, 100Hz - 20kHz, default 1kHz
    dead_time: 10us         # optional, default 10us (max. 98% duty at 1kHz, like before)
    constant_brightness: false  # optional
```

PWM groups: G0 = P6/P7, G1 = P8/P9, G2 = P24/P26.

Only tested on the BK7231N (BK7231T/U use a different PWM driver).
