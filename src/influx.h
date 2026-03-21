// src/influx.h
#pragma once

#include "globals.h"
#include "auth_logic.h"  // for AuthLogEvent

#ifdef ARDUINO

// Writes sauna_status and sauna_control measurements to InfluxDB.
// NaN-valued fields are omitted. Called every INFLUX_WRITE_INTERVAL_MS.
bool writeInflux();

// Writes a login/logout/failure event to sauna_webaccess measurement.
// client_ip must be provided by the caller (not extracted internally).
// Callers pass: server.client().remoteIP().toString().c_str()
void logAccessEvent(const char *event,
                    const char *username,
                    const char *auth_source,
                    const char *client_ip);

#endif // ARDUINO
