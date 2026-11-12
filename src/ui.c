#include "hs80.h"
#include "viper.h"
#include <cui/cui.h>
#include <cui/cui_app.h>
#include <cui/cui_hs80_memory.h>
#include <dbt.h>
#include <hidsdi.h>
#include <process.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windowsx.h>

#define MOUSE_READY (WM_APP+51)
enum { HEADSET_TAB, MOUSE_TAB };
enum {
 PANEL_TICK_TIMER=1, MOUSE_USB_TIMER,
 MOUSE_USB_DELAY_MS=120, MOUSE_USB_RETRY_MS=500, MOUSE_USB_RECONNECT_MS=10000
};
enum { TITLEBAR_HEIGHT=42, DEVICE_TABS_WIDTH=168, CAPTION_BUTTON_WIDTH=42, CAPTION_GAP=8 };
static const int mouse_rates[]={125,500,1000,2000,4000,8000};
static const char *const mouse_rate_labels[]={"125","500","1.000","2.000","4.000","8.000"};

typedef struct {
 HWND window;
 HANDLE stop;
 viper_operation operation;
 int value;
 bool confirmed;
 viper_state previous,result;
} mouse_work;

static int active_tab;
static HANDLE mouse_thread;
static mouse_work mouse_job;
static viper_state mouse_state;
static char dpi_draft[16];
static int rate_choice,last_polling;
static bool mouse_read_once,dpi_dirty,rate_dirty,mouse_notice_error,mouse_refresh_failed;
static bool mouse_discard_result,mouse_refresh_pending,mouse_usb_timer_scheduled;
static bool mouse_input_watched,mouse_input_refresh_used;
static HANDLE other_raw_mouse;
static ULONGLONG mouse_reconnect_until;
static const char *mouse_notice;

static cui_ctx *timer_ui;
static char issue[192];
static bool worker_start_failed;
static bool preview_failed;
static bool startup_enabled;
static WNDPROC base_proc;
static bool titlebar_custom,max_button_hovered;
static LONG titlebar_height_px,titlebar_resize_px,titlebar_margin_px;
static RECT titlebar_controls,titlebar_max_button;
static HANDLE panel_thread;
static PVOID volatile panel_hwnd;
static volatile LONG close_requested;
static ULONG_PTR panel_generation;
static bool open_again,shutting_down;

static unsigned __stdcall mouse_worker(void *context){
 mouse_work *job=context;
 const viper_state *previous=job->operation==VIPER_READ?NULL:&job->previous;
 job->confirmed=viper_run(job->operation,job->value,previous,job->stop,&job->result);
 PostMessageW(job->window,MOUSE_READY,0,0);
 return 0;
}

static void request_mouse(HWND window,viper_operation operation,int value){
 if(mouse_thread)return;
 mouse_job=(mouse_work){.window=window,.operation=operation,.value=value};
 if(operation!=VIPER_READ)mouse_job.previous=mouse_state;
 mouse_discard_result=false;
 mouse_job.stop=CreateEventW(NULL,TRUE,FALSE,NULL);
 if(mouse_job.stop)mouse_thread=(HANDLE)_beginthreadex(NULL,0,mouse_worker,&mouse_job,0,NULL);
 mouse_refresh_failed=operation==VIPER_READ&&!mouse_thread;
 if(!mouse_thread){
  if(mouse_job.stop)CloseHandle(mouse_job.stop);
  mouse_reconnect_until=0;
  mouse_read_once=true;
  mouse_notice_error=operation!=VIPER_READ;
  mouse_notice=mouse_notice_error?"Não foi possível iniciar o ajuste. Use Atualizar e tente novamente.":NULL;
 }
 cui_request_redraw(timer_ui);
}

static void reset_mouse_state(bool read_once){
 mouse_state=(viper_state){.battery=-1};
 mouse_read_once=read_once;
 mouse_input_refresh_used=false;
 dpi_dirty=rate_dirty=mouse_notice_error=mouse_refresh_failed=false;
 mouse_notice=NULL;dpi_draft[0]=0;rate_choice=-1;last_polling=0;
}

static void accept_mouse_state(void){
 viper_state *result=&mouse_job.result;
 bool same_mouse=result->status==VIPER_CONNECTED&&mouse_state.status==VIPER_CONNECTED&&
                 _wcsicmp(result->path,mouse_state.path)==0;
 if(!same_mouse){dpi_dirty=rate_dirty=mouse_notice_error=false;last_polling=0;mouse_notice=NULL;}
 if(result->polling)last_polling=result->polling;
 if(same_mouse&&mouse_job.operation!=VIPER_READ){
  mouse_notice_error=!mouse_job.confirmed;
  if(mouse_job.confirmed){
   if(mouse_job.operation==VIPER_SET_DPI)dpi_dirty=false;
   if(mouse_job.operation==VIPER_SET_POLLING&&rate_choice>=0&&mouse_rates[rate_choice]==mouse_job.value)rate_dirty=false;
   mouse_notice=mouse_job.operation==VIPER_SET_DPI?"DPI confirmado pelo mouse.":"Taxa confirmada pelo mouse.";
  }else mouse_notice="O mouse não confirmou a alteração. Atualize e tente novamente.";
 }else if(!mouse_notice_error&&(result->dpi_x!=mouse_state.dpi_x||result->polling!=mouse_state.polling))mouse_notice=NULL;
 mouse_state=*result;
 mouse_read_once=true;
 if(!dpi_dirty){
  dpi_draft[0]=0;
  if(mouse_state.dpi_x)snprintf(dpi_draft,sizeof(dpi_draft),"%d",mouse_state.dpi_x);
 }
 if(!rate_dirty){
  rate_choice=-1;
  for(size_t i=0;i<_countof(mouse_rates);i++)if(mouse_rates[i]==last_polling)rate_choice=(int)i;
 }
}

