/**
 * @file    TempRateFilter.h
 * @brief   Temperature change rate filter used by software brew detection (BREWDETECTION 1)
 *
 * Extracted unchanged from calculateTemperatureMovingAverage() so it can be unit tested.
 * Note: each stored rate compares the newest sample with the oldest one in the ring buffer
 * (about 14 samples / 5.6 s apart at 400 ms), and the average runs over 15 of those rates.
 */
#pragma once

#include <cmath>

class TempRateFilter {
  public:
    static const int numValues = 15;

    void reset(double temperature) {
        for (int index = 0; index < numValues; index++) {
            tempValues[index] = temperature;
            timeValues[index] = 0;
            tempChangeRates[index] = 0;
        }
    }

    /**
     * @brief add a sensor reading
     * @return average temperature change rate in 1/1000 °C per second (negative = falling)
     */
    double add(unsigned long nowMillis, double temperature) {
        timeValues[valueIndex] = nowMillis;
        tempValues[valueIndex] = temperature;

        int oldest = valueIndex == numValues - 1 ? 0 : valueIndex + 1;
        double tempChangeRate = (tempValues[valueIndex] - tempValues[oldest]) /
                                (timeValues[valueIndex] - timeValues[oldest]) * 10000;
        tempChangeRates[valueIndex] = tempChangeRate;

        double totalTempChangeRateSum = 0;
        for (int i = 0; i < numValues; i++) {
            totalTempChangeRateSum += tempChangeRates[i];
        }

        valueIndex = valueIndex >= numValues - 1 ? 0 : valueIndex + 1;

        return totalTempChangeRateSum / numValues * 100;
    }

  private:
    double tempValues[numValues] = {};
    unsigned long timeValues[numValues] = {};
    double tempChangeRates[numValues] = {};
    int valueIndex = 1;
};

/**
 * @brief start condition of software brew detection: temperature falls faster than the
 *        sensitivity while it is within 5 °C of the brew set point
 */
inline bool isSoftwareBrewStart(double tempRateAverage, double temperature, double brewSetPoint,
                                double brewSensitivity) {
    return tempRateAverage <= -brewSensitivity && fabs(temperature - brewSetPoint) < 5;
}
