# LT-Hbridge for ESPHome replacing the native flickering H-Bridge.

### ESPHome

An ESPHome external component named lt-hbridge used instead of the default ESPHome hbridge which causes flickering.
This component is optimized for ESPHome version 2026.11.x because the old component from [@LeoDJ](https://github.com/LeoDJ) and [@patagonaa](https://github.com/patagonaa) could not compile and stranded with loads of errors.

[@LeoDJ](https://github.com/LeoDJ) implemented a BK7231N-specific (a)synchronous PWM H-bridge component which is used here instead of the janky ESPHome [`hbridge` light component](https://esphome.io/components/light/hbridge) which is flickery and glitchy as it switches polarity in the ~60Hz ESPHome main loop.

### Output Stage

The output stage is basically just two half-bridges so the lights' polarity can be changed (to turn on the two colors).
However, it is designed in a weird way, which causes the transistors to short out the power supply when both outputs are active at the same time.

### Great thanks to

[@LeoDJ](https://github.com/LeoDJ) and [@patagonaa](https://github.com/patagonaa) thanks for your great work in the past. I'm still using this component as of today for my Christmas lights. I hope you don't mind me forking and bringing the component up to date. If you do mind please open an issue in this repo to inform me.

### My use cases for this component

Mainly I am using this component for my christmas lights in combination with the 123-LED smart Tuya plugs. Full write-ups below. 
Use cases are christmas tree light, fairybell lights and icicle lights outside on the rain gutter. 

### Use case projects

https://github.com/AbeltjeNL/123-LED-Kerstverlichting-Wi-Fi-Tuya-BK7231T-WB2S

https://github.com/AbeltjeNL/123-LED-Kerstverlichting-Wi-Fi-Tuya-BK7231N-CB2S


