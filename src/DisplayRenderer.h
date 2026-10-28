/**
 * @file DisplayRenderer.h
 * @brief Draws a silvia::Screen on the SSD1306. All decisions are in DisplayModel.
 */
#pragma once

#include "DisplayModel.h"

namespace display {

void begin();
void bootMessage(const char *line1, const char *line2);
void draw(const silvia::Screen &screen, uint32_t nowMs);

}  // namespace display
