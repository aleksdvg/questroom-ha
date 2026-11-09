#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_PN532.h>
#include <Ethernet.h>
#include <PubSubClient.h>

byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x09};
byte ip[] = {111, 111, 111, 111}; 
byte ipServer[] = {111, 111, 111, 111};
EthernetClient net;
PubSubClient client(net);

// SCK, MISO, MOSI, SS
Adafruit_PN532 nfc1(5, 4, 3, 7);
Adafruit_PN532 nfc2(5, 4, 3, 8);
Adafruit_PN532 nfc3(5, 4, 3, 9);
Adafruit_PN532 nfc4(5, 4, 3, 10);
uint8_t result[] = {0, 0, 0, 0};

/////////////////////////////////////////////////

const char* id = "arduino_toys";
const char* user = "user";
const char* pass = "pass";
const char* status = "toys/status";
const char* off = "offline";
const char* on = "online";
const char* sub = "toys/script";

/////////////////////////////////////////////////

void callback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  
  uint8_t data = payload[0] - '0';
  if(data == 1){Serial.println('l');} // led on
  if(data == 2){Serial.println('r');} // reset led
}

/////////////////////////////////////////////////

bool reconnect() {
  if (client.connect(id, user, pass, status, 0, 1, off)) {
    client.publish(status, on, 1);
    client.subscribe(sub);}
  return client.connected();
}

/////////////////////////////////////////////////

uint32_t lastReconnectAttempt;

void setup(){
  Serial.begin(115200);
  Serial.setTimeout(50);
  client.setServer(ipServer, 1883);
  client.setCallback(callback);
  Ethernet.init(6);
  Ethernet.begin(mac, ip);
  nfc1.begin();
  nfc2.begin();
  nfc3.begin();
  nfc4.begin();
  uint8_t versiondata;
  versiondata = nfc1.getFirmwareVersion();
  if(!versiondata){while (1);}
  versiondata = nfc2.getFirmwareVersion();
  if(!versiondata){while (1);}
  versiondata = nfc3.getFirmwareVersion();
  if(!versiondata){while (1);}
  versiondata = nfc4.getFirmwareVersion();
  if(!versiondata){while (1);}
  lastReconnectAttempt = 0;
  reconnect();
}


/////////////////////////////////////////////////

const char* topic1 = "toys/status_boards/b1";
const char* topic2 = "toys/status_boards/b2";
const char* topic3 = "toys/status_boards/b3";
const char* topic4 = "toys/status_boards/b4";
const char* topic5 = "telephone/status";

void loop(){
  if(!client.connected()){
    uint32_t now = millis();
    if(now - lastReconnectAttempt > 5000){
      lastReconnectAttempt = now;
      if(reconnect()){lastReconnectAttempt = 0;}}}
  else {client.loop();}

  uint16_t data;
  if(Serial.available() > 0){data = Serial.parseInt();}
  if(data > 999){
    Serial.println(data);
    char mdata[5]; mdata[4] = '\0';
    itoa(data, mdata, DEC);
    client.publish(topic5, mdata);
  }

  uint8_t success;
  uint8_t uid1[] = {0, 0, 0, 0, 0, 0, 0};
  uint8_t uid2[] = {0, 0, 0, 0, 0, 0, 0};
  uint8_t uid3[] = {0, 0, 0, 0, 0, 0, 0};
  uint8_t uid4[] = {0, 0, 0, 0, 0, 0, 0};
  uint8_t uidLength; 
  nfc1.startPassiveTargetIDDetection(PN532_MIFARE_ISO14443A);
  success = nfc1.readDetectedPassiveTargetID(uid1, &uidLength);
  if(success){result[0] = uid1[2];success=0;}
  else {result[0] = 0;}
  nfc2.startPassiveTargetIDDetection(PN532_MIFARE_ISO14443A);
  success = nfc2.readDetectedPassiveTargetID(uid2, &uidLength);
  if(success){result[1] = uid2[2];success=0;}
  else {result[1] = 0;}
  nfc3.startPassiveTargetIDDetection(PN532_MIFARE_ISO14443A);
  success = nfc3.readDetectedPassiveTargetID(uid3, &uidLength);
  if(success){result[2] = uid3[2];success=0;}
  else {result[2] = 0;}
  nfc4.startPassiveTargetIDDetection(PN532_MIFARE_ISO14443A);
  success = nfc4.readDetectedPassiveTargetID(uid4, &uidLength);
  if(success){result[3] = uid4[2];success=0;}
  else {result[3] = 0;}
  client.loop();
  static uint32_t timer = 0;
  if(millis() - timer > 1000){
    timer = millis();
    char data[4]; data[3] = '\0';
    itoa(result[0], data, DEC);
    client.publish(topic1, data);
    itoa(result[1], data, DEC);
    client.publish(topic2, data);
    itoa(result[2], data, DEC);
    client.publish(topic3, data);
    itoa(result[3], data, DEC);
    client.publish(topic4, data);
  }
  client.loop();  
}

///////////////////////////////////////////////// final







