#ifndef VIPER_PROTOCOL_H
#define VIPER_PROTOCOL_H
#include <stdbool.h>
#include <stdint.h>

enum { VIPER_REPORT_SIZE=91, VIPER_ARGS=9, VIPER_MAX_ARGS=80, VIPER_MIN_DPI=100, VIPER_MAX_DPI=35000 };
typedef enum {
    VIPER_INVALID_REPLY, VIPER_BUSY, VIPER_OK, VIPER_REJECTED, VIPER_TIMEOUT, VIPER_UNSUPPORTED
} viper_reply;

void viper_report(uint8_t report[VIPER_REPORT_SIZE], uint8_t group, uint8_t command, uint8_t size, const uint8_t *args);
viper_reply viper_reply_status(const uint8_t report[VIPER_REPORT_SIZE], uint8_t group, uint8_t command);
int viper_battery_percent(uint8_t level);
bool viper_valid_dpi(int dpi);
bool viper_parse_dpi(const uint8_t report[VIPER_REPORT_SIZE], int *x, int *y);
int viper_polling_hz(bool wireless, uint8_t code);
uint8_t viper_polling_code(bool wireless, int hz);
#endif