static void cancel_mouse_read(void){
 if(mouse_thread&&mouse_job.operation==VIPER_READ){
  SetEvent(mouse_job.stop);
  CancelSynchronousIo(mouse_thread);
 }
}

static void finish_mouse_request(bool closing){
 if(!mouse_thread)return;
 if(closing)cancel_mouse_read();
 WaitForSingleObject(mouse_thread,INFINITE);
 CloseHandle(mouse_thread);
 CloseHandle(mouse_job.stop);
 mouse_thread=NULL;
 if(!closing&&!mouse_discard_result)accept_mouse_state();
}

static uint16_t mouse_hid_product(const wchar_t *path){
 static const wchar_t prefix[]=L"\\\\?\\HID#VID_1532&PID_00C";
 const size_t length=_countof(prefix)-1;
 if(_wcsnicmp(path,prefix,length)!=0||!path[length])return 0;
 if(path[length+1]!=L'&'&&path[length+1]!=L'#')return 0;
 return path[length]==L'1'?VIPER_WIRELESS_PID:path[length]==L'0'?VIPER_WIRED_PID:0;
}

static const wchar_t *mouse_usb_path(LPARAM data){
 const DEV_BROADCAST_HDR *event=(const DEV_BROADCAST_HDR *)data;
 const size_t name_offset=offsetof(DEV_BROADCAST_DEVICEINTERFACE_W,dbcc_name);
 if(!event||event->dbch_size<name_offset+sizeof(wchar_t)||event->dbch_devicetype!=DBT_DEVTYP_DEVICEINTERFACE)
  return NULL;
 const wchar_t *path=((const DEV_BROADCAST_DEVICEINTERFACE_W *)event)->dbcc_name;
 size_t capacity=(event->dbch_size-name_offset)/sizeof(wchar_t);
 return wcsnlen_s(path,capacity)<capacity&&mouse_hid_product(path)?path:NULL;
}

static void refresh_mouse_after_usb(HWND window){
 if(active_tab!=MOUSE_TAB)return;
 if(mouse_thread)mouse_refresh_pending=true;
 else request_mouse(window,VIPER_READ,0);
}

static void schedule_mouse_usb_refresh(HWND window,UINT delay){
 mouse_refresh_pending=false;
 mouse_usb_timer_scheduled=SetTimer(window,MOUSE_USB_TIMER,delay,NULL)!=0;
 if(!mouse_usb_timer_scheduled)refresh_mouse_after_usb(window);
}

static void watch_mouse_input(HWND window,bool enabled){
 if(enabled==mouse_input_watched)return;
 RAWINPUTDEVICE device={.usUsagePage=1,.usUsage=2,.dwFlags=enabled?0:RIDEV_REMOVE,
                        .hwndTarget=enabled?window:NULL};
 if(RegisterRawInputDevices(&device,1,sizeof(device))){
  mouse_input_watched=enabled;
  other_raw_mouse=NULL;
 }
}

static void mouse_input_ready(HWND window,HRAWINPUT input){
 if(!mouse_input_watched||mouse_input_refresh_used||active_tab!=MOUSE_TAB)return;
 RAWINPUTHEADER header;
 UINT size=sizeof(header);
 if(GetRawInputData(input,RID_HEADER,&header,&size,sizeof(header))!=sizeof(header)||
    header.dwType!=RIM_TYPEMOUSE||!header.hDevice||header.hDevice==other_raw_mouse)return;
 wchar_t path[HID_PATH_CAPACITY]={0};
 UINT capacity=_countof(path)-1;
 if(GetRawInputDeviceInfoW(header.hDevice,RIDI_DEVICENAME,path,&capacity)==(UINT)-1)return;
 if(mouse_hid_product(path)!=mouse_state.product){other_raw_mouse=header.hDevice;return;}
 // The first report from this mouse is enough to retry its settings channel.
 mouse_input_refresh_used=true;
 watch_mouse_input(window,false);
 KillTimer(window,MOUSE_USB_TIMER);
 mouse_usb_timer_scheduled=false;
 refresh_mouse_after_usb(window);
}

static void mouse_usb_change(HWND window,WPARAM change,LPARAM data){
 if(change!=DBT_DEVICEARRIVAL&&change!=DBT_DEVICEREMOVECOMPLETE)return;
 const wchar_t *path=mouse_usb_path(data);
 if(!path)return;
 if(change==DBT_DEVICEREMOVECOMPLETE){
  bool removed_current=mouse_state.path[0]&&_wcsicmp(path,mouse_state.path)==0;
  if(removed_current||!mouse_state.path[0])mouse_reconnect_until=0;
  if(mouse_thread&&(removed_current||!mouse_state.path[0])){
   mouse_discard_result=true;
   cancel_mouse_read();
  }
  if(removed_current){
   watch_mouse_input(window,false);
   reset_mouse_state(true);
   if(timer_ui)cui_request_redraw(timer_ui);
  }
 }
 if(active_tab!=MOUSE_TAB)return;
 if(change==DBT_DEVICEARRIVAL&&mouse_state.status!=VIPER_CONNECTED){
  mouse_reconnect_until=GetTickCount64()+MOUSE_USB_RECONNECT_MS;
  if(timer_ui)cui_request_redraw(timer_ui);
 }
 // One USB receiver can broadcast several HID interface events.
 schedule_mouse_usb_refresh(window,MOUSE_USB_DELAY_MS);
}

