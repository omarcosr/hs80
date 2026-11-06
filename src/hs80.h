#ifndef HS80_H
#define HS80_H
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#define HS_MUTEX L"Local\\HS80-LedsOff-Instance-marcos"
#define HS_STOP L"Local\\HS80-LedsOff-Stop-marcos"
#define HS_MAP L"Local\\HS80-Leds-Native-State-marcos"
#define HS_WINDOW L"HS80.Leds.Native.Controller"
#define HS_PANEL_TITLE L"HS80 MAX — Controle"
#define HS_RUN_NAME L"HS80 LEDs Leve"
#define HS_MSG_LEDS (WM_APP+40)
#define HS_MSG_UNMUTE (WM_APP+41)
#define HS_MSG_SHOW (WM_APP+42)
#define HS_MSG_STATE (WM_APP+43)
#define HS_STATE_MAGIC 0x48533830u
enum {HS_CONNECTING=0,HS_ACTIVE=1,HS_WAITING=2,HS_PAUSED=3,HS_STOPPED=4,HS_AUDIO_ERROR=5};
typedef struct hs_state {
 uint32_t magic,version;
 volatile LONG sequence;
 DWORD pid;
 LONG status,leds_off,mic_hardware,mic_windows,battery,charging;
 ULONGLONG updated;
 DWORD error;
} hs_state;
bool hs_paths(wchar_t *worker,wchar_t *panel,wchar_t *ini,size_t cap);
bool hs_read_state(hs_state *out);
void hs_publish(hs_state *mapped,const hs_state *source);
HWND hs_controller_window(void);
bool hs_launch(const wchar_t *path,const wchar_t *arguments);
bool hs_launch_worker(void);
bool hs_launch_panel(void);
bool hs_autostart_enabled(void);
bool hs_set_autostart(bool enabled);
bool hs_led_preference(void);
bool hs_save_led_preference(bool off);
bool hs_process_present(const wchar_t *name);
HICON hs_icon(void);
void hs_status_json(const hs_state *state);
#endif
