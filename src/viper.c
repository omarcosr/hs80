#include "viper.h"
#include <hidsdi.h>
#include <string.h>
#include <wchar.h>

typedef struct { uint8_t group, command, size; } command;
static const hid_collection viper_collection={
    .vendor=0x1532,.product=VIPER_WIRED_PID,.alternate_product=VIPER_WIRELESS_PID,
    .usage_page=1,.usage=2,.feature_bytes=VIPER_REPORT_SIZE,
};

static bool wait_for_mouse(HANDLE stop, DWORD milliseconds){
    if (stop) return WaitForSingleObject(stop,milliseconds)==WAIT_TIMEOUT;
    if (milliseconds) Sleep(milliseconds);
    return true;
}

static viper_reply exchange(HANDLE mouse, HANDLE stop, command cmd, const uint8_t *args, uint8_t reply[VIPER_REPORT_SIZE]){
    uint8_t report[VIPER_REPORT_SIZE];
    viper_report(report,cmd.group,cmd.command,cmd.size,args);
    int attempts=(cmd.command&0x80)?2:1;
    for (int attempt=0;attempt<attempts;attempt++){
        if (!wait_for_mouse(stop,0)) break;
        if (!HidD_SetFeature(mouse,report,sizeof(report))) continue;
        if (!wait_for_mouse(stop,100)) break;
        memset(reply,0,VIPER_REPORT_SIZE);
        if (!HidD_GetFeature(mouse,reply,VIPER_REPORT_SIZE)) continue;
        viper_reply result=viper_reply_status(reply,cmd.group,cmd.command);
        if (result!=VIPER_BUSY&&result!=VIPER_INVALID_REPLY&&!(attempts>1&&result==VIPER_TIMEOUT)) return result;
    }
    return VIPER_INVALID_REPLY;
}

static void read_state(HANDLE mouse, HANDLE stop, viper_state *state){
    uint8_t reply[VIPER_REPORT_SIZE];
    static const uint8_t stored_dpi[7]={1};
    if (exchange(mouse,stop,(command){0x07,0x80,2},NULL,reply)==VIPER_OK&&reply[6]>=2)
        state->battery=viper_battery_percent(reply[VIPER_ARGS+1]);
    if (exchange(mouse,stop,(command){0x04,0x85,7},stored_dpi,reply)==VIPER_OK)
        viper_parse_dpi(reply,&state->dpi_x,&state->dpi_y);

    // The receiver can answer some commands while the mouse is asleep.
    if (state->battery<0&&!state->dpi_x) return;
    state->status=VIPER_CONNECTED;
    bool wireless=state->product==VIPER_WIRELESS_PID;
    command polling={0x00,wireless?0xc0:0x85,1};
    if (exchange(mouse,stop,polling,NULL,reply)==VIPER_OK&&reply[6]>=1)
        state->polling=viper_polling_hz(wireless,reply[VIPER_ARGS+(wireless?1:0)]);
    if (!wireless&&exchange(mouse,stop,(command){0x07,0x84,2},NULL,reply)==VIPER_OK&&reply[6]>=2)
        state->charging=reply[VIPER_ARGS+1]==1;
}

static bool write_setting(HANDLE mouse, HANDLE stop, viper_operation operation, int value, const viper_state *previous){
    uint8_t reply[VIPER_REPORT_SIZE];
    if (operation==VIPER_SET_DPI&&previous->dpi_x&&viper_valid_dpi(value)){
        uint8_t high=(uint8_t)(value>>8), low=(uint8_t)value;
        const uint8_t args[]={1,high,low,high,low,0,0};
        exchange(mouse,stop,(command){0x04,0x05,7},args,reply);
        return true;
    }
    if (operation==VIPER_SET_POLLING&&previous->polling){
        bool wireless=previous->product==VIPER_WIRELESS_PID;
        uint8_t code=viper_polling_code(wireless,value);
        if (!code) return false;
        const uint8_t args[]={0,code};
        command polling={0x00,wireless?0x40:0x05,wireless?2:1};
        exchange(mouse,stop,polling,wireless?args:args+1,reply);
        // The wireless link can be briefly unavailable after a rate change.
        wait_for_mouse(stop,180);
        return true;
    }
    return false;
}

bool viper_run(viper_operation operation, int value, const viper_state *previous, HANDLE stop, viper_state *state){
    memset(state,0,sizeof(*state));
    state->battery=-1;
    if (!wait_for_mouse(stop,0)) return false;
    state->product=hid_find_collection(&viper_collection,state->path);
    if (!state->product){
        DWORD error=GetLastError();
        state->status=error==ERROR_DUP_NAME?VIPER_DUPLICATE:
                      error==ERROR_DEVICE_NOT_CONNECTED?VIPER_ABSENT:VIPER_UNREACHABLE;
        return false;
    }

    state->status=VIPER_UNREACHABLE;
    // Feature reports are available without taking the mouse's input handle.
    HANDLE mouse=CreateFileW(state->path,0,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if (mouse==INVALID_HANDLE_VALUE) return false;
    HIDD_ATTRIBUTES attributes={sizeof(attributes)};
    if (!HidD_GetAttributes(mouse,&attributes)||attributes.VendorID!=viper_collection.vendor||attributes.ProductID!=state->product){
        CloseHandle(mouse);
        return false;
    }

    bool attempted=operation!=VIPER_READ&&previous&&previous->status==VIPER_CONNECTED&&
                   _wcsicmp(previous->path,state->path)==0&&write_setting(mouse,stop,operation,value,previous);
    read_state(mouse,stop,state);
    CloseHandle(mouse);
    if (operation==VIPER_READ) return state->status==VIPER_CONNECTED;
    if (!attempted||state->status!=VIPER_CONNECTED) return false;
    return operation==VIPER_SET_DPI?state->dpi_x==value&&state->dpi_y==value:state->polling==value;
}
