#include <Arduino.h>
#include <PubSubClient.h>
#include <Ethernet.h>
#include <IWatchdog.h>



byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x07};
byte ip[] = {111, 111, 111, 111};  
byte ipServer[] = {111, 111, 111, 111};
EthernetClient net;
PubSubClient client(net);

/////////////////////////////////////////////////

void releControl(); // Проверяет изменилось ли реле
uint8_t selectPin(uint8_t pin); // выбор пина реле
void pinSetup(); // Инициализация и установка реле в выкл
void sendUpdate(uint8_t pin); // Обновление MQTT
void checkUpdate(); // проверить изменения
void autoUpdate(); // авто обновление
//void sendOnline();

/////////////////////////////////////////////////

uint32_t releGet = 0; // запись сюда полученнных данных
uint32_t releChanges = 0; // какое реле изменилось
//uint8_t getScript = 0; хз
//bool blockMode = 0; хз
const char* command = "rls/commands";
//const char* script = "rls/script";  не используется


void callback(char* topic, byte* payload, unsigned int length) {
  char buf[10];
  if (length >= sizeof(buf)) return;
  memcpy(buf, payload, length);
  buf[length] = '\0';
  
  if(strcmp(topic, command) == 0){  // получает mXXvX
    uint8_t rele = buf[2] - '0'; // берет номер реле
    if(buf[1] == '1'){rele += 10;}
    uint8_t val = buf[4] - '0'; // берет ВКЛ или ВЫКЛ
    bitWrite(releGet, rele-1, val); // запись полученое значие
    bitSet(releChanges, rele-1); // обновление
  }
}


/////////////////////////////////////////////////


void resetW5500() {
  digitalWrite(PA3, LOW);
  delay(50);
  digitalWrite(PA3, HIGH);
  delay(200);
  Ethernet.init(PA4);
  Ethernet.begin(mac, ip);
}

/////////////////////////////////////////////////

const char* id = "arduino_rls";
const char* user = "user";
const char* pass = "pass";
const char* status = "rls/status";
const char* off = "offline";
const char* on = "online";
bool needPublishAll = 0;


void reconnect() {
  static uint8_t lastConnect = 0;
  static uint32_t lastTry = 0;

  if (millis() - lastTry < 5000) return;
  lastTry = millis();
  lastConnect++;

  if(lastConnect > 6){lastConnect = 0; resetW5500();}

  if(!client.connected()){
    if (client.connect(id, user, pass, status, 0, true, off)) {
    lastConnect = 0;
    needPublishAll = true;
    client.publish(status, on, true);
    client.subscribe(command);
    }
  }
}


/////////////////////////////////////////////////


void setup() {

  // Сброс 5500
  pinMode(PA3, OUTPUT);
  digitalWrite(PA3, LOW);
  delay(50);
  digitalWrite(PA3, HIGH);
  delay(200);

  pinSetup();
  delay(3000);

  IWatchdog.begin(15000); // 15 секунд

  client.setServer(ipServer, 1883);
  client.setCallback(callback);
  Ethernet.init(PA4);
  Ethernet.begin(mac, ip);
  delay(2000);
}

/////////////////////////////////////////////////

void loop() {

  if(!client.connected()){reconnect();}
  else {client.loop();}
  releControl();
  checkUpdate();
  if(needPublishAll && client.connected()){autoUpdate();}
  //if(client.connected()){sendOnline();}

  IWatchdog.reload();
}

/////////////////////////////////////////////////
//Выбор пина управления реле

uint8_t selectPin(uint8_t pin){
  switch (pin){
  case 0:
    pin = PB0; 
    break;
  case 1:
    pin = PB1;
    break;
  case 2:
    pin = PB10;
    break;
  case 3:
    pin = PB11;
    break;
  case 4:
    pin = PB12;
    break;
  case 5:
    pin = PB13;
    break;
  case 6:
    pin = PB14;
    break;
  case 7:
    pin = PB15;
    break;
  case 8:
    pin = PA8;
    break;
  case 9:
    pin = PA9;
    break;
  case 10:
    pin = PA10;
    break;
  case 11:
    pin = PA11;
    break;
  case 12:
    pin = PA12;
    break;
  case 13:
    pin = PA15;
    break;
  case 14:
    pin = PB3;
    break;
  case 15:
    pin = PB4;
    break;
  case 16:  // СТРОБОСКОП
    pin = PB7;
    break;
  case 17:  // РОЛЕТА
    pin = PB6;
    break;
  //case 18: не задействован
  //  pin = PB5;
  //  break;
  }
  return pin;
}

/////////////////////////////////////////////////

uint32_t releUpdate = 0;

void releControl(){
  static uint8_t count = 0;
  if(count < 18 && bitRead(releChanges, count)){
    if(count < 16){digitalWrite(selectPin(count), bitRead(releGet, count));}
    else{digitalWrite(selectPin(count), !bitRead(releGet, count));}
    bitClear(releChanges, count);
    bitSet(releUpdate, count);
  }
  count++;
  if(count > 17){count = 0;}
}

/////////////////////////////////////////////////
// Инициализация и установка реле в выкл


void pinSetup(){
  uint8_t nomer = 0;
  for(uint8_t pin = 0; pin<18; pin++){
    nomer = selectPin(pin);
    pinMode(nomer, OUTPUT);
    if(pin < 16){digitalWrite(nomer, 0);}
    else {digitalWrite(nomer, 1);} // 16 и 17 реле с 0 - ВКЛ
  }
}


/////////////////////////////////////////////////


char statusRele[15] = "rls/status/r00";

void sendUpdate(uint8_t pin){
  char rele[2]; rele[1] = '\0';
  if(pin < 9){
    itoa(pin+1, rele, DEC);
    statusRele[12] = '0';
    statusRele[13] = rele[0];}
  if(pin > 8){
    itoa((pin+1)-10, rele, DEC);
    statusRele[12] = '1';
    statusRele[13] = rele[0];}
  char data[2]; data[1] = '\0';
  itoa(bitRead(releGet, pin), data, DEC);
  client.publish(statusRele, data);
}

/////////////////////////////////////////////////

void checkUpdate(){
  static uint8_t count = 0;
  if(bitRead(releUpdate, count)){
    sendUpdate(count);
    bitClear(releUpdate, count);}
  count++;
  if(count > 17){count = 0;}
}

/////////////////////////////////////////////////

void autoUpdate(){
  static uint32_t tmr1 = 0;
  static uint8_t count = 0;
  static uint16_t interval = 5000;
  //if(interval == 0){interval = 300000;}
  if(millis() - tmr1 >= interval){
    tmr1 = millis();
    sendUpdate(count);
    //interval = 5000;
    count++;
  }
  if(count>17){count = 0; interval = 0; needPublishAll = false;}
}

/////////////////////////////////////////////////

//void sendOnline(){
//  static uint32_t tmr3 = 0;
//  if(millis() - tmr3 >= 10000){
//    client.publish(status, on);
//    tmr3 = millis();}
//}

/////////////////////////////////////////////////
// Улучшаная версия  13.12.25