static LRESULT titlebar_hit(HWND window,WPARAM wparam,LPARAM lparam){
 POINT cursor={GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};
 RECT client;
 if(ScreenToClient(window,&cursor)&&GetClientRect(window,&client)&&PtInRect(&client,cursor)){
  // HTMAXBUTTON lets Windows offer Snap Layouts over the custom button.
  if(PtInRect(&titlebar_max_button,cursor))return HTMAXBUTTON;
  if(PtInRect(&titlebar_controls,cursor))return HTCLIENT;
  if(cursor.y<titlebar_height_px)return !IsZoomed(window)&&cursor.y<titlebar_resize_px?HTTOP:HTCAPTION;
 }
 return CallWindowProcW(base_proc,window,WM_NCHITTEST,wparam,lparam);
}

static LRESULT titlebar_mouse(HWND window,UINT message,WPARAM hit,LPARAM position){
 if(hit==HTMAXBUTTON&&message!=WM_NCMOUSELEAVE){
  POINT cursor={GET_X_LPARAM(position),GET_Y_LPARAM(position)};
  ScreenToClient(window,&cursor);
  if(message!=WM_NCMOUSEMOVE){
   bool down=message==WM_NCLBUTTONDOWN||message==WM_NCLBUTTONDBLCLK;
   return CallWindowProcW(base_proc,window,down?WM_LBUTTONDOWN:WM_LBUTTONUP,
                          down?MK_LBUTTON:0,MAKELPARAM(cursor.x,cursor.y));
  }
  if(!max_button_hovered){
   TRACKMOUSEEVENT track={sizeof(track),TME_NONCLIENT|TME_LEAVE,window,0};
   TrackMouseEvent(&track);
   max_button_hovered=true;
  }
  float scale=cui_dpi_scale(timer_ui);
  cui_input_mouse_move(timer_ui,(float)cursor.x/scale,(float)cursor.y/scale);
 }else if(max_button_hovered){
  max_button_hovered=false;
  cui_input_mouse_leave(timer_ui);
 }
 return CallWindowProcW(base_proc,window,message,hit,position);
}

