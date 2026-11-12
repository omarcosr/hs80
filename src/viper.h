#ifndef HS80_VIPER_H
#define HS80_VIPER_H
#include "hid.h"
#include "viper_protocol.h"

enum { VIPER_WIRED_PID=0x00c0, VIPER_WIRELESS_PID=0x00c1 };
typedef enum { VIPER_ABSENT, VIPER_DUPLICATE, VIPER_UNREACHABLE, VIPER_CONNECTED } viper_status;
typedef enum { VIPER_READ, VIPER_SET_DPI, VIPER_SET_POLLING } viper_operation;

typedef struct {
    wchar_t path[HID_PATH_CAPACITY];
    viper_status status;
    uint16_t product;
    int battery, dpi_x, dpi_y, polling;
    bool charging;
} viper_state;

bool viper_run(viper_operation operation, int value, const viper_state *previous, HANDLE stop, viper_state *state);
#endif
