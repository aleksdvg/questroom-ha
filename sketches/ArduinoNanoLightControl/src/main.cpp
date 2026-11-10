#include <Arduino.h>
#include <PubSubClient.h>
#include <Ethernet.h>
#include <Adafruit_PWMServoDriver.h>
#include "Mux.h"
using namespace admux;

byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x08};
byte ip[] = {111, 111, 111, 111};  // IP модуля
byte ipServer[] = {111, 111, 111, 111}; //IP сервера MQTT
EthernetClient net;
PubSubClient client(net);
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40); // Модуль I2C шим PCA9685
Mux mux(Pin(6, INPUT, PinType::Digital), Pinset(2, 3, 4, 5)); // Мультиплексор 16 каналов CD74HC4067
// 6 - Signal input. A0, A1, A2, A3 - 2,3,4,5

/////////////////////////////////////////////////

bool flash2k = 0;
uint8_t flash34 = 0;
uint8_t fflash4k = 0;
uint8_t fflashlaz = 0;

void pwmControl(); // Управление светом
void checkUpdate(); // Проверко изменения состояний
void sendUpdate(uint8_t pin); // Обновить статус MQTT
void autoUpdate(); // Авто обновление
//void sendOnline();
void scriptControl(); //Скрипты
void readSensors(); //Чтение датчиков
void muxUpdate(); // Обновление мультиплексора
void checkClock(); // Часы
void checkSwitch(); // Выключатель УФ
void flashScript2k(); // Мигание скример 2к
void flashScript34(); // Мигание в 3-4-5к
void flash4k(); // Мигание 4к
void flashlaz(); // Мигание лаза

/////////////////////////////////////////////////

uint16_t pwmGet = 0;
uint16_t commandChange = 0;
uint16_t brightnessChange = 0;
uint16_t getScript = 0;
uint8_t getBrightness[16];
const char* command = "pwm/commands/#"; // Топик команды ВКЛ/ВЫКЛ
const char* brightness = "pwm/brightness/#"; // Топик команды яркости
const char* script = "pwm/script"; // Топик для nodered
bool needPublishAll = 0;

void callback(char* topic, byte* payload, unsigned int length) {
  char buf[10];
  if (length >= sizeof(buf)) return;
  memcpy(buf, payload, length);
  buf[length] = '\0';


  if(strspn(topic, command) > 7){  // on/off
    uint8_t val = payload[0] - '0';
    if(val == 0){
      uint8_t board = topic[15] - '0';
      if(topic[14] == '1'){board += 10;}
      bitWrite(pwmGet, board-1, 0);
      bitSet(commandChange, board-1);}}
  if(strspn(topic, brightness) > 7){  // brightness
    uint16_t val = atoi(buf);
    uint8_t board = topic[17] - '0';
    if(topic[16] == '1'){board += 10;}
    getBrightness[board-1] = val;
    bitSet(brightnessChange, board-1);}
  if(strspn(topic, script) > 7){
    uint8_t val = payload[0] - '0';
    bitSet(getScript, val-1);  
  }
}


/////////////////////////////////////////////////

const char* id = "arduino_pwm";
const char* user = "user";
const char* pass = "pass";
const char* status = "pwm/status"; // топик статуса
const char* off = "offline";
const char* on = "online";

static uint32_t lastTry = 0;


void reconnect() {
  if (millis() - lastTry < 5000) return;
  lastTry = millis();


  if(!client.connected()){
    if (client.connect(id, user, pass, status, 0, true, off)) {
    needPublishAll = true;
    client.publish(status, on, true);
    client.subscribe(command);
    client.subscribe(brightness);
    client.subscribe(script);
    }
  }
}

/////////////////////////////////////////////////

void setup() {
  delay(4000);
  client.setServer(ipServer, 1883);
  client.setCallback(callback);
  Ethernet.begin(mac, ip);
  pwm.begin();
  pwm.setPWMFreq(1000);
  Wire.setClock(400000);
  delay(2000);
  for(uint8_t pin = 0; pin<16; pin++){pwm.setPin(pin, 0); delay(50);} // RESET
  reconnect();
}

/////////////////////////////////////////////////

void loop() {
  if(!client.connected()){reconnect();}
  else {client.loop();}
  pwmControl();
  checkUpdate();
  if(needPublishAll && client.connected()){autoUpdate();}
  //if(client.connected()){sendOnline();}
  scriptControl();
  readSensors();
  if(client.connected()){muxUpdate();}
  if(client.connected()){checkClock();}
  if(client.connected()){checkSwitch();}
  if(flash2k == 1){flashScript2k();}
  if(flash34 > 0){flashScript34();}
  if(fflash4k > 0){flash4k();}
  if(fflashlaz > 0){flashlaz();}
}

/////////////////////////////////////////////////

