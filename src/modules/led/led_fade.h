/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef LED_FADE_H_
#define LED_FADE_H_

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Starts or stops the asynchronous fading "breathing" effect on LED 2.
 * 
 * @param enable True to start the fading thread, false to stop it and turn off the LED.
 */
void led_fade_set_state(bool enable);

#ifdef __cplusplus
}
#endif

#endif /* LED_FADE_H_ */