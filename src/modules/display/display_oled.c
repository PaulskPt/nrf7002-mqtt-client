/*! @file display_oled.c
 * @brief Implements the use of an 1.12in 128x128 mono OLED
 *
 * @author Paulus Schulinck (Github @PaulskPt)
 *    ===  Programming keeps the mind going ===
 *
 *  Update 2026-10-04: added functionality to update the time in between MQTT epoch updates. 
 *  This is done by calculating the elapsed time since the last epoch update and adding it 
 *  to the baseline epoch value. The display will now show a continuously updating time, 
 * even if new MQTT data is not received every second.
*/
#include "../transport/transport.h"  // Added by Paulus Schulinck (Github @PaulskPt)
#include <zephyr/logging/log.h>

// LOG_MODULE_REGISTER(display, CONFIG_MQTT_SAMPLE_DISPLAY_LOG_LEVEL);
LOG_MODULE_REGISTER(display, LOG_LEVEL_INF); // Previous line, for text, replaced by this line. On advice MS Copilot
#include <zephyr/arch/arch_interface.h>
#include <zephyr/kernel.h>
#include <zephyr/display/cfb.h>

#include <zephyr/settings/settings.h>
//#include <zephyr/posix/posix_time.h>  // commented-out on advice MS Copilot
#include <time.h>
#ifdef CONFIG_ZBUS
#include <zephyr/zbus/zbus.h>
#endif
#include "../../common/message_channel.h"
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/hci.h>

// ---- Additions @PaulskPt:

#include <zephyr/drivers/display.h>
#include "dst_table_west.h"

#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/sys/uuid.h>
#include <stdbool.h>
#include <stdio.h>
#include <inttypes.h> // Required for PRId64
#include <string.h>
#include <time.h>    // <-- This is the ONLY time header you need!
// DO NOT #include <posix/time.h>
// DO NOT #include <zephyr/posix/posix_time.h>

#include "../telemetry/telemetry.h"
#include <zephyr/drivers/rtc.h>
#include <zephyr/sys/time_units.h>

#ifndef ARCH_STACK_PTR_ALIGN
#define ARCH_STACK_PTR_ALIGN 4
#endif

// Display task priority level
// #define DISPLAY_THREAD_STACK_SIZE 1024  // was: 4096
#ifndef CONFIG_MQTT_SAMPLE_DISPLAY_THREAD_STACK_SIZE
#define CONFIG_MQTT_SAMPLE_DISPLAY_THREAD_STACK_SIZE 1024
#endif

#ifndef DISPLAY_PRIORITY
#define DISPLAY_PRIORITY 7  // wa: 3
#endif 

static bool my_debug = false;

struct tm time_info_old = {0}; // Old time_info for comparison

// from Google AI. See set_system_epoch()
static bool epoch_changed = false;
static bool epoch_err_msg_shown = false;
static bool datetime_shown = false;
static int64_t last_epoch = 0; // last epoch received from MQTT telemetry
static int64_t epoch_baseline_seconds = 0; // is a copy of g_telemetry.epoch
static int64_t uptime_baseline_ms = 0; // is a copy of k_cycle_get_64() when the epoch was received
bool last_epoch_displayed = false;
static bool is_time_synchronized = false;

/* MASTER TIME SYNC STORAGE: Placed here so update_screen can see them! */

/* Initially 5 minutes for testing. Later once a day or so */
#define RTC_SYNC_INTERVAL_MS (5 * 60 * 1000)
bool use_running_time = true; /* display running time instead of refresh after received epoch (once a minute)*/
time_t utc_running_time = 0;/* MASTER TIME SYNC STORAGE: Placed here so update_screen can see them! */
/*
#define BT_UUID_OLED_SERVICE_VAL \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x567812345678)
#define BT_UUID_OLED_WRITE_VAL \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x567812345679)

static struct bt_uuid_128 oled_service_uuid = BT_UUID_INIT_128(BT_UUID_OLED_SERVICE_VAL);
static struct bt_uuid_128 oled_write_uuid = BT_UUID_INIT_128(BT_UUID_OLED_WRITE_VAL);
*/

/* ==================================================================== */
/* GLOBAL VARIABLES & STATE FLAGS                                       */
/* ==================================================================== */

const struct device *display_dev;

static bool lStart = true;
static bool lTimeout = false;
static bool connected_msg_shown = false;
static bool mqtt_is_connected = false;
static bool mqtt_is_connected_old = false;

