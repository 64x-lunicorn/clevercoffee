/**
 * @file EepromStore.cpp
 *
 * Based on Storage.cpp of Clever Coffee 3.3.0.
 * Modified 2026 for the Silvia-only fork (github.com/64x-lunicorn/clevercoffee).
 */
#include "EepromStore.h"

#include <Arduino.h>
#include <EEPROM.h>
#include <stddef.h>

#include "ISR.h"

using namespace silvia;

namespace {

// storage data structure of 3.3.0, unchanged (unused fields keep their bytes)
typedef struct __attribute__((packed)) {
    // Any 'freeToUse' areas ensure the compatibility to origin EEPROM layout and can be used by new value.
    double pidKpRegular;
    uint8_t reserved1[2];
    double pidTnRegular;
    uint8_t pidOn;
    uint8_t freeToUse1;
    double pidTvRegular;
    double pidIMaxRegular;
    uint8_t freeToUse2;
    double brewSetpoint;
    double brewTempOffset;
    uint8_t freeToUse3;
    double brewTimeMs;
    uint8_t freeToUse4[2];
    double preInfusionTimeMs;
    uint8_t freeToUse5[2];
    double preInfusionPauseMs;
    uint8_t freeToUse6[21];
    uint8_t pidBdOn;
    double pidKpBd;
    uint8_t freeToUse7[2];
    double pidTnBd;
    uint8_t freeToUse8[2];
    double pidTvBd;
    uint8_t freeToUse9[2];
    double brewSwTimeSec;
    uint8_t freeToUse10[2];
    double brewDetectionThreshold;
    uint8_t freeToUse11;
    uint8_t useStartPonM;
    double pidKpStart;
    uint8_t freeToUse12[2];
    uint8_t softApEnabledCheck;
    uint8_t freeToUse13[9];
    double pidTnStart;
    uint8_t freeToUse14[2];
    char wifiSSID[25 + 1];
    char wifiPassword[25 + 1];
    double weightsetpoint;
    double steamkp;
    double steamsetpoint;
} sto_data_t;

struct SlotInfo {
    size_t addr;
    size_t size;
};

bool slotInfo(StorageSlot slot, SlotInfo &info) {
    switch (slot) {
        case kSlotPidKpRegular: info = {offsetof(sto_data_t, pidKpRegular), sizeof(double)}; return true;
        case kSlotPidTnRegular: info = {offsetof(sto_data_t, pidTnRegular), sizeof(double)}; return true;
        case kSlotPidTvRegular: info = {offsetof(sto_data_t, pidTvRegular), sizeof(double)}; return true;
        case kSlotBrewSetpoint: info = {offsetof(sto_data_t, brewSetpoint), sizeof(double)}; return true;
        case kSlotPidKpBd: info = {offsetof(sto_data_t, pidKpBd), sizeof(double)}; return true;
        case kSlotPidTnBd: info = {offsetof(sto_data_t, pidTnBd), sizeof(double)}; return true;
        case kSlotPidTvBd: info = {offsetof(sto_data_t, pidTvBd), sizeof(double)}; return true;
        case kSlotBrewSwTime: info = {offsetof(sto_data_t, brewSwTimeSec), sizeof(double)}; return true;
        case kSlotBdThreshold: info = {offsetof(sto_data_t, brewDetectionThreshold), sizeof(double)}; return true;
        case kSlotPidOn: info = {offsetof(sto_data_t, pidOn), sizeof(uint8_t)}; return true;
        case kSlotPidKpSteam: info = {offsetof(sto_data_t, steamkp), sizeof(double)}; return true;
        case kSlotPidIMaxRegular: info = {offsetof(sto_data_t, pidIMaxRegular), sizeof(double)}; return true;
        case kSlotBrewTempOffset: info = {offsetof(sto_data_t, brewTempOffset), sizeof(double)}; return true;
        case kSlotUseBdPid: info = {offsetof(sto_data_t, pidBdOn), sizeof(uint8_t)}; return true;
        case kSlotSteamSetpoint: info = {offsetof(sto_data_t, steamsetpoint), sizeof(double)}; return true;
        default: return false;
    }
}

}  // namespace

void EepromStore::begin() {
    EEPROM.begin(sizeof(sto_data_t));
}

bool EepromStore::load(StorageSlot slot, bool isByte, double &value) {
    SlotInfo info;
    if (!slotInfo(slot, info) || info.size != (isByte ? 1u : sizeof(double))) return false;

    const uint8_t *bytes = EEPROM.getConstDataPtr() + info.addr;
    bool empty = true;  // erased flash: never written
    for (size_t i = 0; i < info.size; i++) empty &= bytes[i] == 0xFF;
    if (empty) return false;

    if (isByte) {
        value = bytes[0];
    } else {
        double stored;
        memcpy(&stored, bytes, sizeof stored);
        value = stored;
    }
    return true;
}

void EepromStore::save(StorageSlot slot, bool isByte, double value) {
    SlotInfo info;
    if (!slotInfo(slot, info) || info.size != (isByte ? 1u : sizeof(double))) return;
    if (isByte) {
        EEPROM.write(info.addr, (uint8_t)value);
    } else {
        EEPROM.put(info.addr, value);
    }
}

bool EepromStore::commit() {
    // no code may run from flash while it is erased/written, including the heater ISR's digitalWrite
    HeaterIsrPause heaterIsrPause;
    return EEPROM.commit();
}
