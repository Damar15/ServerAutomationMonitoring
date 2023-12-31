#include <esp_wifi.h>
#include <WiFi.h>
#include <esp_now.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <IRremoteESP8266.h>
#include <ir_Daikin.h>
#include <IRutils.h>
#include <IRac.h>
#include <esp_wifi.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_MOSI 23
#define OLED_CLK 18
#define OLED_DC 16
#define OLED_CS 5
#define OLED_RESET 17

#if DECODE_AC
// Some A/C units have gaps in their protocols of ~40ms. e.g. Kelvinator
// A value this large may swallow repeats of some protocols
const uint8_t kTimeout = 40;
#else   // DECODE_AC
// Suits most messages, while not swallowing many repeats.
const uint8_t kTimeout = 10;
#endif  // DECODE_AC

const uint16_t kCaptureBufferSize = 1024;

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

String sinkNode1 = "08:B6:1F:3D:23:AC";
String sinkNode2 = "40:22:D8:3C:60:54";

struct Message {
  uint8_t signalCode1Temp;
  uint8_t signalCode1Humd;
  uint8_t signalCode2Temp;
  uint8_t signalCode2Humd;
  bool AC_Condition;
  uint8_t roomTemp;
} msg{ 0, 0, 0, 0, false, 16 };

enum signalStatus {
  roomOK = 0,
  loweringTemp = 1,
  raisingTemp = 2,
  modeCool = 3,
  modeDry = 4,
  roomDanger = 5,
};

int sendTemp;
const uint8_t IR_RECV_PIN = 15;  //decode
const uint16_t IR_LED_PIN = 4;
uint8_t currentTemp = msg.roomTemp;
// 1 cool, 4 dry
uint8_t currentMode = kDaikin64Dry;
bool togglePower = true;
bool dataReceived;
String temporary;
String compare;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);
IRDaikin64 ac(IR_LED_PIN);
IRrecv remote(IR_RECV_PIN, kCaptureBufferSize, kTimeout, true);
decode_results result;

void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len);
void whatSink(bool AC_Condition);
void onRecvCommandSink(uint8_t decodeSignalTemp, uint8_t decodeSignalHumd);
void changeDisplayMode(uint8_t currentMode);
void changeDisplayTemp(uint8_t currentTemp);
void updateDisplay(uint16_t currentTemp, uint8_t currentMode);

void setup() {
  // put your setup code here, to run once:
  //Initialize Serial Monitor
  Serial.begin(115200);

  ac.setPowerToggle(togglePower);
  remote.enableIRIn();  //receiving IR

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

  // Once ESPNow is successfully Init, we will register for recv CB to
  // get recv packer info
  esp_now_register_recv_cb(OnDataRecv);



  ac.begin();
  Serial.println("======= Informasi awal remote AC ======= ");
  Serial.print("Power button: ");
  Serial.println(togglePower == 1 ? "ON" : "OFF");
  Serial.print("Nilai awal ac.getPowerToggle(): ");
  Serial.println(ac.getPowerToggle());
  Serial.print("AC Temperature: ");
  Serial.print(currentTemp);
  Serial.println((char)247);
  Serial.print("AC Mode: ");
  Serial.println(currentMode == 2 ? "Cool" : "Dry");
}

void loop() {
  if (dataReceived == true) {  //run command whenever received information from base node
    whatSink(msg.AC_Condition);
    dataReceived = false;
  } else {
    inRemote();  // check if there is interference from remote AC by user
  }
}

void inRemoteMode() {
  if (compare == "On") {
    compare = temporary.substring(24, 25);
  } else {
    compare = temporary.substring(25, 26);
  }
  if (compare.toInt() == 2) {
    currentMode = kDaikin64Cool;
  } else if (compare.toInt() == 1) {
    currentMode = kDaikin64Dry;
  } else {
    currentMode = currentMode;
  }
}

void inRemoteTemp() {
  if (currentMode == kDaikin64Cool) {
    compare = temporary.substring(40, 43);
    Serial.println("Ini mode cool");
  } else if (currentMode == kDaikin64Dry) {
    compare = temporary.substring(39, 42);
    Serial.println("Ini mode dry");
  }


  if (compare != String(currentTemp)) {
    currentTemp = compare.toInt();
  } else {
    currentTemp = currentTemp;
  }
}

