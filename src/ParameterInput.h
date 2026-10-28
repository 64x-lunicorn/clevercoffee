/**
 * @file    ParameterInput.h
 * @brief   Validation of parameter values posted to /parameters
 *
 * The web interface posts every parameter, including the read-only ones. Before this check
 * the posted (possibly minutes old) TEMP value was written into the live temperature, and
 * values outside min/max were accepted.
 */
#pragma once

#include <cstdlib>
#include <cstring>

enum class ParamInput { Ok, ReadOnly, Invalid, OutOfRange };

inline bool isReadOnlyParameter(const char *name) {
    return strcmp(name, "TEMP") == 0 || strcmp(name, "VERSION") == 0;
}

/**
 * @brief parse a posted value and check it against the parameter's limits (inclusive)
 * @param integral the target is an int/uint8 variable, fractional values are rejected
 */
inline ParamInput parseParameterValue(const char *name, const char *text, double minValue,
                                      double maxValue, bool integral, double &value) {
    if (isReadOnlyParameter(name)) return ParamInput::ReadOnly;

    while (*text == ' ') text++;
    if (*text == '\0') return ParamInput::Invalid;

    char *end;
    double parsed = strtod(text, &end);
    while (*end == ' ') end++;
    if (*end != '\0' || parsed != parsed) return ParamInput::Invalid;  // trailing garbage or NaN
    if (integral && parsed != (double)(long)parsed) return ParamInput::Invalid;
    if (parsed < minValue || parsed > maxValue) return ParamInput::OutOfRange;

    value = parsed;
    return ParamInput::Ok;
}
