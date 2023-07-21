#include <esp_wifi.h>
#include <WiFi.h>
#include <esp_now.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <IRremoteESP8266.h>
#include <ir_Daikin.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_MOSI 23
#define OLED_CLK 18
#define OLED_DC 16
#define OLED_CS 5
#define OLED_RESET 17

// // 'weather', 24x24px
// const unsigned char ACMode_icon [] PROGMEM = {
// 	0x00, 0x18, 0x00, 0x01, 0x1c, 0x00, 0x01, 0x9c, 0x00, 0x00, 0x98, 0x40, 0x08, 0x18, 0x70, 0x04,
// 	0x78, 0x70, 0x00, 0xf8, 0x78, 0x61, 0x98, 0x80, 0x33, 0x19, 0x00, 0x06, 0x1a, 0x00, 0x06, 0x1c,
// 	0x06, 0xe4, 0x1f, 0xff, 0xe4, 0x1f, 0xff, 0x06, 0x1c, 0x06, 0x06, 0x1a, 0x00, 0x33, 0x19, 0x00,
// 	0x61, 0x98, 0x80, 0x00, 0xf8, 0x78, 0x04, 0x78, 0x70, 0x08, 0x18, 0x70, 0x00, 0x98, 0x40, 0x01,
// 	0x9c, 0x00, 0x01, 0x1c, 0x00, 0x00, 0x18, 0x00
// };


// 'cool (1)', 24x24px
const unsigned char coolMode[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x5a, 0x00, 0x00, 0x7e, 0x00, 0x02,
  0x3c, 0x40, 0x12, 0x18, 0x48, 0x1e, 0x18, 0x78, 0x0f, 0x18, 0xf0, 0x1f, 0x99, 0xf8, 0x18, 0xff,
  0x18, 0x00, 0x3c, 0x00, 0x00, 0x3c, 0x00, 0x18, 0xff, 0x18, 0x1f, 0x99, 0xf8, 0x0f, 0x18, 0xf0,
  0x1e, 0x18, 0x78, 0x12, 0x18, 0x48, 0x02, 0x3c, 0x40, 0x00, 0x7e, 0x00, 0x00, 0x5a, 0x00, 0x00,
  0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// 'dry', 24x24px
const unsigned char dryMode[] PROGMEM = {
  0x00, 0x00, 0x20, 0x00, 0x20, 0x70, 0x00, 0x30, 0xf8, 0x00, 0x78, 0xf8, 0x00, 0xdc, 0x60, 0x01,
  0xcc, 0x60, 0x01, 0x86, 0x60, 0x03, 0x00, 0xc0, 0x07, 0x01, 0xc0, 0x06, 0x03, 0x80, 0x0c, 0x0e,
  0x00, 0x0c, 0x1c, 0x40, 0x18, 0x18, 0x40, 0x18, 0x30, 0x60, 0x10, 0x30, 0x60, 0x30, 0x30, 0x60,
  0x10, 0x20, 0x60, 0x18, 0x00, 0x60, 0x18, 0x00, 0x60, 0x1c, 0x00, 0xc0, 0x0e, 0x01, 0xc0, 0x07,
  0x03, 0x80, 0x03, 0xff, 0x00, 0x00, 0xfc, 0x00
};

// Array of all bitmaps for convenience. (Total bytes used to store images in PROGMEM = 96)
const int epd_bitmap_allArray_LEN = 2;
const unsigned char *epd_bitmap_allArray[2] = {
  coolMode, dryMode
};


struct Message {
  uint8_t signalCode1;
  uint8_t signalCode2;
  bool AC_Condition;
  uint8_t roomTemp;
} msg{ 0, 0, false, 22 };

enum signalStatus {
  roomOK = 0,
  loweringTemp = 1,
  raisingTemp = 2,
  modeCool = 3,
  modeDry = 4,
  roomDanger = 5,
};

String sinkNode1 = "40:22:D8:3E:99:7C";
String sinkNode2 = "40:22:D8:3C:60:54";

int sendTemp;
const uint16_t IR_LED_PIN = 4;
uint8_t currentTemp = 20;
// 1 cool, 4 dry
uint8_t currentMode = kDaikin64Cool;
bool dataReceived;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);
IRDaikin64 ac(IR_LED_PIN);

void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len);
void whatSink(bool AC_Condition);
void onRecvCommandSink(uint8_t decodeSignal);
void changeDisplayMode(uint8_t currentMode);
void changeDisplayTemp(uint8_t currentTemp);
void updateDisplay(uint16_t currentTemp, uint8_t currentMode);

void setup() {
  // put your setup code here, to run once:
  //Initialize Serial Monitor
  Serial.begin(115200);

  // Initialize OLED Display
  if (!display.begin(SSD1306_SWITCHCAPVCC)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;
  }
  display.clearDisplay();

  //Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);

  //Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  ac.setPowerToggle(false);

  // Once ESPNow is successfully Init, we will register for recv CB to
  // get recv packer info
  esp_now_register_recv_cb(OnDataRecv);
  ac.begin();
  Serial.println("Suhu Awal: ");
  Serial.println(currentTemp);
}

void loop() {
  if (dataReceived == true) {
    whatSink(msg.AC_Condition);
    dataReceived = false;
  }
}

