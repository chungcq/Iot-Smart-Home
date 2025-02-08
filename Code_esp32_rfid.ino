#include <WiFi.h>
#include <MFRC522.h> //library responsible for communicating with the module RFID-RC522
#include <SPI.h> //library responsible for communicating of SPI bus
#include <EEPROM.h>
#include <FirebaseESP32.h>
#include <PubSubClient.h>
#include "ArduinoJson.h" // Library for processing JSON data
#include <WebServer.h>
#include <EEPROM.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <ESP32Servo.h>
//#include <freertos/FreeRTOS.h>
//#include <freertos/task.h>

#define EEPROM_SIZE 64 // Dung lu?ng EEPROM (tùy ch?nh theo nhu c?u)
#define SSID_ADDR 300     // V? trí luu SSID (b?t d?u t? d?a ch? 0)
#define PASS_ADDR 332    // V? trí luu Password (b?t d?u t? d?a ch? 32)
#define HOMEID_ADDR 364
#define RFID_LED_PIN 26
#define SS_PIN 5
#define RST_PIN 2
#define ADD_NEW_CARD 1
#define DELETE_CARD 2
#define CHECK_CARD 3
// Firebase project credentials
#define FIREBASE_HOST "https://testhome-3eab0-default-rtdb.firebaseio.com" 
#define FIREBASE_AUTH "FW6tRaUJm4QDKUkYPS0AhJTPcK6tEW8yyQB4e9Kv"
#define MQTT_SERVER "broker.hivemq.com"
#define MQTT_PORT 1883

#define ADD_RFID 1
#define CHECK_RFID 2
#define LED_PIN 12
#define BUTTON_LED 14
#define BUTTON_FAN 35
int motor1Pin1 = 33;
int motor1Pin2 = 25;
int enable1Pin = 32;
bool motorRunning = false;
// Setting PWM properties
const int freq = 30000;
const int pwmChannel = 0;
const int resolution = 8;
int dutyCycle = 200;

const char* ap_ssid = "ESP32-AP";    // Tên Access Point
const char* ap_password = "12345678"; // M?t kh?u Access Point
int button_state;
int last_button_state;
int led_state = LOW;
int button_fan_state = 0;
int last_button_fan_state = 1;
int fan_state = LOW;
unsigned long lastTimeButtonStateChanged = millis();
unsigned long debounceDuration = 50; //milis()
int ledCount = 0;
unsigned long UID[4];
unsigned long i;
int diachi = 1;
int numberOfCard;
int id_moi[4]; // Card m?i 
int id_eeprom[4];
String NameCard;
int RFID_mode = CHECK_RFID;
String homeID;
int posDegrees = 0;

WebServer server(80);
FirebaseConfig config;  // Ð?i tu?ng c?u hình Firebase
FirebaseAuth auth;      // Ð?i tu?ng xác th?c Firebase
FirebaseData firebaseData; // Bi?n Firebase d? th?c hi?n giao ti?p
WiFiClient wifiClient;
PubSubClient client(wifiClient);

// Khai báo NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7 * 3600, 60000);  // GMT+7, c?p nh?t m?i 60s

MFRC522 mfrc522(SS_PIN, RST_PIN);


// Hàm d?c chu?i t? EEPROM
String readStringFromEEPROM(int startAddr, int maxLength) {
    String data = "";
    for (int i = 0; i < maxLength; i++) {
        char c = EEPROM.read(startAddr + i);
        if (c == '\0') break;
        data += c;
    }
    return data;
}

// Hàm ghi chu?i vào EEPROM
void writeStringToEEPROM(int startAddr, const String& data) {
    for (int i = 0; i < data.length(); i++) {
        EEPROM.write(startAddr + i, data[i]);
    }
    EEPROM.write(startAddr + data.length(), '\0'); // Ký t? k?t thúc chu?i
    EEPROM.commit();
}

// Hàm x? lý form nh?p Wi-Fi
void handleRoot() {
    const char* htmlForm = R"rawliteral(
        <!DOCTYPE html>
        <html>
        <head>
            <title>Wi-Fi Config</title>
        </head>
        <body>
            <h1>Nhap thong tin Wi-Fi</h1>
            <form action="/connect" method="POST">
                SSID: <input type="text" name="ssid"><br>
                Password: <input type="password" name="password"><br>
                HomeID: <input type="text" name = "HomeID"><br>
                <input type="submit" value="Submit">
            </form>
        </body>
        </html>
    )rawliteral";
    server.send(200, "text/html", htmlForm);
}

