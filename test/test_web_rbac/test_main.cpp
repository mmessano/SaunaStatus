#define ARDUINO 1
#define FIRMWARE_VERSION "2.0.0"

#include <unity.h>
#include <cstring>

#include "auth_logic.h"
#include "globals.h"

// Include the real handler translation units under the native stubbed Arduino layer.
#include "../../src/web_auth.cpp"
#include "../../src/web.cpp"

static uint8_t g_randCounter = 0;

static void testRandFn(uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; ++i) buf[i] = g_randCounter++;
}

static void resetAuthState() {
    std::memset(g_auth_sessions, 0, sizeof(g_auth_sessions));
    std::memset(&g_auth_users, 0, sizeof(g_auth_users));
    std::memset(&g_rate_limiter, 0, sizeof(g_rate_limiter));
    g_randCounter = 0;
}

static void resetRuntimeState() {
    server.reset();
    server.setRemoteIP("192.168.1.77");
    HTTPClient::setPostCode(204);
    g_sensor_read_interval_ms = 5000UL;
    g_serial_log_interval_ms = 10000UL;
    std::strncpy(g_static_ip_str, "192.168.1.201", 15);
    g_static_ip_str[15] = '\0';
    std::strncpy(g_device_name, "ESP32-S3", 24);
    g_device_name[24] = '\0';
    Ceilingpoint = 71.1f;
    Benchpoint = 48.9f;
    ceiling_pid_en = false;
    bench_pid_en = false;
}

static void addBearerHeaderForRole(const char *username, const char *role) {
    char token[65];
    bool ok = authIssueToken(g_auth_sessions, AUTH_MAX_SESSIONS,
                             username, role, millis(), testRandFn, token);
    TEST_ASSERT_TRUE(ok);
    char header[80];
    std::snprintf(header, sizeof(header), "Bearer %s", token);
    server.setHeaderValue("Authorization", header);
}

typedef void (*HandlerFn)();
typedef void (*RequestSetupFn)();

static void prepareNoArgs() {}

static void prepareMotorStop() {
    server.setArgValue("motor", "outflow");
    server.setArgValue("cmd", "stop");
}

static void prepareConfigSave() {
    server.setArgValue("ceiling_setpoint_f", "170");
}

static void prepareUsersCreate() {
    server.setArgValue("plain", "{\"username\":\"viewer2\",\"password\":\"goodpass1\",\"role\":\"viewer\"}");
}

static void prepareUsersDelete() {
    g_auth_users.count = 2;
    std::strncpy(g_auth_users.users[0].name, "admin", sizeof(g_auth_users.users[0].name) - 1);
    std::strncpy(g_auth_users.users[0].role, "admin", sizeof(g_auth_users.users[0].role) - 1);
    std::strncpy(g_auth_users.users[1].name, "viewer2", sizeof(g_auth_users.users[1].name) - 1);
    std::strncpy(g_auth_users.users[1].role, "viewer", sizeof(g_auth_users.users[1].role) - 1);
    g_auth_users.users[0].active = true;
    g_auth_users.users[1].active = true;
    server.setArgValue("username", "viewer2");
}

static void prepareUsersChangePassword() {
    g_auth_users.count = 2;
    std::strncpy(g_auth_users.users[0].name, "admin", sizeof(g_auth_users.users[0].name) - 1);
    std::strncpy(g_auth_users.users[0].role, "admin", sizeof(g_auth_users.users[0].role) - 1);
    std::strncpy(g_auth_users.users[1].name, "viewer2", sizeof(g_auth_users.users[1].name) - 1);
    std::strncpy(g_auth_users.users[1].role, "viewer", sizeof(g_auth_users.users[1].role) - 1);
    g_auth_users.users[0].active = true;
    g_auth_users.users[1].active = true;
    server.setArgValue("username", "viewer2");
    server.setArgValue("plain", "{\"password\":\"newpass12\"}");
}

static void preparePidToggle() {
    server.setArgValue("ceiling", "1");
    server.setArgValue("bench", "0");
}

static void prepareSetpointUpdate() {
    server.setArgValue("ceiling", "170");
    server.setArgValue("bench", "130");
}

static void addAdminHeader() {
    addBearerHeaderForRole("admin_user", "admin");
}

