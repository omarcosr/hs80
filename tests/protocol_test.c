#include "protocol.h"
#include "viper_protocol.h"
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
 uint8_t mouse[VIPER_REPORT_SIZE];
 viper_report(mouse,0x07,0x80,2,NULL);
 CHECK(mouse[0]==0&&mouse[2]==0x1f&&mouse[6]==2&&mouse[7]==0x07&&mouse[8]==0x80&&mouse[89]==0x85);
 uint8_t dpi[VIPER_REPORT_SIZE]={[1]=2,[2]=0x1f,[6]=7,[7]=4,[8]=0x85,[9]=1,[10]=6,[11]=64,[12]=6,[13]=64,[89]=0x87};
 int x=0,y=0;
 CHECK(viper_reply_status(dpi,4,0x85)==VIPER_OK&&viper_parse_dpi(dpi,&x,&y)&&x==1600&&y==1600);
 CHECK(viper_reply_status(dpi,0,0x85)==VIPER_INVALID_REPLY);
 dpi[11]^=1;CHECK(viper_reply_status(dpi,4,0x85)==VIPER_INVALID_REPLY);
 CHECK(viper_battery_percent(90)==35&&viper_battery_percent(255)==100);
 CHECK(!viper_valid_dpi(99)&&viper_valid_dpi(35000)&&!viper_valid_dpi(35001));
 CHECK(viper_polling_hz(true,0x08)==1000&&viper_polling_hz(false,0x08)==125);
 CHECK(viper_polling_code(true,8000)==1&&viper_polling_code(false,8000)==0);
 CHECK(viper_polling_code(false,500)==2&&viper_polling_hz(true,3)==0);
 puts("PASS: HS80 and Viper reports, validation, battery and polling");return 0;
}
