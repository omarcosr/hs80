#ifndef HS80_PROTOCOL_H
#define HS80_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
void hs_packet_get(uint8_t out[64],uint8_t child,uint8_t property);
void hs_packet_set(uint8_t out[64],uint8_t property,uint16_t value);
void hs_packet_black(uint8_t out[64]);
int hs_parse_reply(const uint8_t *bytes,size_t size,uint8_t child,uint8_t command,uint16_t *value);
bool hs_model_valid(uint16_t receiver,uint16_t headset);
int hs_battery_percent(uint16_t raw);
#endif
