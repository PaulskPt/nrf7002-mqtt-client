/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>
#include "led_fade.h"

LOG_MODULE_REGISTER(led_fade, CONFIG_MQTT_SAMPLE_LED_LOG_LEVEL);

/* Fetch the PWM configuration for LED 2 from the device tree */
static const struct pwm_dt_spec led2_pwm = PWM_DT_SPEC_GET(DT_NODELABEL(pwm_led_2));


/* Thread control variables */
static bool running;
static struct k_sem toggle_sem;

#define STACK_SIZE 1024
#define PRIORITY 7  // Low priority background task

K_THREAD_STACK_DEFINE(fade_thread_stack, STACK_SIZE);
static struct k_thread fade_thread_data;

/**
 * @brief Thread entry point handling the fading (breathing) loop.
 */
static void fade_thread_handler(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    if (!pwm_is_ready_dt(&led2_pwm)) {
        LOG_ERR("PWM device for LED 2 is not ready");
        return;
    }

    uint32_t duration_ms = 2000;                // 2-second breathing cycle
    uint32_t pulse_step = led2_pwm.period / 100; // 50 smooth steps
    uint32_t step_delay = duration_ms / 100;    // Split into fade-in and fade-out chunks

    while (1) {
        // Wait until led_fade_set_state(true) signals this semaphore
        k_sem_take(&toggle_sem, K_FOREVER);

        LOG_INF("LED 2 fading effect started");

        while (running) {
            // 1. Fade In
            for (uint32_t pulse = 0; pulse <= led2_pwm.period && running; pulse += pulse_step) {
                pwm_set_pulse_dt(&led2_pwm, pulse);
                k_msleep(step_delay);
            }

            // 2. Fade Out
            for (uint32_t pulse = led2_pwm.period; pulse > 0 && running; pulse -= pulse_step) {
                pwm_set_pulse_dt(&led2_pwm, pulse);
                k_msleep(step_delay);
            }

            // Small floor delay to mimic a human breath pause
            if (running) {
                pwm_set_pulse_dt(&led2_pwm, 0);
                 /* Increased from 150ms to 600ms for a more dramatic, visible pause */
                k_msleep(600);
            }
        }

        // Clean up hardware when execution state drops out
        pwm_set_pulse_dt(&led2_pwm, 0);
        LOG_INF("LED 2 fading effect stopped");
    }
}

/**
 * @brief Thread initializer and state manipulator interface.
 */
void led_fade_set_state(bool enable)
{
    static bool thread_initialized = false;

    // Lazily spin up the thread context on the first invocation
    if (!thread_initialized) {
        k_sem_init(&toggle_sem, 0, 1);
        running = false;
        
        k_thread_create(&fade_thread_data, fade_thread_stack,
                        K_THREAD_STACK_SIZEOF(fade_thread_stack),
                        fade_thread_handler,
                        NULL, NULL, NULL,
                        PRIORITY, 0, K_NO_WAIT);
        
        k_thread_name_set(&fade_thread_data, "led_fade_thread");
        thread_initialized = true;
    }

    if (enable && !running) {
        running = true;
        k_sem_give(&toggle_sem); // Release thread from block hook
    } else if (!enable && running) {
        running = false; // Breaking out of active loop automatically shuts off the PWM pin
    }
}