static void enable_titlebar(HWND window){
 RECT outer;
 POINT client={0};
 if(!GetWindowRect(window,&outer)||!ClientToScreen(window,&client))return;
 titlebar_margin_px=client.y-outer.top;
 titlebar_custom=true;
 SetWindowPos(window,NULL,0,0,outer.right-outer.left,outer.bottom-outer.top-titlebar_margin_px,
              SWP_FRAMECHANGED|SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
}

HWND hs_panel_window(void){return (HWND)InterlockedCompareExchangePointer(&panel_hwnd,NULL,NULL);}
static LRESULT CALLBACK panel_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
 if(titlebar_custom){
  if(message==WM_NCCALCSIZE&&wparam){
   RECT *client=&((NCCALCSIZE_PARAMS *)lparam)->rgrc[0];
   LONG top=client->top,bottom=client->bottom;
   CallWindowProcW(base_proc,window,message,wparam,lparam);
   titlebar_resize_px=bottom-client->bottom;
   titlebar_margin_px=client->top-top;
   // The maximized window retains its inset to stay inside the monitor work area.
   client->top=top+(IsZoomed(window)?titlebar_resize_px:0);
   return 0;
  }
  if(message==WM_GETMINMAXINFO){
   LRESULT result=CallWindowProcW(base_proc,window,message,wparam,lparam);
   ((MINMAXINFO *)lparam)->ptMinTrackSize.y-=titlebar_margin_px;
   return result;
  }
  if(message==WM_NCHITTEST)return titlebar_hit(window,wparam,lparam);
  if(timer_ui&&(message==WM_NCMOUSEMOVE||message==WM_NCMOUSELEAVE||
      (wparam==HTMAXBUTTON&&message>=WM_NCLBUTTONDOWN&&message<=WM_NCLBUTTONDBLCLK)))
   return titlebar_mouse(window,message,wparam,lparam);
 }
 if(message==MOUSE_READY){
  finish_mouse_request(false);
  if(mouse_state.status==VIPER_CONNECTED)mouse_input_refresh_used=false;
  watch_mouse_input(window,active_tab==MOUSE_TAB&&!mouse_input_refresh_used&&
                          mouse_state.status==VIPER_UNREACHABLE&&mouse_state.product!=0);
  if(mouse_reconnect_until){
   bool retryable=mouse_state.status==VIPER_ABSENT||mouse_state.status==VIPER_UNREACHABLE;
   if(active_tab!=MOUSE_TAB||!retryable||GetTickCount64()>=mouse_reconnect_until)mouse_reconnect_until=0;
   else if(!mouse_refresh_pending&&!mouse_usb_timer_scheduled)schedule_mouse_usb_refresh(window,MOUSE_USB_RETRY_MS);
  }
  if(mouse_refresh_pending){mouse_refresh_pending=false;refresh_mouse_after_usb(window);}
  if(timer_ui)cui_request_redraw(timer_ui);
  return 0;
 }
 if(message==WM_DEVICECHANGE)mouse_usb_change(window,wparam,lparam);
 if(message==WM_INPUT)mouse_input_ready(window,(HRAWINPUT)lparam);
 if(message==WM_TIMER&&wparam==MOUSE_USB_TIMER){
  KillTimer(window,MOUSE_USB_TIMER);
  mouse_usb_timer_scheduled=false;
  refresh_mouse_after_usb(window);
  return 0;
 }
 if(message==HS_MSG_STATE){if(timer_ui)cui_request_redraw(timer_ui);return 0;}
 if(message==HS_MSG_RAISE||message==HS_MSG_STARTUP_CHANGED){
  startup_enabled=hs_autostart_enabled();
  if(timer_ui)cui_request_redraw(timer_ui);
  if(message==HS_MSG_RAISE){if(IsIconic(window))ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);}
  return 0;
 }
 if(message==HS_MSG_SOUND_RESULT){
  preview_failed=wparam==0;
  if(timer_ui)cui_request_redraw(timer_ui);
  return 0;
 }
 return CallWindowProcW(base_proc,window,message,wparam,lparam);
}
static void CALLBACK tick(HWND window,UINT message,UINT_PTR id,DWORD time){
 (void)message;(void)id;(void)time;
 if(timer_ui&&active_tab==MOUSE_TAB)request_mouse(window,VIPER_READ,0);
 if(timer_ui)cui_request_redraw(timer_ui);
}
static bool device_notice(cui_ctx *ui,const char *icon,const char *title,const char *detail,const char *action,bool busy){
 bool clicked=false;
 cui_card(ui,.width=CUI_FILL,.pad=20){
  cui_row(ui,.width=CUI_FILL,.gap=14,.align=CUI_ALIGN_START){
   cui_column(ui,.width=CUI_PX(36),.height=CUI_PX(36),.align=CUI_ALIGN_CENTER,.justify=CUI_JUSTIFY_CENTER,
              .bg=CUI_COLOR_SURFACE_MUTED,.radius=8){
    cui_icon(ui,icon,19,CUI_COLOR_TEXT_SECONDARY);
   }
   cui_column(ui,.width=CUI_FILL,.gap=7){
    cui_heading(ui,title,4);
    cui_secondary(ui,detail);
    if(action){
     cui_gap(ui,3);
     clicked=cui_button(ui,action,.variant=CUI_BUTTON_OUTLINE,.size=CUI_SIZE_SM,.disabled=busy);
    }
   }
  }
 }
 return clicked;
}
static void sound_choice(cui_ctx *ui,bool mic_active){
 static const char *const choices[HS_SOUND_COUNT]={
  [HS_SOUND_NONE]="Sem som", [HS_SOUND_PULSE]="Pulso", [HS_SOUND_DOUBLE]="Duplo",
  [HS_SOUND_RISE]="Subida", [HS_SOUND_FALL]="Descida",
 };
 int sound=hs_sound_preference(mic_active);
 cui_push_id(ui,mic_active?"som-ativo":"som-mudo");
 cui_column(ui,.width=CUI_FILL,.gap=5){
  cui_row(ui,.width=CUI_FILL,.justify=CUI_JUSTIFY_BETWEEN){
   cui_label(ui,mic_active?"Ao ativar":"Ao silenciar");
   if(cui_button(ui,"Ouvir",.variant=CUI_BUTTON_GHOST,.size=CUI_SIZE_SM,
                 .icon=CUI_ICON_PLAY,.disabled=sound==HS_SOUND_NONE)){
    HWND controller=hs_controller_window();
    preview_failed=!controller||!PostMessageW(controller,HS_MSG_SOUND_PREVIEW,(WPARAM)sound,0);
   }
  }
  if(cui_segmented(ui,"aviso",choices,HS_SOUND_COUNT,&sound,.full_width=true)){
   if(hs_save_sound_preference(mic_active,sound)){
    HWND controller=hs_controller_window();
    if(controller)PostMessageW(controller,HS_MSG_SOUND_OPTIONS,0,0);
    issue[0]=0;
    preview_failed=false;
   }else{
    strcpy_s(issue,sizeof(issue),"Não foi possível salvar a escolha de som.");
   }
  }
 }
 cui_pop_id(ui);
}
static void headset_unavailable(cui_ctx *ui,LONG status){
 switch(status){
 case HS_STOPPED:
  if(device_notice(ui,CUI_ICON_PAUSE,"Controle encerrado","Inicie o controle para detectar o headset.","Iniciar controle",false))
   worker_start_failed=!hs_launch_worker();
  if(worker_start_failed)cui_alert(ui,"O controle não iniciou",
   .description="Abra o painel pela pasta original do aplicativo e tente novamente.",.tone=CUI_TONE_DANGER);
  break;
 case HS_PAUSED:
  device_notice(ui,CUI_ICON_PAUSE,"Controle em pausa","Feche o iCUE para retomar o controle do headset neste painel.",NULL,false);
  break;
 case HS_AUDIO_ERROR:
  device_notice(ui,CUI_ICON_MIC,"Microfone indisponível","Confira se o microfone HS80 MAX está habilitado no Windows.",NULL,false);
  break;
 case HS_CONNECTING:
  device_notice(ui,CUI_ICON_LOADER_CIRCLE,"Procurando headset","O painel está verificando a conexão do HS80 MAX.",NULL,false);
  break;
 default:
  device_notice(ui,CUI_ICON_RADIO,"Desconectado","Ligue o headset com o receptor USB conectado.",NULL,false);
  break;
 }
}

