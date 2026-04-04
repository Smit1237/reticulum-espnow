#pragma once

// Display type constants — used in board_config.h
#define DISPLAY_NONE     0
#define DISPLAY_SSD1306  1
#define DISPLAY_ST7789   2

// DISPLAY_TYPE is defined per-board in board_config.h.
// If no board is selected, board_config.h falls back to DISPLAY_NONE.

#define HAS_DISPLAY (DISPLAY_TYPE != DISPLAY_NONE)
