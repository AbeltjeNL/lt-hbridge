# LT-Hbridge for ESPHome replacing the native flickering H-Bridge.

### ESPHome

An ESPHome `fairylights.base.yaml` and example `fairylights-1.yaml` are provided in `/esphome/`. Currently, the lights themselves and the button.

[@LeoDJ](https://github.com/LeoDJ) implemented a BK7231N-specific (a)synchronous PWM H-bridge component which is used here instead of the janky ESPHome [`hbridge` light component](https://esphome.io/components/light/hbridge) which is flickery and glitchy as it switches polarity in the ~60Hz ESPHome main loop.

### Pins

- `P6`: Button (should be input with internal pullup)
- `P7`: Output stage L1
- `P8`: Output stage L2
- `P16`: IR receiver (should be input with internal pulldown)

### Output Stage

The output stage is basically just two half-bridges so the lights' polarity can be changed (to turn on the two colors).
However, it is designed in a weird way, which causes the transistors to short out the power supply when both outputs are active at the same time.

![PCB output stage](images/pcb-output-stage.jpg)
(part numbers match PCB silkscreen)
![LTSpice output stage](images/ltspice-output-stage.png)
