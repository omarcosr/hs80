#include "audio.h"
#include <propsys.h>
#include <propvarutil.h>
#include <wchar.h>
#include <wctype.h>
#include <string.h>
static const GUID clsid_enumerator={0xbcde0395,0xe52f,0x467c,{0x8e,0x3d,0xc4,0x57,0x92,0x91,0x69,0x2e}};
static const GUID iid_enumerator={0xa95664d2,0x9614,0x4f35,{0xa7,0x46,0xde,0x8d,0xb6,0x36,0x17,0xe6}};
static const GUID iid_volume={0x5cdf2c82,0x841e,0x4546,{0x97,0x22,0x0c,0xf7,0x40,0x78,0x22,0x9a}};
static const PROPERTYKEY friendly_name={{0xa45c254e,0xdf1c,0x4efd,{0x80,0x20,0x67,0xd1,0x46,0xa8,0x50,0xe0}},14};
static bool contains(const wchar_t *text,const wchar_t *needle){for(;*text;text++){const wchar_t *a=text,*b=needle;while(*a&&*b&&towupper(*a)==towupper(*b)){a++;b++;}if(!*b)return true;}return false;}
void hs_audio_close(hs_audio *a){if(a->volume)IAudioEndpointVolume_Release(a->volume);if(a->device)IMMDevice_Release(a->device);if(a->enumerator)IMMDeviceEnumerator_Release(a->enumerator);memset(a,0,sizeof(*a));}
bool hs_audio_open(hs_audio *a){
 memset(a,0,sizeof(*a));HRESULT hr=CoCreateInstance(&clsid_enumerator,NULL,CLSCTX_ALL,&iid_enumerator,(void **)&a->enumerator);if(FAILED(hr)){SetLastError((DWORD)hr);return false;}
 IMMDeviceCollection *devices=NULL;hr=IMMDeviceEnumerator_EnumAudioEndpoints(a->enumerator,eCapture,DEVICE_STATE_ACTIVE,&devices);
 if(FAILED(hr))goto failed;UINT count=0;IMMDeviceCollection_GetCount(devices,&count);
 for(UINT i=0;i<count;i++){
  IMMDevice *device=NULL;IPropertyStore *properties=NULL;PROPVARIANT value;PropVariantInit(&value);
  if(SUCCEEDED(IMMDeviceCollection_Item(devices,i,&device))&&SUCCEEDED(IMMDevice_OpenPropertyStore(device,STGM_READ,&properties))&&SUCCEEDED(IPropertyStore_GetValue(properties,&friendly_name,&value))&&value.vt==VT_LPWSTR&&value.pwszVal&&contains(value.pwszVal,L"CORSAIR")&&contains(value.pwszVal,L"HS80 MAX")){
   a->device=device;device=NULL;
  }
  PropVariantClear(&value);if(properties)IPropertyStore_Release(properties);if(device)IMMDevice_Release(device);if(a->device)break;
 }
 IMMDeviceCollection_Release(devices);devices=NULL;
 if(!a->device){hr=HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);goto failed;}
 hr=IMMDevice_Activate(a->device,&iid_volume,CLSCTX_ALL,NULL,(void **)&a->volume);if(FAILED(hr))goto failed;return true;
failed:
 if(devices)IMMDeviceCollection_Release(devices);hs_audio_close(a);SetLastError((DWORD)hr);return false;
}
bool hs_audio_set(hs_audio *a,bool muted){HRESULT hr=IAudioEndpointVolume_SetMute(a->volume,muted?TRUE:FALSE,NULL);if(FAILED(hr))SetLastError((DWORD)hr);return SUCCEEDED(hr);}
bool hs_audio_get(hs_audio *a,bool *muted){BOOL value=FALSE;HRESULT hr=IAudioEndpointVolume_GetMute(a->volume,&value);if(FAILED(hr)){SetLastError((DWORD)hr);return false;}*muted=value!=FALSE;return true;}
