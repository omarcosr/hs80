#include "hs80.h"
#include <tlhelp32.h>
#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>

bool hs_paths(wchar_t *worker,wchar_t *panel,wchar_t *ini,size_t cap){
 wchar_t base[1024];DWORD n=GetModuleFileNameW(NULL,base,1024);
 if(!n||n>=1024)return false;
 wchar_t *slash=wcsrchr(base,L'\\');if(!slash)return false;slash[1]=0;
 if(worker&&swprintf_s(worker,cap,L"%sHS80-Leds.exe",base)<0)return false;
 if(panel&&swprintf_s(panel,cap,L"%sHS80-Controle.exe",base)<0)return false;
 if(ini&&swprintf_s(ini,cap,L"%shs80.ini",base)<0)return false;
 return true;
}
bool hs_read_state(hs_state *out){
 HANDLE h=OpenFileMappingW(FILE_MAP_READ,FALSE,HS_MAP);if(!h)return false;
 hs_state *p=MapViewOfFile(h,FILE_MAP_READ,0,0,sizeof(*p));bool ok=false;
 if(p){for(int i=0;i<5;i++){
  LONG before=*(volatile LONG *)&p->sequence;
  if(before&1){SwitchToThread();continue;}
  MemoryBarrier();memcpy(out,p,sizeof(*out));MemoryBarrier();
  LONG after=*(volatile LONG *)&p->sequence;
  if(before==after&&out->magic==HS_STATE_MAGIC&&out->version==1){ok=true;break;}
 }UnmapViewOfFile(p);}
 CloseHandle(h);return ok;
}
void hs_publish(hs_state *p,const hs_state *s){
 InterlockedIncrement(&p->sequence);MemoryBarrier();
 p->magic=HS_STATE_MAGIC;p->version=1;p->pid=GetCurrentProcessId();
 p->status=s->status;p->leds_off=s->leds_off;p->mic_hardware=s->mic_hardware;
 p->mic_windows=s->mic_windows;p->battery=s->battery;p->charging=s->charging;
 p->updated=GetTickCount64();p->error=s->error;
 MemoryBarrier();InterlockedIncrement(&p->sequence);
}
HWND hs_controller_window(void){return FindWindowExW(HWND_MESSAGE,NULL,HS_WINDOW,NULL);}
bool hs_launch(const wchar_t *path,const wchar_t *arguments){
 wchar_t command[2300];if(swprintf_s(command,2300,L"\"%s\" %s",path,arguments?arguments:L"")<0)return false;
 STARTUPINFOW si={sizeof(si)};PROCESS_INFORMATION pi={0};
 if(!CreateProcessW(path,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi))return false;
 CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return true;
}
bool hs_launch_worker(void){wchar_t path[1024];return hs_paths(path,NULL,NULL,1024)&&hs_launch(path,L"--run");}
bool hs_launch_panel(void){wchar_t path[1024];return hs_paths(NULL,path,NULL,1024)&&hs_launch(path,L"");}
static bool startup_command(wchar_t *out,size_t cap){
 wchar_t path[1024];return hs_paths(path,NULL,NULL,1024)&&swprintf_s(out,cap,L"\"%s\" --run",path)>=0;
}
bool hs_autostart_enabled(void){
 wchar_t expected[1200],actual[1200];DWORD size=sizeof(actual);
 if(!startup_command(expected,1200))return false;
 if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",HS_RUN_NAME,RRF_RT_REG_SZ,NULL,actual,&size)!=ERROR_SUCCESS)return false;
 return _wcsicmp(expected,actual)==0;
}
bool hs_set_autostart(bool enabled){
 HKEY key;wchar_t command[1200];if(!startup_command(command,1200))return false;
 LONG result=RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,NULL,0,KEY_SET_VALUE,NULL,&key,NULL);
 if(result!=ERROR_SUCCESS){SetLastError((DWORD)result);return false;}
 if(enabled)result=RegSetValueExW(key,HS_RUN_NAME,0,REG_SZ,(BYTE *)command,(DWORD)((wcslen(command)+1)*sizeof(wchar_t)));
 else result=RegDeleteValueW(key,HS_RUN_NAME);
 RegCloseKey(key);if(result==ERROR_FILE_NOT_FOUND&&!enabled)return true;
 if(result!=ERROR_SUCCESS)SetLastError((DWORD)result);return result==ERROR_SUCCESS;
}
bool hs_led_preference(void){wchar_t path[1024];return !hs_paths(NULL,NULL,path,1024)||GetPrivateProfileIntW(L"controle",L"leds_apagados",1,path)!=0;}
bool hs_save_led_preference(bool off){wchar_t path[1024];return hs_paths(NULL,NULL,path,1024)&&WritePrivateProfileStringW(L"controle",L"leds_apagados",off?L"1":L"0",path);}
bool hs_process_present(const wchar_t *name){
 HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)return false;
 PROCESSENTRY32W entry={sizeof(entry)};bool found=false;
 if(Process32FirstW(snapshot,&entry))do{if(_wcsicmp(entry.szExeFile,name)==0){found=true;break;}}while(Process32NextW(snapshot,&entry));
 CloseHandle(snapshot);return found;
}
HICON hs_icon(void){
 const int n=32;BITMAPV5HEADER info={0};info.bV5Size=sizeof(info);info.bV5Width=n;info.bV5Height=-n;info.bV5Planes=1;info.bV5BitCount=32;info.bV5Compression=BI_BITFIELDS;
 info.bV5RedMask=0x00ff0000;info.bV5GreenMask=0x0000ff00;info.bV5BlueMask=0x000000ff;info.bV5AlphaMask=0xff000000;
 HDC dc=CreateCompatibleDC(NULL);void *bits=NULL;HBITMAP color=CreateDIBSection(dc,(BITMAPINFO *)&info,DIB_RGB_COLORS,&bits,NULL,0);
 HBITMAP mask=CreateBitmap(n,n,1,1,NULL);if(!dc||!color||!mask){if(dc)DeleteDC(dc);if(color)DeleteObject(color);if(mask)DeleteObject(mask);return LoadIconW(NULL,IDI_APPLICATION);}
 HGDIOBJ old=SelectObject(dc,color);memset(bits,0,n*n*4);
 HPEN pen=CreatePen(PS_SOLID,3,RGB(85,180,140));HGDIOBJ oldPen=SelectObject(dc,pen),oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));
 Arc(dc,5,4,27,26,5,16,27,16);RoundRect(dc,4,14,10,27,3,3);RoundRect(dc,22,14,28,27,3,3);
 SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);
 uint32_t *pixels=bits;for(int i=0;i<n*n;i++)if(pixels[i]&0xffffff)pixels[i]|=0xff000000;
 SelectObject(dc,old);ICONINFO icon={TRUE,0,0,mask,color};HICON result=CreateIconIndirect(&icon);
 DeleteObject(mask);DeleteObject(color);DeleteDC(dc);return result;
}
void hs_status_json(const hs_state *s){
 wchar_t path[1024];if(!hs_paths(NULL,NULL,path,1024))return;
 wchar_t *slash=wcsrchr(path,L'\\');if(!slash)return;wcscpy_s(slash+1,1024-(size_t)(slash+1-path),L"estado-nativo.json");
 FILE *file=NULL;if(_wfopen_s(&file,path,L"wb")!=0||!file)return;
 fprintf(file,"{\"version\":2,\"pid\":%lu,\"status\":%ld,\"ledsOff\":%ld,\"micHardware\":%ld,\"micWindows\":%ld,\"battery\":%ld,\"charging\":%ld,\"updated\":%llu,\"error\":%lu}\n",s->pid,s->status,s->leds_off,s->mic_hardware,s->mic_windows,s->battery,s->charging,(unsigned long long)s->updated,s->error);fclose(file);
}
