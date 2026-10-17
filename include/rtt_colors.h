/* By Google AI, 2026-10-16
   Definitions and Macros using ANSI Escape codes
*/
#ifndef RTT_COLORS_H_
#define RTT_COLORS_H_

/* ========================================================================== */
/* ANSI Escape Code Base Elements                                             */
/* ========================================================================== */
#define ANSI_ESC        "\x1b["
#define ANSI_RESET      "\x1b[0m"

/* ========================================================================== */
/* Foreground Colors (Standard and Bright)                                    */
/* ========================================================================== */
#define FG_BLACK        "30m"
#define FG_RED          "31m"
#define FG_GREEN        "32m"
#define FG_YELLOW       "33m"
#define FG_BLUE         "34m"
#define FG_MAGENTA      "35m"
#define FG_CYAN         "36m"
#define FG_WHITE        "37m"

#define FG_BR_BLACK     "1;30m"
#define FG_BR_RED       "1;31m"
#define FG_BR_GREEN     "1;32m"
#define FG_BR_YELLOW    "1;33m"
#define FG_BR_BLUE      "1;34m"
#define FG_BR_MAGENTA   "1;35"
#define FG_BR_CYAN      "1;36m"
#define FG_BR_WHITE     "1;37m"

/* ========================================================================== */
/* Background Colors                                                          */
/* ========================================================================== */
#define BG_BLACK        "40;"
#define BG_RED          "41;"
#define BG_GREEN        "42;"
#define BG_YELLOW       "43;"
#define BG_BLUE         "44;"
#define BG_MAGENTA      "45;"
#define BG_CYAN         "46;"
#define BG_WHITE        "47;"

/* ========================================================================== */
/* Shorthand Text Styling Macros (Automatic String Concatenation)            */
/* ========================================================================== */

// Standard Foreground Macros
#define CLR_RED(text)     ANSI_ESC FG_RED text ANSI_RESET
#define CLR_GREEN(text)   ANSI_ESC FG_GREEN text ANSI_RESET
#define CLR_YELLOW(text)  ANSI_ESC FG_YELLOW text ANSI_RESET
#define CLR_BLUE(text)    ANSI_ESC FG_BLUE text ANSI_RESET
#define CLR_CYAN(text)    ANSI_ESC FG_CYAN text ANSI_RESET

// Bright Foreground Macros
#define CLR_B_RED(text)    ANSI_ESC FG_BR_RED text ANSI_RESET
#define CLR_B_GREEN(text)  ANSI_ESC FG_BR_GREEN text ANSI_RESET
#define CLR_B_YELLOW(text) ANSI_ESC FG_BR_YELLOW text ANSI_RESET

// Combined Background + Foreground Macros
#define CLR_YEL_ON_RED(text)  ANSI_ESC BG_RED FG_YELLOW text ANSI_RESET
#define CLR_WHT_ON_RED(text)  ANSI_ESC BG_RED FG_WHITE text ANSI_RESET
#define CLR_BLK_ON_AMB(text)  ANSI_ESC BG_YELLOW FG_BLACK text ANSI_RESET
#define CLR_WHT_ON_GRN(text)  ANSI_ESC BG_GREEN FG_WHITE text ANSI_RESET

#endif /* RTT_COLORS_H_ */
