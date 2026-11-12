#ifndef HS80_HID_H
#define HS80_HID_H
#include "hs80.h"
enum { HID_PATH_CAPACITY=1024 };
typedef struct {
 uint16_t vendor,product,alternate_product;
 uint16_t usage_page,usage,input_bytes,output_bytes,feature_bytes;
} hid_collection;
uint16_t hid_find_collection(const hid_collection *match,wchar_t path[HID_PATH_CAPACITY]);
typedef struct hs_hid {HANDLE handle,stop,io_event;ULONGLONG last_write;} hs_hid;
bool hs_hid_open(hs_hid *device,HANDLE stop);
void hs_hid_close(hs_hid *device);
bool hs_hid_get(hs_hid *device,uint8_t child,uint8_t property,uint16_t *value);
bool hs_hid_black(hs_hid *device);
bool hs_hid_hardware(hs_hid *device);
#endif
