// library headers first: Arduino.h's round() macro breaks <random> if it comes before them
#include <Arduino.h>
#include <U8g2lib.h>
#include <math.h>

#include "DisplayRenderer.h"

#include "Version.h"
#include "hardware.h"
#include "icon.h"

using namespace silvia;

namespace display {

namespace {

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);

void centered(const char *text, int y) {
    int w = u8g2.getUTF8Width(text);
    u8g2.drawUTF8((128 - w) / 2, y, text);
}

void icons(const Screen &s) {
    int x = 119;
    if (s.wifiLost) {
        u8g2.drawXBMP(x, 0, 8, 8, antenna_NOK_u8g2);
        x -= 11;
    }
    if (s.mqttLost) {
        u8g2.setFont(u8g2_font_5x7_tf);
        u8g2.drawStr(x + 2, 7, "M");
        u8g2.drawLine(x, 8, x + 9, 0);
    }
}

/**
 * the coffee cup, drawn from primitives; scale 1 is 13x16 px, scale 2 is 26x32 px
 * @param fill   0..1 coffee in the cup
 * @param steam  0..1 how much of the steam above it is shown (0 = none)
 */
void cup(int x, int y, int scale, float fill, float steam, uint8_t frame) {
    const int bw = 10 * scale, bh = 9 * scale, by = y + 6 * scale;
    u8g2.drawRFrame(x, by, bw, bh, 2);
    u8g2.drawFrame(x + bw - 1, by + 2 * scale, 3 * scale, 4 * scale);  // handle
    u8g2.drawHLine(x - scale, y + 16 * scale - 1, bw + 2 * scale);     // saucer
    int h = (int)((bh - 2) * fill + 0.5f);
    if (h > 0) u8g2.drawBox(x + 1, by + bh - 1 - h, bw - 2, h);

    // two wisps that wave sideways; the frame moves the wave upwards
    static const int8_t wave[] = {0, 1, 0, -1};
    int rows = (int)(5 * scale * steam + 0.5f);
    for (int wisp = 0; wisp < 2; wisp++) {
        int wx = x + (3 + 4 * wisp) * scale;
        for (int r = 0; r < rows; r++) {
            int row = 5 * scale - 1 - r;  // from the cup upwards
            u8g2.drawPixel(wx + wave[(row / scale + frame + 2 * wisp) % 4] * scale, y + row);
        }
    }
}

// the small cup in the bottom-right corner of the normal and shot screens
void smallCup(const Screen &s) {
    const int x = 113, y = 48;
    switch (s.cup) {
        case Cup::Empty: cup(x, y, 1, 0, 0, s.frame); break;
        case Cup::Heating: cup(x, y, 1, 0, s.cupFill, s.frame); break;
        case Cup::Ready: cup(x, y, 1, 1, 1, s.frame); break;
        case Cup::Filling: cup(x, y, 1, s.cupFill, 1, s.frame); break;
        default: break;
    }
}

void drawStandby(const Screen &s) {
    u8g2.drawXBMP(s.cupX, s.cupY, cat_width, cat_height, cat_bits);
    if (isnan(s.temperature)) return;
    char text[12];
    snprintf(text, sizeof text, "%.0f °C", s.temperature);
    u8g2.setFont(u8g2_font_6x10_tf);
    int w = u8g2.getUTF8Width(text);
    u8g2.drawUTF8(s.cupX + DisplayModel::kStandbyCupW / 2 - w / 2,
                  s.cupY + DisplayModel::kStandbyCupH + DisplayModel::kStandbyTextH - 1, text);
}

/**
 * move the finished frame buffer by dx/dy pixels (pixel shift against burn-in); the SSD1306
 * buffer is 8 pages of 128 columns, one byte per column holds 8 rows
 */
void shiftBuffer(int dx, int dy) {
    if (dx == 0 && dy == 0) return;
    uint8_t *buf = u8g2.getBufferPtr();
    uint64_t cols[128];
    for (int x = 0; x < 128; x++) {
        uint64_t c = 0;
        for (int p = 0; p < 8; p++) c |= (uint64_t)buf[p * 128 + x] << (8 * p);
        cols[x] = c;
    }
    for (int x = 0; x < 128; x++) {
        int from = x - dx;
        uint64_t c = from >= 0 && from < 128 ? cols[from] : 0;
        c = dy >= 0 ? c << dy : c >> -dy;
        for (int p = 0; p < 8; p++) buf[p * 128 + x] = (uint8_t)(c >> (8 * p));
    }
}

// one line in the column right of the large cat, centered; long lines get the smaller font
void rightColumn(const char *text, int y) {
    const int left = catLarge_width, width = 128 - catLarge_width;
    u8g2.setFont(u8g2_font_6x10_tf);
    if (u8g2.getUTF8Width(text) > width) u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawUTF8(left + (width - u8g2.getUTF8Width(text)) / 2, y, text);
}

// start and update screen: the large cat on the left, a title and up to two lines on the right
void startScreen(const char *title, const char *line1, const char *line2) {
    u8g2.drawXBMP(0, (64 - catLarge_height) / 2, catLarge_width, catLarge_height, catLarge_bits);
    u8g2.setFont(u8g2_font_helvB12_tf);
    const int width = 128 - catLarge_width;
    u8g2.drawUTF8(catLarge_width + (width - u8g2.getUTF8Width(title)) / 2, 22, title);
    if (line1) rightColumn(line1, 38);
    if (line2) rightColumn(line2, 52);
}

void drawSplash(const Screen &s) {
    char text[12];
    snprintf(text, sizeof text, "%.0f °C", s.temperature);
    startScreen("Silvia", SILVIA_VERSION, isnan(s.temperature) ? nullptr : text);
}

void drawError(const Screen &s, uint32_t nowMs) {
    bool blink = (nowMs / 500) % 2;
    u8g2.drawFrame(0, 0, 128, 64);
    if (blink) u8g2.drawFrame(2, 2, 124, 60);
    u8g2.setFont(u8g2_font_helvB12_tf);
    char line[24];
    if (s.error == ErrorKind::Sensor) {
        centered("FÜHLER-FEHLER", 26);
    } else {
        centered("ZU HEISS", 22);
        if (!isnan(s.temperature)) {
            snprintf(line, sizeof line, "%.0f °C", s.temperature);
            centered(line, 40);
        }
    }
    u8g2.setFont(u8g2_font_helvB10_tf);
    centered("Heizung aus", 57);
}

void drawShot(const Screen &s, uint32_t nowMs) {
    char text[8];
    snprintf(text, sizeof text, "%d", (int)floorf(s.timerS));

    bool inverse = s.timerLevel != TimerLevel::Neutral;
    if (s.timerLevel == TimerLevel::TooLong && !s.timerFinal) inverse = (nowMs / 400) % 2;  // blink
    if (inverse) {
        u8g2.drawBox(0, 0, 128, 64);
        u8g2.setDrawColor(0);
    }

    u8g2.setFont(u8g2_font_helvB10_tf);
    const char *title = s.timerFinal ? "Shot fertig" : "Shot";
    if (s.timerLevel == TimerLevel::TooLong) title = s.timerFinal ? "Shot: zu lang" : "zu lang";
    u8g2.drawUTF8(2, 12, title);

    u8g2.setFont(u8g2_font_logisoso42_tn);
    int w = u8g2.getStrWidth(text);
    u8g2.drawStr((116 - w) / 2, 62, text);
    u8g2.setFont(u8g2_font_helvB12_tf);
    u8g2.drawStr((116 + w) / 2 + 3, 62, "s");

    u8g2.setDrawColor(1);
    if (!inverse) icons(s);
    // the cup inverts with the screen: drawn in XOR it stays visible on the highlighted timer
    u8g2.setDrawColor(2);
    smallCup(s);
    u8g2.setDrawColor(1);
}

void drawNormal(const Screen &s) {
    char text[16];
    u8g2.setFont(u8g2_font_helvB10_tf);
    u8g2.drawUTF8(0, 12, s.stateLabel);
    icons(s);

    u8g2.setFont(u8g2_font_logisoso32_tn);
    snprintf(text, sizeof text, "%.1f", s.temperature);
    int w = u8g2.getStrWidth(text);
    u8g2.drawStr((116 - w) / 2, 50, text);
    u8g2.setFont(u8g2_font_helvB12_tf);
    u8g2.drawUTF8((116 + w) / 2 + 2, 30, "°C");

    u8g2.setFont(u8g2_font_6x10_tf);
    if (s.state == State::Off) {
        centered("Regelung aus", 63);
    } else {
        snprintf(text, sizeof text, "Soll %.1f", s.setpoint);
        centered(text, 63);
    }
    smallCup(s);
}

bool panelOn = true;
bool panelDim = false;

}  // namespace

