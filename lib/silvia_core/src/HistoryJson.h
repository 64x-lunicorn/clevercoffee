/**
 * @file HistoryJson.h
 * @brief GET /api/history (about 14 KB) produced piece by piece for a chunked HTTP response,
 *        so it never has to exist in RAM as a whole.
 *
 * {"interval_s":1,"end_uptime_s":1234,"states":["off",...],
 *  "temperature":[95.1,...],"setpoint":[96.0,...],"heater":[27.6,...],"state":[3,...]}
 *
 * Oldest sample first; "state" holds indexes into "states"; unknown temperatures are null.
 */
#pragma once

#include <stddef.h>

#include "History.h"

namespace silvia {

class HistoryJson {
  public:
    explicit HistoryJson(const History &history) : history(history), count(history.sampleCount()) {}

    /**
     * @brief copy the next part of the document into buf
     * @return bytes written, 0 when the document is complete
     */
    size_t read(char *buf, size_t max);

  private:
    bool render();  // next piece into `piece`, false when done

    const History &history;
    int count;       // fixed at construction: samples added meanwhile are not sent
    int section = 0;
    int index = 0;
    char piece[200];
    size_t pieceLen = 0;
    size_t piecePos = 0;
};

}  // namespace silvia
