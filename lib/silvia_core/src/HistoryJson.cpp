#include "HistoryJson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace silvia {

namespace {
enum Section { kHead, kTemperature, kSetpoint, kHeater, kState, kTail, kDone };
const char *const sectionKeys[] = {"", "temperature", "setpoint", "heater", "state"};
}  // namespace

namespace {
struct Appender {
    char *buf;
    size_t cap;
    size_t len;
    void raw(const char *text) {
        while (*text && len + 1 < cap) buf[len++] = *text++;
        buf[len] = '\0';
    }
};
}  // namespace

bool HistoryJson::render() {
    Appender out = {piece, sizeof piece, 0};
    pieceLen = 0;
    piecePos = 0;

    switch (section) {
        case kHead: {
            // the samples may have moved on since construction: the newest one we send
            int skipped = history.sampleCount() - count;
            uint32_t endMs = history.lastSampleMs() - (uint32_t)(skipped > 0 ? skipped : 0) * History::kIntervalMs;
            out.raw("{\"interval_s\":1,\"end_uptime_s\":");
            char text[16];
            snprintf(text, sizeof text, "%lu", (unsigned long)(endMs / 1000));
            out.raw(text);
            out.raw(",\"states\":[");
            for (int s = (int)State::Off; s <= (int)State::Error; s++) {
                if (s) out.raw(",");
                out.raw("\"");
                out.raw(stateName((State)s));
                out.raw("\"");
            }
            out.raw("]");
            section = kTemperature;
            index = 0;
            break;
        }
        case kTemperature:
        case kSetpoint:
        case kHeater:
        case kState: {
            if (index == 0) {
                out.raw(",\"");
                out.raw(sectionKeys[section]);
                out.raw("\":[");
            }
            // a few samples per piece keeps the per-call overhead low
            for (int n = 0; n < 8 && index < count; n++, index++) {
                // oldest first; index into the history counted from the oldest sample we send
                const HistorySample &s = history.sample(index + (history.sampleCount() - count));
                if (index) out.raw(",");
                char text[12];
                switch (section) {
                    case kTemperature:
                        if (s.tempTenths == History::kNoTemp) {
                            out.raw("null");
                            continue;
                        }
                        snprintf(text, sizeof text, "%d.%d", s.tempTenths / 10, abs(s.tempTenths % 10));
                        if (s.tempTenths < 0 && s.tempTenths > -10) out.raw("-");
                        break;
                    case kSetpoint:
                        snprintf(text, sizeof text, "%d.%d", s.setpointTenths / 10, abs(s.setpointTenths % 10));
                        break;
                    case kHeater: {
                        int tenths = s.heaterQuarter * 4;  // duty in ‰ = heater in 1/10 %
                        snprintf(text, sizeof text, "%d.%d", tenths / 10, tenths % 10);
                        break;
                    }
                    default:
                        snprintf(text, sizeof text, "%d", s.state);
                }
                out.raw(text);
            }
            if (index >= count) {
                out.raw("]");
                section++;
                index = 0;
            }
            break;
        }
        case kTail:
            out.raw("}");
            section = kDone;
            break;
        default:
            return false;
    }

    pieceLen = out.len;
    return true;
}

size_t HistoryJson::read(char *buf, size_t max) {
    size_t written = 0;
    while (written < max) {
        if (piecePos == pieceLen && !render()) break;
        size_t n = pieceLen - piecePos;
        if (n > max - written) n = max - written;
        memcpy(buf + written, piece + piecePos, n);
        piecePos += n;
        written += n;
    }
    return written;
}

}  // namespace silvia
