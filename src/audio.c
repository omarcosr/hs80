#include "audio.h"
#include <audioclient.h>
#include <math.h>
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

static HRESULT open_enumerator(IMMDeviceEnumerator **enumerator){
 return CoCreateInstance(&clsid_enumerator,NULL,CLSCTX_ALL,&iid_enumerator,(void **)enumerator);
}
static bool endpoint_property(IMMDevice *device,const PROPERTYKEY *key,PROPVARIANT *value){
 IPropertyStore *properties=NULL;
 PropVariantInit(value);
 if(FAILED(IMMDevice_OpenPropertyStore(device,STGM_READ,&properties)))return false;
 HRESULT result=IPropertyStore_GetValue(properties,key,value);
 IPropertyStore_Release(properties);
 return SUCCEEDED(result);
}
static bool is_hs80_endpoint(IMMDevice *device){
 PROPVARIANT name;
 bool matches=endpoint_property(device,&friendly_name,&name)&&name.vt==VT_LPWSTR&&name.pwszVal&&
              contains(name.pwszVal,L"CORSAIR")&&contains(name.pwszVal,L"HS80 MAX");
 PropVariantClear(&name);
 return matches;
}

void hs_audio_close(hs_audio *a){if(a->volume)IAudioEndpointVolume_Release(a->volume);if(a->device)IMMDevice_Release(a->device);if(a->enumerator)IMMDeviceEnumerator_Release(a->enumerator);memset(a,0,sizeof(*a));}
bool hs_audio_open(hs_audio *a){
 memset(a,0,sizeof(*a));HRESULT hr=open_enumerator(&a->enumerator);if(FAILED(hr)){SetLastError((DWORD)hr);return false;}
 IMMDeviceCollection *devices=NULL;hr=IMMDeviceEnumerator_EnumAudioEndpoints(a->enumerator,eCapture,DEVICE_STATE_ACTIVE,&devices);
 if(FAILED(hr))goto failed;UINT count=0;IMMDeviceCollection_GetCount(devices,&count);
 for(UINT i=0;i<count;i++){
  IMMDevice *device=NULL;
  if(SUCCEEDED(IMMDeviceCollection_Item(devices,i,&device))&&is_hs80_endpoint(device)){
   a->device=device;device=NULL;
  }
  if(device)IMMDevice_Release(device);
  if(a->device)break;
 }
 IMMDeviceCollection_Release(devices);devices=NULL;
 if(!a->device){hr=HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);goto failed;}
 hr=IMMDevice_Activate(a->device,&iid_volume,CLSCTX_ALL,NULL,(void **)&a->volume);if(FAILED(hr))goto failed;return true;
failed:
 if(devices)IMMDeviceCollection_Release(devices);hs_audio_close(a);SetLastError((DWORD)hr);return false;
}
bool hs_audio_set(hs_audio *a,bool muted){HRESULT hr=IAudioEndpointVolume_SetMute(a->volume,muted?TRUE:FALSE,NULL);if(FAILED(hr))SetLastError((DWORD)hr);return SUCCEEDED(hr);}
bool hs_audio_get(hs_audio *a,bool *muted){BOOL value=FALSE;HRESULT hr=IAudioEndpointVolume_GetMute(a->volume,&value);if(FAILED(hr)){SetLastError((DWORD)hr);return false;}*muted=value!=FALSE;return true;}

static const GUID iid_audio_client={0x1cb9ad4c,0xdbfa,0x4c32,{0xb1,0x78,0xc2,0xf5,0x68,0xa7,0x03,0xb2}};
static const GUID iid_render_client={0xf294acfc,0x3146,0x4483,{0xa7,0xbf,0xad,0xdc,0xa7,0xc2,0x60,0xe2}};

typedef struct {unsigned frequency,ms;} hs_note;
static const hs_note chimes[HS_SOUND_COUNT][3]={
 [HS_SOUND_PULSE]={{720,130}},
 [HS_SOUND_DOUBLE]={{650,90},{0,45},{650,115}},
 [HS_SOUND_RISE]={{520,105},{820,140}},
 [HS_SOUND_FALL]={{820,105},{520,150}},
};
enum {CHIME_RATE=48000,CHIME_CHANNELS=2};
static IAudioClient *chime_client;
static UINT_PTR chime_timer,next_chime_timer=10;