static void headset_details(cui_ctx *ui,const hs_state *state){
 bool leds=hs_led_preference(),startup=startup_enabled;
 cui_column(ui,.gap=16){
  cui_column(ui,.gap=6){
   cui_row(ui,.justify=CUI_JUSTIFY_BETWEEN,.width=CUI_FILL){
    cui_label(ui,"Microfone");
    const char *mic=state->mic_hardware==1?"Silenciado pela haste":state->mic_windows==1?"Silenciado no Windows":"Ativo";
    cui_text(ui,mic,.font=CUI_FONT_MEDIUM);
   }
   cui_secondary(ui,state->mic_hardware==1?"Abaixe a haste para falar.":"Levante a haste para silenciar o microfone.");
   if(state->mic_hardware==0&&state->mic_windows==1){
    if(cui_button(ui,"Reativar no Windows",.variant=CUI_BUTTON_OUTLINE)){
     HWND controller=hs_controller_window();
     if(!controller||!PostMessageW(controller,HS_MSG_UNMUTE,0,0))strcpy_s(issue,sizeof(issue),"Não foi possível reativar. Tente novamente em alguns segundos.");
     else issue[0]=0;
    }
   }
  }
  cui_separator(ui);
  cui_column(ui,.gap=8){
   char battery[40]="Sem leitura";
   if(state->battery>=0)snprintf(battery,sizeof(battery),"%ld%%",state->battery);
   cui_row(ui,.justify=CUI_JUSTIFY_BETWEEN,.width=CUI_FILL){cui_label(ui,"Bateria");cui_text(ui,battery,.tabular=true,.font=CUI_FONT_MEDIUM);}
   if(state->battery>=0){
    cui_progress(ui,(float)state->battery/100.0f,.width=CUI_FILL,.height=6,.tone=state->battery<=20?CUI_TONE_WARNING:CUI_TONE_NEUTRAL);
    if(state->charging==1)cui_secondary(ui,"Carregando");else if(state->charging==2)cui_secondary(ui,"Carga completa");
   }
  }
 }
 cui_column(ui,.gap=18){
  cui_heading(ui,"Preferências",4);
  if(cui_switch(ui,"Manter LEDs apagados",&leds,.description="Logotipos e luz na ponta do microfone.")){
   if(!hs_save_led_preference(leds))strcpy_s(issue,sizeof(issue),"Não foi possível salvar a preferência dos LEDs. Verifique a pasta do aplicativo.");
   else{HWND controller=hs_controller_window();if(controller)PostMessageW(controller,HS_MSG_LEDS,leds?1:0,0);issue[0]=0;}
  }
  if(cui_switch(ui,"Iniciar com o Windows",&startup,.description="Aplicar as preferências em segundo plano ao entrar na sua conta.")){
   if(!hs_set_autostart(startup))strcpy_s(issue,sizeof(issue),"Não foi possível alterar a inicialização. Tente novamente pela sua conta do Windows.");
   else issue[0]=0;
   startup_enabled=hs_autostart_enabled();
  }
 }
 cui_column(ui,.width=CUI_FILL,.gap=9){
  cui_heading(ui,"Sons do microfone",4);
  sound_choice(ui,true);
  sound_choice(ui,false);
  cui_secondary(ui,preview_failed?
   "Não foi possível tocar a prévia. Confira a saída de áudio.":
   "Os avisos acompanham o mudo da haste e do Windows.");
 }
 if(issue[0])cui_alert(ui,"A alteração não foi aplicada",.description=issue,.tone=CUI_TONE_DANGER);
//  cui_separator(ui);
//  cui_secondary(ui,"Pode fechar esta janela. O controle continua no ícone do headset, perto do relógio.");
}

static void headset_frame(cui_ctx *ui){
 hs_state state={0};
 if(!hs_read_state(&state)||GetTickCount64()-state.updated>=10000)state.status=HS_STOPPED;
 if(state.status!=HS_STOPPED)worker_start_failed=false;
 bool active=state.status==HS_ACTIVE;
 cui_scroll(ui,.id=active?"headset-panel":"headset-notice",.width=CUI_FILL,.height=CUI_FILL,
             .persistent_bar=true,.reveal_focus=true){
  cui_column(ui,.width=CUI_FILL,.pad=28,.gap=24){
   cui_column(ui,.gap=10){
    cui_heading(ui,"HS80 MAX",2);
    if(active)cui_badge(ui,"Conectado",.tone=CUI_TONE_SUCCESS,.dot=true);
   }
   if(active)headset_details(ui,&state);
   else headset_unavailable(ui,state.status);
  }
 }
}

static int draft_dpi(void){
 for(const char *ch=dpi_draft;*ch;ch++)if(*ch<'0'||*ch>'9')return 0;
 long value=strtol(dpi_draft,NULL,10);
 return viper_valid_dpi((int)value)?(int)value:0;
}

static void mouse_unavailable(cui_ctx *ui,HWND window,bool busy){
 const char *icon=CUI_ICON_RADIO;
 const char *title="Desconectado";
 const char *detail="Conecte o receptor sem fio ou o cabo USB e ligue o Viper.";
 if(mouse_state.status==VIPER_DUPLICATE){
  icon=CUI_ICON_CIRCLE_ALERT;
  title="Mais de um Viper detectado";
  detail="Deixe conectado apenas o mouse que você quer configurar.";
 }else if(mouse_state.product==VIPER_WIRELESS_PID&&mouse_state.status==VIPER_UNREACHABLE){
  title="Aguardando mouse";
  detail="O receptor USB está conectado. Ligue ou mova o Viper para retomar a comunicação.";
 }else if(mouse_reconnect_until||!mouse_read_once){
  icon=CUI_ICON_LOADER_CIRCLE;
  title="Procurando mouse";
  detail="O painel está verificando a conexão do Viper.";
 }else if(mouse_state.status==VIPER_UNREACHABLE){
  icon=CUI_ICON_CIRCLE_ALERT;
  title="Sem resposta";
  detail="Confira a conexão USB do Viper e tente atualizar.";
 }
 if(device_notice(ui,icon,title,detail,"Atualizar",busy))request_mouse(window,VIPER_READ,0);
}

