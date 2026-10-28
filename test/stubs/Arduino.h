// Minimal Arduino shim for host-side unit tests.
#pragma once

#include <cmath>
#include <cstdint>

inline unsigned long fakeMillis = 0;

inline unsigned long millis() { return fakeMillis; }
