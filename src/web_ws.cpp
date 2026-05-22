// src/web_ws.cpp — WebSocket event handling and broadcast extracted from web.cpp
// This file is Arduino-only; it must not be compiled in native test builds.
#ifdef ARDUINO

#include "secrets.h"
#include "web.h"
#include "web_internal.h"
#include "auth.h"
#include <Arduino.h>
#include <ArduinoJson.h>

#ifndef WS_JSON_BUF_SIZE
// Worst-case buildJsonFull output ≈ 300 chars (212 fixed + 88 max-width values).
// 384 gives 80+ bytes of headroom for future field additions without silent truncation.
#define WS_JSON_BUF_SIZE 384
#endif
#ifndef WS_MAX_CLIENTS
#define WS_MAX_CLIENTS 8
#endif
#ifndef WS_AUTH_TIMEOUT_MS
#define WS_AUTH_TIMEOUT_MS 5000UL
#endif

// Per-client WebSocket authentication state
static bool     ws_authenticated[WS_MAX_CLIENTS] = {};
static uint32_t ws_connect_time[WS_MAX_CLIENTS]  = {};

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
  if (num >= WS_MAX_CLIENTS) return;

  switch (type) {
    case WStype_CONNECTED:
      ws_authenticated[num] = false;
      ws_connect_time[num]  = millis();
      // Send auth challenge — client must reply with {"token":"..."}
      webSocket.sendTXT(num, "{\"auth_required\":true}");
      break;

    case WStype_TEXT:
      if (!ws_authenticated[num]) {
        // Expect {"token":"<64-char hex>"}
        JsonDocument doc;
        if (deserializeJson(doc, (const char *)payload) != DeserializationError::Ok) {
          webSocket.sendTXT(num, "{\"error\":\"invalid_json\"}");
          webSocket.disconnect(num);
          return;
        }
        const char *token = doc["token"] | "";
        if (token[0] == '\0') {
          webSocket.sendTXT(num, "{\"error\":\"token_missing\"}");
          webSocket.disconnect(num);
          return;
        }
        const AuthSession *s = authValidateToken(
            g_auth_sessions, AUTH_MAX_SESSIONS,
            token, millis(), AUTH_TOKEN_TTL_MS);
        if (!s) {
          webSocket.sendTXT(num, "{\"error\":\"token_invalid\"}");
          webSocket.disconnect(num);
          return;
        }
        ws_authenticated[num] = true;
        // Push current readings now that client is authenticated
        char json[WS_JSON_BUF_SIZE];
        buildJson(json, sizeof(json));
        webSocket.sendTXT(num, json);
      }
      break;

    case WStype_DISCONNECTED:
      ws_authenticated[num] = false;
      ws_connect_time[num]  = 0;
      break;

    default:
      break;
  }
}

// Broadcast to authenticated WebSocket clients only
void wsBroadcastAuthenticated(const char *payload) {
  for (uint8_t i = 0; i < WS_MAX_CLIENTS; i++) {
    if (ws_authenticated[i]) {
      webSocket.sendTXT(i, payload);
    }
  }
}

// Disconnect clients that haven't authenticated within the timeout
void wsCheckAuthTimeouts() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < WS_MAX_CLIENTS; i++) {
    if (!ws_authenticated[i] && ws_connect_time[i] != 0 &&
        (now - ws_connect_time[i]) > WS_AUTH_TIMEOUT_MS) {
      webSocket.sendTXT(i, "{\"error\":\"auth_timeout\"}");
      webSocket.disconnect(i);
      ws_connect_time[i] = 0;
    }
  }
}

#endif // ARDUINO
