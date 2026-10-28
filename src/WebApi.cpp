// library headers first: Arduino.h's round() macro breaks <random> if it comes before them
#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include "WebApi.h"

#include "StatusPage.h"

using namespace silvia;

namespace webapi {

namespace {

AsyncWebServer server(80);
Context ctx;

const size_t kMaxFields = 20;

bool authorized(AsyncWebServerRequest *request) {
    if (request->authenticate(ctx.user, ctx.password)) return true;
    request->requestAuthentication("silvia", false);  // basic auth: the browser asks once
    return false;
}

void sendJson(AsyncWebServerRequest *request, int code, size_t capacity,
              void (*write)(JsonOut &out)) {
    char *buf = (char *)malloc(capacity);
    if (!buf) {
        request->send(503, "application/json", "{\"error\":\"out_of_memory\"}");
        return;
    }
    JsonOut out(buf, capacity);
    write(out);
    if (out.overflow()) {
        request->send(500, "application/json", "{\"error\":\"overflow\"}");
    } else {
        AsyncWebServerResponse *response = request->beginResponse(code, "application/json", buf);
        response->addHeader("Cache-Control", "no-store");
        request->send(response);
    }
    free(buf);
}

void writeStateDoc(JsonOut &out) { writeState(out, ctx.core->snapshot(), ctx.device()); }
void writeParamsDoc(JsonOut &out) { writeParameters(out, ctx.core->params()); }

/** JSON body values as text, so they go through the same parser as every other input */
struct TextFields {
    const char *keys[kMaxFields];
    const char *values[kMaxFields];
    char storage[kMaxFields][24];
    size_t count = 0;
    bool tooMany = false;

    void add(const char *key, JsonVariant value) {
        if (count == kMaxFields) {
            tooMany = true;
            return;
        }
        char *text = storage[count];
        if (value.is<bool>()) {
            strcpy(text, value.as<bool>() ? "1" : "0");
        } else if (value.is<long>()) {
            snprintf(text, sizeof storage[0], "%ld", value.as<long>());
        } else if (value.is<double>()) {
            snprintf(text, sizeof storage[0], "%.6g", value.as<double>());
        } else if (value.is<const char *>()) {
            strncpy(text, value.as<const char *>(), sizeof storage[0] - 1);
            text[sizeof storage[0] - 1] = '\0';
        } else {
            text[0] = '\0';  // null, arrays, objects: invalid
        }
        keys[count] = key;
        values[count] = text;
        count++;
    }
};

FieldError fieldErrors[kMaxFields];
size_t fieldErrorCount;

void writeErrorsDoc(JsonOut &out) { writeFieldErrors(out, fieldErrors, fieldErrorCount); }

void applyFields(AsyncWebServerRequest *request, TextFields &fields, void (*onSuccess)(JsonOut &out)) {
    if (fields.tooMany) {
        request->send(413, "application/json", "{\"error\":\"too_many_fields\"}");
        return;
    }
    fieldErrorCount = ctx.core->setParams(fields.keys, fields.values, fields.count, fieldErrors, kMaxFields);
    if (fieldErrorCount > kMaxFields) fieldErrorCount = kMaxFields;
    if (fieldErrorCount) {
        sendJson(request, 400, 1024, writeErrorsDoc);
    } else {
        sendJson(request, 200, onSuccess == writeParamsDoc ? 6144 : 1024, onSuccess);
    }
}

void postParameters(AsyncWebServerRequest *request, JsonVariant &json) {
    if (!authorized(request)) return;
    if (!json.is<JsonObject>()) {
        request->send(400, "application/json", "{\"error\":\"expected_object\"}");
        return;
    }
    TextFields fields;
    for (JsonPair kv : json.as<JsonObject>()) fields.add(kv.key().c_str(), kv.value());
    applyFields(request, fields, writeParamsDoc);
}

void postCommand(AsyncWebServerRequest *request, JsonVariant &json) {
    if (!authorized(request)) return;
    if (!json.is<JsonObject>()) {
        request->send(400, "application/json", "{\"error\":\"expected_object\"}");
        return;
    }
    TextFields fields;
    for (JsonPair kv : json.as<JsonObject>()) {
        const char *key = kv.key().c_str();
        // commands are the runtime switches of the parameter table
        if (strcmp(key, "steam") == 0) key = "steam_on";
        else if (strcmp(key, "pid") == 0) key = "pid_on";
        else key = "unknown_command";
        fields.add(key, kv.value());
    }
    applyFields(request, fields, writeStateDoc);
}

}  // namespace

void begin(const Context &context) {
    ctx = context;

    server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest *request) {
        sendJson(request, 200, 1024, writeStateDoc);
    });
    server.on("/api/parameters", HTTP_GET, [](AsyncWebServerRequest *request) {
        sendJson(request, 200, 6144, writeParamsDoc);
    });

    auto *params = new AsyncCallbackJsonWebHandler("/api/parameters", postParameters, 1024);
    params->setMethod(HTTP_POST);
    params->setMaxContentLength(1024);
    server.addHandler(params);

    auto *command = new AsyncCallbackJsonWebHandler("/api/command", postCommand, 512);
    command->setMethod(HTTP_POST);
    command->setMaxContentLength(512);
    server.addHandler(command);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html; charset=utf-8", statusPage);
        request->send(response);
    });

    server.onNotFound([](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_POST && (request->url() == "/api/parameters" || request->url() == "/api/command")) {
            request->send(415, "application/json", "{\"error\":\"content_type_must_be_application_json\"}");
            return;
        }
        request->send(404, "application/json", "{\"error\":\"not_found\"}");
    });

    server.begin();
}

}  // namespace webapi
