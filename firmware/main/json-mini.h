#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Minimal accessor for flat JSON objects with string / non-negative integer values.
// No escape sequences, nesting or arrays are supported; that is all the protocol needs.
bool json_mini_get_string(const char *json, const char *key, char *out, size_t out_len);
bool json_mini_get_uint(const char *json, const char *key, uint32_t *out);
bool json_mini_has_key(const char *json, const char *key);
