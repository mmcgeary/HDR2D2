> For the complete documentation index, see [llms.txt](https://r2djp.gitbook.io/astropixels/llms.txt). Markdown versions of documentation pages are available by appending `.md` to page URLs; this page is available as [Markdown](https://r2djp.gitbook.io/astropixels/advanced/overview.md).

# Overview

The Astropixels are standard ws2812 RGB LEDs, running off an ESP32 controller.

They use a library created by another builder called the ReelTwo library. This combines the code for a number of different systems supplied by the club over the years and is a great base for new ones.

## Hardware

### Standard pins

| Board       | Default Pin |
| ----------- | ----------- |
| RLD         | 33          |
| FLD         | 15          |
| FPSI        | 32          |
| RPSI        | 23          |
| THP         | 27          |
| RHP         | 26          |
| FHP         | 25          |
| AUX1        | 2           |
| AUX2        | 4           |
| AUX3        | 5           |
| AUX4        | 18          |
| AUX5        | 19          |
| Serial2\_RX | 16          |
| Serial2\_TX | 17          |

### ESP32

I use a standard, off the shelf ESP32 dev board (30 pin version). These can be bought from anywhere (ebay, amazon, aliexpress) if you do break yours. Then you can simply flash the correct firmware back on it. I generally have a couple of spares on me, just in case. The USB ports can be fragile and easily knocked off whilst working in the dome.
