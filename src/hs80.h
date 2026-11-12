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
#define HS_PANEL_TITLE L"Controle de periféricos"
#define HS_MSG_LEDS (WM_APP+40)
#define HS_MSG_UNMUTE (WM_APP+41)
#define HS_MSG_SHOW (WM_APP+42)
#define HS_MSG_STATE (WM_APP+43)
#define HS_MSG_HIDE (WM_APP+44)
#define HS_MSG_RAISE (WM_APP+45)
#define HS_MSG_PANEL_DONE (WM_APP+46)
#define HS_MSG_SOUND_OPTIONS (WM_APP+47)
#define HS_MSG_SOUND_PREVIEW (WM_APP+48)
#define HS_MSG_SOUND_RESULT (WM_APP+49)
#define HS_MSG_STARTUP_CHANGED (WM_APP+50)
#define HS_STATE_MAGIC 0x48533830u
enum {HS_CONNECTING=0,HS_ACTIVE=1,HS_WAITING=2,HS_PAUSED=3,HS_STOPPED=4,HS_AUDIO_ERROR=5};
enum {
 HS_SOUND_NONE, HS_SOUND_PULSE, HS_SOUND_DOUBLE, HS_SOUND_RISE, HS_SOUND_FALL,
 HS_SOUND_COUNT
};
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
void hs_bind_state(hs_state *mapped);
HWND hs_controller_window(void);
bool hs_launch(const wchar_t *path,const wchar_t *arguments);
bool hs_launch_worker(void);
void hs_panel_show(void);
void hs_panel_close(void);
void hs_panel_reap(ULONG_PTR generation);
void hs_panel_shutdown(void);
HWND hs_panel_window(void);
int hs_panel_capture(void);
bool hs_autostart_enabled(void);
bool hs_set_autostart(bool enabled);
bool hs_migrate_autostart(void);
bool hs_led_preference(void);
bool hs_save_led_preference(bool off);
int hs_sound_preference(bool mic_active);
bool hs_save_sound_preference(bool mic_active,int sound);
bool hs_process_present(const wchar_t *name);
HICON hs_icon(void);
void hs_status_json(const hs_state *state);
#endif
