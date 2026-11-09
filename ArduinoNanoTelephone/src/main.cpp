#include <Arduino.h>
#include <Keypad.h>

const byte ROWS = 4; //four rows
const byte COLS = 4; //four columns
//define the cymbols on the buttons of the keypads
char hexaKeys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'D','0','E','F'}
};
byte rowPins[ROWS] = {5, 4, 3, 2}; //connect to the row pinouts of the keypad
byte colPins[COLS] = {9, 8, 7, 6}; //connect to the column pinouts of the keypad
//initialize an instance of class NewKeypad
Keypad customKeypad = Keypad( makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

char nomer[] = "0000";
bool reset = 0;
uint8_t position = 0;
uint8_t led = 0;

void setup(){
  Serial.begin(115200);
  Serial.setTimeout(50);
  pinMode(11, INPUT); // Reset, 0 - трубка на телефоне
  pinMode(14, OUTPUT); // A0 led green
  pinMode(15, OUTPUT); // A1 led red
}
  
void loop(){
  char data;
  if(Serial.available() > 0){data = Serial.read();}
  if(data == 'l'){led = 1; data = '0';}
  if(data == 'r'){led = 0; data = '0';}

  static uint32_t tmr1 = 0;
  static uint32_t tmr2 = 0;
  if(led == 0){tmr1 = 0; tmr2 = 0; digitalWrite(14, 0); digitalWrite(15, 0);}
  if(led == 1){
    if(tmr1 == 0){tmr1 = millis() + 1800000; digitalWrite(14, 1);}   // на пол часа вкл
    if(millis() >= tmr1){led = 0;}}
  if(led == 2){
    if(tmr2 == 0){tmr2 = millis() + 300000; digitalWrite(14, 0); digitalWrite(15, 1);}   // на 10 мин вкл
    if(millis() >= tmr2){led = 0;}}

  // Reset nomer
  reset = digitalRead(11);
  if(position > 3){
    position = 0; reset = 0;
    uint16_t nomerInt = atoi(nomer);
    if(nomerInt == 2549){led = 2;}
    Serial.println(nomer);}
  if(reset == 0){position = 0; for(uint8_t i=0; i<4; i++){nomer[i] = '0';}}
  
  // get nomer
  char key = customKeypad.getKey();
  if(key){nomer[position] = key; position++;}

  // Гудок
  static uint32_t timeGudok = 0;
  if(digitalRead(11) == 1){
  if(millis() - timeGudok > 1200){
    timeGudok = millis();
    tone(12, 220, 600);
  }}
}


// final