#include "hs80.h"
#include "hid.h"
#include "audio.h"
#include "protocol.h"
#include <shellapi.h>
#include <wchar.h>
#include <string.h>

#define TRAY_CALLBACK (WM_APP+100)
static HANDLE stop_event;
static bool lights_off=true,refresh_requested=false,unmute_requested=false;
static NOTIFYICONDATAW tray;
static HICON tray_icon;
static hs_state published;

static void set_lights(bool off){lights_off=off;hs_save_led_preference(off);refresh_requested=true;}
static void tray_menu(HWND window){
 HMENU menu=CreatePopupMenu();if(!menu)return;
 AppendMenuW(menu,MF_STRING,1,L"Abrir painel");
 AppendMenuW(menu,MF_STRING|(lights_off?MF_CHECKED:0),2,L"Manter LEDs apagados");
 AppendMenuW(menu,MF_STRING|(hs_autostart_enabled()?MF_CHECKED:0),3,L"Iniciar com o Windows");
 AppendMenuW(menu,MF_SEPARATOR,0,NULL);AppendMenuW(menu,MF_STRING,4,L"Encerrar controle");
 POINT point;GetCursorPos(&point);SetForegroundWindow(window);
 UINT selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window,NULL);
 DestroyMenu(menu);
 if(selected==1)hs_launch_panel();else if(selected==2)set_lights(!lights_off);
 else if(selected==3)hs_set_autostart(!hs_autostart_enabled());else if(selected==4)SetEvent(stop_event);
 PostMessageW(window,WM_NULL,0,0);
}
static LRESULT CALLBACK controller_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
 switch(message){
 case HS_MSG_LEDS:if(wparam<=1)set_lights(wparam!=0);return 0;
 case HS_MSG_UNMUTE:unmute_requested=true;return 0;
 case HS_MSG_SHOW:hs_launch_panel();return 0;
 case TRAY_CALLBACK:
  if(LOWORD(lparam)==WM_LBUTTONDBLCLK||LOWORD(lparam)==NIN_SELECT)hs_launch_panel();
  else if(LOWORD(lparam)==WM_RBUTTONUP||LOWORD(lparam)==WM_CONTEXTMENU)tray_menu(window);
  return 0;
 case WM_CLOSE:SetEvent(stop_event);return 0;
 case WM_QUERYENDSESSION:return TRUE;
 case WM_ENDSESSION:if(wparam)SetEvent(stop_event);return 0;
 }return DefWindowProcW(window,message,wparam,lparam);
}
static void update(hs_state *mapped,LONG status,DWORD error){
 static hs_state last={0};static bool sent=false;
 published.status=status;published.leds_off=lights_off?1:0;published.error=error;hs_publish(mapped,&published);
 if(!sent||memcmp(&last,&published,sizeof(last))!=0){
  HWND panel=FindWindowW(NULL,HS_PANEL_TITLE);if(panel)PostMessageW(panel,HS_MSG_STATE,0,0);
  last=published;sent=true;
 }
}
static void disconnect(hs_hid *device,hs_audio *audio,bool restore){
 if(device->handle){if(restore){HANDLE stop=device->stop;device->stop=NULL;hs_hid_hardware(device);device->stop=stop;}hs_hid_close(device);}
 hs_audio_close(audio);
}
static int run_controller(hs_state *mapped){
 hs_hid device={0};hs_audio audio={0};
 int previous_mute=-1;bool cue=false;ULONGLONG cue_check=0,reconnect=0,mode_check=0,light_refresh=0,battery_check=0,windows_check=0;
 published.pid=GetCurrentProcessId();published.battery=-1;published.charging=-1;published.mic_hardware=-1;published.mic_windows=-1;
 update(mapped,HS_CONNECTING,0);
 while(WaitForSingleObject(stop_event,0)!=WAIT_OBJECT_0){
  MSG message;while(PeekMessageW(&message,NULL,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
  if(WaitForSingleObject(stop_event,0)==WAIT_OBJECT_0)break;
  ULONGLONG now=GetTickCount64();
  if(now>=cue_check){cue=hs_process_present(L"iCUE.exe");cue_check=now+3000;}
  if(cue){disconnect(&device,&audio,false);previous_mute=-1;update(mapped,HS_PAUSED,0);MsgWaitForMultipleObjects(1,&stop_event,FALSE,500,QS_ALLINPUT);continue;}
  if(!device.handle){
   if(now<reconnect){update(mapped,published.status,published.error);MsgWaitForMultipleObjects(1,&stop_event,FALSE,250,QS_ALLINPUT);continue;}
   if(!hs_hid_open(&device,stop_event)){update(mapped,HS_WAITING,GetLastError());reconnect=now+3000;continue;}
   if(!hs_audio_open(&audio)){DWORD error=GetLastError();disconnect(&device,&audio,true);update(mapped,HS_AUDIO_ERROR,error);reconnect=now+3000;continue;}
   uint16_t physical=0;
   if(!hs_hid_get(&device,1,0xa6,&physical)||physical>1||!hs_audio_set(&audio,physical==1))goto failed;
   previous_mute=(int)physical;published.mic_hardware=physical;published.mic_windows=physical;
   if(lights_off){if(!hs_hid_black(&device))goto failed;}else if(!hs_hid_hardware(&device))goto failed;
   mode_check=light_refresh=battery_check=windows_check=0;refresh_requested=false;
  }
  {
   uint16_t physical=0;bool changed=false;
   if(!hs_hid_get(&device,1,0xa6,&physical)||physical>1)goto failed;
   changed=previous_mute!=(int)physical;
   if(changed){if(!hs_audio_set(&audio,physical==1))goto failed;previous_mute=physical;}
   published.mic_hardware=physical;
   if(unmute_requested){if(physical==0&&!hs_audio_set(&audio,false))goto failed;unmute_requested=false;}
   bool need_light=refresh_requested||changed;
   if(lights_off&&now>=mode_check){uint16_t mode=0;if(!hs_hid_get(&device,1,3,&mode))goto failed;if(mode!=2)need_light=true;mode_check=now+1000;}
   if(lights_off&&(need_light||now>=light_refresh)){if(!hs_hid_black(&device))goto failed;light_refresh=GetTickCount64()+3000;}
   else if(!lights_off&&refresh_requested){if(!hs_hid_hardware(&device))goto failed;}
   refresh_requested=false;
   if(now>=windows_check||changed){bool muted=false;if(!hs_audio_get(&audio,&muted))goto failed;published.mic_windows=muted?1:0;windows_check=now+1000;}
   if(now>=battery_check){
    uint16_t raw=0;published.battery=-1;
    for(int attempt=0;attempt<3;attempt++){
     if(!hs_hid_get(&device,1,0x0f,&raw))break;
     published.battery=hs_battery_percent(raw);if(published.battery>=0)break;
    }
    battery_check=GetTickCount64()+15000;
   }
   update(mapped,HS_ACTIVE,0);
  }
  MsgWaitForMultipleObjects(1,&stop_event,FALSE,250,QS_ALLINPUT);continue;
failed:
  {DWORD error=GetLastError();disconnect(&device,&audio,true);previous_mute=-1;published.mic_hardware=published.mic_windows=-1;published.battery=-1;update(mapped,HS_WAITING,error);reconnect=GetTickCount64()+3000;}
 }
 disconnect(&device,&audio,!cue);update(mapped,HS_STOPPED,0);hs_status_json(&published);return 0;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,LPWSTR command_line,int show){
 (void)previous;(void)command_line;(void)show;
 int argc=0;LPWSTR *argv=CommandLineToArgvW(GetCommandLineW(),&argc);const wchar_t *command=argc>1?argv[1]:L"--show";
 if(wcscmp(command,L"--stop")==0){HANDLE stop=OpenEventW(EVENT_MODIFY_STATE,FALSE,HS_STOP);if(stop){SetEvent(stop);CloseHandle(stop);}LocalFree(argv);return 0;}
 if(wcscmp(command,L"--status")==0){hs_state state={0};bool ok=hs_read_state(&state);if(!ok){state.status=HS_STOPPED;state.battery=-1;state.mic_hardware=state.mic_windows=-1;}hs_status_json(&state);LocalFree(argv);return ok?0:1;}
 bool show_panel=wcscmp(command,L"--show")==0;
 if(!show_panel&&wcscmp(command,L"--run")!=0){LocalFree(argv);return 2;}
 HANDLE mutex=CreateMutexW(NULL,TRUE,HS_MUTEX);if(!mutex){LocalFree(argv);return 1;}
 if(GetLastError()==ERROR_ALREADY_EXISTS){if(show_panel)hs_launch_panel();CloseHandle(mutex);LocalFree(argv);return 0;}
 stop_event=CreateEventW(NULL,TRUE,FALSE,HS_STOP);if(!stop_event){ReleaseMutex(mutex);CloseHandle(mutex);LocalFree(argv);return 1;}ResetEvent(stop_event);
 HANDLE map=CreateFileMappingW(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,sizeof(hs_state),HS_MAP);
 hs_state *mapped=map?MapViewOfFile(map,FILE_MAP_WRITE,0,0,sizeof(hs_state)):NULL;
 if(!mapped){if(map)CloseHandle(map);CloseHandle(stop_event);ReleaseMutex(mutex);CloseHandle(mutex);LocalFree(argv);return 1;}memset(mapped,0,sizeof(*mapped));
 WNDCLASSW cls={0};cls.lpfnWndProc=controller_proc;cls.hInstance=instance;cls.lpszClassName=HS_WINDOW;RegisterClassW(&cls);
 HWND window=CreateWindowExW(0,HS_WINDOW,L"HS80 LEDs",0,0,0,0,0,HWND_MESSAGE,NULL,instance,NULL);
 tray_icon=hs_icon();memset(&tray,0,sizeof(tray));tray.cbSize=sizeof(tray);tray.hWnd=window;tray.uID=1;tray.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;tray.uCallbackMessage=TRAY_CALLBACK;tray.hIcon=tray_icon;
 wcscpy_s(tray.szTip,128,L"HS80 MAX · Controle dos LEDs");Shell_NotifyIconW(NIM_ADD,&tray);tray.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&tray);
 lights_off=hs_led_preference();HRESULT hr=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);int result=1;
 if(SUCCEEDED(hr)){if(show_panel)hs_launch_panel();result=run_controller(mapped);CoUninitialize();}
 Shell_NotifyIconW(NIM_DELETE,&tray);if(tray_icon)DestroyIcon(tray_icon);if(window)DestroyWindow(window);
 UnmapViewOfFile(mapped);CloseHandle(map);CloseHandle(stop_event);ReleaseMutex(mutex);CloseHandle(mutex);LocalFree(argv);return result;
}
