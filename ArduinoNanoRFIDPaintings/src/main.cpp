#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_PN532.h>
#include <Ethernet.h>
#include <PubSubClient.h>

byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x10};
byte ip[] = {111, 111, 111, 111}; 
byte ipServer[] = {111, 111, 111, 111};
EthernetClient net;
PubSubClient client(net);

// SCK, MISO, MOSI, SS
Adafruit_PN532 nfc1(5, 4, 3, 7);
Adafruit_PN532 nfc2(5, 4, 3, 8);
uint8_t result[] = {0, 0};

/////////////////////////////////////////////////

const char* id = "arduino_paintings";
const char* user = "user";
const char* pass = "pass";
const char* status = "paintings/status";
const char* off = "offline";
const char* on = "online";

bool reconnect() {
  if (client.connect(id, user, pass, status, 0, 1, off)) {
    client.publish(status, on, 1);}
  return client.connected();
}

/////////////////////////////////////////////////

uint32_t lastReconnectAttempt;

void setup(){
  delay(5000);
  //Serial.begin(115200);
  //Serial.println("START");
  client.setServer(ipServer, 1883);
  Ethernet.init(6);
  Ethernet.begin(mac, ip);
  nfc1.begin();
  nfc2.begin();
  //uint8_t versiondata;
  //versiondata = nfc1.getFirmwareVersion();
  //Serial.println(versiondata);
  //if(!versiondata){while (1);}
  //versiondata = nfc2.getFirmwareVersion();
  //Serial.println(versiondata);
  //if(!versiondata){while (1);}
  lastReconnectAttempt = 0;
  reconnect();
}


/////////////////////////////////////////////////

const char* topic1 = "paintings/p1";
const char* topic2 = "paintings/p2";

void loop(){
  if(!client.connected()){
    //Serial.println("connecting");
    uint32_t now = millis();
    if(now - lastReconnectAttempt > 5000){
      lastReconnectAttempt = now;
      if(reconnect()){lastReconnectAttempt = 0;}}}
  else {client.loop();}

  uint8_t success;
  uint8_t uid1[] = {0, 0, 0, 0, 0, 0, 0};
  uint8_t uid2[] = {0, 0, 0, 0, 0, 0, 0};
  uint8_t uidLength;

  nfc1.startPassiveTargetIDDetection(PN532_MIFARE_ISO14443A);
  success = nfc1.readDetectedPassiveTargetID(uid1, &uidLength);
  if(success){result[0] = uid1[2];success=0;}
  else {result[0] = 0;}

  nfc2.startPassiveTargetIDDetection(PN532_MIFARE_ISO14443A);
  success = nfc2.readDetectedPassiveTargetID(uid2, &uidLength);
  if(success){result[1] = uid2[2];success=0;}
  else {result[1] = 0;}

  
  
  if(client.connected()){
  static uint32_t timer = 0;
  if(millis() - timer > 1000){
    timer = millis();
    char data[4]; data[3] = '\0';
    itoa(result[0], data, DEC);
    client.publish(topic1, data);
    itoa(result[1], data, DEC);
    client.publish(topic2, data);
  }

  static uint32_t timer2 = 0;
  if(millis() - timer2 > 10000){
    timer2 = millis();
    client.publish(status, on);
  }}  
}

/////////////////////////////////////////////////
// Последняя версия, ставить её 22.06.25






