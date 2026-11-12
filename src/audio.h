#ifndef HS80_AUDIO_H
#define HS80_AUDIO_H
#define COBJMACROS
#include "hs80.h"
#include <mmdeviceapi.h>
#include <endpointvolume.h>
typedef struct hs_audio {IMMDeviceEnumerator *enumerator;IMMDevice *device;IAudioEndpointVolume *volume;} hs_audio;
bool hs_audio_open(hs_audio *audio);
void hs_audio_close(hs_audio *audio);
bool hs_audio_set(hs_audio *audio,bool muted);
bool hs_audio_get(hs_audio *audio,bool *muted);
bool hs_audio_chime(HWND window,int sound);
void hs_audio_chime_timer(HWND window,UINT_PTR timer);
void hs_audio_chime_stop(HWND window);
#endif
