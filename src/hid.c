#include "hid.h"
#include "protocol.h"
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <string.h>
#include <wchar.h>
static bool transfer(hs_hid *d,BYTE packet[64],bool write){
 OVERLAPPED op={0};op.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);if(!op.hEvent)return false;
 DWORD done=0;BOOL ok=write?WriteFile(d->handle,packet,64,&done,&op):ReadFile(d->handle,packet,64,&done,&op);
 DWORD error=ok?ERROR_SUCCESS:GetLastError();
 if(!ok&&error==ERROR_IO_PENDING){
  HANDLE events[2]={op.hEvent,d->stop};DWORD wait=WaitForMultipleObjects(d->stop?2:1,events,FALSE,900);
  if(wait==WAIT_OBJECT_0)ok=GetOverlappedResult(d->handle,&op,&done,FALSE);
  else {CancelIoEx(d->handle,&op);GetOverlappedResult(d->handle,&op,&done,TRUE);error=(wait==WAIT_OBJECT_0+1)?ERROR_CANCELLED:ERROR_TIMEOUT;ok=FALSE;}
  if(!ok&&error==ERROR_IO_PENDING)error=GetLastError();
 }
 CloseHandle(op.hEvent);
 if(!ok){SetLastError(error);return false;}
 if(done<6){SetLastError(ERROR_INVALID_DATA);return false;}return true;
}
static bool command(hs_hid *d,BYTE packet[64],BYTE child,BYTE cmd,uint16_t *value){
 ULONGLONG now=GetTickCount64();if(now-d->last_write<50){DWORD delay=(DWORD)(50-(now-d->last_write));if(d->stop&&WaitForSingleObject(d->stop,delay)==WAIT_OBJECT_0){SetLastError(ERROR_CANCELLED);return false;}if(!d->stop)Sleep(delay);}
 if(!transfer(d,packet,true))return false;d->last_write=GetTickCount64();
 for(int i=0;i<6;i++){BYTE reply[64]={0};if(!transfer(d,reply,false))return false;int result=hs_parse_reply(reply,64,child,cmd,value);if(result>0)return true;if(result<0){SetLastError((DWORD)(0x2000-result));return false;}}
 SetLastError(ERROR_INVALID_DATA);return false;
}
bool hs_hid_get(hs_hid *d,uint8_t child,uint8_t property,uint16_t *value){BYTE packet[64];hs_packet_get(packet,child,property);return command(d,packet,child,2,value);}
static bool set(hs_hid *d,BYTE property,uint16_t value){BYTE packet[64];hs_packet_set(packet,property,value);return command(d,packet,1,1,NULL);}
void hs_hid_close(hs_hid *d){if(d->handle&&d->handle!=INVALID_HANDLE_VALUE){CancelIoEx(d->handle,NULL);CloseHandle(d->handle);}d->handle=NULL;}
bool hs_hid_open(hs_hid *d,HANDLE stop){
 memset(d,0,sizeof(*d));d->stop=stop;GUID guid;HidD_GetHidGuid(&guid);
 HDEVINFO list=SetupDiGetClassDevsW(&guid,NULL,NULL,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);if(list==INVALID_HANDLE_VALUE)return false;
 wchar_t selected[1024]={0};bool ambiguous=false;
 for(DWORD i=0;;i++){
  SP_DEVICE_INTERFACE_DATA iface={sizeof(iface)};
  if(!SetupDiEnumDeviceInterfaces(list,NULL,&guid,i,&iface))break;
  DWORD required=0;SetupDiGetDeviceInterfaceDetailW(list,&iface,NULL,0,&required,NULL);
  if(required<sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)||required>32768)continue;
  PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,required);
  if(!detail)continue;detail->cbSize=sizeof(*detail);
  if(SetupDiGetDeviceInterfaceDetailW(list,&iface,detail,required,NULL,NULL)){
   HANDLE h=CreateFileW(detail->DevicePath,0,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
   if(h!=INVALID_HANDLE_VALUE){
    HIDD_ATTRIBUTES attributes={sizeof(attributes)};PHIDP_PREPARSED_DATA data=NULL;HIDP_CAPS caps;
    if(HidD_GetAttributes(h,&attributes)&&attributes.VendorID==0x1b1c&&attributes.ProductID==0x0a97&&HidD_GetPreparsedData(h,&data)){
     if(HidP_GetCaps(data,&caps)==HIDP_STATUS_SUCCESS&&caps.UsagePage==0xff42&&caps.Usage==1&&caps.InputReportByteLength==64&&caps.OutputReportByteLength==64){
      if(selected[0])ambiguous=true;else wcsncpy_s(selected,1024,detail->DevicePath,_TRUNCATE);
     }HidD_FreePreparsedData(data);
    }CloseHandle(h);
   }
  }HeapFree(GetProcessHeap(),0,detail);
 }
 SetupDiDestroyDeviceInfoList(list);
 if(!selected[0]||ambiguous){SetLastError(ambiguous?ERROR_DUP_NAME:ERROR_DEVICE_NOT_CONNECTED);return false;}
 d->handle=CreateFileW(selected,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,NULL);
 if(d->handle==INVALID_HANDLE_VALUE){d->handle=NULL;return false;}
 uint16_t receiver=0,headset=0;
 if(!hs_hid_get(d,0,0x12,&receiver)||!hs_hid_get(d,1,0x12,&headset)||!hs_model_valid(receiver,headset)){DWORD error=GetLastError();hs_hid_close(d);SetLastError(error?error:ERROR_BAD_DEVICE);return false;}
 return true;
}
bool hs_hid_hardware(hs_hid *d){return set(d,3,1);}
bool hs_hid_black(hs_hid *d){
 BYTE packet[64]={0};bool opened=false;
 if(!set(d,3,2))return false;
 packet[0]=2;packet[1]=9;packet[2]=0x0d;packet[4]=0x22;
 if(!command(d,packet,1,0x0d,NULL))goto failed;opened=true;
 hs_packet_black(packet);if(!command(d,packet,1,6,NULL))goto failed;
 memset(packet,0,64);packet[0]=2;packet[1]=9;packet[2]=5;
 if(!command(d,packet,1,5,NULL))goto failed;opened=false;
 return set(d,2,0);
failed:
 {DWORD error=GetLastError();if(opened){memset(packet,0,64);packet[0]=2;packet[1]=9;packet[2]=5;command(d,packet,1,5,NULL);}SetLastError(error);return false;}
}
