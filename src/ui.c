#include "hs80.h"
#include <cui/cui.h>
#include <cui/cui_app.h>
#include <cui/cui_hs80_memory.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cui_ctx *timer_ui;
static char issue[192];
static WNDPROC base_proc;
static HANDLE panel_thread;
static PVOID volatile panel_hwnd;
static volatile LONG close_requested;
static ULONG_PTR panel_generation;
static bool open_again,shutting_down;
HWND hs_panel_window(void){return (HWND)InterlockedCompareExchangePointer(&panel_hwnd,NULL,NULL);}
static LRESULT CALLBACK panel_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
 if(message==HS_MSG_STATE){if(timer_ui)cui_request_redraw(timer_ui);return 0;}
 if(message==HS_MSG_RAISE){ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);return 0;}
 return CallWindowProcW(base_proc,window,message,wparam,lparam);
}
static void CALLBACK tick(HWND window,UINT message,UINT_PTR id,DWORD time){
 (void)window;(void)message;(void)id;(void)time;
 if(timer_ui)cui_request_redraw(timer_ui);
}
static const char *connection_name(LONG status){
 switch(status){case HS_ACTIVE:return "Conectado";case HS_PAUSED:return "Em pausa";
 case HS_CONNECTING:return "Conectando";case HS_AUDIO_ERROR:return "Verificar microfone";
 case HS_STOPPED:return "Controle encerrado";default:return "Aguardando headset";}
}
static void frame(cui_ctx *ui){
 hs_state state={0};bool running=hs_read_state(&state)&&GetTickCount64()-state.updated<10000;
 if(!running){state.status=HS_STOPPED;state.mic_hardware=state.mic_windows=state.battery=-1;}
 bool active=running&&state.status==HS_ACTIVE;
 bool leds=hs_led_preference(),startup=hs_autostart_enabled();
 cui_tone tone=active?CUI_TONE_SUCCESS:state.status==HS_AUDIO_ERROR?CUI_TONE_DANGER:CUI_TONE_NEUTRAL;
 cui_scroll(ui,.id="panel",.width=CUI_FILL,.height=CUI_FILL){
  cui_column(ui,.width=CUI_FILL,.pad=28,.gap=24){
   cui_column(ui,.gap=10){
    cui_heading(ui,"HS80 MAX",2);
    cui_badge(ui,connection_name(state.status),.tone=tone,.dot=true);
   }
   if(state.status==HS_STOPPED){
    cui_alert(ui,"O controle está encerrado",.description="Inicie o controle para aplicar os LEDs e acompanhar o headset.",.tone=CUI_TONE_WARNING);
    if(cui_button(ui,"Iniciar controle",.variant=CUI_BUTTON_PRIMARY)){
     if(!hs_launch_worker())strcpy_s(issue,sizeof(issue),"Não foi possível iniciar. Abra o painel pela pasta original do aplicativo.");
     else issue[0]=0;
    }
   }else if(state.status==HS_PAUSED){
    cui_alert(ui,"O iCUE está controlando o headset",.description="Feche o iCUE para retomar automaticamente suas preferências neste painel.",.tone=CUI_TONE_INFO);
   }else if(state.status==HS_AUDIO_ERROR){
    cui_alert(ui,"O microfone não está disponível",.description="Ligue o headset e confira se o microfone HS80 MAX está habilitado no Windows. O controle tenta reconectar automaticamente.",.tone=CUI_TONE_DANGER);
   }else if(!active){
    cui_alert(ui,"Aguardando conexão",.description="Ligue o headset e conecte o receptor USB. O painel se atualiza automaticamente.",.tone=CUI_TONE_INFO);
   }
   cui_column(ui,.gap=16){
    cui_column(ui,.gap=6){
     cui_row(ui,.justify=CUI_JUSTIFY_BETWEEN,.width=CUI_FILL){
      cui_label(ui,"Microfone");
      const char *mic=!active?"Indisponível":state.mic_hardware==1?"Silenciado pela haste":state.mic_windows==1?"Silenciado no Windows":"Ativo";
      cui_text(ui,mic,.font=CUI_FONT_MEDIUM);
     }
     cui_secondary(ui,active&&state.mic_hardware==1?"Abaixe a haste para falar.":"Levante a haste para silenciar o microfone.");
     if(active&&state.mic_hardware==0&&state.mic_windows==1){
      if(cui_button(ui,"Reativar no Windows",.variant=CUI_BUTTON_OUTLINE)){
       HWND controller=hs_controller_window();
       if(!controller||!PostMessageW(controller,HS_MSG_UNMUTE,0,0))strcpy_s(issue,sizeof(issue),"Não foi possível reativar. Tente novamente em alguns segundos.");
       else issue[0]=0;
      }
     }
    }
    cui_separator(ui);
    cui_column(ui,.gap=8){
     char battery[40];
     if(active&&state.battery>=0)snprintf(battery,sizeof(battery),"%ld%%",state.battery);else strcpy_s(battery,sizeof(battery),"Sem leitura");
     cui_row(ui,.justify=CUI_JUSTIFY_BETWEEN,.width=CUI_FILL){cui_label(ui,"Bateria");cui_text(ui,battery,.tabular=true,.font=CUI_FONT_MEDIUM);}
     if(active&&state.battery>=0){
      cui_progress(ui,(float)state.battery/100.0f,.width=CUI_FILL,.height=6,.tone=state.battery<=20?CUI_TONE_WARNING:CUI_TONE_NEUTRAL);
      if(state.charging==1)cui_secondary(ui,"Carregando");else if(state.charging==2)cui_secondary(ui,"Carga completa");
     }
    }
   }
   cui_column(ui,.gap=18){
    cui_heading(ui,"Preferências",4);
    if(cui_switch(ui,"Manter LEDs apagados",&leds,.description="Logotipos e luz na ponta do microfone.")){
     if(!hs_save_led_preference(leds))strcpy_s(issue,sizeof(issue),"Não foi possível salvar a preferência dos LEDs. Verifique a pasta do aplicativo.");
     else{HWND controller=hs_controller_window();if(controller)PostMessageW(controller,HS_MSG_LEDS,leds?1:0,0);issue[0]=0;}
    }
    if(cui_switch(ui,"Iniciar com o Windows",&startup,.description="Aplicar as preferências ao entrar na sua conta.")){
     if(!hs_set_autostart(startup))strcpy_s(issue,sizeof(issue),"Não foi possível alterar a inicialização. Tente novamente pela sua conta do Windows.");
     else issue[0]=0;
    }
   }
   if(issue[0])cui_alert(ui,"A alteração não foi aplicada",.description=issue,.tone=CUI_TONE_DANGER);
   cui_separator(ui);
   cui_secondary(ui,running?"Pode fechar esta janela. O controle continua no ícone do headset, perto do relógio.":"Suas preferências estão salvas. Inicie o controle quando quiser voltar a aplicá-las.");
  }
 }
}
static int panel_loop(bool capture){
 if(!cui_hs80_memory_begin())return 1;
 issue[0]=0;int width=560,height=590;
 if(capture){const char *size=getenv("HS80_CAPTURE_SIZE");int w=0,h=0;if(size&&sscanf_s(size,"%dx%d",&w,&h)==2&&w>=460&&w<=1600&&h>=500&&h<=1200){width=w;height=h;}}
 cui_app *app=cui_app_create(&(cui_app_desc){.title="HS80 MAX — Controle",.width=width,.height=height,.min_width=460,.min_height=500,.hidden=capture,.renderer=CUI_RENDERER_SOFTWARE,.follow_system_theme=true,.accent=CUI_ACCENT_GREEN});
 if(!app){cui_hs80_memory_end();return 1;}
 timer_ui=cui_app_ui(app);cui_set_reduced_motion(timer_ui,true);cui_theme_get(timer_ui)->font_scale=1.08f;
 HWND window=(HWND)cui_app_native_window(app);HICON icon=hs_icon();
 base_proc=(WNDPROC)SetWindowLongPtrW(window,GWLP_WNDPROC,(LONG_PTR)panel_proc);
 InterlockedExchangePointer(&panel_hwnd,window);
 if(icon){SendMessageW(window,WM_SETICON,ICON_SMALL,(LPARAM)icon);SendMessageW(window,WM_SETICON,ICON_BIG,(LPARAM)icon);}
 SetTimer(window,1,15000,tick);
 if(!capture&&InterlockedCompareExchange(&close_requested,0,0))cui_app_quit(app);
 while(cui_app_frame(app)){
  if(!capture&&InterlockedCompareExchange(&close_requested,0,0)){cui_app_quit(app);continue;}
  frame(cui_app_ui(app));
 }
 InterlockedExchangePointer(&panel_hwnd,NULL);
 timer_ui=NULL;KillTimer(window,1);cui_app_destroy(app);if(icon)DestroyIcon(icon);
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
