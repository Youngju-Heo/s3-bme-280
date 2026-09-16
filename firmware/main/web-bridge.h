#pragma once
#include <stdbool.h>
#include <stddef.h>

// Translates an HTTP method + URL query into a protocol request line, allowing only read-only
// commands over GET and set_time over POST. Returns false for anything else or malformed input.
bool web_bridge_build_request(const char *method, const char *query, char *out, size_t out_len);
