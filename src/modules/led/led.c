/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include "led_fade.h" // Include your new fade controller header
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/drivers/led.h>

#include "message_channel.h"

/* Register log module */
LOG_MODULE_REGISTER(led, CONFIG_MQTT_SAMPLE_LED_LOG_LEVEL);

const static struct device *led_device = DEVICE_DT_GET_ANY(gpio_leds);

/* LED 1: Wi-Fi connection status */
#define LED_1_GREEN 1

/* LED 2: MQTT connection status */
#define LED_2_GREEN 2


void led_callback(const struct zbus_channel *chan)
{
    int err = 0;

    if (!device_is_ready(led_device)) {
        LOG_ERR("LED device is not ready");
        return;
    }

    /*
     * Network/Wi-Fi connection status
     */
    if (&NETWORK_CHAN == chan) {

        const enum network_status *status;

        /* Get network status from channel. */
        status = zbus_chan_const_msg(chan);

        switch (*status) {

        case NETWORK_CONNECTED:
            err = led_on(led_device, LED_1_GREEN);
            if (err) {
                LOG_ERR("led_on (led nr: %d), error: %d",
                        LED_1_GREEN, err);
            }
            break;

        case NETWORK_DISCONNECTED:
            err = led_off(led_device, LED_1_GREEN);
            if (err) {
                LOG_ERR("led_off (led nr: %d), error: %d",
                        LED_1_GREEN, err);
            }
            break;

        default:
            LOG_ERR("Unknown network event: %d", *status);
            break;
        }

    /*
     * MQTT connection status
     */
    } else if (&MQTT_CHAN == chan) {

        const enum mqtt_status *status;

        /* Get MQTT status from channel. */
        status = zbus_chan_const_msg(chan);

        switch (*status) {

        case MQTT_STATUS_CONNECTED:
#if defined(CONFIG_PWM)
            /* Safely kick off the asynchronous breathing thread (e.g., 2000ms cycle) */
            led_fade_set_state(true);
#else
            err = led_on(led_device, LED_2_GREEN);

            if (err) {
                LOG_ERR("led_on (led nr: %d), error: %d",
                        LED_2_GREEN, err);
            }
#endif
            break;

        case MQTT_STATUS_DISCONNECTED:
#if defined(CONFIG_PWM)
            /* Stop the breathing thread and turn off the PWM channel */
            led_fade_set_state(false);
#else
            err = led_off(led_device, LED_2_GREEN);
            if (err) {
                LOG_ERR("led_off (led nr: %d), error: %d",
                        LED_2_GREEN, err);
            }
#endif
            break;

        default:
            LOG_ERR("Unknown MQTT event: %d", *status);
            break;
        }

    } else {

        LOG_WRN("Message received from unknown zbus channel");
    }
}


/*
 * Register listener.
 *
 * led_callback will be called every time a channel that this module
 * listens on receives a new message.
 */
ZBUS_LISTENER_DEFINE(led, led_callback);