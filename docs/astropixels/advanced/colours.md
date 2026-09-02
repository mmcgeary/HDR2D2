> For the complete documentation index, see [llms.txt](https://r2djp.gitbook.io/astropixels/llms.txt). Markdown versions of documentation pages are available by appending `.md` to page URLs; this page is available as [Markdown](https://r2djp.gitbook.io/astropixels/advanced/colours.md).

# Changing Colors

The Astropixel light boards are all fully RGB and so can be customised to any colour you want.

## PSIs

For the PSIs you can create a new definition. The PSIs use the LogicEngine code, the same as the main Logics.

```
LogicEngineSettings LogicEngineFrontPSICustom(
    LogicEngineDefaults::FRONT_FADE,
    LogicEngineDefaults::FRONT_HUE,
    LogicEngineDefaults::FRONT_DELAY,
    LogicEngineDefaults::FRONT_PSI_PAL,
    LogicEngineDefaults::FRONT_BRI,    LogicEngineDefaults::sequence(LogicEngineDefaults::PSICOLORWIPE,LogicEngineDefaults::kRed));

AstroPixelFrontPSI<> frontPSI(LogicEngineFrontPSICustom, 4);
```

The simplest change you can make is to the colours used. This is set in the

`LogicEngineDefaults::sequence(LogicEngineDefaults::PSICOLORWIPE,LogicEngineDefaults::kRed));`

section. Change the kRed to a different value.

|         | Primary Colour | Secondary Colour |
| ------- | -------------- | ---------------- |
| kRed    | Red            | Blue             |
| kBlue   | Blue           | Red              |
| kYellow | Yellow         | Green            |
| kGreen  | Green          | Yellow           |
| kCyan   | Cyan           | Orange           |
| kOrange | Orange         | Cyan             |
| kPurple | Purple         | Magenta          |
| kPink   | Pink           | Blue             |

## Logics

Logics are configure using a set of values much like the PSIs:

```
static LogicEngineSettings LogicEngineFLDCustom(
    LogicEngineDefaults::FRONT_FADE,
    LogicEngineDefaults::FRONT_HUE,
    LogicEngineDefaults::FRONT_DELAY,
    LogicEngineDefaults::FRONT_PAL,
    LogicEngineDefaults::FRONT_BRI,
    LogicEngineDefaults::sequence(LogicEngineDefaults::NORMAL));
```

### HUE

The hue setting rotates the selected palette around a colour wheel using a value between 0 and 255. So by combining this with a palette you can make various different colours of lights. In general the order is:

red -> yellow -> green -> cyan -> blue -> violet -> pink -> red

For instance, if you set the palette to 2 (monotone red) and then set the hue to around 220, you will get a nice R2-KT colour. Set hue to 255 for a nice red imperial look. Check the [Precompiled](/astropixels/advanced/precompiled.md) page for installing some custom colour firmware.

### DELAY and FADE

These effect the transition of the hues.

### PAL

There are 5 palettes to currently choose from.

* 0 – Default front colours
* 1 – Default rear colours
* 2 – Monotone red
* 3 – Dual colour red and yellow
* 4 – blue and red
* 5 – yellow and green

### BRI

This is just the brightness of the display as a value between 0 and 255. The defaults are normally ok but you can tweak them. Beware of both heat and power consumption if you raise it too high.

### Defaults

|       | Front | Rear |
| ----- | ----- | ---- |
| FADE  | 1     | 3    |
| HUE   | 0     | 0    |
| DELAY | 10    | 40   |
| PAL   | 0     | 1    |
| BRI   | 160   | 140  |

Also, the last option is for the default sequence that the display will run. This was needed to make the PSI default to the swipe, but could also be used to have the logics do a different effect constantly such as a line swiping left and right (cylon droid?!)
