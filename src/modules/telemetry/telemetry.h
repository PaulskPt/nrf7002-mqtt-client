/*! @file telemetry.h
 * @brief Implements the filtring
 *        EPOCH value and sensor Temperature value
 *        from a received MQTT message
 *        and store the data to be used by the module Display
 *
 * @author Paulus Schulinck (Github @PaulskPt)
 *    ===  Programming keeps the mind going ===

*/
#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdint.h>
#include <stdbool.h>

struct telemetry_data {
    int64_t epoch;
    float temperature;
    float humidity;
    float pressure;
    bool valid;
    bool epoch_logIt;
    bool temp_logIt;
};

extern struct telemetry_data g_telemetry;

#endif