// Hàm x? lý k?t n?i Wi-Fi
void handleConnect() {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    homeID = server.arg("HomeID");
    
    server.send(200, "text/plain", "Ðang k?t n?i Wi-Fi...");

    // Luu thông tin Wi-Fi vào EEPROM
    writeStringToEEPROM(SSID_ADDR, ssid);
    writeStringToEEPROM(PASS_ADDR, password);
    writeStringToEEPROM(HOMEID_ADDR, homeID);
    // Ng?t Access Point
    WiFi.softAPdisconnect(true);

    // K?t n?i vào Wi-Fi
    WiFi.begin(ssid.c_str(), password.c_str());
    Serial.println("Ðang k?t n?i Wi-Fi...");

    unsigned long timeout = millis();
    const unsigned long maxTimeout = 10000; // 10 giây

    while (WiFi.status() != WL_CONNECTED && millis() - timeout < maxTimeout) {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        connectToFirebase_MQTT();
        Serial.println("\nK?t n?i thành công!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
        server.send(200, "text/plain", "K?t n?i thành công! Ð?a ch? IP: " + WiFi.localIP().toString());
        // B?t d?u NTP
        timeClient.begin();
    } else {
        Serial.println("\nK?t n?i th?t b?i!");
        server.send(200, "text/plain", "K?t n?i Wi-Fi th?t b?i! Vui lòng th? l?i.");
        WiFi.softAP(ap_ssid, ap_password); // T?o l?i Access Point n?u th?t b?i
    }
}

void connect_to_broker() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32";
    clientId += String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      String RFID_add_topic = homeID+"/RFID/add"; 
      String RFID_delete_topic = homeID+"/RFID/delete"; 
      String led_topic = homeID + "/out/led";
      String fan_topic = homeID + "/out/fan";
      client.subscribe(RFID_add_topic.c_str());
      client.subscribe(RFID_delete_topic.c_str());
      client.subscribe(led_topic.c_str());
      client.subscribe(fan_topic.c_str());
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 2 seconds");
      delay(2000);
    }
  }
}

// K?t n?i Firebase
void connectToFirebase_MQTT() {
    // C?u hình Firebase
  config.host = FIREBASE_HOST; // URL Firebase c?a b?n
  config.signer.tokens.legacy_token = FIREBASE_AUTH;

  Firebase.begin(&config, &auth); // Kh?i t?o Firebase v?i c?u hình và xác th?c
  Firebase.reconnectWiFi(true);

  Serial.println("Firebase connected.");

  //connecting to a mqtt broker
  client.setServer(MQTT_SERVER, MQTT_PORT); // Configure MQTT Broker
  client.setCallback(callback); // Set callback function to handle MQTT messages
  connect_to_broker();
}

