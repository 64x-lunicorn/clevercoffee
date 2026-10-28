// Home Assistant over MQTT: discovery, payloads, cadence and validated commands.
#include <Arduino.h>
#include <unity.h>

#include <cstring>
#include <set>
#include <string>

#include "Mqtt.h"

using namespace silvia;

namespace {

void start(SilviaCore &core) {
    fakeMillis = 1000;
    core.begin(1000);
}

std::string discoveryDoc(int i, std::string &topic) {
    static char buf[1500];
    char t[96];
    JsonOut out(buf, sizeof buf);
    TEST_ASSERT_TRUE(mqtt::discovery(i, "silvia-4", "http://silvia/", t, sizeof t, out));
    TEST_ASSERT_FALSE(out.overflow());
    topic = t;
    return buf;
}

int count(const std::string &s, char c) {
    int n = 0;
    for (char x : s) n += x == c;
    return n;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_temperature_discovery_document() {
    std::string topic;
    std::string doc = discoveryDoc(0, topic);
    TEST_ASSERT_EQUAL_STRING("homeassistant/sensor/silvia/temperature/config", topic.c_str());
    TEST_ASSERT_EQUAL_STRING(
        "{\"name\":\"Temperatur\",\"unique_id\":\"silvia_temperature\",\"default_entity_id\":\"sensor.silvia_temperature\","
        "\"availability_topic\":\"silvia/status\",\"state_topic\":\"silvia/state\","
        "\"device_class\":\"temperature\",\"unit_of_measurement\":\"°C\","
        "\"value_template\":\"{{ value_json.temperature }}\",\"state_class\":\"measurement\","
        "\"suggested_display_precision\":1,"
        "\"device\":{\"identifiers\":[\"silvia\"],\"name\":\"Silvia\",\"manufacturer\":\"Rancilio\","
        "\"model\":\"Silvia E\",\"sw_version\":\"silvia-4\",\"configuration_url\":\"http://silvia/\"}}",
        doc.c_str());
}

void test_all_discovery_documents_are_well_formed_and_unique() {
    std::set<std::string> topics, ids;
    int controls = 0, diagnostics = 0;
    for (int i = 0; i < mqtt::discoveryCount(); i++) {
        std::string topic;
        std::string doc = discoveryDoc(i, topic);
        TEST_ASSERT_EQUAL_MESSAGE(count(doc, '{'), count(doc, '}'), topic.c_str());
        TEST_ASSERT_EQUAL_MESSAGE(count(doc, '['), count(doc, ']'), topic.c_str());
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(doc.c_str(), "\"availability_topic\":\"silvia/status\""), topic.c_str());
        TEST_ASSERT_TRUE(topics.insert(topic).second);
        size_t at = doc.find("\"unique_id\":\"");
        TEST_ASSERT_TRUE(ids.insert(doc.substr(at, doc.find('"', at + 13) - at)).second);
        if (doc.find("command_topic") != std::string::npos) controls++;
        if (doc.find("\"entity_category\":\"diagnostic\",\"enabled_by_default\":false") != std::string::npos) {
            diagnostics++;
        }
    }
    TEST_ASSERT_EQUAL(4, controls);  // brew/steam set point, steam, PID
    TEST_ASSERT_EQUAL(6, diagnostics);
    std::string t;
    char buf[8];
    JsonOut out(buf, sizeof buf);
    char topic[8];
    TEST_ASSERT_FALSE(mqtt::discovery(mqtt::discoveryCount(), "x", "", topic, sizeof topic, out));
}

void test_controls_point_at_validated_commands() {
    std::string topic;
    for (int i = 0; i < mqtt::discoveryCount(); i++) {
        std::string doc = discoveryDoc(i, topic);
        if (topic == "homeassistant/number/silvia/brew_setpoint/config") {
            TEST_ASSERT_NOT_NULL(strstr(doc.c_str(), "\"command_topic\":\"silvia/set/brew_setpoint\""));
            TEST_ASSERT_NOT_NULL(strstr(doc.c_str(), "\"min\":80.0,\"max\":105.0,\"step\":0.1,\"mode\":\"box\""));
        }
        if (topic == "homeassistant/switch/silvia/steam/config") {
            TEST_ASSERT_NOT_NULL(strstr(doc.c_str(), "\"value_template\":\"{{ 'ON' if value_json.steam_on else 'OFF' }}\""));
        }
        if (topic == "homeassistant/sensor/silvia/state/config") {
            TEST_ASSERT_NOT_NULL(strstr(doc.c_str(),
                "\"options\":[\"off\",\"booting\",\"heating\",\"ready\",\"shot\",\"recovering\",\"steam\",\"error\"]"));
        }
        if (topic == "homeassistant/event/silvia/shot/config") {
            TEST_ASSERT_NOT_NULL(strstr(doc.c_str(), "\"event_types\":[\"shot\"]"));
        }
    }
}

void test_state_settings_and_shot_payloads() {
    Snapshot s = {};
    s.temperature = 95.8f;
    s.setpoint = 96;
    s.heaterDuty = 312;
    s.state = State::Ready;
    s.ready = true;
    s.shotCount = 1;
    s.uptimeMs = 61000;
    s.hasLastShot = true;
    s.lastShot.durationS = 32.7f;
    DeviceInfo device = {"silvia-4", -61, true, true, 30000};
    char buf[512];
    JsonOut out(buf, sizeof buf);
    mqtt::writeState(out, s, device);
    TEST_ASSERT_EQUAL_STRING(
        "{\"temperature\":95.8,\"setpoint\":96.0,\"heater\":31.2,\"state\":\"ready\",\"ready\":true,"
        "\"error\":\"none\",\"shot_timer_s\":null,\"last_shot_s\":32.7,\"shot_count\":1,\"rssi\":-61,"
        "\"uptime_s\":61,\"version\":\"silvia-4\"}",
        buf);

    SilviaCore core;
    start(core);
    JsonOut settings(buf, sizeof buf);
    mqtt::writeSettings(settings, core.params());
    TEST_ASSERT_EQUAL_STRING(
        "{\"brew_setpoint\":96.0,\"steam_setpoint\":120.0,\"steam_on\":false,\"pid_on\":true,\"pid_kp\":62.0,"
        "\"pid_tn\":52.0,\"pid_tv\":11.5,\"pid_i_max\":75,\"bd_pid_on\":false}",
        buf);

    ShotSummary shot = {3, 100000, 1793086098, 32.7f, 95.9f, 88.1f, NAN, NAN, false};
    JsonOut ev(buf, sizeof buf);
    mqtt::writeShotEvent(ev, shot);
    TEST_ASSERT_EQUAL_STRING(
        "{\"event_type\":\"shot\",\"id\":3,\"duration_s\":32.7,\"start_temp\":95.9,\"min_temp\":88.1,"
        "\"start_unix\":1793086098}",
        buf);
}

void test_cadence_1s_active_10s_idle_and_on_change() {
    mqtt::Cadence c;
    Snapshot s = {};
    s.state = State::Heating;
    TEST_ASSERT_TRUE(c.due(0, s));
    TEST_ASSERT_FALSE(c.due(999, s));
    TEST_ASSERT_TRUE(c.due(1000, s));
    s.state = State::Ready;
    s.ready = true;
    TEST_ASSERT_TRUE(c.due(1100, s));  // change: at once
    TEST_ASSERT_FALSE(c.due(2100, s));
    TEST_ASSERT_FALSE(c.due(11099, s));
    TEST_ASSERT_TRUE(c.due(11100, s));
    s.state = State::Shot;
    s.ready = false;
    TEST_ASSERT_TRUE(c.due(11200, s));
    TEST_ASSERT_TRUE(c.due(12200, s));
}

void test_commands_go_through_the_parameter_table() {
    SilviaCore core;
    start(core);
    TEST_ASSERT_EQUAL(ParamError::None, mqtt::handleCommand(core, "silvia/set/brew_setpoint", "95.5"));
    TEST_ASSERT_EQUAL_DOUBLE(95.5, core.params().get(kBrewSetpoint));
    TEST_ASSERT_EQUAL(ParamError::OutOfRange, mqtt::handleCommand(core, "silvia/set/brew_setpoint", "150"));
    TEST_ASSERT_EQUAL(ParamError::Invalid, mqtt::handleCommand(core, "silvia/set/brew_setpoint", "warm"));
    TEST_ASSERT_EQUAL_DOUBLE(95.5, core.params().get(kBrewSetpoint));

    TEST_ASSERT_EQUAL(ParamError::None, mqtt::handleCommand(core, "silvia/set/steam_on", "ON"));
    TEST_ASSERT_TRUE(core.params().flag(kSteamOn));
    TEST_ASSERT_EQUAL(ParamError::None, mqtt::handleCommand(core, "silvia/set/pid_on", "OFF"));
    TEST_ASSERT_FALSE(core.params().flag(kPidOn));

    TEST_ASSERT_EQUAL(ParamError::ReadOnly, mqtt::handleCommand(core, "silvia/set/temperature", "20"));
    TEST_ASSERT_EQUAL(ParamError::Unknown, mqtt::handleCommand(core, "silvia/set/pid_kp", "10"));  // expert: web only
    TEST_ASSERT_EQUAL(ParamError::Unknown, mqtt::handleCommand(core, "silvia/set/BREW_SET_POINT", "95"));
    TEST_ASSERT_EQUAL(ParamError::Unknown, mqtt::handleCommand(core, "other/set/brew_setpoint", "95"));
    TEST_ASSERT_EQUAL_DOUBLE(62, core.params().get(kPidKp));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_temperature_discovery_document);
    RUN_TEST(test_all_discovery_documents_are_well_formed_and_unique);
    RUN_TEST(test_controls_point_at_validated_commands);
    RUN_TEST(test_state_settings_and_shot_payloads);
    RUN_TEST(test_cadence_1s_active_10s_idle_and_on_change);
    RUN_TEST(test_commands_go_through_the_parameter_table);
    return UNITY_END();
}
