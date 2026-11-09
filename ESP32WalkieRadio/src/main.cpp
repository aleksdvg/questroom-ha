/*
 * ESP32 DOIT DevKit V1 + INMP441 - "РАЦИЯ" (ФИНАЛЬНАЯ ВЕРСИЯ)
 * -------------------------------------------------------------------------
 * Что делает:
 *  1) Подключается к WiFi и держит постоянное MQTT-соединение с Home Assistant.
 *  2) Светодиод SK6812 показывает статус:
 *       КРАСНЫЙ   - нет WiFi или нет MQTT (что-то не так с сетью)
 *       ЗЕЛЁНЫЙ   - всё подключено, ждём нажатия кнопки
 *       ОРАНЖЕВЫЙ - кнопка нажата, идёт передача звука
 *  3) При нажатии кнопки - подключается по обычному TCP к аддону на
 *     Raspberry Pi (порт 8765) и льёт сырой PCM-звук с микрофона.
 *     При отпускании - закрывает соединение.
 *  4) Усиление микрофона регулируется на лету через MQTT (без перепрошивки).
 *
 * ВАЖНО: впишите свои настройки в блоке НАСТРОЙКИ ниже перед прошивкой.
 *
 * Требуемые библиотеки (см. platformio.ini):
 *   knolleary/PubSubClient      - MQTT
 *   adafruit/Adafruit NeoPixel  - управление светодиодом SK6812
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>
#include <driver/i2s.h>

// ============ НАСТРОЙКИ WIFI ============
const char* WIFI_SSID     = "SSID";
const char* WIFI_PASSWORD = "pass";

// ============ НАСТРОЙКИ АДДОНА (приём звука на Raspberry Pi) ============
const char* RECEIVER_HOST = "111.111.111.111"; // IP Home Assistant
const uint16_t RECEIVER_PORT = 8765;

// ============ НАСТРОЙКИ MQTT ============
const char* MQTT_HOST = "111.111.111.111";     // обычно тот же IP, что и выше
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER = "user";                  // оставьте "" если без авторизации
const char* MQTT_PASSWORD = "pass";
const char* MQTT_CLIENT_ID = "esp32-walkie";

// ---- Топики MQTT (подробное описание каждого - см. в ответе в чате) ----
const char* TOPIC_STATUS     = "walkie/status";        // ESP32 -> HA:  online/offline (LWT)
const char* TOPIC_TALKING    = "walkie/talking";       // ESP32 -> HA:  ON/OFF (идёт разговор)
const char* TOPIC_GAIN_SET   = "walkie/mic_gain/set";  // HA -> ESP32:  10-16 (усиление микрофона)
const char* TOPIC_GAIN_STATE = "walkie/mic_gain/state";// ESP32 -> HA:  текущее значение усиления

// ============ ПИНЫ ============
#define I2S_WS      25   // WS / LRCL микрофона
#define I2S_SCK     26   // SCK / BCLK микрофона
#define I2S_SD      32   // SD  / DOUT микрофона
#define I2S_PORT    I2S_NUM_0

#define BUTTON_PIN   27  // кнопка: один контакт сюда, другой на GND
#define LED_DATA_PIN 2   // DIN светодиода SK6812

// Если ваш SK6812 - 4-канальный (RGBW, с отдельным белым кристаллом),
// замените строку ниже на: NEO_GRBW + NEO_KHZ800 и раскомментируйте
// белый канал в функции setColor(). Обычный 3-канальный SK6812 (как WS2812B)
// работает с NEO_GRB + NEO_KHZ800, как задано сейчас.
Adafruit_NeoPixel led(1, LED_DATA_PIN, NEO_GRB + NEO_KHZ800);

// ============ ПАРАМЕТРЫ ЗВУКА ============
#define SAMPLE_RATE     16000   // ДОЛЖНО совпадать с sample_rate в настройках аддона
#define I2S_READ_BYTES  1024

volatile int sampleShift = 13; // усиление, регулируется через MQTT (10-16, меньше = громче)

WiFiClient receiverClient;     // TCP-соединение с аддоном на RPi (только во время разговора)
WiFiClient espMqttClient;
PubSubClient mqttClient(espMqttClient);

int32_t i2sReadBuffer[I2S_READ_BYTES / 4];
int16_t pcmBuffer[I2S_READ_BYTES / 4];

volatile bool talking = false;      // кнопка нажата прямо сейчас
volatile bool transmitting = false; // идёт реальная передача (включая короткий хвост затухания)

// Длительность "заглушки" сразу после нажатия - пропускаем физический звук
// щелчка самой кнопки (микрофон его слышит как обычный звук из воздуха).
const int MUTE_MS = 240;
const int MUTE_SAMPLES = SAMPLE_RATE * MUTE_MS / 1000;

// Длительность плавного нарастания/затухания громкости после заглушки -
// убирает щелчки от резкого старта/обрыва самого звукового потока.
const int FADE_MS = 60;
const int FADE_SAMPLES = SAMPLE_RATE * FADE_MS / 1000;

// ============ СВЕТОДИОД ============
void setColor(uint8_t r, uint8_t g, uint8_t b) {
  led.setPixelColor(0, led.Color(r, g, b));
  led.show();
}

void updateLed() {
  if (transmitting) {
    setColor(255, 80, 0);           // оранжевый - идёт передача (в т.ч. хвост затухания)
  } else if (WiFi.status() == WL_CONNECTED && mqttClient.connected()) {
    setColor(0, 255, 0);            // зелёный - всё готово, ждём кнопку
  } else {
    setColor(255, 0, 0);            // красный - нет WiFi или нет MQTT
  }
}

// ============ I2S ИНИЦИАЛИЗАЦИЯ ============
void i2sInit() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT, // если тихо/нули - смените на ONLY_LEFT
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 16,
    .dma_buf_len = 256,
    .use_apll = true,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD
  };
  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
  i2s_zero_dma_buffer(I2S_PORT);
}

inline int16_t convertSample(int32_t raw, float fadeMul) {
  int32_t val = raw >> sampleShift;
  val = (int32_t)(val * fadeMul);
  if (val > 32767) val = 32767;
  if (val < -32768) val = -32768;
  return (int16_t)val;
}

// ============ MQTT ============
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

  if (String(topic) == TOPIC_GAIN_SET) {
    int newShift = msg.toInt();
    if (newShift >= 10 && newShift <= 16) {
      sampleShift = newShift;
      mqttClient.publish(TOPIC_GAIN_STATE, String(sampleShift).c_str(), true);
    }
  }
}

void mqttReconnect() {
  if (mqttClient.connected()) return;
  bool ok;
  if (strlen(MQTT_USER) > 0) {
    ok = mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD,
                             TOPIC_STATUS, 0, true, "offline");
  } else {
    ok = mqttClient.connect(MQTT_CLIENT_ID, TOPIC_STATUS, 0, true, "offline");
  }
  if (ok) {
    Serial.println("MQTT подключен");
    mqttClient.publish(TOPIC_STATUS, "online", true);
    mqttClient.publish(TOPIC_GAIN_STATE, String(sampleShift).c_str(), true);
    mqttClient.subscribe(TOPIC_GAIN_SET);
  }
}

// ============ ЗАДАЧА: ЗАХВАТ + ОТПРАВКА ЗВУКА ПО TCP ============
enum StreamState { ST_IDLE, ST_MUTE, ST_FADE_IN, ST_TALKING };

int16_t lastSentSample = 0; // последний реально переданный уровень громкости

// При отпускании кнопки микрофон СРАЗУ перестаём слушать (чтобы физически
// не успеть поймать щелчок самой кнопки), а плавное затухание генерируем
// программно - от последнего известного уровня громкости до тишины.
// Микрофон в этот момент уже не опрашивается вообще.
void sendSyntheticFadeOutAndClose() {
  static int16_t tail[FADE_SAMPLES];
  for (int i = 0; i < FADE_SAMPLES; i++) {
    float mul = 1.0f - (float)i / FADE_SAMPLES;
    tail[i] = (int16_t)(lastSentSample * mul);
  }
  if (receiverClient.connected()) {
    receiverClient.write((uint8_t*)tail, FADE_SAMPLES * 2);
  }
  receiverClient.stop();
  Serial.println("Микрофон отключен, синтетическое затухание отправлено");
}

void audioStreamTask(void *pvParameters) {
  StreamState state = ST_IDLE;
  int fadeCounter = 0;
  int muteCounter = 0;
  bool ignoreUntilReleased = false; // защита от переподключения сразу после отпускания

  for (;;) {
    // сбрасываем защиту, как только дебаунсированное состояние кнопки
    // догонит реальное (подтвердит, что кнопка правда отпущена)
    if (ignoreUntilReleased && !talking) {
      ignoreUntilReleased = false;
    }

    // --- переходы состояний по кнопке ---
    if (state == ST_IDLE && talking && !ignoreUntilReleased) {
      Serial.println("Подключаюсь к приёмнику...");
      if (receiverClient.connect(RECEIVER_HOST, RECEIVER_PORT)) {
        Serial.println("Подключились, заглушка щелчка кнопки...");
        state = ST_MUTE;
        muteCounter = 0;
        lastSentSample = 0;
        transmitting = true;
      } else {
        Serial.println("Не удалось подключиться к приёмнику");
      }
    } else if ((state == ST_MUTE || state == ST_FADE_IN || state == ST_TALKING) &&
               digitalRead(BUTTON_PIN) == HIGH) {
      // Реагируем на СЫРОЕ состояние пина, а не на дебаунсированный "talking" -
      // софтверный дебаунс (30мс) достаточен для логики MQTT/подключения, но
      // для звука он слишком медленный: за эти 30мс успевал захватываться
      // физический щелчок кнопки. Здесь микрофон отключаем максимально быстро.
      sendSyntheticFadeOutAndClose();
      state = ST_IDLE;
      transmitting = false;
      ignoreUntilReleased = true;
      continue;
    }

    if (state == ST_IDLE) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    if (!receiverClient.connected()) {
      // соединение оборвалось само (сеть) - сбрасываемся в IDLE
      receiverClient.stop();
      state = ST_IDLE;
      transmitting = false;
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    size_t bytesRead = 0;
    i2s_read(I2S_PORT, i2sReadBuffer, sizeof(i2sReadBuffer), &bytesRead, pdMS_TO_TICKS(50));
    if (bytesRead > 0) {
      int samples = bytesRead / 4;
      for (int i = 0; i < samples; i++) {
        float fadeMul = 1.0f;

        if (state == ST_MUTE) {
          fadeMul = 0.0f; // полная тишина - пропускаем звук щелчка кнопки
          muteCounter++;
          if (muteCounter >= MUTE_SAMPLES) {
            state = ST_FADE_IN;
            fadeCounter = 0;
          }
        } else if (state == ST_FADE_IN) {
          fadeMul = (float)fadeCounter / FADE_SAMPLES;
          if (fadeMul > 1.0f) fadeMul = 1.0f;
          fadeCounter++;
          if (fadeCounter >= FADE_SAMPLES) state = ST_TALKING;
        }

        pcmBuffer[i] = convertSample(i2sReadBuffer[i], fadeMul);
        lastSentSample = pcmBuffer[i]; // запоминаем для синтетического затухания
      }
      receiverClient.write((uint8_t*)pcmBuffer, samples * 2);
    }
  }
}

// ============ КНОПКА (с простым программным дебаунсом) ============
void handleButton() {
  static bool lastReading = HIGH;
  static bool stableState = HIGH;
  static unsigned long lastChangeTime = 0;

  bool reading = digitalRead(BUTTON_PIN);
  if (reading != lastReading) lastChangeTime = millis();

  if (millis() - lastChangeTime > 30) {
    if (reading != stableState) {
      stableState = reading;
      bool nowTalking = (stableState == LOW);
      if (nowTalking != talking) {
        talking = nowTalking;
        Serial.println(talking ? "Кнопка: НАЖАТА" : "Кнопка: отпущена");
        if (mqttClient.connected()) {
          mqttClient.publish(TOPIC_TALKING, talking ? "ON" : "OFF");
        }
      }
    }
  }
  lastReading = reading;
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  led.begin();
  led.setBrightness(45); // 0-255, поменяйте если хотите ярче/тусклее
  setColor(255, 0, 0);   // красный сразу при старте - ещё не подключились

  i2sInit();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Подключение к WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP адрес ESP32: ");
  Serial.println(WiFi.localIP());

  WiFi.setSleep(false); // убирает щелчки от энергосбережения WiFi во время передачи

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  xTaskCreatePinnedToCore(audioStreamTask, "audioStream", 4096, NULL, 2, NULL, 1);
}

void loop() {
  if (!mqttClient.connected()) {
    static unsigned long lastAttempt = 0;
    if (millis() - lastAttempt > 3000) {
      lastAttempt = millis();
      mqttReconnect();
    }
  } else {
    mqttClient.loop();
  }

  handleButton();
  updateLed();
  delay(5);
}