// Hàm k?t n?i Wi-Fi t? thông tin trong EEPROM
void connectWiFiFromEEPROM() {
    String ssid = readStringFromEEPROM(SSID_ADDR, 32);
    String password = readStringFromEEPROM(PASS_ADDR, 32);
    homeID = readStringFromEEPROM(HOMEID_ADDR, 32);

    if (ssid.isEmpty() || password.isEmpty()) {
        Serial.println("Không có thông tin Wi-Fi trong EEPROM.");
        return;
    }

    Serial.println("Ðang k?t n?i v?i Wi-Fi t? EEPROM...");
    WiFi.begin(ssid.c_str(), password.c_str());

    unsigned long timeout = millis();
    const unsigned long maxTimeout = 10000; // 10 giây

    while (WiFi.status() != WL_CONNECTED && millis() - timeout < maxTimeout) {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        connectToFirebase_MQTT();
        Serial.println("\nK?t n?i thành công!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
        // B?t d?u NTP
        timeClient.begin();
    } else {
        Serial.println("\nK?t n?i th?t b?i!");
    }
}

// Function to handle MQTT messages
void callback(char* topic, byte* message, unsigned int length) {
  Serial.println("Message arrived on topic: ");
  Serial.println(topic);
  Serial.println("Message: ");
  String stMessage;
  for (int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
    stMessage += (char)message[i];
  }
  if (String(topic) == (homeID + "/RFID/delete")) {
    Serial.println("delete:dsflkdf");
    StaticJsonDocument<200> doc;
    deserializeJson(doc, stMessage);
    String value = doc["Card_ID"];
    // L?y tru?ng Card_ID
    const char* cardID = doc["Card_ID"];
    Serial.print("Card_ID: ");
    Serial.println(cardID);
  
    // Tách các s? nguyên
    int numbers[10]; // M?ng luu các s? nguyên
    int index = 0;
    char* token = strtok((char*)cardID, "_");
    while (token != NULL) {
      numbers[index++] = atoi(token); // Chuy?n chu?i sang s? nguyên
      token = strtok(NULL, "_");
    }
  
    // In các s? nguyên
    Serial.println("Extracted numbers:");
    for (int i = 0; i < index; i++) {
      Serial.println(numbers[i]);
    }
    deleteCard(numbers);
   } else if ( String(topic) == homeID + "/RFID/add" ){
      Serial.println("addddddddddddddd:dsflkdf");
      RFID_mode = ADD_RFID;
      StaticJsonDocument<200> doc;
      deserializeJson(doc, stMessage);
      String Name = doc["Name"];
      NameCard = Name;
   } else if ( String(topic) == homeID + "/out/led") {
      StaticJsonDocument<200> doc;
      deserializeJson(doc, stMessage);
      if (doc.containsKey("led1")) {
        int temp = doc["led1"];
        int pwmLed = (int)((float)temp * 255.0 / 100.0);
        analogWrite(LED_PIN, temp);
      }
      if (doc.containsKey("fan1")) {
        int fanStatus = doc["fan1"];
        if (fanStatus == 1) {
          Serial.println("turn on the fan");
          digitalWrite(motor1Pin1, LOW);
          digitalWrite(motor1Pin2, HIGH);
          ledcWrite(enable1Pin, dutyCycle);
        } else if (fanStatus == 0) {
          Serial.println("turn off the fan");
          digitalWrite(motor1Pin1, LOW);
          digitalWrite(motor1Pin2, LOW);
        }
      }
   }
}

void uploadCardToFirebase(int id[]) {
    String path = "home/" + homeID + "/rfid/rfid" + String(id[0]) + "_" + String(id[1]) + "_" + String(id[2]) + "_" + String(id[3]);
//    String path = "/RFID_Cards/Card_" + String(id[0]) + "_" + String(id[1]) + "_" + String(id[2]) + "_" + String(id[3]) ;
    String cardID = String(id[0]) + "_" + String(id[1]) + "_" + String(id[2]) + "_" + String(id[3]);
    if (Firebase.setString(firebaseData, path + "/id", cardID)
        && Firebase.setString(firebaseData, path + "/name", NameCard)) {
        DynamicJsonDocument doc(1024);
        doc["card"] = 2;
        char buffer[256];
        serializeJson(doc, buffer);
        String topic = homeID + "/out/sub";
        client.publish(topic.c_str(), buffer);
        Serial.println("Card uploaded to Firebase successfully!");
    } else {
        Serial.print("Failed to upload card: ");
        Serial.println(firebaseData.errorReason());
    }
}

int findEmptyMemory () {
  numberOfCard = EEPROM.read(0); //d?c ô nh? 0 xem dã s? d?ng bao nhiêu ô nh?
  Serial.print("Card Number findEmptyMemory: "); 
  Serial.println(numberOfCard); 
  for ( int i = 1 ; i <= numberOfCard*4+1 ; i+=4) {
    for ( int j = 0; j < 4; j+=1 ) {
      id_eeprom[j] = EEPROM.read(i+j);
    }
    if ( id_eeprom[0] == 0 && id_eeprom[1] == 0 && id_eeprom[2] == 0 && id_eeprom[3] == 0 )
    {
      return i;
    }
  }
  return numberOfCard * 4 +1;
}

bool checkCard() {
  numberOfCard = EEPROM.read(0); // Ð?c s? th? dã luu
  Serial.print("Card Number numberOfCard: "); 
  Serial.println(numberOfCard); 
  for (int i = 1; i <= numberOfCard * 4+1; i += 4) {
    Serial.println(i); 
    for (int j = 0; j < 4; j++) {
      id_eeprom[j] = EEPROM.read(i + j);
      Serial.print("id_eeprom[j]"); 
      Serial.println(id_eeprom[j]); 
    }
    // So sánh th? m?i v?i th? trong EEPROM
    if (id_moi[0] == id_eeprom[0] && id_moi[1] == id_eeprom[1] &&
        id_moi[2] == id_eeprom[2] && id_moi[3] == id_eeprom[3]) {
      return true; // Th? dã t?n t?i
    }
  }
  return false; // Th? không t?n t?i
  
}

void addNewCard() {
  numberOfCard = EEPROM.read(0); //d?c ô nh? 0 xem dã s? d?ng bao nhiêu ô nh?
  //Serial.print("a: "); Serial.println(a); 
  if ( ! mfrc522.PICC_IsNewCardPresent()) 
  { return; } 
  if ( ! mfrc522.PICC_ReadCardSerial()) 
  { return; }  
  for (byte i = 0; i < 4; i++)  //Quét th? m?i
  {          
    UID[i] = mfrc522.uid.uidByte[i];
    id_moi[i] = UID[i];
  }
  mfrc522.PICC_HaltA(); 
  mfrc522.PCD_StopCrypto1(); 
  if ( !checkCard() ) { // Neu chua có the
    int n = findEmptyMemory();
    Serial.print("n: "); 
    Serial.println(n); 
    for (int i = 0; i < 4; i++)
    {
      EEPROM.write(n+i, id_moi[i]);
    }
    EEPROM.commit();
    delay(50);
    if ( n == numberOfCard * 4 + 1) {
      numberOfCard = numberOfCard + 1;
      Serial.print("Card Number: "); 
      Serial.println(numberOfCard); 
      EEPROM.write(0, numberOfCard); //Sau khi luu 1 th? m?i vào thì c?p nh?t s? ô nh? dã s? d?ng vào ô 0
      EEPROM.commit();
      delay(50); 
      Serial.print("Card Number EEPROM: "); 
      Serial.println(EEPROM.read(0)); 
    }
    uploadCardToFirebase(id_moi);
    Serial.println("   DANG LUU...  "); 
    delay(1000);     
  }
  else {
    Serial.println("   The da ton tai  ");
  }
  RFID_mode = CHECK_RFID;
}

void deleteCardFirebase(int id[]) {
  String path = "home/" + homeID + "/rfid"; // Đường dẫn đến mảng dữ liệu

  if (Firebase.getJSON(firebaseData, path)) {
    if (firebaseData.dataType() == "json") {
      // Lấy dữ liệu JSON dưới dạng chuỗi
      String jsonStr = firebaseData.jsonString();
      Serial.println("Received JSON Data: ");
      Serial.println(jsonStr);

      // Giải mã JSON
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, jsonStr);
      // Chuyển doc thành JsonObject để duyệt
      JsonObject rfidObject = doc.as<JsonObject>();
      
      // Duyệt qua từng ID RFID (ví dụ: "195_54_240_26", "11", "S")
      for (JsonPair kv : rfidObject) {
        String rfidKey = kv.key().c_str();  // Lấy tên RFID key (ID)
        JsonObject rfidData = kv.value().as<JsonObject>();  // Dữ liệu cho mỗi RFID

        String cardID = rfidData["id"].as<String>();
        Serial.print("CardID:");
        Serial.println(cardID);
        String nameScan = rfidData["name"].as<String>();  // Lấy Name
        Serial.print("Name:");
        Serial.println(nameScan);
        char cardIDBuffer[50];  // Tạo mảng ký tự để chứa ID
        cardID.toCharArray(cardIDBuffer, sizeof(cardIDBuffer));

        // Tách các s? nguyên
        int numbers[10]; // M?ng luu các s? nguyên
        int index = 0;
        char* token = strtok(cardIDBuffer, "_");
        while (token != NULL) {
          numbers[index++] = atoi(token); // Chuy?n chu?i sang s? nguyên
          token = strtok(NULL, "_");
        }

        if (id[0] == numbers[0] && id[1] == numbers[1] &&
        id[2] == numbers[2] && id[3] == numbers[3]) {
          if (Firebase.deleteNode(firebaseData, path + "/" + rfidKey)) {
            DynamicJsonDocument doc(1024);
            doc["card"] = 1;
            char buffer[256];
            serializeJson(doc, buffer);
            String topic = homeID + "/out/sub";
            client.publish(topic.c_str(), buffer);
            Serial.println("Node deleted successfully!");
          } else {
            Serial.print("Failed to delete node: ");
            Serial.println(firebaseData.errorReason());
          }
          return;
        }
      }
    }
  }
}

