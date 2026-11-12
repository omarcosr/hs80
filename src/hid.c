#include "hid.h"
#include "protocol.h"
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <string.h>
#include <wchar.h>
static bool transfer(hs_hid *d,BYTE packet[64],bool write){
 OVERLAPPED op={0};op.hEvent=d->io_event;if(!op.hEvent||!ResetEvent(op.hEvent))return false;
 DWORD done=0;BOOL ok=write?WriteFile(d->handle,packet,64,&done,&op):ReadFile(d->handle,packet,64,&done,&op);
 DWORD error=ok?ERROR_SUCCESS:GetLastError();
 if(!ok&&error==ERROR_IO_PENDING){
  HANDLE events[2]={op.hEvent,d->stop};DWORD wait=WaitForMultipleObjects(d->stop?2:1,events,FALSE,900);
  if(wait==WAIT_OBJECT_0)ok=GetOverlappedResult(d->handle,&op,&done,FALSE);
  else {CancelIoEx(d->handle,&op);GetOverlappedResult(d->handle,&op,&done,TRUE);error=(wait==WAIT_OBJECT_0+1)?ERROR_CANCELLED:ERROR_TIMEOUT;ok=FALSE;}
  if(!ok&&error==ERROR_IO_PENDING)error=GetLastError();
 }
 if(!ok){SetLastError(error);return false;}
 if((write&&done!=64)||done<6){SetLastError(ERROR_INVALID_DATA);return false;}return true;
}
static bool command(hs_hid *d,BYTE packet[64],BYTE child,BYTE cmd,uint16_t *value){
 ULONGLONG now=GetTickCount64();if(now-d->last_write<50){DWORD delay=(DWORD)(50-(now-d->last_write));if(d->stop&&WaitForSingleObject(d->stop,delay)==WAIT_OBJECT_0){SetLastError(ERROR_CANCELLED);return false;}if(!d->stop)Sleep(delay);}
 if(!transfer(d,packet,true))return false;d->last_write=GetTickCount64();
 for(int i=0;i<6;i++){BYTE reply[64]={0};if(!transfer(d,reply,false))return false;int result=hs_parse_reply(reply,64,child,cmd,value);if(result>0)return true;if(result<0){SetLastError((DWORD)(0x2000-result));return false;}}
 SetLastError(ERROR_INVALID_DATA);return false;
}
bool hs_hid_get(hs_hid *d,uint8_t child,uint8_t property,uint16_t *value){BYTE packet[64];hs_packet_get(packet,child,property);return command(d,packet,child,2,value);}
static bool set(hs_hid *d,BYTE property,uint16_t value){BYTE packet[64];hs_packet_set(packet,property,value);return command(d,packet,1,1,NULL);}
void hs_hid_close(hs_hid *d){if(d->handle&&d->handle!=INVALID_HANDLE_VALUE){CancelIoEx(d->handle,NULL);CloseHandle(d->handle);}d->handle=NULL;if(d->io_event)CloseHandle(d->io_event);d->io_event=NULL;}
uint16_t hid_find_collection(const hid_collection *match,wchar_t path[HID_PATH_CAPACITY]){
 GUID guid;HidD_GetHidGuid(&guid);
 HDEVINFO list=SetupDiGetClassDevsW(&guid,NULL,NULL,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
 if(list==INVALID_HANDLE_VALUE)return 0;
 uint16_t selected=0;bool ambiguous=false;
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
    if(HidD_GetAttributes(h,&attributes)&&attributes.VendorID==match->vendor&&
       (attributes.ProductID==match->product||(match->alternate_product&&attributes.ProductID==match->alternate_product&&selected!=match->product))&&
       HidD_GetPreparsedData(h,&data)){
     if(HidP_GetCaps(data,&caps)==HIDP_STATUS_SUCCESS&&caps.UsagePage==match->usage_page&&caps.Usage==match->usage&&
        (!match->input_bytes||caps.InputReportByteLength==match->input_bytes)&&
        (!match->output_bytes||caps.OutputReportByteLength==match->output_bytes)&&
        (!match->feature_bytes||caps.FeatureReportByteLength==match->feature_bytes)){
      if(selected==attributes.ProductID)ambiguous=true;
      else if(wcsncpy_s(path,HID_PATH_CAPACITY,detail->DevicePath,_TRUNCATE)==0){selected=attributes.ProductID;ambiguous=false;}
     }HidD_FreePreparsedData(data);
    }CloseHandle(h);
   }
  }HeapFree(GetProcessHeap(),0,detail);
 }
 DWORD error=GetLastError();SetupDiDestroyDeviceInfoList(list);
 if(error!=ERROR_NO_MORE_ITEMS){SetLastError(error);return 0;}
 if(!selected||ambiguous){SetLastError(ambiguous?ERROR_DUP_NAME:ERROR_DEVICE_NOT_CONNECTED);return 0;}
 return selected;
}
bool hs_hid_open(hs_hid *d,HANDLE stop){
 static const hid_collection match={.vendor=0x1b1c,.product=0x0a97,.usage_page=0xff42,.usage=1,.input_bytes=64,.output_bytes=64};
 memset(d,0,sizeof(*d));d->stop=stop;
 wchar_t path[HID_PATH_CAPACITY];if(!hid_find_collection(&match,path))return false;
 d->handle=CreateFileW(path,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,NULL);
 if(d->handle==INVALID_HANDLE_VALUE){d->handle=NULL;return false;}
 d->io_event=CreateEventW(NULL,TRUE,FALSE,NULL);
 if(!d->io_event){DWORD error=GetLastError();hs_hid_close(d);SetLastError(error);return false;}
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