uint16_t pwmUpdate = 0;

void pwmControl(){
  static uint8_t count = 0;
  if(bitRead(commandChange, count)){
    pwm.setPin(count, 0);
    bitClear(commandChange, count);
    bitSet(pwmUpdate, count);}
  if(bitRead(brightnessChange, count)){
    pwm.setPin(count, map(getBrightness[count], 0, 255, 0, 4095));
    bitClear(brightnessChange, count);
    bitSet(pwmGet, count);
    bitSet(pwmUpdate, count);}
  count++;
  if(count > 15){count = 0;}
}

/////////////////////////////////////////////////

char statusPWM[15] = "pwm/status/b00";
char statusBrPWM[18] = "pwm/status_br/b00";

void sendUpdate(uint8_t pin){
  char board[2]; board[1] = '\0';
  if(pin < 9){
    itoa(pin+1, board, DEC);
    statusPWM[12] = '0';
    statusPWM[13] = board[0];
    statusBrPWM[15] = '0';
    statusBrPWM[16] = board[0];}
  if(pin > 8){
    itoa((pin+1)-10, board, DEC);
    statusPWM[12] = '1';
    statusPWM[13] = board[0];
    statusBrPWM[15] = '1';
    statusBrPWM[16] = board[0];}
  char data[2]; data[1] = '\0';
  itoa(bitRead(pwmGet, pin), data, DEC);
  client.publish(statusPWM, data);
  char brightness[4]; brightness[3] = '\0';
  itoa(getBrightness[pin], brightness, DEC);
  client.publish(statusBrPWM, brightness);
}

/////////////////////////////////////////////////

void checkUpdate(){
  static uint8_t count = 0;
  if(bitRead(pwmUpdate, count)){
    sendUpdate(count);
    bitClear(pwmUpdate, count);}
  count++;
  if(count > 15){count = 0;}
}

/////////////////////////////////////////////////

void autoUpdate(){
  static uint32_t tmr1 = 0;
  static uint8_t count = 0;
  static uint16_t interval = 5000;
  //if(interval == 0){interval = 5000;}
  if(millis() - tmr1 >= interval){
    tmr1 = millis();
    sendUpdate(count);
    //interval = 1000;
    count++;
  }
  if(count>15){count = 0; interval = 0; needPublishAll = false;}
}

/////////////////////////////////////////////////

//void sendOnline(){


/////////////////////////////////////////////////

void scriptControl(){
  static uint8_t count = 0;
  if(count == 0 && bitRead(getScript, count)){
    flash2k = 1;
    bitClear(getScript, count);}
  if(count == 1 && bitRead(getScript, count)){
    flash34 = 1;
    bitClear(getScript, count);}
  if(count == 2 && bitRead(getScript, count)){
    flash34 = 2;
    bitClear(getScript, count);}
  if(count == 3 && bitRead(getScript, count)){
    fflash4k = 1;
    bitClear(getScript, count);}
  if(count == 4 && bitRead(getScript, count)){
    fflash4k = 2;
    bitClear(getScript, count);}
  if(count == 5 && bitRead(getScript, count)){
    fflashlaz = 1;
    bitClear(getScript, count);}
  if(count == 6 && bitRead(getScript, count)){
    fflashlaz = 2;
    bitClear(getScript, count);}
  count++;
  if(count > 6){count = 0;}
}

/////////////////////////////////////////////////

//uint16_t muxReads = 0;
uint16_t muxStatus = 0;
char muxTopic[15] = "mux/status/b00";

void readSensors(){
  static uint8_t pin = 0;
  //bitWrite(muxReads, pin, mux.read(pin));
  
  
  byte reads = mux.read(pin);
  if(bitRead(muxStatus, pin) != reads){
    bitWrite(muxStatus, pin, reads);
    char data[2]; data[1] = '\0';
    if(reads > 0){data[0] = '1';} else {data[0] = '0';}
    if(pin<9){
      char board[2]; board[1] = '\0';
      itoa(pin+1, board, DEC);
      muxTopic[12] = '0';
      muxTopic[13] = board[0];
      client.publish(muxTopic, data);}
      if(pin>=9){
      char board[2]; board[1] = '\0';
      itoa((pin+1)-10, board, DEC);
      muxTopic[12] = '1';
      muxTopic[13] = board[0];
      client.publish(muxTopic, data);}}
  pin++;
  if(pin > 14){pin = 0;}
}


/////////////////////////////////////////////////