void deleteCard(int id[]) {
  numberOfCard = EEPROM.read(0); // Ð?c s? th? dã luu
  Serial.print("card delete");
  Serial.println(id[0]);
  Serial.println(id[1]);
  Serial.println(id[2]);
  Serial.println(id[3]);
  for (int i = 1; i <= numberOfCard * 4+1; i += 4) {
    for (int j = 0; j < 4; j++) {
      id_eeprom[j] = EEPROM.read(i + j);
      Serial.println(id_eeprom[j]);
    }
    Serial.println("-----------");
    // So sánh th? m?i v?i th? trong EEPROM
    if (id[0] == id_eeprom[0] && id[1] == id_eeprom[1] &&
        id[2] == id_eeprom[2] && id[3] == id_eeprom[3]) {
        EEPROM.write(i, 0);
        EEPROM.write(i+1, 0);
        EEPROM.write(i+2, 0);
        EEPROM.write(i+3, 0);
        EEPROM.commit();
        deleteCardFirebase(id);
        delay(50); 
        Serial.println("delete successfully");
        return;
    }
  }
}

void WiFiTask(void *parameter) {
  server.handleClient();
}

void RFIDTask( void *parameter ) {
  if (RFID_mode == ADD_RFID ){
    addNewCard();
  } else if ( RFID_mode == CHECK_RFID ){
    if ( ! mfrc522.PICC_IsNewCardPresent()) 
    { return; } 
    if ( ! mfrc522.PICC_ReadCardSerial()) 
    { return; }  
    for (byte i = 0; i < 4; i++)  //Quét th? m?i
    {          
      UID[i] = mfrc522.uid.uidByte[i];
      id_moi[i] = UID[i];
    }
    if( WiFi.status() == WL_CONNECTED) {
      String currentTime = timeClient.getFormattedTime();
      uploadTimeScanRFID(currentTime);
    }
    mfrc522.PICC_HaltA(); 
    mfrc522.PCD_StopCrypto1(); 
    if ( checkCard() ) {
      Serial.println("   The da ton tai  ");
    } else {
      Serial.println("   the khong ton tai  ");
    }
  }
}

