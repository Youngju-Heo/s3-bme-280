#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Minimal accessor for flat JSON objects with string / non-negative integer values.
// Only \" and \\ escapes are supported; nesting and arrays are not. That is all the protocol needs.
bool json_mini_get_string(const char *json, const char *key, char *out, size_t out_len);
bool json_mini_get_uint(const char *json, const char *key, uint32_t *out);
bool json_mini_has_key(const char *json, const char *key);
