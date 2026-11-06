#include "hs80.h"
#include <cui/cui.h>
#include <cui/cui_app.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cui_ctx *timer_ui;
static char issue[192];
static WNDPROC base_proc;
static LRESULT CALLBACK panel_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
 if(message==HS_MSG_STATE){if(timer_ui)cui_request_redraw(timer_ui);return 0;}
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
int main(void){
 HANDLE mutex=CreateMutexW(NULL,TRUE,L"Local\\HS80-Control-Panel-marcos");
 if(!mutex)return 1;
 if(GetLastError()==ERROR_ALREADY_EXISTS){HWND existing=FindWindowW(NULL,HS_PANEL_TITLE);if(existing){ShowWindow(existing,SW_RESTORE);SetForegroundWindow(existing);}CloseHandle(mutex);return 0;}
 bool capture=getenv("CUI_SCREENSHOT")!=NULL;int width=560,height=590;
 if(capture){const char *size=getenv("HS80_CAPTURE_SIZE");int w=0,h=0;if(size&&sscanf_s(size,"%dx%d",&w,&h)==2&&w>=460&&w<=1600&&h>=500&&h<=1200){width=w;height=h;}}
 if(!capture&&!hs_controller_window())hs_launch_worker();
 cui_app *app=cui_app_create(&(cui_app_desc){.title="HS80 MAX — Controle",.width=width,.height=height,.min_width=460,.min_height=500,.hidden=capture,.renderer=CUI_RENDERER_SOFTWARE,.follow_system_theme=true,.accent=CUI_ACCENT_GREEN});
 if(!app){ReleaseMutex(mutex);CloseHandle(mutex);return 1;}
 timer_ui=cui_app_ui(app);cui_set_reduced_motion(timer_ui,true);cui_theme_get(timer_ui)->font_scale=1.08f;
 HWND window=(HWND)cui_app_native_window(app);HICON icon=hs_icon();
 base_proc=(WNDPROC)SetWindowLongPtrW(window,GWLP_WNDPROC,(LONG_PTR)panel_proc);
 if(icon){SendMessageW(window,WM_SETICON,ICON_SMALL,(LPARAM)icon);SendMessageW(window,WM_SETICON,ICON_BIG,(LPARAM)icon);}
 SetTimer(window,1,15000,tick);
 while(cui_app_frame(app))frame(cui_app_ui(app));
 timer_ui=NULL;KillTimer(window,1);cui_app_destroy(app);if(icon)DestroyIcon(icon);
 ReleaseMutex(mutex);CloseHandle(mutex);return 0;
}
