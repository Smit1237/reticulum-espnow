#pragma once

#define DISPLAY_NONE     0
#define DISPLAY_SSD1306  1
#define DISPLAY_ST7789   2

#ifndef DISPLAY_TYPE
  #define DISPLAY_TYPE DISPLAY_NONE
#endif

#define HAS_DISPLAY (DISPLAY_TYPE != DISPLAY_NONE)
