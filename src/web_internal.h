// src/web_internal.h
#pragma once

#ifdef ARDUINO

// Shared Arduino-only helpers used across the split web translation units.
bool ensureLittleFsMounted();

#endif // ARDUINO