void whatSink(bool AC_Condition) {
  Serial.println("Kondisi AC saat ini: ");
  Serial.println(ac.toString());
  Serial.println("");
  if (AC_Condition == true && WiFi.macAddress() == sinkNode1) {  // Mengatur AC pada sink node 1
    if (ac.getPowerToggle() == true) {
      ac.setPowerToggle(false);
      onRecvCommandSink(msg.signalCode1);
      Serial.println("Mengontrol AC 1");
      ac.setPowerToggle(true);
    } else {
      ac.setPowerToggle(true);
      ac.send();
      Serial.println("Menyalakan AC 1");
      ac.setPowerToggle(false);
      onRecvCommandSink(msg.signalCode1);
      Serial.println("Mengontrol AC 1");
      ac.setPowerToggle(true);
    }
    updateDisplay(currentTemp, currentMode);
    Serial.println("Hasil pemberian perintah: ");
    Serial.println(ac.toString());
    Serial.println("==========================================");
  }
  if (AC_Condition == false && WiFi.macAddress() == sinkNode2) {  // Mengatur AC pada sink node 2
    if (ac.getPowerToggle() == true) {
      ac.setPowerToggle(false);
      onRecvCommandSink(msg.signalCode2);
      Serial.println("Mengontrol AC 2");
      ac.setPowerToggle(true);
    } else {
      ac.setPowerToggle(true);
      ac.send();
      Serial.println("Menyalakan AC 2");
      ac.setPowerToggle(false);
      onRecvCommandSink(msg.signalCode2);
      Serial.println("Mengontrol AC 2");
    }
    updateDisplay(currentTemp, currentMode);
    Serial.println("Hasil pemberian perintah: ");
    Serial.println(ac.toString());
    Serial.println("==========================================");
  }
  if ( !(AC_Condition == false && WiFi.macAddress() == sinkNode2) && !(AC_Condition == true && WiFi.macAddress() == sinkNode1) ) {
    if(AC_Condition == false) {
      Serial.println("==========================================================");
      Serial.println("AC 1 Mati");
    } else {
      Serial.println("==========================================================");
      Serial.println("AC 2 Mati");
    }
  }
}

void onRecvCommandSink(uint8_t decodeSignal) {
  currentTemp = currentTemp;
  currentMode = currentMode;
  updateDisplay(currentTemp, currentMode);
  switch (decodeSignal) {
    case roomOK:
      {
        // continue the conditions
        Serial.println("Room temp & humd are ok!");
        break;
      }
    case loweringTemp:
    case raisingTemp:
      {

        if (msg.roomTemp > currentTemp) {
          sendTemp = msg.roomTemp - currentTemp;
          if (sendTemp != 0 && !(round(sendTemp > 5))) {
            sendTemp = currentTemp - sendTemp;
            ac.setTemp(sendTemp);
            Serial.println("Lowering AC temp");
            Serial.println(sendTemp);
          } else {
            Serial.println("AC is broken!");
          }
        } else {
          sendTemp = currentTemp - msg.roomTemp;
          if (sendTemp != 0 && !(round(sendTemp > 5))) {
            sendTemp = currentTemp + abs(sendTemp);
            ac.setTemp(sendTemp);
            Serial.println("Raising AC temp");
            Serial.println(sendTemp);
          } else {
            Serial.println("AC is broken!");
          }
        }
        currentTemp = sendTemp;
        ac.send();
        Serial.println(ac.toString());
        break;
      }
    case modeCool:
      {
        ac.setMode(kDaikin64Cool);
        ac.send();
        currentMode = ac.getMode();
        changeDisplayMode(currentMode);
        Serial.println("Switching to cool mode");
        Serial.println(ac.toString());
        break;
      }
    case modeDry:
      {
        ac.setMode(kDaikin64Dry);
        ac.send();
        currentMode = ac.getMode();
        changeDisplayMode(currentMode);
        Serial.println("Switching to dry mode");
        Serial.println(ac.toString());
        break;
      }
    default:
      // Send command to call security with WA/telegram API
      Serial.println("Server room in danger!");
      break;
  }
  currentTemp = ac.getTemp();
  currentMode = ac.getMode();
  Serial.print("Suhu saat ini: ");
  Serial.println(currentTemp);
  Serial.print("Mode saat ini: ");
  Serial.println(currentMode);
}

void updateDisplay(uint16_t currentTemp, uint8_t currentMode) {
  display.clearDisplay();
  changeDisplayTemp(currentTemp);
  changeDisplayMode(currentMode);
}

void changeDisplayTemp(uint8_t currentTemp) {
  display.setTextSize(3);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(30, 40);
  display.print(currentTemp);
  display.print((char)247);
  display.println("C");
  display.display();
}

void changeDisplayMode(uint8_t currentMode) {
  if (currentMode == kDaikin64Cool) {
    display.drawBitmap(55, 0, coolMode, 24, 24, SSD1306_WHITE);
  }
  if (currentMode == kDaikin64Dry) {
    display.drawBitmap(55, 0, dryMode, 24, 24, SSD1306_WHITE);
  }
  display.display();
}

//callback function that will be executed when data is received
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  memcpy(&msg, incomingData, sizeof(msg));
  Serial.print("Bytes received: ");
  Serial.println(len);
  Serial.print("signalCode1: ");
  Serial.println(msg.signalCode1);
  Serial.print("signalCode2: ");
  Serial.println(msg.signalCode2);
  Serial.print("AC_Condition: ");
  Serial.println(msg.AC_Condition);
  Serial.println();
  dataReceived = true;
}