static void render_chime(const hs_note notes[3],int16_t *samples){
 for(size_t n=0;n<3;n++){
  unsigned frames=notes[n].ms*(CHIME_RATE/1000);
  double step=6.283185307179586*notes[n].frequency/CHIME_RATE;
  for(unsigned i=0;i<frames;i++){
   double attack=(double)i/(CHIME_RATE/125);
   double release=(double)(frames-i-1)/(CHIME_RATE/40);
   double envelope=fmin(1.0,fmin(attack,release));
   int16_t sample=(int16_t)(4000.0*envelope*sin(step*i));
   *samples++=sample;
   *samples++=sample;
  }
 }
}

void hs_audio_chime_stop(HWND window){
 if(chime_timer){
  KillTimer(window,chime_timer);
  chime_timer=0;
 }
 if(chime_client){
  IAudioClient_Stop(chime_client);
  IAudioClient_Release(chime_client);
  chime_client=NULL;
 }
}
void hs_audio_chime_timer(HWND window,UINT_PTR timer){
 if(timer==chime_timer)hs_audio_chime_stop(window);
}
bool hs_audio_chime(HWND window,int sound){
 if(!window||sound<=HS_SOUND_NONE||sound>=HS_SOUND_COUNT)return false;
 hs_audio_chime_stop(window);
 IMMDeviceEnumerator *enumerator=NULL;
 if(FAILED(open_enumerator(&enumerator)))return false;
 IMMDevice *output=NULL;
 HRESULT result=IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator,eRender,eMultimedia,&output);
 IMMDeviceEnumerator_Release(enumerator);
 if(FAILED(result))return false;

 IAudioClient *client=NULL;
 result=IMMDevice_Activate(output,&iid_audio_client,CLSCTX_ALL,NULL,(void **)&client);
 IMMDevice_Release(output);
 if(FAILED(result))return false;

 IAudioRenderClient *render=NULL;
 const hs_note *notes=chimes[sound];
 unsigned duration=notes[0].ms+notes[1].ms+notes[2].ms;
 UINT32 frames=duration*(CHIME_RATE/1000),capacity=0;
 WAVEFORMATEX format={
  WAVE_FORMAT_PCM,CHIME_CHANNELS,CHIME_RATE,CHIME_RATE*CHIME_CHANNELS*sizeof(int16_t),
  CHIME_CHANNELS*sizeof(int16_t),16,0
 };
 DWORD flags=AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY|
             AUDCLNT_STREAMFLAGS_NOPERSIST;
 bool playing=false;
 result=IAudioClient_Initialize(client,AUDCLNT_SHAREMODE_SHARED,flags,(REFERENCE_TIME)(duration+50)*10000,0,&format,NULL);
 if(FAILED(result))goto done;
 if(FAILED(IAudioClient_GetBufferSize(client,&capacity))||capacity<frames)goto done;
 if(FAILED(IAudioClient_GetService(client,&iid_render_client,(void **)&render)))goto done;
 BYTE *buffer=NULL;
 if(FAILED(IAudioRenderClient_GetBuffer(render,frames,&buffer)))goto done;
 render_chime(notes,(int16_t *)buffer);
 if(FAILED(IAudioRenderClient_ReleaseBuffer(render,frames,0)))goto done;

 REFERENCE_TIME latency=0;
 IAudioClient_GetStreamLatency(client,&latency);
 UINT allowance=(UINT)fmin(500.0,fmax(0.0,(double)latency/10000.0));
 if(FAILED(IAudioClient_Start(client)))goto done;
 chime_timer=SetTimer(window,++next_chime_timer,duration+allowance+60,NULL);
 if(!chime_timer){
  IAudioClient_Stop(client);
  goto done;
 }
 chime_client=client;
 playing=true;
done:
 if(render)IAudioRenderClient_Release(render);
 if(!playing)IAudioClient_Release(client);
 return playing;
}
