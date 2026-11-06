#include "protocol.h"
#include <string.h>
void hs_packet_get(uint8_t out[64],uint8_t child,uint8_t property){memset(out,0,64);out[0]=2;out[1]=(uint8_t)(8|child);out[2]=2;out[3]=property;}
void hs_packet_set(uint8_t out[64],uint8_t property,uint16_t value){memset(out,0,64);out[0]=2;out[1]=9;out[2]=1;out[3]=property;out[5]=(uint8_t)value;out[6]=(uint8_t)(value>>8);}
void hs_packet_black(uint8_t out[64]){memset(out,0,64);out[0]=2;out[1]=9;out[2]=6;out[4]=8;out[8]=0x12;}
int hs_parse_reply(const uint8_t *b,size_t n,uint8_t child,uint8_t command,uint16_t *value){
 if(n<6||b[0]!=1||b[1]!=child||b[2]!=command)return 0;
 if(b[3])return -(int)b[3];
 if(value)*value=(uint16_t)(b[4]|((uint16_t)b[5]<<8));return 1;
}
bool hs_model_valid(uint16_t receiver,uint16_t headset){return receiver==0x0a97&&headset==0x0a96;}
int hs_battery_percent(uint16_t raw){return raw<=1000?(int)(raw/10):-1;}
