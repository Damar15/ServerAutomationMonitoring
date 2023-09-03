#include <WiFi.h>
#include <ThingSpeak.h>
#include <DHT.h>
#include <esp_wifi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <WiFiMulti.h>
#include <esp_now.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define DHT_TYPE DHT22
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define DHTPIN 27
#define BUZZPIN 4
#define OLED_MOSI 23
#define OLED_CLK 18
#define OLED_DC 16
#define OLED_CS 5
#define OLED_RESET 17

// 'temp', 24x24px
const unsigned char temp_icon[] PROGMEM = {
  0x00, 0x70, 0x00, 0x00, 0xd8, 0x00, 0x01, 0x8c, 0x00, 0x01, 0x84, 0xe0, 0x01, 0x84, 0x00, 0x01,
  0x84, 0x00, 0x01, 0xa4, 0xe0, 0x01, 0xa4, 0x00, 0x01, 0xa4, 0xc0, 0x01, 0xa4, 0xe0, 0x01, 0xa4,
  0x00, 0x01, 0xa4, 0xe0, 0x01, 0xa4, 0xe0, 0x01, 0xa4, 0x00, 0x03, 0x26, 0x00, 0x02, 0x22, 0x00,
  0x06, 0xfb, 0x00, 0x04, 0x89, 0x00, 0x04, 0x89, 0x00, 0x06, 0xd9, 0x00, 0x06, 0x73, 0x00, 0x03,
  0x06, 0x00, 0x01, 0xdc, 0x00, 0x00, 0xf8, 0x00
};


// 'humidity', 24x24px
const unsigned char humd_icon[] PROGMEM = {
  0x00, 0x18, 0x00, 0x00, 0x18, 0x00, 0x00, 0x3c, 0x00, 0x00, 0x66, 0x00, 0x00, 0xc3, 0x00, 0x00,
  0xc3, 0x00, 0x01, 0x81, 0x80, 0x03, 0x00, 0xc0, 0x02, 0x00, 0x40, 0x06, 0x60, 0x60, 0x0c, 0xf0,
  0x30, 0x08, 0xb3, 0x10, 0x18, 0xe7, 0x18, 0x18, 0x0e, 0x18, 0x18, 0x1c, 0x18, 0x18, 0x38, 0x18,
  0x18, 0x70, 0x18, 0x08, 0xe7, 0x10, 0x08, 0xcd, 0x10, 0x0c, 0x0f, 0x30, 0x06, 0x06, 0x60, 0x03,
  0x00, 0xc0, 0x01, 0xff, 0x80, 0x00, 0x3c, 0x00
};

// Array to store all bitmaps value
const int epd_bitmap_allArray_LEN = 2;
const unsigned char* epd_bitmap_allArray[2] = {
  humd_icon, temp_icon
};

String weekDays[7] = { "SUN", "MON", "TUE", "WED", "THUR", "FRI", "SAT" };

const char* ssid = "Zenix5";
const char* password = "pangandaran5";
const char* ntpserver = "pool.ntp.org";  // ntp server time sync
const long gmtOffset_sec = 25200;        //GMT +7 hours in sec
unsigned long myChannelNumber = 2259165;
const char* myWriteAPIKey = "F41XM1LOR6HTPAUD";

// Timer variables for sending data via ThingSpeak
unsigned long lastTime = 0;
// unsigned long timerDelay = 900000;  // send data every x sec e.g 15 minutes
unsigned long timerDelay = 60000;  // send data every x sec e.g 1 minutes
int currentHour = 0;               // for storing data from NTP server
int currentMinutes;

float humidity;  // store humdity sensor
float temp;      // store temp sensor

uint8_t sinkNode1[] = { 0x08, 0xB6, 0x1F, 0x3D, 0x23, 0xAC };
uint8_t sinkNode2[] = { 0x40, 0x22, 0xD8, 0x3C, 0x60, 0x54 };
// * signalCode refering to what command that sink node should give to AC
// * 1 --> lower down the temp of AC
// * 2 --> higher up the temp of AC
// * 3 --> change mode to cool
// * 4 --> change mode to dry
struct Message {
  uint8_t signalCode1Temp;
  uint8_t signalCode1Humd;
  uint8_t signalCode2Temp;
  uint8_t signalCode2Humd;
  bool AC_Condition;
  uint8_t roomTemp;
} msg{ 0, 0, 0, 0, false };