static void assertAdminOnlyRoute(HandlerFn handler,
                                 RequestSetupFn setup,
                                 int expected_admin_status) {
    resetAuthState();
    resetRuntimeState();
    setup();
    handler();
    TEST_ASSERT_EQUAL_INT_MESSAGE(401, server.statusCode(), "missing token should be unauthorized");

    resetRuntimeState();
    setup();
    addBearerHeaderForRole("viewer_user", "viewer");
    handler();
    TEST_ASSERT_EQUAL_INT_MESSAGE(403, server.statusCode(), "viewer should be forbidden");

    resetRuntimeState();
    setup();
    addBearerHeaderForRole("admin_user", "admin");
    handler();
    TEST_ASSERT_EQUAL_INT_MESSAGE(expected_admin_status, server.statusCode(), "admin should reach handler logic");
}

void setUp(void) {}
void tearDown(void) {}

void test_users_get_requires_admin(void) {
    g_auth_users.count = 1;
    std::strncpy(g_auth_users.users[0].name, "admin", sizeof(g_auth_users.users[0].name) - 1);
    std::strncpy(g_auth_users.users[0].role, "admin", sizeof(g_auth_users.users[0].role) - 1);
    g_auth_users.users[0].active = true;
    assertAdminOnlyRoute(handleUsersGet, prepareNoArgs, 200);
}

void test_users_create_requires_admin(void) {
    assertAdminOnlyRoute(handleUsersCreate, prepareUsersCreate, 200);
}

void test_users_delete_requires_admin(void) {
    assertAdminOnlyRoute(handleUsersDelete, prepareUsersDelete, 200);
}

void test_users_change_password_requires_admin(void) {
    assertAdminOnlyRoute(handleUsersChangePassword, prepareUsersChangePassword, 200);
}

void test_config_save_requires_admin(void) {
    assertAdminOnlyRoute(handleConfigSave, prepareConfigSave, 200);
}

void test_motor_requires_admin(void) {
    assertAdminOnlyRoute(handleMotorCmd, prepareMotorStop, 200);
}

void test_log_requires_admin(void) {
    assertAdminOnlyRoute(handleLog, prepareNoArgs, 200);
}

void test_delete_status_requires_admin(void) {
    assertAdminOnlyRoute(handleDeleteStatus, prepareNoArgs, 200);
}

void test_delete_control_requires_admin(void) {
    assertAdminOnlyRoute(handleDeleteControl, prepareNoArgs, 200);
}

void test_setpoint_requires_admin(void) {
    assertAdminOnlyRoute(handleSetpoint, prepareNoArgs, 200);
}

void test_pid_requires_admin(void) {
    assertAdminOnlyRoute(handlePidToggle, prepareNoArgs, 200);
}

void test_ota_status_requires_admin(void) {
    assertAdminOnlyRoute(handleOtaStatus, prepareNoArgs, 200);
}

void test_ota_update_requires_admin(void) {
    assertAdminOnlyRoute(handleOtaUpdate, prepareNoArgs, 400);
}

void test_users_create_admin_adds_user(void) {
    resetAuthState();
    resetRuntimeState();
    addAdminHeader();
    prepareUsersCreate();

    handleUsersCreate();

    TEST_ASSERT_EQUAL_INT(200, server.statusCode());
    TEST_ASSERT_EQUAL_INT(1, g_auth_users.count);
    TEST_ASSERT_TRUE(g_auth_users.users[0].active);
    TEST_ASSERT_EQUAL_STRING("viewer2", g_auth_users.users[0].name);
    TEST_ASSERT_EQUAL_STRING("viewer", g_auth_users.users[0].role);
    TEST_ASSERT_EQUAL_UINT16(AUTH_PBKDF2_ITERATIONS, g_auth_users.users[0].iterations);
}

void test_users_delete_admin_removes_target_user(void) {
    resetAuthState();
    resetRuntimeState();
    prepareUsersDelete();
    addAdminHeader();

    handleUsersDelete();

    TEST_ASSERT_EQUAL_INT(200, server.statusCode());
    TEST_ASSERT_EQUAL_INT(1, g_auth_users.count);
    TEST_ASSERT_EQUAL_STRING("admin", g_auth_users.users[0].name);
}

