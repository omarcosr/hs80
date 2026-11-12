#include "viper_protocol.h"
#include <string.h>

enum { STATUS=1, TRANSACTION=2, PAYLOAD_SIZE=6, GROUP=7, COMMAND=8, CRC=89 };

static uint8_t checksum(const uint8_t report[VIPER_REPORT_SIZE]){
    uint8_t value=0;
    for (int i=3;i<CRC;i++) value^=report[i];
    return value;
}

void viper_report(uint8_t report[VIPER_REPORT_SIZE], uint8_t group, uint8_t command, uint8_t size, const uint8_t *args){
    memset(report,0,VIPER_REPORT_SIZE);
    if (size>VIPER_MAX_ARGS) return;
    report[TRANSACTION]=0x1f;
    report[PAYLOAD_SIZE]=size;
    report[GROUP]=group;
    report[COMMAND]=command;
    if (args) memcpy(report+VIPER_ARGS,args,size);
    report[CRC]=checksum(report);
}

viper_reply viper_reply_status(const uint8_t report[VIPER_REPORT_SIZE], uint8_t group, uint8_t command){
    if (report[0]||report[TRANSACTION]!=0x1f||report[3]||report[4]||report[5]||
        report[GROUP]!=group||report[COMMAND]!=command||report[PAYLOAD_SIZE]>VIPER_MAX_ARGS||
        report[CRC]!=checksum(report)||report[STATUS]>VIPER_UNSUPPORTED) return VIPER_INVALID_REPLY;
    return (viper_reply)report[STATUS];
}

int viper_battery_percent(uint8_t level){
    return (level*100+127)/255;
}

bool viper_valid_dpi(int dpi){
    return dpi>=VIPER_MIN_DPI&&dpi<=VIPER_MAX_DPI;
}

bool viper_parse_dpi(const uint8_t report[VIPER_REPORT_SIZE], int *x, int *y){
    const uint8_t *args=report+VIPER_ARGS;
    int horizontal=args[1]*256+args[2], vertical=args[3]*256+args[4];
    if (report[PAYLOAD_SIZE]<5||!viper_valid_dpi(horizontal)||!viper_valid_dpi(vertical)) return false;
    *x=horizontal;
    *y=vertical;
    return true;
}

int viper_polling_hz(bool wireless, uint8_t code){
    int base=wireless?8000:1000;
    if (!code||code>base/125||(code&(code-1))) return 0;
    return base/code;
}

uint8_t viper_polling_code(bool wireless, int hz){
    if (hz!=125&&hz!=500&&hz!=1000&&!(wireless&&(hz==2000||hz==4000||hz==8000))) return 0;
    return (uint8_t)((wireless?8000:1000)/hz);
}