String checkRFIDScan(int id[]) {
  String path = "home/" + homeID + "/rfid"; // Đường dẫn đến mảng dữ liệu

  if (Firebase.getJSON(firebaseData, path)) {
    if (firebaseData.dataType() == "json") {
      // Lấy dữ liệu JSON dưới dạng chuỗi
      String jsonStr = firebaseData.jsonString();
      Serial.println("Received JSON Data: ");
      Serial.println(jsonStr);

      // Giải mã JSON
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, jsonStr);
      // Chuyển doc thành JsonObject để duyệt
      JsonObject rfidObject = doc.as<JsonObject>();
      
      // Duyệt qua từng ID RFID (ví dụ: "195_54_240_26", "11", "S")
      for (JsonPair kv : rfidObject) {
        String rfidKey = kv.key().c_str();  // Lấy tên RFID key (ID)
        JsonObject rfidData = kv.value().as<JsonObject>();  // Dữ liệu cho mỗi RFID

        String cardID = rfidData["id"].as<String>();
        Serial.print("CardID:");
        Serial.println(cardID);
        String nameScan = rfidData["name"].as<String>();  // Lấy Name
        Serial.print("Name:");
        Serial.println(nameScan);
        char cardIDBuffer[50];  // Tạo mảng ký tự để chứa ID
        cardID.toCharArray(cardIDBuffer, sizeof(cardIDBuffer));

        // Tách các s? nguyên
        int numbers[10]; // M?ng luu các s? nguyên
        int index = 0;
        char* token = strtok(cardIDBuffer, "_");
        while (token != NULL) {
          numbers[index++] = atoi(token); // Chuy?n chu?i sang s? nguyên
          token = strtok(NULL, "_");
        }

        if (id[0] == numbers[0] && id[1] == numbers[1] &&
        id[2] == numbers[2] && id[3] == numbers[3]) {
          return nameScan;
        }
      }
    }
  }
  return "";
}

void uploadTimeScanRFID(String timeScan){
  String path = "home/" + homeID + "/RFIDScan/";
  String namePerson = checkRFIDScan(id_moi);
  if (Firebase.setString(firebaseData, path + timeScan + "/name" , namePerson)) {
      Serial.println("Card uploaded to Firebase successfully!");
  } else {
      Serial.print("Failed to upload card: ");
      Serial.println(firebaseData.errorReason());
  }
}