void inRemote() {
  if (remote.decode(&result)) {
    delay(1000);

    String outCode = resultToHumanReadableBasic(&result);

    if (outCode.substring(12, 20) == "DAIKIN64") {
      IRDaikin64 acCommand(result.value);
      temporary = IRAcUtils::resultAcToString(&result);
      // *** Debug for knowing on / off AC ***
      compare = temporary.substring(14, 16);
      if (compare == "On") {
        togglePower = !togglePower;
        display.clearDisplay();  // OLED not showing info
        display.display();
      }
      if (togglePower == 1) {
        inRemoteMode();  // UpdateMode
        inRemoteTemp();  // UpdateTemp
        updateDisplay(currentTemp, currentMode);
      }
    }
    Serial.println("======= Informasi perubahan dari remote AC ======= ");
    Serial.print("Power button: ");
    Serial.println(togglePower == 1 ? "ON" : "OFF");
    Serial.print("AC Temperature: ");
    Serial.print(currentTemp);
    Serial.println((char)247);
    Serial.print("AC Mode: ");
    Serial.println(currentMode == 2 ? "Cool" : "Dry");
    remote.resume();
  }
}

void whatSink(bool AC_Condition) {
  Serial.println("Kondisi AC saat ini: ");
  Serial.println(ac.toString());
  Serial.println("");
  if (AC_Condition == true && WiFi.macAddress() == sinkNode1) {  // Mengatur AC pada sink node 1
    if (togglePower == true) {
      togglePower = true;  //menyala
      ac.setPowerToggle(false);
      onRecvCommandSink(msg.signalCode1Temp, msg.signalCode1Humd);
      Serial.println("Mengontrol AC 1");
      ac.setPowerToggle(true);
    } else {
      ac.setPowerToggle(true);
      ac.send();
      togglePower = true;  //menyala
      Serial.println("Menyalakan AC 1");
      ac.setPowerToggle(false);
      onRecvCommandSink(msg.signalCode1Temp, msg.signalCode1Humd);
      Serial.println("Mengontrol AC 1");
      ac.setPowerToggle(true);
    }
    updateDisplay(currentTemp, currentMode);
    Serial.println("Hasil pemberian perintah: ");
    Serial.println(ac.toString());
    Serial.println("==========================================");
    Serial.print("Updated togglePower: ");
    Serial.println(togglePower);
    Serial.println("==========================================");
    Serial.print("Nilai ac.getPowerToggle updated: ");
    Serial.println(ac.getPowerToggle());
    Serial.println("==========================================");
  }
  if (AC_Condition == false && WiFi.macAddress() == sinkNode2) {  // Mengatur AC pada sink node 2
    if (togglePower == true) {
      togglePower = true;  //menyala
      ac.setPowerToggle(false);
      onRecvCommandSink(msg.signalCode2Temp, msg.signalCode2Humd);
      Serial.println("Mengontrol AC 2");
      ac.setPowerToggle(true);
    } else {
      ac.setPowerToggle(true);
      ac.send();
      togglePower = true;  //menyala
      Serial.println("Menyalakan AC 2");
      ac.setPowerToggle(false);
      onRecvCommandSink(msg.signalCode2Temp, msg.signalCode2Humd);
      Serial.println("Mengontrol AC 2");
      ac.setPowerToggle(true);
    }
    updateDisplay(currentTemp, currentMode);
    Serial.println("Hasil pemberian perintah: ");
    Serial.println(ac.toString());
    Serial.println("==========================================");
    Serial.print("Updated togglePower: ");
    Serial.println(togglePower);
    Serial.println("==========================================");
  }
  if (!(AC_Condition == false && WiFi.macAddress() == sinkNode2) && !(AC_Condition == true && WiFi.macAddress() == sinkNode1)) {  //check MAC address sink node 1 & 2
    if (AC_Condition == false) {
      if (togglePower == 1) {
        ac.setPowerToggle(true);
        ac.send();
        ac.setPowerToggle(false);
        Serial.println("==========================================================");
        Serial.println("AC 1 Mati");
        display.clearDisplay();  // OLED not showing info
        display.display();
        togglePower = !togglePower;
      } else {
        Serial.println("==========================================================");
        Serial.println("AC 1 tidak menyala");
        togglePower = false;
      }
    } else {
      if (togglePower == 1) {
        ac.setPowerToggle(true);
        ac.send();
        ac.setPowerToggle(false);
        Serial.println("==========================================================");
        Serial.println("AC 2 Mati");
        display.clearDisplay();  // OLED not showing info
        display.display();
        togglePower = !togglePower;
      } else {
        Serial.println("==========================================================");
        Serial.println("AC 2 tidak menyala");
        togglePower = false;
      }
    }
  }
  Serial.println("==========================================================");
  Serial.print("Nilai togglePower: ");
  Serial.println(togglePower);
}

