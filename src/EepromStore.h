/**
 * @file EepromStore.h
 *
 * @brief Persisted parameters in the EEPROM emulation, in the exact 3.3.0 layout so values
 *        stored by the original firmware survive the flash.
 *
 * Based on Storage.cpp of Clever Coffee 3.3.0.
 * Modified 2026 for the Silvia-only fork (github.com/64x-lunicorn/clevercoffee).
 */
#pragma once

#include "Params.h"

class EepromStore : public silvia::ParamStore {
  public:
    void begin();
    bool load(silvia::StorageSlot slot, bool isByte, double &value) override;
    void save(silvia::StorageSlot slot, bool isByte, double value) override;
    bool commit() override;
};
