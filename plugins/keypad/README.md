## I2C keypad driver plugin

This plugin can be used for issuing jog commands, controlling feedhold, cycle start etc.

Settings \($n=...\) are provided for jog speed and distance for step, slow and fast jogging.

Dependencies:

I2C Keypad such as [this implementation](https://github.com/terjeio/I2C-interface-for-4x4-keyboard).

Driver (and keypad) must support I2C communication and a keypad strobe interrupt signal.

---
2019-07-25