/* Your global incoming text buffer variable from the Pi */
/* CORRECT FIX: Allocate the actual memory buffer here so the linker can find it */
char display_buffer[128] = {0}; 

char epoch_str[32]       = {0};
char last_epoch_str[11]  = {0}; // see write_rx_data()
// char last_epoch_str[32] = {0};
char dow_str[10]         = {0};
char date_str[32]        = {0};
char date_str_old[32]    = {0};
char time_str[32]        = {0};
char time_str_old[32]    = {0};
char time_header_str[32] = {0};

static bool date_changed = false;
static bool time_changed = false;
const char *zone_label = ""; /* Starts blank until time data packet lands */

/* end of global variables */

/**
* @brief Function sets the contrast of the screen
*/
void setContrast()
{
	int contr = 50;
	int err = display_set_contrast(display_dev, contr);
	if (err) {
		LOG_WRN("Display set contrast failed (err %d)", err);
	} else {
		LOG_INF("Display contrast successfully initialized to %d", contr);
	}
}

/**
 * @brief Prints an input string onto the OLED display with smart word-wrapping.
 *        Prevents mid-word splitting by wrapping whole words at space boundaries.
 *        Max character capacity per line: 16 characters (128 pixels / 8 pixels per char).
 */
static void print_word_wrapped(const char *text, uint8_t start_row)
{
    if (!text || !display_dev) return;
	// static char txt0[] = "print_word_wrapped(): ";
    char buffer[128] = {0};
    strncpy(buffer, text, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
	// LOG_INF("%sGoing to display the text: \'%s\'", txt0, buffer);
    char line_buffer[32] = "";
    uint8_t current_row = start_row;
    
    /* Tokenize the input text by looking for space characters */
    char *word = strtok(buffer, " ");
    
    while (word != NULL) {
        size_t current_len = strlen(line_buffer);
        size_t word_len = strlen(word);
        
        /* Check if adding this word (plus a space) exceeds our 16-character boundary */
        size_t space_needed = (current_len > 0) ? 1 : 0;
        
        if (current_len + space_needed + word_len <= 16) {
            /* Word fits cleanly on the current line! Append it */
            if (current_len > 0) {
                strcat(line_buffer, " ");
            }
            strcat(line_buffer, word);
        } else {
            /* Word does not fit. Print the completed line buffer onto the current row */
            if (current_len > 0 && current_row <= 112) {
                cfb_print(display_dev, line_buffer, 0, current_row);
                current_row += 16; // Drop down exactly one font row height
            }
            
            /* Start the next line buffer with the word that didn't fit */
            strncpy(line_buffer, word, sizeof(line_buffer) - 1);
            line_buffer[sizeof(line_buffer) - 1] = '\0';
        }
        
        /* Move to the next space-separated word token */
        word = strtok(NULL, " ");
    }
    
    /* Print any remaining characters left over in the line buffer */
    if (strlen(line_buffer) > 0 && current_row <= 112) {
        cfb_print(display_dev, line_buffer, 0, current_row);
    }
}


static void float_to_str(float value,
	char *buffer,
	size_t buffer_size)
{
    int whole;
    int frac;

    whole = (int)value;
    frac = ((int)(value * 10)) % 10;

    snprintf(buffer,
             buffer_size,
             "%d.%d",
             whole,
             frac);
}

/**
* @brief Function prints to LONG_INF() a line of dashes 
* that has a length of parameter lenline
*/
void log_line(int lenLine)
{
    char line[65];   /* allows up to 64 '-' chars */
    int i;

    if (lenLine < 0) {
        lenLine = 0;
    }

    if (lenLine > (sizeof(line) - 1)) {
        lenLine = sizeof(line) - 1;
    }

    for (i = 0; i < lenLine; i++) {
        line[i] = '-';
    }

    line[lenLine] = '\0';

    LOG_INF("%s", line);
}

/**
* @brief Function logs the received (MQTT) telemetry data
*/
void telemetry_inf(void) {
	char temperature_str[16];

	float_to_str(g_telemetry.temperature,
		temperature_str,
		sizeof(temperature_str));

	log_line(27);
	LOG_INF("TELEMETRY INFO: g_telemetry:");
	LOG_INF(".valid=             %d", g_telemetry.valid);
	LOG_INF(".epoch=             %llu", (unsigned long long)g_telemetry.epoch);
	LOG_INF(".temperature=       %s C", temperature_str);
	LOG_INF(".epoch_logIt=       %d", g_telemetry.epoch_logIt);
	LOG_INF(".temp_logIt=        %d", g_telemetry.temp_logIt);
	LOG_INF("global: last_epoch= %llu", last_epoch);
	log_line(27);
}

/**
 * @brief displays a line on the screen
 */
void screen_line(void) {
	/* FIXED SEPARATOR LINE: Maintained strictly on Row 16 */
	cfb_print(display_dev, "------------", 0, 16);
}

/**
 * @brief displays the screen header dynamically
 */
void screen_header(void) {
	/* DYNAMIC TOP HEADER: Always draw status cleanly based on active event states (Row 0) */
	if (mqtt_is_connected) { // See screen_update(). Was: if(transport_is_connected()) {
		cfb_print(display_dev, "MQTT: LINKED", 0, 0);
	} else {
		cfb_print(display_dev, "MQTT: DISCON", 0, 0);
	}
}

/**
 * @brief Fills the screen with default data
 */
void screen_init(void) {
	static char txt0[] = "screen_init(): ";
	LOG_INF("%sSetting the initial screen", txt0);
	screen_header();
	screen_line();
	cfb_print(display_dev, "No Epoch", 0, 32);
	snprintf(date_str, sizeof(date_str), "---- -- --");
	cfb_print(display_dev, date_str, 0, 48);
	snprintf(time_header_str, sizeof(time_header_str), "Time");
	cfb_print(display_dev, time_header_str, 0, 64);
	snprintf(time_str, sizeof(time_str), "UNKNOWN");
	cfb_print(display_dev, time_str, 0, 80);
	print_word_wrapped("Waiting for data", 96);
}

/**
* @brief 
* Instead of pushing updates from the callback, let the display thread pull from g_telemetry. 
* If g_telemetry.epoch changes, update the internal system time offset instantly.
*/
void update_datetime_from_system_clock(void) {
	// Read back the moving time (even if MQTT hasn't sent a new payload this exact second)
	struct timespec current_ts;

	// Pull the moving real-time clock value
	clock_gettime(CLOCK_REALTIME, &current_ts);

	// Break it down into hours/minutes/seconds
	struct tm time_info;
	gmtime_r(&current_ts.tv_sec, &time_info);
	
	// For date_str ("YYYY-MM-DD")
	strftime(date_str, sizeof(date_str), "%Y-%m-%d", &time_info);
				
	snprintf(date_str, sizeof(date_str), "%04d-%02d-%02d", 
				time_info.tm_year + 1900, time_info.tm_mon + 1, time_info.tm_mday);

	// For time_str ("HH:MM:SS")
	strftime(time_str, sizeof(time_str), "%H:%M:%S", &time_info);
}


/**
 * @brief Call this function whenever you successfully receive a new 
 * Epoch timestamp string or integer from your MQTT callback.
 */
void set_system_from_epoch(void) 
{
	static char txt0[] = "set_system_from_epoch(): ";
	if (g_telemetry.epoch <= 0)
		return;

	struct timespec ts;
	ts.tv_sec = (time_t)g_telemetry.epoch;
	ts.tv_nsec = 0;

	// Update the underlying system wall-clock (calculated seamlessly over GRTC)
	if (clock_settime(CLOCK_REALTIME, &ts) == 0) {
		is_time_synchronized = true;
		if(my_debug) {
			LOG_INF("%sSystem wall-clock synchronized from telemetry epoch: %lld", txt0, g_telemetry.epoch);
		}
	} else {
		is_time_synchronized = false;
	}

	if (is_time_synchronized) {
		epoch_baseline_seconds = g_telemetry.epoch;
		uptime_baseline_ms = k_uptime_get();
		LOG_INF("%sTime synchronized! Baseline Epoch: %lld", txt0, epoch_baseline_seconds);
	}
}

/**
* @brief Function logs the received (MQTT) telemetry data
*  Paste the companion calculation function
*/
int64_t get_current_epoch(void)
{
if (!is_time_synchronized) {
return 0;
}

int64_t current_uptime_ms = k_uptime_get();
int64_t elapsed_ms = current_uptime_ms - uptime_baseline_ms;

return epoch_baseline_seconds + (elapsed_ms / 1000);
}

void epoch_to_string(void)
{
	static char txt0[] = "epoch_to_string(): ";

	// Google AI: Use PRId64 to perfectly match int64_t on any compiler/architecture
	snprintf(epoch_str, sizeof(epoch_str), "%" PRId64, g_telemetry.epoch);

	if(my_debug) {
		LOG_INF("%sepoch_str=\"%s\"", txt0, epoch_str);
	}
}

/**
 * @brief updates the global date_str and time_str variables 
 * from the received telemetry epoch value
 */
void screen_update_DateTime(void) {
	static char txt0[] = "screen_update_DateTime(): ";

	/* RAW INCOMING EPOCH STRING DISPLAY: Shifted from Row 96 up to Row 32 */

	/* Continuous serial diagnostic output check */
	if (my_debug && g_telemetry.epoch_logIt) {
		LOG_INF("%sMQTT received epoch = \"%s\"", txt0,
			epoch_str);

		LOG_INF("%sUsing MQTT epoch: %lld", txt0, g_telemetry.epoch);

		g_telemetry.epoch_logIt = false;
	}

	/* ==================================================================== */
	/* AUTOMATED PORTUGAL TIME ZONE ENGINE (Europe/Lisbon)                  */
	/* ==================================================================== */

	int current_gmt_offset_seconds = 0;
	zone_label = " (WET)";
	/*  DYNAMIC DATA TRACKING VARIABLES DECLARATION */
	int64_t current_epoch = 0;

	if (use_running_time) {
		current_epoch = get_current_epoch();
	} else {
		current_epoch = g_telemetry.epoch;
	}
	
	if (current_epoch <= 0) {
	return;
	}
	
	utc_running_time = (time_t)current_epoch;
	
	if (my_debug) {
		LOG_INF("%scurrent_epoch: %lld",
			txt0, (long long)current_epoch);
		
		LOG_INF("%sUTC running time (epoch): %lld",
			txt0, (long long)utc_running_time);
	}

	/* utc_running_time is a global variable */
	for (size_t i = 0; i < WEST_TABLE_SIZE; i++) {
		if (utc_running_time >= west_table[i].start_epoch &&
			utc_running_time <= west_table[i].end_epoch) {

			current_gmt_offset_seconds = 3600;
			zone_label = " (WEST)";
			break;
		}
	}

	time_t local_running_time = utc_running_time + current_gmt_offset_seconds;
    struct tm time_info;

	if (my_debug) {
		LOG_INF("%sCurrent GMT offset seconds: %d", txt0, current_gmt_offset_seconds);
		LOG_INF("%sUTC running time (epoch): %lld", txt0, (long long)utc_running_time);
		LOG_INF("%sLocal running time (epoch): %lld", txt0, (long long)local_running_time);
	}

 	if (gmtime_r(&local_running_time, &time_info) == NULL) {
		LOG_WRN("%sFailed to convert local_running_time (epoch).", txt0);

		snprintf(dow_str, sizeof(dow_str), "ERR");
		
		snprintf(date_str, sizeof(date_str), "GMTIME ERR");

		snprintf(time_str, sizeof(time_str), "UNKNOWN");

    } else {

		date_changed =
		(time_info_old.tm_year != time_info.tm_year) ||
		(time_info_old.tm_mon != time_info.tm_mon) ||
		(time_info_old.tm_mday != time_info.tm_mday);
		
		time_changed =
		(time_info_old.tm_hour != time_info.tm_hour) ||
		(time_info_old.tm_min != time_info.tm_min) ||
		(time_info_old.tm_sec != time_info.tm_sec);
		
		/* Save for comparison on next invocation */
		time_info_old = time_info;

		int year = time_info.tm_year + 1900;
		
		if (year <= 1970 && !epoch_err_msg_shown) {
			LOG_WRN("%sInvalid epoch time: %lld. Year=%d.",
				txt0,
				(long long)current_epoch,
				year);
		
			epoch_err_msg_shown = true;
			return;
		}

		snprintf(dow_str, sizeof(dow_str), "%s", 
			(time_info.tm_wday == 0) ? "Sun" :
			(time_info.tm_wday == 1) ? "Mon" :
			(time_info.tm_wday == 2) ? "Tue" :
			(time_info.tm_wday == 3) ? "Wed" :
			(time_info.tm_wday == 4) ? "Thu" :
			(time_info.tm_wday == 5) ? "Fri" :
			(time_info.tm_wday == 6) ? "Sat" : "ERR");

		// For date_str ("YYYY-MM-DD")
		strftime(date_str, sizeof(date_str), "%Y-%m-%d", &time_info);

		// For time_str ("HH:MM:SS")
		strftime(time_str, sizeof(time_str), "%H:%M:%S", &time_info);

		if (my_debug) {
			LOG_INF("%s%s Date=%s Time=%s%s",
				txt0,
				dow_str,
				date_str,
				time_str,
				zone_label);
		}
		if (strcmp(date_str, date_str_old) != 0) {
			// Safely overwrite the old buffer with the new contents
			memcpy(date_str_old, date_str, sizeof(date_str_old));
			//date_changed = true;
		}
		
		if (strcmp(time_str, time_str_old) != 0) {
			// Safely overwrite the old buffer with the new contents
			memcpy(time_str_old, time_str, sizeof(time_str_old));
			//time_changed = true;
		}

		if (!datetime_shown) {
			datetime_shown = true;
			if (date_changed) {
				LOG_INF("%sDate changed to: %s", txt0, date_str);
			}
			if (time_changed) {
				LOG_INF("%sTime changed to: %s", txt0, time_str);
			}
		}
	}
}

/**
 * @brief Handles all text frame drawings and screen updates systematically.
 */
void screen_update(void) {

	if ( (!display_dev) || (!device_is_ready(display_dev)) ) return;

	static char txt0[] = "screen_update(): ";
	char temperature_str[16];
	uint16_t len_temperature_str;

	/* Handle frame buffer clear while honoring your verified gating filter */
	// LOG_INF("%slStart=%s", txt0, lStart ? "true" : "false");

	if (lStart) {
		lStart = false;
		// setContrast();
		// cfb_framebuffer_clear(display_dev, true);
		// cfb_framebuffer_set_font(display_dev, 0);
		screen_init();
		/* Push changes to the screen */
		cfb_framebuffer_finalize(display_dev);
	}

	/* ==================================================================== */
	/* CASCADING PUSHED-DOWN VERTICAL LAYOUT MATRIX                         */
	/* ==================================================================== */
	
	mqtt_is_connected = transport_is_connected();

	if (!mqtt_is_connected) {
		// LOG_INF("%sMQTT not connected. Exiting function", txt0);
		return;
	}

	if (mqtt_is_connected != mqtt_is_connected_old)
		connected_msg_shown = false;
	
	if (!connected_msg_shown) {
		LOG_INF("%sMQTT connected? %s", txt0, mqtt_is_connected ? "Yes" : "No");
		mqtt_is_connected_old = mqtt_is_connected;
		connected_msg_shown = true;
		screen_header(); /* Update the screen header */
	}


	if (epoch_changed || use_running_time) {
		/* Only print to LOG if we have values greater than zero */
		if (my_debug && (g_telemetry.epoch > 0 || last_epoch > 0)) {
			LOG_INF("%sg_telemetry.epoch= %lld, last_epoch= %lld", txt0, g_telemetry.epoch, last_epoch);
		}

		if (epoch_changed) {
			cfb_framebuffer_clear(display_dev, true); // clear the screen
			screen_header();
			screen_line();
	    }

		/* Print to your SH1107 OLED glass memory segments */
		cfb_print(display_dev, epoch_str, 0, 32);

		if (epoch_changed || date_changed) {
			/*  CALENDAR DATE ROW: Pushed down from Row 32 to Row 48 */
			cfb_print(display_dev, date_str, 0, 48);
			// date_changed = false;

			/*  TIME ZONE HEADER ROW: Pushed down from Row 48 to Row 64 */
			snprintf(time_header_str, sizeof(time_header_str), "Time%s", zone_label);
			cfb_print(display_dev, time_header_str, 0, 64);
		}

		if (epoch_changed || date_changed || time_changed) {
			char oled_time_str[48];

			snprintf(oled_time_str, sizeof(oled_time_str),
			"%s %s",
			time_str,
			dow_str);

			/*  CLOCK COUNTER ROW: Pushed down from Row 64 to Row 80 */
			// cfb_print(display_dev, time_str, 0, 80);
			cfb_print(display_dev, oled_time_str, 0, 80);
		}

		if (epoch_changed) {
			if (g_telemetry.temperature >= 0.0f) {
				snprintf(temperature_str, \
				sizeof(temperature_str), \
				"%d.%d", \
				(int)g_telemetry.temperature, \
				((int)(g_telemetry.temperature * 10)) % 10);
			
				len_temperature_str = strlen(temperature_str);

				if (len_temperature_str > 0) {

					if (my_debug && g_telemetry.temp_logIt) {
						LOG_INF("%sMQTT received temperature = \"%s\" (Length: %d)", txt0, temperature_str, len_temperature_str);
						g_telemetry.temp_logIt = false; /* Toggle state flag guard */
					}
				
					/*  FORMAT AND COPY SENSOR METRIC INTO DISPLAY BUFFER */
					snprintf(display_buffer, sizeof(display_buffer), "Temp: %s C", temperature_str);
					if (my_debug) {
						LOG_INF("%ssensor temperature displayed: \"%s\"", txt0, display_buffer);
					}

					/*  PERSISTENT SENSOR METRIC DISPLAY: Pushed down from Row 80 to Row 96 */
					if (strlen(display_buffer) > 0) {
						print_word_wrapped(display_buffer, 96);
					}
				}
			} else {
				print_word_wrapped("Waiting for data", 96);
			}
		}
		/* Re-render changes directly onto your Adafruit glass surface */
		/* Pus the changes to the screen */
		cfb_framebuffer_finalize(display_dev);
	}
}

// 1. This is your loop task (Notice it uses the 3 required NULL pointer arguments)
void Display_Task(void *p1, void *p2, void *p3) {
	// int err;
    static char txt0[] = "Display_Task(): ";
	bool mqtt_is_connected_local = false;
	bool mqtt_is_connected_shown = false;

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	if (!device_is_ready(display_dev)) {
		LOG_ERR("%sDisplay device not ready", txt0);
		// eventually do a software reset
	}

	/* 1. Turn off hardware blanking to enable the pixel array */
	display_blanking_off(display_dev);
	
    /* 2. Initialize the character framebuffer base engine */
	cfb_framebuffer_init(display_dev);

	/* 3 Clear the screen */
	cfb_framebuffer_clear(display_dev, true);

	/* 4. Set the global font profile baseline first */
	cfb_framebuffer_set_font(display_dev, 0);

	/* 5. Establish initial hardware performance constraints */
	setContrast();

	screen_update();


	/* ==================================================================== */
	/* MASTER TICK REFRESH ENGINE LOOP                                      */
	/* ==================================================================== */
	while (1) {
		/* 1. Sleep for exactly 1000ms (1 second) before running checks */
		k_sleep(K_MSEC(500));

		if (!mqtt_is_connected_shown) {
			mqtt_is_connected_local = transport_is_connected();

			if (mqtt_is_connected_local) {
				mqtt_is_connected = mqtt_is_connected_local;
				if (mqtt_is_connected)
					mqtt_is_connected_shown = true;
				screen_header();
			}
		}
		
		epoch_changed = g_telemetry.epoch != last_epoch ? true : false;

		if (epoch_changed) {
			datetime_shown = false; /* Reset the flag to allow logging of date/time changes */
			/* We have new MQTT data */
			LOG_INF("%sepoch has changed? %s", txt0, epoch_changed ? "Yes" : "No");
			LOG_INF("%sg_telemetry.epoch= %lld, last_epoch= %lld", txt0, g_telemetry.epoch, last_epoch);
			/* copy to global variables */
			last_epoch = g_telemetry.epoch;

	 		if (g_telemetry.epoch > 0) {
				set_system_from_epoch();
				/* set epoch_str, date_str and time_str */
				epoch_to_string();
				strcpy(last_epoch_str, epoch_str);
				if (!use_running_time) {
					// Update datetime only wen new epoch received (once a minute)
					if (g_telemetry.valid) {
						telemetry_inf(); 
						screen_update_DateTime();
					}
				}
			}
		} else {
			/* Update datetime every second */
			if (use_running_time) {
				screen_update_DateTime();
			}
			
		}
		/* read system clock and set date_str and time_str accordingly */
		// update_datetime_from_system_clock();

		// Temporarily var timeout to clean the display_buffer
		if (lTimeout) {
			memset(display_buffer, 0, sizeof(display_buffer));
			LOG_WRN("%sAlert timeout expired. Wiping display memory segments...", txt0);
		}

		/* 3. Force a complete screen drawing update to handle the active views */
		screen_update();
		date_changed = false; /* force a screen update */
		time_changed = false; /* force a screen update */
	}
}

/*
// The Old thread difinition
K_THREAD_DEFINE(displayThread,
		DISPLAY_STACK_SIZE,
		Display_Task, NULL, NULL, NULL, DISPLAY_PRIORITY, 0, 0);
*/

K_THREAD_DEFINE(
    display_task_thread, 
    CONFIG_MQTT_SAMPLE_DISPLAY_THREAD_STACK_SIZE, \
    Display_Task, \
    NULL, \
    NULL, \
    NULL, \
    DISPLAY_PRIORITY, \
    0, \
    0
);