void test_users_change_password_admin_rehashes_user(void) {
    resetAuthState();
    resetRuntimeState();
    prepareUsersChangePassword();
    std::strncpy(g_auth_users.users[1].hash, "legacyhash", sizeof(g_auth_users.users[1].hash) - 1);
    std::strncpy(g_auth_users.users[1].salt, "legacysalt", sizeof(g_auth_users.users[1].salt) - 1);
    g_auth_users.users[1].iterations = 0;
    addAdminHeader();

    handleUsersChangePassword();

    TEST_ASSERT_EQUAL_INT(200, server.statusCode());
    TEST_ASSERT_EQUAL_UINT16(AUTH_PBKDF2_ITERATIONS, g_auth_users.users[1].iterations);
    TEST_ASSERT_NOT_EQUAL('\0', g_auth_users.users[1].hash[0]);
    TEST_ASSERT_NOT_EQUAL('\0', g_auth_users.users[1].salt[0]);
    TEST_ASSERT_TRUE(std::strcmp("legacyhash", g_auth_users.users[1].hash) != 0);
    TEST_ASSERT_TRUE(std::strcmp("legacysalt", g_auth_users.users[1].salt) != 0);
}

void test_config_save_admin_updates_runtime_state(void) {
    resetAuthState();
    resetRuntimeState();
    addAdminHeader();
    server.setArgValue("ceiling_setpoint_f", "180");
    server.setArgValue("bench_setpoint_f", "140");
    server.setArgValue("ceiling_pid_en", "true");
    server.setArgValue("bench_pid_en", "true");
    server.setArgValue("sensor_read_interval_ms", "6000");
    server.setArgValue("serial_log_interval_ms", "12000");

    handleConfigSave();

    TEST_ASSERT_EQUAL_INT(200, server.statusCode());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, f2c(180.0f), Ceilingpoint);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, f2c(140.0f), Benchpoint);
    TEST_ASSERT_TRUE(ceiling_pid_en);
    TEST_ASSERT_TRUE(bench_pid_en);
    TEST_ASSERT_EQUAL_UINT32(6000UL, g_sensor_read_interval_ms);
    TEST_ASSERT_EQUAL_UINT32(12000UL, g_serial_log_interval_ms);
}

void test_pid_admin_toggles_flags(void) {
    resetAuthState();
    resetRuntimeState();
    addAdminHeader();
    preparePidToggle();

    handlePidToggle();

    TEST_ASSERT_EQUAL_INT(200, server.statusCode());
    TEST_ASSERT_TRUE(ceiling_pid_en);
    TEST_ASSERT_FALSE(bench_pid_en);
}

void test_motor_admin_updates_target_and_direction(void) {
    resetAuthState();
    resetRuntimeState();
    addAdminHeader();
    server.setArgValue("motor", "outflow");
    server.setArgValue("cmd", "cw");
    server.setArgValue("steps", "100");

    handleMotorCmd();

    TEST_ASSERT_EQUAL_INT(200, server.statusCode());
    TEST_ASSERT_EQUAL_INT(100, outflow_target);
    TEST_ASSERT_EQUAL_INT(1, outflow_dir);
    TEST_ASSERT_EQUAL_UINT16(9, outflow_pos);
}

void test_setpoint_admin_updates_celsius_targets(void) {
    resetAuthState();
    resetRuntimeState();
    addAdminHeader();
    prepareSetpointUpdate();

    handleSetpoint();

    TEST_ASSERT_EQUAL_INT(200, server.statusCode());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, f2c(170.0f), Ceilingpoint);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, f2c(130.0f), Benchpoint);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_users_get_requires_admin);
    RUN_TEST(test_users_create_requires_admin);
    RUN_TEST(test_users_delete_requires_admin);
    RUN_TEST(test_users_change_password_requires_admin);
    RUN_TEST(test_config_save_requires_admin);
    RUN_TEST(test_motor_requires_admin);
    RUN_TEST(test_log_requires_admin);
    RUN_TEST(test_delete_status_requires_admin);
    RUN_TEST(test_delete_control_requires_admin);
    RUN_TEST(test_setpoint_requires_admin);
    RUN_TEST(test_pid_requires_admin);
    RUN_TEST(test_ota_status_requires_admin);
    RUN_TEST(test_ota_update_requires_admin);
    RUN_TEST(test_users_create_admin_adds_user);
    RUN_TEST(test_users_delete_admin_removes_target_user);
    RUN_TEST(test_users_change_password_admin_rehashes_user);
    RUN_TEST(test_config_save_admin_updates_runtime_state);
    RUN_TEST(test_pid_admin_toggles_flags);
    RUN_TEST(test_motor_admin_updates_target_and_direction);
    RUN_TEST(test_setpoint_admin_updates_celsius_targets);
    return UNITY_END();
}