String getFormattedDate() {
  time_t rawTime = timeClient.getEpochTime();
  struct tm *timeInfo = localtime(&rawTime);

  char formattedDate[11];  // "dd/MM/yyyy" -> 10 ký tự + '\0'
  sprintf(formattedDate, "%02d-%02d-%04d", timeInfo->tm_mday, timeInfo->tm_mon + 1, timeInfo->tm_year + 1900);

  return String(formattedDate);
} 

void setup() {
  Serial.begin(115200);  
  EEPROM.begin(512);  // Kh?i t?o EEPROM v?i 512 bytes
  SPI.begin();    
  mfrc522.PCD_Init();
  pinMode(LED_PIN, OUTPUT);
  pinMode(RFID_LED_PIN, OUTPUT);
  pinMode(motor1Pin1, OUTPUT);
  pinMode(motor1Pin2, OUTPUT);
  pinMode(enable1Pin, OUTPUT);

  ledcAttachChannel(enable1Pin, freq, resolution, pwmChannel);
  pinMode(BUTTON_LED, INPUT_PULLUP);
  pinMode(BUTTON_FAN, INPUT_PULLUP);
  last_button_state = digitalRead(BUTTON_LED);
  last_button_fan_state = digitalRead(BUTTON_FAN);
  
  Serial.println("Approach your reader card...");
  Serial.println();
  connectWiFiFromEEPROM();
  
  if (WiFi.status() != WL_CONNECTED) {
      // Chuy?n sang ch? d? Access Point
      WiFi.softAP(ap_ssid, ap_password);
      Serial.println("ESP32 dang ? ch? d? Access Point");
      Serial.print("SSID: ");
      Serial.println(ap_ssid);

      // Ð?t d?a ch? IP m?c d?nh cho Access Point
      IPAddress IP(192, 168, 4, 1);
      IPAddress NMask(255, 255, 255, 0);
      WiFi.softAPConfig(IP, IP, NMask);

      // B?t d?u WebServer
      server.on("/", handleRoot);
      server.on("/connect", HTTP_POST, handleConnect);
      server.begin();
      Serial.println("WebServer dã kh?i d?ng!");
  }
  
  timeClient.begin();
}

void loop() {
  server.handleClient();
  // Cập nhật thời gian thực
  timeClient.update();
  if (WiFi.status() == WL_CONNECTED){
    client.loop();
    if (!client.connected()) {
      connect_to_broker();
    }
  }
  if((millis() - lastTimeButtonStateChanged) >= debounceDuration){
    int button_state = digitalRead(BUTTON_LED); // read new state
    delay(0);
    if(last_button_state!=button_state){
      lastTimeButtonStateChanged=millis();
      last_button_state = button_state;
      if (ledCount < 4) {
        ledCount ++;
      } else {
        ledCount = 0;
      }
      int pwmLed = 255/4*ledCount;
      DynamicJsonDocument doc(1024);
      float msg = ledCount * 25/100;
      doc["led1"] = msg;
      Serial.print("leddfffffffffffff");
      Serial.println(msg);
      char buffer[256];
      serializeJson(doc, buffer);
      String topic = homeID + "/out/sub";
      client.publish(topic.c_str(), buffer);
      analogWrite(LED_PIN, pwmLed);
    }
  }

  if (RFID_mode == ADD_RFID ){
    addNewCard();
  } else if ( RFID_mode == CHECK_RFID ){
    if ( ! mfrc522.PICC_IsNewCardPresent()) 
    { return; } 
    if ( ! mfrc522.PICC_ReadCardSerial()) 
    { return; }  
    for (byte i = 0; i < 4; i++)  //Quét th? m?i
    {          
      UID[i] = mfrc522.uid.uidByte[i];
      id_moi[i] = UID[i];
    }
    mfrc522.PICC_HaltA(); 
    mfrc522.PCD_StopCrypto1(); 
    if ( checkCard() ) {
      digitalWrite(RFID_LED_PIN, 1);
      if( WiFi.status() == WL_CONNECTED) {
      String currentTime = timeClient.getFormattedTime();
      String currentDate = getFormattedDate();  
      String timeScan = currentDate + "-" + currentTime;
      uploadTimeScanRFID(timeScan);
    }
      Serial.println("   The da ton tai  ");
      delay(3000);
      digitalWrite(RFID_LED_PIN, 0);
    } else {
      Serial.println("   the khong ton tai  ");
    }
  }
  delay(2000);
}