static void mouse_details(cui_ctx *ui,HWND window,bool wireless,bool busy){
 char battery[24]="Sem leitura",current_dpi[48]="Sem leitura",current_rate[24]="Sem leitura";
 if(mouse_state.battery>=0)snprintf(battery,sizeof(battery),"%d%%",mouse_state.battery);
 if(mouse_state.dpi_x){
  if(mouse_state.dpi_x==mouse_state.dpi_y)snprintf(current_dpi,sizeof(current_dpi),"%d DPI",mouse_state.dpi_x);
  else snprintf(current_dpi,sizeof(current_dpi),"X %d / Y %d",mouse_state.dpi_x,mouse_state.dpi_y);
 }
 if(last_polling)snprintf(current_rate,sizeof(current_rate),"%d Hz",last_polling);
  cui_column(ui,.width=CUI_FILL,.gap=8){
   cui_row(ui,.width=CUI_FILL,.justify=CUI_JUSTIFY_BETWEEN){cui_label(ui,"Bateria");cui_text(ui,battery,.tabular=true,.font=CUI_FONT_MEDIUM);}
   if(mouse_state.battery>=0)cui_progress(ui,(float)mouse_state.battery/100.0f,.width=CUI_FILL,.height=6,
                                        .tone=mouse_state.battery<=20?CUI_TONE_WARNING:CUI_TONE_SUCCESS);
   if(mouse_state.charging)cui_secondary(ui,"Carregando via USB");
  }
  cui_separator(ui);
  cui_column(ui,.width=CUI_FILL,.gap=9){
   cui_row(ui,.width=CUI_FILL,.justify=CUI_JUSTIFY_BETWEEN){cui_label(ui,"Sensibilidade");cui_text(ui,current_dpi,.tabular=true,.font=CUI_FONT_MEDIUM);}
   if(mouse_state.dpi_x){
    cui_row(ui,.width=CUI_FILL,.gap=10,.align=CUI_ALIGN_CENTER){
     int input=cui_input(ui,"dpi",dpi_draft,sizeof(dpi_draft),.width=CUI_FILL,.suffix="DPI",.disabled=busy&&mouse_job.operation!=VIPER_READ);
     if(input&CUI_CHANGED){dpi_dirty=true;mouse_notice=NULL;}
     int value=draft_dpi();
     bool editable=!busy&&value&&(value!=mouse_state.dpi_x||value!=mouse_state.dpi_y);
     bool apply=cui_button(ui,"Aplicar DPI",.variant=CUI_BUTTON_OUTLINE,.disabled=!editable);
     if(editable&&(apply||(input&CUI_SUBMITTED)))request_mouse(window,VIPER_SET_DPI,value);
    }
    cui_secondary(ui,"100 a 35.000 DPI. O valor vale para os dois eixos.");
   }
  }
  cui_column(ui,.width=CUI_FILL,.gap=9){
   cui_row(ui,.width=CUI_FILL,.justify=CUI_JUSTIFY_BETWEEN){cui_label(ui,"Taxa de polling");cui_text(ui,current_rate,.tabular=true,.font=CUI_FONT_MEDIUM);}
   int count=wireless?(int)_countof(mouse_rates):3;
   cui_disabled_scope(ui,!mouse_state.polling){
    if(cui_segmented(ui,"taxa",mouse_rate_labels,count,&rate_choice,.size=CUI_SIZE_SM,.full_width=true)){
     rate_dirty=true;
     mouse_notice=NULL;
    }
   }
   if(mouse_state.polling){
    cui_row(ui,.width=CUI_FILL,.justify=CUI_JUSTIFY_END){
     bool editable=!busy&&rate_choice>=0&&rate_choice<count&&mouse_rates[rate_choice]!=mouse_state.polling;
     if(cui_button(ui,"Aplicar taxa",.variant=CUI_BUTTON_OUTLINE,.disabled=!editable))
      request_mouse(window,VIPER_SET_POLLING,mouse_rates[rate_choice]);
    }
   }else{
    cui_secondary(ui,last_polling?"Última leitura. Use Atualizar antes de aplicar outra taxa.":"Use Atualizar para consultar a taxa do mouse.");
   }
  }
  if(busy&&mouse_job.operation!=VIPER_READ)cui_secondary(ui,"Confirmando o ajuste no mouse…");
  if(mouse_notice){
   if(mouse_notice_error)cui_alert(ui,mouse_notice,.tone=CUI_TONE_WARNING);
   else cui_secondary(ui,mouse_notice);
  }
}

