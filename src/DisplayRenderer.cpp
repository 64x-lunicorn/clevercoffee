// library headers first: Arduino.h's round() macro breaks <random> if it comes before them
#include <Arduino.h>
#include <U8g2lib.h>
#include <math.h>

#include "DisplayRenderer.h"

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
}

}  // namespace

void begin() {
    u8g2.setI2CAddress(0x3C * 2);
    u8g2.begin();
    u8g2.enableUTF8Print();
}

void bootMessage(const char *line1, const char *line2) {
    u8g2.clearBuffer();
    u8g2.drawXBMP(44, 0, startLogoRancilio_width, startLogoRancilio_height, startLogoRancilio_bits);
    u8g2.setFont(u8g2_font_6x10_tf);
    centered(line1, 52);
    centered(line2, 63);
    u8g2.sendBuffer();
}

void draw(const Screen &s, uint32_t nowMs) {
    u8g2.clearBuffer();
    u8g2.setDrawColor(1);
    switch (s.layout) {
        case Layout::Error: drawError(s, nowMs); break;
        case Layout::Shot: drawShot(s, nowMs); break;
        case Layout::Booting:
            u8g2.setFont(u8g2_font_helvB12_tf);
            centered("Start…", 36);
            icons(s);
            break;
        case Layout::Normal: drawNormal(s); break;
    }
    u8g2.sendBuffer();
}

}  // namespace display
