#define COBJMACROS
#include "hs80.h"
#include <sddl.h>
#include <stdlib.h>
#include <taskschd.h>
#include <wchar.h>

static const wchar_t run_key[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t run_name[]=L"HS80 LEDs Leve";
static const wchar_t approved_key[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";

typedef struct {
 ITaskFolder *folder;
 BSTR name;
 LPWSTR sid;
 bool com_initialized;
} hs_startup_task;

static HRESULT open_startup(hs_startup_task *startup){
 HRESULT result=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
 if(FAILED(result)&&result!=RPC_E_CHANGED_MODE)return result;
 startup->com_initialized=SUCCEEDED(result);

 HANDLE token=NULL;
 struct { TOKEN_USER user; BYTE sid[SECURITY_MAX_SID_SIZE]; } identity;
 DWORD size=0;
 if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return HRESULT_FROM_WIN32(GetLastError());
 BOOL read=GetTokenInformation(token,TokenUser,&identity,sizeof(identity),&size);
 DWORD error=read?ERROR_SUCCESS:GetLastError();
 CloseHandle(token);
 if(!read)return HRESULT_FROM_WIN32(error);
 if(!ConvertSidToStringSidW(identity.user.User.Sid,&startup->sid))return HRESULT_FROM_WIN32(GetLastError());

 wchar_t name[256];
 if(swprintf_s(name,_countof(name),L"HS80 MAX - %ls",startup->sid)<0)return E_INVALIDARG;
 startup->name=SysAllocString(name);
 if(!startup->name)return E_OUTOFMEMORY;

 ITaskService *service=NULL;
 result=CoCreateInstance(&CLSID_TaskScheduler,NULL,CLSCTX_INPROC_SERVER,&IID_ITaskService,(void **)&service);
 if(FAILED(result))return result;
 VARIANT empty={0};
 result=ITaskService_Connect(service,empty,empty,empty,empty);
 if(SUCCEEDED(result)){
  BSTR root=SysAllocString(L"\\");
  result=root?ITaskService_GetFolder(service,root,&startup->folder):E_OUTOFMEMORY;
  SysFreeString(root);
 }
 ITaskService_Release(service);
 return result;
}

static void close_startup(hs_startup_task *startup){
 if(startup->folder)ITaskFolder_Release(startup->folder);
 SysFreeString(startup->name);
 LocalFree(startup->sid);
 if(startup->com_initialized)CoUninitialize();
}

static HRESULT find_startup(hs_startup_task *startup,const wchar_t *path,IRegisteredTask **matched){
 IRegisteredTask *task=NULL;
 ITaskDefinition *definition=NULL;
 IActionCollection *actions=NULL;
 IAction *action=NULL;
 IExecAction *exec=NULL;
 BSTR program=NULL,arguments=NULL;
 LONG count=0;

 HRESULT result=ITaskFolder_GetTask(startup->folder,startup->name,&task);
 if(result==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))return S_FALSE;
 if(FAILED(result))return result;
 if(FAILED(result=IRegisteredTask_get_Definition(task,&definition)))goto done;
 if(FAILED(result=ITaskDefinition_get_Actions(definition,&actions)))goto done;
 if(FAILED(result=IActionCollection_get_Count(actions,&count)))goto done;
 if(count!=1){result=S_FALSE;goto done;}
 if(FAILED(result=IActionCollection_get_Item(actions,1,&action)))goto done;
 result=IAction_QueryInterface(action,&IID_IExecAction,(void **)&exec);
 if(result==E_NOINTERFACE){result=S_FALSE;goto done;}
 if(FAILED(result))goto done;
 if(FAILED(result=IExecAction_get_Path(exec,&program)))goto done;
 if(FAILED(result=IExecAction_get_Arguments(exec,&arguments)))goto done;
 if(program&&arguments&&_wcsicmp(program,path)==0&&wcscmp(arguments,L"--run")==0){
  *matched=task;
  task=NULL;
  result=S_OK;
 }else result=S_FALSE;
done:
 SysFreeString(arguments);
 SysFreeString(program);
 if(exec)IExecAction_Release(exec);
 if(action)IAction_Release(action);
 if(actions)IActionCollection_Release(actions);
 if(definition)ITaskDefinition_Release(definition);
 if(task)IRegisteredTask_Release(task);
 return result;
}

static BSTR startup_xml(const wchar_t *path,const wchar_t *sid){
 static const wchar_t format[]=
  L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
  L"<RegistrationInfo><Description>Inicia o HS80 MAX em segundo plano ao entrar na conta.</Description></RegistrationInfo>"
  L"<Triggers><LogonTrigger><UserId>%ls</UserId><Delay>PT15S</Delay></LogonTrigger></Triggers>"
  L"<Principals><Principal id=\"HS80\"><UserId>%ls</UserId><LogonType>InteractiveToken</LogonType>"
  L"<RunLevel>LeastPrivilege</RunLevel></Principal></Principals>"
  L"<Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>"
  L"<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>"
  L"<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>"
  L"<ExecutionTimeLimit>PT0S</ExecutionTimeLimit></Settings>"
  L"<Actions Context=\"HS80\"><Exec><Command>%ls</Command><Arguments>--run</Arguments></Exec></Actions>"
  L"</Task>";

 // Only text inside Command needs escaping. A SID contains no XML metacharacters.
 wchar_t escaped[5*1024],*next=escaped;
 for(const wchar_t *ch=path;*ch;ch++){
  const wchar_t *entity=NULL;
  switch(*ch){case L'&':entity=L"&amp;";break;case L'<':entity=L"&lt;";break;case L'>':entity=L"&gt;";break;}
  if(entity){size_t length=wcslen(entity);wmemcpy(next,entity,length);next+=length;}
  else *next++=*ch;
 }
 *next=0;
 int length=_scwprintf(format,sid,sid,escaped);
 if(length<0)return NULL;
 BSTR xml=SysAllocStringLen(NULL,(UINT)length);
 if(xml&&swprintf_s(xml,(size_t)length+1,format,sid,sid,escaped)<0){SysFreeString(xml);return NULL;}
 return xml;
}

static HRESULT register_startup(hs_startup_task *startup,const wchar_t *path){
 BSTR xml=startup_xml(path,startup->sid);
 VARIANT user={0},empty={0};
 V_VT(&user)=VT_BSTR;
 V_BSTR(&user)=SysAllocString(startup->sid);
 IRegisteredTask *task=NULL;
 HRESULT result=E_OUTOFMEMORY;
 if(xml&&V_BSTR(&user))
  result=ITaskFolder_RegisterTask(startup->folder,startup->name,xml,TASK_CREATE_OR_UPDATE,user,empty,TASK_LOGON_INTERACTIVE_TOKEN,empty,&task);
 if(task)IRegisteredTask_Release(task);
 VariantClear(&user);
 SysFreeString(xml);
 return result;
}

static bool legacy_matches(const wchar_t *path){
 wchar_t actual[1200],expected[1200];
 DWORD size=sizeof(actual);
 if(RegGetValueW(HKEY_CURRENT_USER,run_key,run_name,RRF_RT_REG_SZ,NULL,actual,&size)!=ERROR_SUCCESS)return false;
 if(swprintf_s(expected,_countof(expected),L"\"%ls\" --run",path)<0)return false;
 if(_wcsicmp(expected,actual)==0)return true;
 const wchar_t *name=wcsrchr(path,L'\\');
 if(!name)return false;
 return swprintf_s(expected,_countof(expected),L"\"%.*lsHS80-Leds.exe\" --run",(int)(name-path+1),path)>=0&&
        _wcsicmp(expected,actual)==0;
}

static HRESULT remove_legacy(const wchar_t *path){
 if(!legacy_matches(path))return S_OK;
 LONG result=RegDeleteKeyValueW(HKEY_CURRENT_USER,run_key,run_name);
 return result==ERROR_FILE_NOT_FOUND?S_OK:HRESULT_FROM_WIN32(result);
}

bool hs_autostart_enabled(void){
 wchar_t path[1024];
 if(!hs_paths(path,NULL,NULL,_countof(path)))return false;
 hs_startup_task startup={0};
 IRegisteredTask *task=NULL;
 VARIANT_BOOL enabled=VARIANT_FALSE;
 if(SUCCEEDED(open_startup(&startup))&&find_startup(&startup,path,&task)==S_OK){
  IRegisteredTask_get_Enabled(task,&enabled);
  IRegisteredTask_Release(task);
 }
 close_startup(&startup);
 return enabled==VARIANT_TRUE;
}

bool hs_set_autostart(bool enabled){
 wchar_t path[1024];
 if(!hs_paths(path,NULL,NULL,_countof(path)))return false;
 hs_startup_task startup={0};
 HRESULT result=open_startup(&startup);
 if(SUCCEEDED(result)&&enabled){
  result=register_startup(&startup,path);
  if(SUCCEEDED(result))result=remove_legacy(path);
 }else if(SUCCEEDED(result)){
  IRegisteredTask *task=NULL;
  result=find_startup(&startup,path,&task);
  if(SUCCEEDED(result))result=remove_legacy(path);
  if(task){
   IRegisteredTask_Release(task);
   if(SUCCEEDED(result))result=ITaskFolder_DeleteTask(startup.folder,startup.name,0);
   if(result==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))result=S_OK;
  }
 }
 close_startup(&startup);
 if(FAILED(result))SetLastError((DWORD)result);
 return SUCCEEDED(result);
}

bool hs_migrate_autostart(void){
 wchar_t path[1024];
 if(!hs_paths(path,NULL,NULL,_countof(path)))return false;
 if(!legacy_matches(path))return true;
 DWORD approval[3],size=sizeof(approval);
 LONG result=RegGetValueW(HKEY_CURRENT_USER,approved_key,run_name,RRF_RT_REG_BINARY,NULL,approval,&size);
 // Migrate only enabled entries. Leave unrecognized approval values for Windows to manage.
 if(result!=ERROR_FILE_NOT_FOUND&&
    (result!=ERROR_SUCCESS||size!=sizeof(approval)||(approval[0]!=2&&approval[0]!=6)))return true;
 return hs_set_autostart(true);
}
