// src/mqtt.h
#pragma once

#include "globals.h"

#ifdef ARDUINO

// Connects to MQTT broker, subscribes to control topics, publishes HA discovery.
void mqttConnect();

// PubSubClient callback — handles incoming messages on subscribed topics.
void mqttCallback(char *topic, byte *payload, unsigned int len);

// Publishes current sauna state JSON to sauna/state.
void mqttPublishState();

// Publishes Home Assistant MQTT Discovery configs (retained).
void mqttPublishDiscovery();

#endif // ARDUINO