void begin() {
    u8g2.setI2CAddress(0x3C * 2);
    u8g2.begin();
    u8g2.enableUTF8Print();
}

void bootMessage(const char *line1, const char *line2) {
    u8g2.clearBuffer();
    startScreen(line1, line2, nullptr);
    u8g2.sendBuffer();
}

void draw(const Screen &s, uint32_t nowMs) {
    if (s.displayOn != panelOn) {
        u8g2.setPowerSave(s.displayOn ? 0 : 1);
        panelOn = s.displayOn;
    }
    if (!panelOn) return;
    if (s.dim != panelDim) {
        u8g2.setContrast(s.dim ? 8 : 255);
        panelDim = s.dim;
    }

    u8g2.clearBuffer();
    u8g2.setDrawColor(1);
    switch (s.layout) {
        case Layout::Error: drawError(s, nowMs); break;
        case Layout::Shot: drawShot(s, nowMs); break;
        case Layout::Booting:
            u8g2.setFont(u8g2_font_helvB12_tf);
            centered("Start…", 36);
            icons(s);
            smallCup(s);
            break;
        case Layout::Normal: drawNormal(s); break;
        case Layout::Standby: drawStandby(s); break;
        case Layout::Splash: drawSplash(s); break;
    }
    shiftBuffer(s.shiftX, s.shiftY);
    u8g2.sendBuffer();
}

}  // namespace display
