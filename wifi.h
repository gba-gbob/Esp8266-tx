#include <ESP8266WiFi.h>
extern "C" {
  #include "user_interface.h"
}
#define RANDOM_REG32  ESP8266_DREG(0x20E44)

uint8_t probe_packet[54] = {
0x50, 0x88,                         //Type/Subtype: Probe Response
0x00, 0x00,                         //Duration - will be overridden
0x01, 0x02, 0x03, 0x04, 0x05, 0x06, //Receiver/Destination address
0xaa, 0x91, 0x8f, 0x13, 0x3c, 0xc9, //Transmitter/Source address
0xaa, 0x91, 0x8f, 0x13, 0x3c, 0xc9, //BSS Id
0x00, 0x00,                         //Fragment/Sequence
0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, //Timestamp - will be overridden
//managment frame fixed and taged params use for data
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

typedef struct {
  uint8_t type[2];
  uint8_t duration[2];
  uint8_t receiver[6];
  uint8_t transmitter[6];
  uint8_t bss[6];
  uint8_t sequence[2];
  uint8_t timestampt[8];
  uint8_t data[22];
} Packet;

typedef struct {
  signed rssi: 8;
  unsigned rate: 4;
  unsigned is_group: 1;
  unsigned: 1;
  unsigned sig_mode: 2;
  unsigned legacy_length: 12;
  unsigned damatch0: 1;
  unsigned damatch1: 1;
  unsigned bssidmatch0: 1;
  unsigned bssidmatch1: 1;
  unsigned MCS: 7;
  unsigned CWB: 1;
  unsigned HT_length: 16;
  unsigned Smoothing: 1;
  unsigned Not_Sounding: 1;
  unsigned: 1;
  unsigned Aggregation: 1;
  unsigned STBC: 2;
  unsigned FEC_CODING: 1;
  unsigned SGI: 1;
  unsigned rxend_state: 8;
  unsigned ampdu_cnt: 8;
  unsigned channel: 4;
  unsigned: 12;
 } RxControl;

struct Frame {
  RxControl rx_ctrl;
  Packet packet;
};

uint8_t bind_address[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
uint8_t bind_channel = 3;
uint8_t tx_address[6] = {0x16, 0xC2, 0xD4, 0xFB, 0xF6, 0x4C};
uint8_t rx_address[6] = {0x76, 0x3D, 0xDF, 0xF3, 0xAF, 0x29};
uint8_t channel;
uint32_t packetCount = 0;
uint8_t enablePacketCount=1;
uint8_t binding = 1;
int8_t rssi = -127;

void promisc_cb(uint8_t *buf, uint16_t len);
uint8_t findEmptyChannel();
void generateAddress(uint8_t *address);
void printPacket(Packet packet);


Packet wifiInitBind(Packet packet){
  wifi_set_opmode(STATION_MODE);
  wifi_promiscuous_enable(0);
  wifi_set_promiscuous_rx_cb(promisc_cb);
  wifi_promiscuous_enable(1);
  system_phy_set_max_tpw(82);
  wifi_set_channel(bind_channel);
  //TODO write to EEPROM on initial startup instead of hardcoding
  //generateAddress(rx_address);
  //TODO think how to handle tx swich off and reneogaotiate chanel on startup
  //channel = findEmptyChannel();
  channel = bind_channel;

  memcpy( &packet, probe_packet, sizeof(probe_packet));
  memcpy( &packet.transmitter, tx_address, 6);
  memcpy( &packet.receiver, bind_address, 6);
  generateAddress(packet.bss);
  for (uint8_t i = 0; i < 6; i++ ) {
    packet.data[i] = rx_address[i];
  }
  packet.data[6] = channel;
  #ifdef DEBUG_WIRELESS
    printPacket(packet);
  #endif
  return packet;
}

Packet wifiInitData(Packet packet){
  enablePacketCount = 0;
  wifi_set_channel(channel);
  memcpy( &packet.receiver, rx_address, 6);
  #ifdef DEBUG_WIRELESS
    printPacket(packet);
  #endif
  return packet;
}

void promisc_cb(uint8_t *buf, uint16_t len) {
  if(enablePacketCount){
    packetCount++;
  }
  if (buf[12] == 0x50 && buf[13] == 0x88) { //discard if not Type/Subtype: Probe Response
    struct Frame *frame = (struct Frame*) buf;
    for (uint8_t i = 0; i < 6; i++ ) { //discard if does not match expected address
      if ( tx_address [i] != frame->packet.receiver[i] ) {
        return;
      }
    }
    if(binding) {
      binding = 0; //stop binding as soon as first packet is recieved
      #ifdef DEBUG_WIRELESS
        Serial.println("Recieved reply from rx bnding complete");
      #endif
    }
    rssi = frame->packet.data[0];
  }
}

uint8_t findEmptyChannel(){
  uint16_t packets[14];
  for (uint8_t i = 1; i < 14; i++) {
    wifi_set_channel(i);
    packetCount = 0;
    delay(300);
    packets[i] = packetCount;
    #ifdef DEBUG_WIRELESS
      Serial.print("Channel:"); Serial.print(i); Serial.print(" packets:"); Serial.println( packets[i]);
    #endif
  }
  uint16_t previous = packets[1];
  uint8_t channel = 1;
  for (uint8_t i = 2; i < 14; i++) {
    if(packets[i] < previous){
        channel = i;
        previous = packets[i];
    }
  }
    #ifdef DEBUG_WIRELESS
      Serial.print("Selected least busy wifi channel: "); Serial.println(channel);
    #endif
  return channel;

}

void generateAddress(uint8_t *address){
 #ifdef DEBUG_WIRELESS
    Serial.print("Generating random address: ");
  #endif
  for (uint8_t i = 0; i < 6; i++) {
    address[i] = RANDOM_REG32 % 256;
    #ifdef DEBUG_WIRELESS
      Serial.print(address[i], HEX); Serial.print("_");
    #endif
  }
  #ifdef DEBUG_WIRELESS
    Serial.println();
  #endif
}

void wifiSendPacket(Packet packet){
    wifi_send_pkt_freedom((uint8_t*) &packet, 54 , 0);
}

//not implemented
float remoteBatteryVoltage(){
    return 0;
}

int8_t remoteRssi(){
    return rssi;
}

#ifdef DEBUG_WIRELESS
void printPacket(Packet packet){
    uint8_t i = 0;
    for(i=0; i < 2; i++){
        Serial.print(packet.type[i], HEX);  Serial.print("_");
    }
    Serial.println();
    for(i=0; i < 2; i++){
        Serial.print(packet.duration[i], HEX);  Serial.print("_");
    }
    Serial.println();
    for(i=0; i < 6; i++){
        Serial.print(packet.receiver[i], HEX);  Serial.print("_");
    }
    Serial.println();
    for(i=0; i < 6; i++){
        Serial.print(packet.transmitter[i], HEX);  Serial.print("_");
    }
    Serial.println();
    for(i=0; i < 6; i++){
        Serial.print(packet.bss[i], HEX);  Serial.print("_");
    }
    Serial.println();
    for(i=0; i < 2; i++){
        Serial.print(packet.sequence[i], HEX);  Serial.print("_");
    }
    Serial.println();
    for(i=0; i < 8; i++){
        Serial.print(packet.timestampt[i], HEX);  Serial.print("_");
    }
    Serial.println();
    for(i=0; i < 22; i++){
        Serial.print(packet.data[i], HEX);  Serial.print("_");
    }
    Serial.println();
}
#endif