static void mouse_frame(cui_ctx *ui,HWND window){
 bool connected=mouse_state.status==VIPER_CONNECTED;
 bool wireless=mouse_state.product==VIPER_WIRELESS_PID;
 bool busy=mouse_thread!=NULL;
 cui_scroll(ui,.id=connected?"mouse-panel":"mouse-notice",.width=CUI_FILL,.height=CUI_FILL,
             .persistent_bar=true,.reveal_focus=true){
  cui_column(ui,.width=CUI_FILL,.pad=28,.gap=24){
   cui_column(ui,.width=CUI_FILL,.gap=10){
    cui_heading(ui,"Razer Viper V3 Pro",2);
    if(connected){
     cui_row(ui,.width=CUI_FILL,.justify=CUI_JUSTIFY_BETWEEN,.align=CUI_ALIGN_CENTER){
      cui_badge(ui,wireless?"Conectado sem fio":"Conectado via USB",.tone=CUI_TONE_SUCCESS,.dot=true);
      if(cui_button(ui,"Atualizar",.variant=CUI_BUTTON_GHOST,.size=CUI_SIZE_SM,.icon=CUI_ICON_REFRESH_CW,.disabled=busy))
       request_mouse(window,VIPER_READ,0);
     }
    }
   }
   if(connected)mouse_details(ui,window,wireless,busy);
   else mouse_unavailable(ui,window,busy);
   if(mouse_refresh_failed)cui_alert(ui,"Não foi possível consultar o mouse. Use Atualizar.",.tone=CUI_TONE_WARNING);
  }
 }
}

static void device_tabs(cui_ctx *ui,HWND window,bool in_titlebar){
 static const char *const tabs[]={"Headset","Mouse"};
 if(cui_tabs(ui,"dispositivos",tabs,2,&active_tab,.variant=in_titlebar?CUI_TABS_PILL:CUI_TABS_UNDERLINE,
             .full_width=in_titlebar)){
  if(active_tab==MOUSE_TAB)request_mouse(window,VIPER_READ,0);
  else{mouse_reconnect_until=0;watch_mouse_input(window,false);}
 }
}

static void panel_titlebar(cui_ctx *ui,HWND window){
 float scale=cui_dpi_scale(ui),right=cui_viewport_size(ui).x-CAPTION_GAP;
 float controls_width=DEVICE_TABS_WIDTH+CAPTION_GAP+3*CAPTION_BUTTON_WIDTH;
 float button_top=(TITLEBAR_HEIGHT-cui_theme_get(ui)->control_sm)/2;
 bool maximized=IsZoomed(window)!=0;
 titlebar_height_px=(LONG)(TITLEBAR_HEIGHT*scale);
 titlebar_controls=(RECT){(LONG)((right-controls_width)*scale),(LONG)(3*scale),
                          (LONG)(right*scale),(LONG)((TITLEBAR_HEIGHT-3)*scale)};
 titlebar_max_button=(RECT){(LONG)((right-2*CAPTION_BUTTON_WIDTH)*scale),(LONG)(button_top*scale),
                            (LONG)((right-CAPTION_BUTTON_WIDTH)*scale),(LONG)((TITLEBAR_HEIGHT-button_top)*scale)};
 cui_row(ui,.width=CUI_FILL,.height=CUI_PX(TITLEBAR_HEIGHT),.pad_left=16,.pad_right=CAPTION_GAP,
         .align=CUI_ALIGN_CENTER,.justify=CUI_JUSTIFY_BETWEEN){
  cui_row(ui,.align=CUI_ALIGN_CENTER,.gap=8){
   cui_icon(ui,CUI_ICON_SLIDERS_HORIZONTAL,15,CUI_COLOR_TEXT_SECONDARY);
   cui_text(ui,"Periféricos",.font=CUI_FONT_MEDIUM,.color=CUI_COLOR_TEXT_SECONDARY);
  }
  cui_row(ui,.width=CUI_PX(controls_width),.align=CUI_ALIGN_CENTER,.gap=CAPTION_GAP){
   cui_column(ui,.width=CUI_PX(DEVICE_TABS_WIDTH)){device_tabs(ui,window,true);}
   cui_row(ui,.width=CUI_PX(3*CAPTION_BUTTON_WIDTH),.align=CUI_ALIGN_CENTER){
    cui_button_opts button={.variant=CUI_BUTTON_GHOST,.size=CUI_SIZE_SM,.width=CUI_PX(CAPTION_BUTTON_WIDTH)};
    button.icon=CUI_ICON_MINUS;
    button.tooltip="Minimizar";
    if(cui_button_ex(ui,"##minimize",&button))PostMessageW(window,WM_SYSCOMMAND,SC_MINIMIZE,0);
    button.icon=maximized?CUI_ICON_COPY:CUI_ICON_SQUARE;
    button.tooltip=maximized?"Restaurar":"Maximizar";
    if(cui_button_ex(ui,"##maximize",&button))PostMessageW(window,WM_SYSCOMMAND,maximized?SC_RESTORE:SC_MAXIMIZE,0);
    button.icon=CUI_ICON_X;
    button.tooltip="Fechar painel";
    if(cui_button_ex(ui,"##close-panel",&button))PostMessageW(window,WM_SYSCOMMAND,SC_CLOSE,0);
   }
  }
 }
 cui_separator(ui);
}

static void frame(cui_ctx *ui,HWND window){
 cui_column(ui,.width=CUI_FILL,.height=CUI_FILL){
  if(titlebar_custom)panel_titlebar(ui,window);
  else cui_column(ui,.width=CUI_FILL,.pad_x=28,.pad_top=8){device_tabs(ui,window,false);}
  if(active_tab==MOUSE_TAB)mouse_frame(ui,window);
  else headset_frame(ui);
 }
}

