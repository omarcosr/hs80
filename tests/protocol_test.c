#include "protocol.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){
 uint8_t packet[64],expected[64]={2,9,6,0,8,0,0,0,0x12,0};hs_packet_black(packet);CHECK(memcmp(packet,expected,64)==0);
 hs_packet_get(packet,1,0xa6);CHECK(packet[0]==2&&packet[1]==9&&packet[2]==2&&packet[3]==0xa6&&packet[4]==0);
 hs_packet_set(packet,2,1000);CHECK(packet[5]==0xe8&&packet[6]==3&&packet[4]==0);
 uint8_t response[64]={1,1,2,0,0x96,0x0a};uint16_t value=0;
 CHECK(hs_parse_reply(response,64,1,2,&value)==1&&value==0x0a96);
 CHECK(hs_parse_reply(response,3,1,2,&value)==0);
 response[0]=3;CHECK(hs_parse_reply(response,64,1,2,&value)==0);
 response[0]=1;response[1]=0;CHECK(hs_parse_reply(response,64,1,2,&value)==0);
 response[1]=1;response[2]=1;CHECK(hs_parse_reply(response,64,1,2,&value)==0);
 response[2]=2;response[3]=3;CHECK(hs_parse_reply(response,64,1,2,&value)==-3);
 CHECK(hs_model_valid(0x0a97,0x0a96));CHECK(!hs_model_valid(0x0a97,0x0a98));CHECK(!hs_model_valid(0x0a96,0x0a97));
 CHECK(hs_battery_percent(950)==95);CHECK(hs_battery_percent(1000)==100);CHECK(hs_battery_percent(0)==0);CHECK(hs_battery_percent(2710)==-1);CHECK(hs_battery_percent(65535)==-1);
 puts("PASS: frames, routing, unsolicited reports, device errors, exact model guard, battery scale and bounds");return 0;
}
