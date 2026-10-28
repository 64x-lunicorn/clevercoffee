/**
 * @file WebApi.h
 * @brief HTTP API and web UI (see docs/api.md): reads are open, writes need basic auth (user
 *        "silvia"). All writes go through the core's parameter table. The web UI is one gzipped
 *        file in LittleFS (/www/index.html.gz, built by web/build.py).
 */
#pragma once

#include "History.h"
#include "Json.h"
#include "SilviaCore.h"

namespace webapi {

struct Context {
    silvia::SilviaCore *core;
    const silvia::History *history;
    const char *user;
    const char *password;
    silvia::DeviceInfo (*device)();
};

void begin(const Context &context);

/** event stream: the state once per second while someone listens */
void loop(uint32_t nowMs);

/** event stream: a discrete event (shot, error, ready) right away */
void publish(const silvia::Event &event);

}  // namespace webapi