static int panel_loop(bool capture){
 if(!cui_hs80_memory_begin())return 1;
 issue[0]=0;worker_start_failed=preview_failed=false;startup_enabled=hs_autostart_enabled();int width=560,height=744;
 active_tab=HEADSET_TAB;
 reset_mouse_state(false);
 mouse_discard_result=mouse_refresh_pending=mouse_usb_timer_scheduled=false;
 mouse_reconnect_until=0;
 mouse_input_watched=false;other_raw_mouse=NULL;
 titlebar_custom=max_button_hovered=false;
 titlebar_height_px=titlebar_resize_px=titlebar_margin_px=0;
 titlebar_controls=titlebar_max_button=(RECT){0};
 if(capture){const char *size=getenv("HS80_CAPTURE_SIZE");int w=0,h=0;if(size&&sscanf_s(size,"%dx%d",&w,&h)==2&&w>=460&&w<=1600&&h>=500&&h<=1200){width=w;height=h;}}
 if(capture){
  const char *tab=getenv("HS80_CAPTURE_TAB");
  if(tab&&strcmp(tab,"mouse")==0){
   active_tab=MOUSE_TAB;
   mouse_job.operation=VIPER_READ;
   viper_run(VIPER_READ,0,NULL,NULL,&mouse_job.result);
   accept_mouse_state();
  }
 }
 cui_app *app=cui_app_create(&(cui_app_desc){.title="Controle de periféricos",.width=width,.height=height,.min_width=460,.min_height=500,.hidden=true,.renderer=CUI_RENDERER_SOFTWARE,.follow_system_theme=true,.accent=CUI_ACCENT_GREEN});
 if(!app){cui_hs80_memory_end();return 1;}
 timer_ui=cui_app_ui(app);cui_set_reduced_motion(timer_ui,true);cui_theme_get(timer_ui)->font_scale=1.08f;
 HWND window=(HWND)cui_app_native_window(app);HICON icon=hs_icon();
 base_proc=(WNDPROC)SetWindowLongPtrW(window,GWLP_WNDPROC,(LONG_PTR)panel_proc);
 if(base_proc)enable_titlebar(window);
 InterlockedExchangePointer(&panel_hwnd,window);
 if(icon){SendMessageW(window,WM_SETICON,ICON_SMALL,(LPARAM)icon);SendMessageW(window,WM_SETICON,ICON_BIG,(LPARAM)icon);}
 HDEVNOTIFY mouse_notifications=NULL;
 if(!capture){
  DEV_BROADCAST_DEVICEINTERFACE_W filter={.dbcc_size=sizeof(filter),.dbcc_devicetype=DBT_DEVTYP_DEVICEINTERFACE};
  HidD_GetHidGuid(&filter.dbcc_classguid);
  mouse_notifications=RegisterDeviceNotificationW(window,&filter,DEVICE_NOTIFY_WINDOW_HANDLE);
 }
 SetTimer(window,PANEL_TICK_TIMER,15000,tick);
 if(!capture){
  if(InterlockedCompareExchange(&close_requested,0,0))cui_app_quit(app);
  else ShowWindow(window,SW_SHOWNORMAL);
 }
 while(cui_app_frame(app)){
  if(!capture&&InterlockedCompareExchange(&close_requested,0,0)){cui_app_quit(app);continue;}
  frame(cui_app_ui(app),window);
 }
 if(mouse_notifications)UnregisterDeviceNotification(mouse_notifications);
 watch_mouse_input(window,false);
 KillTimer(window,MOUSE_USB_TIMER);
 KillTimer(window,PANEL_TICK_TIMER);
 finish_mouse_request(true);
 InterlockedExchangePointer(&panel_hwnd,NULL);
 timer_ui=NULL;cui_app_destroy(app);if(icon)DestroyIcon(icon);
 cui_hs80_memory_end();return 0;
}
static unsigned __stdcall panel_entry(void *context){
 int result=panel_loop(false);
 HWND controller=hs_controller_window();
 if(controller)PostMessageW(controller,HS_MSG_PANEL_DONE,(WPARAM)(ULONG_PTR)context,0);
 return (unsigned)result;
}
void hs_panel_show(void){
 if(shutting_down)return;
 if(panel_thread&&WaitForSingleObject(panel_thread,0)==WAIT_OBJECT_0){CloseHandle(panel_thread);panel_thread=NULL;}
 if(panel_thread){
  if(InterlockedCompareExchange(&close_requested,0,0)){open_again=true;return;}
  HWND window=hs_panel_window();if(window)PostMessageW(window,HS_MSG_RAISE,0,0);
  return;
 }
 open_again=false;InterlockedExchange(&close_requested,0);panel_generation++;
 panel_thread=(HANDLE)_beginthreadex(NULL,0,panel_entry,(void *)panel_generation,0,NULL);
}
void hs_panel_close(void){
 open_again=false;InterlockedExchange(&close_requested,1);
 HWND window=hs_panel_window();if(window)PostMessageW(window,WM_CLOSE,0,0);
}
void hs_panel_reap(ULONG_PTR generation){
 if(!panel_thread||generation!=panel_generation)return;
 WaitForSingleObject(panel_thread,INFINITE);CloseHandle(panel_thread);panel_thread=NULL;
 if(open_again&&!shutting_down)hs_panel_show();
}
void hs_panel_shutdown(void){
 shutting_down=true;hs_panel_close();
 if(panel_thread){WaitForSingleObject(panel_thread,INFINITE);CloseHandle(panel_thread);panel_thread=NULL;}
}
int hs_panel_capture(void){
 if(!getenv("CUI_SCREENSHOT"))return 2;
 return panel_loop(true);
}