esp_now_peer_info_t peerInfo;

DHT dht(DHTPIN, DHT_TYPE);
WiFiClient client;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpserver);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);

uint8_t onSendCommand(uint8_t temp, float hum);
uint8_t offSendCommand();
bool conditionAC();
String get_wifi_status(int status);
void oledDisplay();
void systemInit();
void recheckConnection();
void OnDataSent(const uint8_t* mac_addr, esp_now_send_status_t status);
void switchingToESPNOW();
uint8_t changeModeCommand(float hum);
uint8_t changeTempCommand(uint8_t temp);

void setup() {
  pinMode(BUZZPIN, OUTPUT);
  ledcAttachPin(BUZZPIN, 0);
  systemInit();
  delay(1000);
}

void loop() {
  delay(1000);
  humidity = dht.readHumidity();
  temp = dht.readTemperature();

  if (isnan(humidity) || isnan(temp)) {
    Serial.println(F("Failed to read from DHT sensor!"));
    return;
  }
  oledDisplay();


  if ((millis() - lastTime) > timerDelay) {
    msg.AC_Condition = conditionAC();  // Recheck AC condition
    recheckConnection();               // connect or reconnect to WiFi

    oledDisplay();

    ThingSpeak.setField(1, temp);
    ThingSpeak.setField(2, humidity);
    int count = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);

    if (count == 200) {
      Serial.println("Channel update successful.");
    } else {
      Serial.println("Problem updating channel. HTTP error code " + String(count));
    }

    // sending data
    msg.roomTemp = temp;
    if (msg.AC_Condition == true) {
      msg.signalCode1Temp = changeTempCommand(msg.roomTemp);
      msg.signalCode1Humd = changeModeCommand(humidity);
      msg.signalCode2Temp = offSendCommand();
      msg.signalCode2Humd = offSendCommand();
    } else {
      msg.signalCode1Temp = offSendCommand();
      msg.signalCode1Humd = offSendCommand();
      msg.signalCode2Temp = changeTempCommand(msg.roomTemp);
      msg.signalCode2Humd = changeModeCommand(humidity);
    }

    switchingToESPNOW();

    esp_err_t result = esp_now_send(0, (uint8_t*)&msg, sizeof(Message));  // Sending message

    if (result == ESP_OK) {
      Serial.println("Sent with success");
    } else {
      Serial.println("Error sending the data");
    }
    lastTime = millis();
  }
}

void systemInit() {  // Initialization system OLED, ThingSpeak, DHT22 Sensor, TimeSync
  int status = WL_IDLE_STATUS;
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  gpio_set_level(GPIO_NUM_0, 0);

  if (!display.begin(SSD1306_SWITCHCAPVCC)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(12, 10);
  display.print("Connecting to ");
  display.println(ssid);
  display.display();

  Serial.print("\nConnecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    status = WiFi.status();
    display.clearDisplay();
    display.setCursor(12, 10);
    display.print(get_wifi_status(status));
    display.display();
    Serial.println(get_wifi_status(status));
  }
  Serial.println("\nConnected to the WiFi network");
  Serial.print("Local ESP32 IP: ");
  Serial.println(WiFi.localIP());


  timeClient.begin();                       // Init NTPClient
  timeClient.setTimeOffset(gmtOffset_sec);  // Set GMT +7 Jakarta (WIB)
  oledDisplay();
  ThingSpeak.begin(client);  // Initialize ThingSpeak
  dht.begin();               // Initialize dht22 sensor
}

void switchingToESPNOW() {  // Disconnecting WiFi and use radio WiFi to communicate between node via ESP-NOW
  // switching to ESP-NOW
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_send_cb(OnDataSent);

  // register peer
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  // peering each node
  memcpy(peerInfo.peer_addr, sinkNode1, 6);
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  memcpy(peerInfo.peer_addr, sinkNode2, 6);
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }
}

void OnDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {  // Get to know if data is successfully sent or not to which node
  char macStr[18];
  Serial.print("Packet to: ");
  // Copies the sender mac address to a string
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.print(macStr);
  Serial.print(" send status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

uint8_t offSendCommand() {  // Function to return 0 or do nothing to node
  return 0;
}

uint8_t changeModeCommand(float hum) {  // Function to give command to change Mode
  if (hum >= 40.00 && hum < 60.00) {
    // Continue nothing happen
    Serial.println("Server Room is good!");
    return 0;
  } else if (hum < 40.00) {
    // Change mode to cool
    Serial.println("Changing mode to cool");
    return 3;
  } else {
    // Change mode to dry
    Serial.println("Changing mode to dry");
    return 4;
  }
}

uint8_t changeTempCommand(uint8_t temp) {  // Function to give command to change temp or give an alert to the system
  delay(1000);
  if (temp >= 18 && temp < 25) {  // 25 default
    // Continue nothing happen
    Serial.println("Server temp is good!");
    noTone(BUZZPIN);
    return 0;
  } else if (temp < 18) {
    // Raising up the temp
    Serial.print("Current Temperature: ");
    Serial.println(temp);
    Serial.println("Raising the temp");
    noTone(BUZZPIN);
    return 2;
  } else if (temp >= 25 && temp < 27) {
    // Lower down the temp
    Serial.print("Current Temperature: ");
    Serial.println(temp);
    Serial.println("Lowering the temp");
    noTone(BUZZPIN);
    return 1;
  } else {
    // Turn on the buzzer
    Serial.println("Server room in danger!");
    tone(BUZZPIN, 50);
    delay(2000);
    tone(BUZZPIN, 10);
    return 5;
  }
}

bool conditionAC() {  // Function to determined which node should be controling the AC
  // * AC_Condition value based on time range
  // * True Sink Node 1 ON & Sink Node 2 OFF
  // * False Sink Node 1 OFF & Sink Node 2 ON
  timeClient.update();
  // currentHour = timeClient.getHours();
  currentHour = currentHour;
  // currentHour = 20;
  if (currentHour == 24) {
    currentHour = 0;
  }
  Serial.print("Simulasi jam: ");
  Serial.println(currentHour);
  if ((currentHour >= 0 && currentHour <= 5) || (currentHour >= 12 && currentHour <= 17)) {
    // checking if sink node 1 is up? if up then continue if not then power it up
    Serial.println("Menghidupkan Sink Node 1");
    currentHour += 1;
    return 1;
  } else {
    // checking process sink node 2 is up?
    Serial.println("Menghidupkan Sink Node 2");
    currentHour += 1;
    return 0;
  }

  // currentMinutes = timeClient.getMinutes();
  // currentMinutes = 43;


  //   if ((currentMinutes > 0 && currentMinutes <= 15) || (currentMinutes >= 30 && currentMinutes < 45)) {
  //   Serial.println("Menghidupkan Sink Node 1");
  //   return 1;
  // } else {
  //   Serial.println("Menghidupkan Sink Node 2");
  //   return 0;
  // }
}

String get_wifi_status(int status) {  // Get to know the current WiFi status for debugging
  switch (status) {
    case WL_IDLE_STATUS:
      return "WL_IDLE_STATUS";
    case WL_SCAN_COMPLETED:
      return "WL_SCAN_COMPLETED";
    case WL_NO_SSID_AVAIL:
      return "WL_NO_SSID_AVAIL";
    case WL_CONNECT_FAILED:
      return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST:
      return "WL_CONNECTION_LOST";
    case WL_CONNECTED:
      return "WL_CONNECTED";
    case WL_DISCONNECTED:
      return "WL_DISCONNECTED";
  }
}

void recheckConnection() {  // Function to make sure the WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print(F("Attempting to connect "));
    Serial.println(ssid);
    while (WiFi.status() != WL_CONNECTED) {
      WiFi.begin(ssid, password);
      delay(5000);
    }
    Serial.println(F("\nConnected"));
  }
}

void oledDisplay() {  //Printing the template of the OLED display
  timeClient.update();
  String formattedTime = timeClient.getFormattedTime();
  String weekDay = weekDays[timeClient.getDay()];

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(weekDay);
  display.setCursor(95, 0);
  display.print(formattedTime.substring(0, 5));
  display.setTextSize(1);
  display.setCursor(46, 25);
  display.print(temp);
  display.print(" ");
  display.print((char)247);
  display.print("C");
  display.setCursor(46, 50);
  display.print(humidity);
  display.print(" %");

  display.drawBitmap(18, 15, temp_icon, 24, 24, SSD1306_WHITE);
  display.drawBitmap(18, 40, humd_icon, 24, 24, SSD1306_WHITE);
  display.display();
  delay(1000);
}
