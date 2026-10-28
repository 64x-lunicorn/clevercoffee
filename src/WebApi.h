/**
 * @file WebApi.h
 * @brief HTTP API (see docs/api.md): reads are open, writes need basic auth (user "silvia").
 *        All writes go through the core's parameter table.
 */
#pragma once

#include "Json.h"
#include "SilviaCore.h"

namespace webapi {

struct Context {
    silvia::SilviaCore *core;
    const char *user;
    const char *password;
    silvia::DeviceInfo (*device)();
};

void begin(const Context &context);

}  // namespace webapi