void muxUpdate(){
  static uint32_t tmr3 = 0;
  static uint32_t tmr4 = 0;
  static uint32_t pin = 0;
  static uint16_t interval = 0;
  if(interval > 0){
    if(millis() - tmr3 >= interval){
    tmr3 = millis();
    byte reads = bitRead(muxStatus, pin); 
    char data[2]; data[1] = '\0';
    if(reads > 0){data[0] = '1';} else {data[0] = '0';}
    if(pin<9){
      char board[2]; board[1] = '\0';
      itoa(pin+1, board, DEC);
      muxTopic[12] = '0';
      muxTopic[13] = board[0];
      client.publish(muxTopic, data);}
      if(pin>=9){
      char board[2]; board[1] = '\0';
      itoa((pin+1)-10, board, DEC);
      muxTopic[12] = '1';
      muxTopic[13] = board[0];
      client.publish(muxTopic, data);}
    pin++;
    }
  }
  if(pin > 14){pin = 0; interval = 0;}
  if(interval == 0){
    if(millis() - tmr4 > 5000){
      tmr4 = millis();
      interval = 1000;
    }
  }
}

/////////////////////////////////////////////////

const char* clockTopic = "mux/clock";

void checkClock(){
  static uint32_t tmr5 = 0;
  if(millis() - tmr5 >= 333){
  if(bitRead(muxStatus, 9) == 1 && bitRead(muxStatus, 10) == 1 && bitRead(muxStatus, 11) == 1 && 
bitRead(muxStatus, 12) == 1 && bitRead(muxStatus, 13) == 1 && bitRead(muxStatus, 14) == 1){
    char data[2] = "1";
    client.publish(clockTopic, data);
  }
  tmr5 = millis();}
}

/////////////////////////////////////////////////

const char* switchTopic = "mux/switch";

void checkSwitch(){
  static uint32_t tmr6 = 0;
  if(millis() - tmr6 >= 333){
  if(bitRead(muxStatus, 2) == 1 && bitRead(muxStatus, 3) == 1 && bitRead(muxStatus, 4) == 1 && bitRead(muxStatus, 5) == 1){
    char data[2] = "1";
    client.publish(switchTopic, data);
  }
  tmr6 = millis();}
}

/////////////////////////////////////////////////

bool inv1 = 0;

void flashScript2k(){
  static uint32_t tmr7 = 0;
  static uint32_t tmr8 = 0;

  if(millis() - tmr8 >= 100){
    tmr8 = millis();
    pwm.setPin(2, 4095, inv1);
    pwm.setPin(7, 4095, inv1);
    inv1 = !inv1;}
  if(tmr7 == 0){tmr7 = millis() + 4000;}
  if(millis() >= tmr7){tmr7 = 0, flash2k = 0; pwm.setPin(2, 0); pwm.setPin(7, 0);} 
}

/////////////////////////////////////////////////

void flashScript34(){
  static uint32_t tmr9 = 0;
  if(flash34 == 2){pwm.setPin(12, 0); pwm.setPin(5, 0); pwm.setPin(14, 0); flash34 = 0; }
  if(flash34 == 1){
    if(millis() - tmr9 >= 1000){
      static uint8_t counter = 0;
      tmr9 = millis();
      
      if(counter == 0){pwm.setPin(12, 500); pwm.setPin(5, 0); pwm.setPin(14, 0);}
      if(counter == 1){pwm.setPin(12, 0); pwm.setPin(5, 500); pwm.setPin(14, 0);}
      if(counter == 2){pwm.setPin(12, 0); pwm.setPin(5, 0); pwm.setPin(14, 500);}

      counter++;
      if(counter > 2){counter = 0;}
    }
  }
}

/////////////////////////////////////////////////

uint16_t interval1;
bool inv2 = 0;

void flash4k(){
  static uint32_t tmr10 = 0;
  if(fflash4k == 2){pwm.setPin(5, 250); fflash4k = 0;}
  if(fflash4k == 1){
    if(interval1 == 0){interval1 = 3000;}
    if(millis() - tmr10 >= interval1){
      tmr10 = millis();
      if(inv2 == 0){interval1 = 3000;} else {interval1 = 1000;}
      if(inv2 == 0){pwm.setPin(5, 250);} else {pwm.setPin(5, 0);}
      inv2 = !inv2;
    }
  }
}


/////////////////////////////////////////////////

uint16_t interval2 = 0;
bool inv3 = 0;

void flashlaz(){
  static uint32_t tmr11 = 0;
  if(fflashlaz == 2){pwm.setPin(11, 4095); fflashlaz = 0;}
  if(fflashlaz == 1){
    if(interval2 == 0){interval2 = 2000;}
    if(millis() - tmr11 >= interval2){
      tmr11 = millis();
      if(inv3 == 0){interval2 = 2000;} else {interval2 = 1000;}
      if(inv3 == 0){pwm.setPin(11, 4095);} else {pwm.setPin(11, 0);}
      inv3 = !inv3;
    }
  }
}

/////////////////////////////////////////////////
// Последняя версия, ставить её  16.12.25