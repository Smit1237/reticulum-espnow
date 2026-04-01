#pragma once

#include "board_config.h"
#include "DisplayTypes.h"

#if DISPLAY_TYPE == DISPLAY_SSD1306
  #include "SSD1306Display.h"
#elif DISPLAY_TYPE == DISPLAY_ST7789
  #include "ST7789Display.h"
#else
  #include "NullDisplay.h"
#endif