void onRecvCommandSink(uint8_t decodeSignalTemp, uint8_t decodeSignalHumd) {
  currentTemp = currentTemp;
  currentMode = currentMode;
  updateDisplay(currentTemp, currentMode);
  switch (decodeSignalTemp) {
    case roomOK:
      {
        // continue the conditions
        Serial.println("Room temp & humd are ok!");
        break;
      }
    case loweringTemp:
    case raisingTemp:
      {
        Serial.print("Room Temperature: ");
        Serial.println(msg.roomTemp);
        if (msg.roomTemp < 18 || (msg.roomTemp >= 25 && msg.roomTemp < 27)) {
          if (msg.roomTemp > currentTemp) {
            sendTemp = msg.roomTemp - currentTemp;
            if (sendTemp != 0 && !(round(sendTemp > 7))) {
              sendTemp = currentTemp - sendTemp;
              if (sendTemp <= 16 || sendTemp >= 25) {
                sendTemp = 16;
              }
              ac.setTemp(sendTemp);
              Serial.println("Lowering AC temp");
              Serial.println(sendTemp);
            } else if (sendTemp == 0) {
              Serial.println("Room Temperature & AC Temperature are the same");
            } else {
              sendTemp = 16;
              Serial.println("AC is broken!");
            }
          } else {
            sendTemp = currentTemp - msg.roomTemp;
            if (sendTemp != 0 && !(round(sendTemp > 7))) {
              sendTemp = currentTemp + abs(sendTemp);
              if (sendTemp >= 30) {
                sendTemp = 30;
              }
              ac.setTemp(sendTemp);
              Serial.println("Raising AC temp");
              Serial.println(sendTemp);
            } else if (sendTemp == 0) {
              Serial.println("Room Temperature & AC Temperature are the same");
            } else {
              sendTemp = 30;
              Serial.println("AC is broken!");
            }
          }
          currentTemp = sendTemp;
          ac.send();
        }
        break;
      }
    default:
      // Send command to call security with via IP-PBX for next feature
      Serial.println("Server room in danger!");
      currentTemp = 16;
      ac.setTemp(currentTemp);
      ac.setMode(kDaikin64Cool);
      ac.send();
      changeDisplayTemp(currentTemp);
      changeDisplayMode(kDaikin64Cool);
      Serial.println("Ac temp set to default: 16°C");
      Serial.println("Ac mode set to default: Cool");
      break;
  }

  switch (decodeSignalHumd) {
    case roomOK:
      {
        // continue the conditions
        Serial.println("Room humd is good!");
        break;
      }
    case modeCool:
      {
        ac.setMode(kDaikin64Cool);
        ac.send();
        currentMode = ac.getMode();
        changeDisplayMode(currentMode);
        Serial.println("Switching to cool mode");
        break;
      }
    case modeDry:
      {
        ac.setMode(kDaikin64Dry);
        ac.send();
        currentMode = ac.getMode();
        changeDisplayMode(currentMode);
        Serial.println("Switching to dry mode");
        break;
      }
    default:
      {
        // Send command to call security with WA/telegram API
        Serial.println("Server room in danger!");
        break;
      }
  }
  currentTemp = currentTemp;
  currentMode = currentMode;
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
  Serial.print("signalCode1Temp: ");
  Serial.println(msg.signalCode1Temp);
  Serial.print("signalCode1Humd: ");
  Serial.println(msg.signalCode1Humd);
  Serial.print("signalCode2Temp: ");
  Serial.println(msg.signalCode2Temp);
  Serial.print("signalCode2Humd: ");
  Serial.println(msg.signalCode2Humd);
  Serial.print("Room Temperature: ");
  Serial.println(msg.roomTemp);
  Serial.print("AC_Condition: ");
  Serial.println(msg.AC_Condition);
  Serial.println();
  dataReceived = true;
}