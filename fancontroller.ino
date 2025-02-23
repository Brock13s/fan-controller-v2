#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <RCSwitch.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <map>
#include <LittleFS.h>
#include "esp_ota_ops.h"
#include "HtmlContent.h"

// ------------------------- WiFi & NTP Configuration -------------------------
String ssid = "BrockNet";
String password = "*****";

uint16_t utcOffsetInSeconds = 36000; 
uint32_t ntpUpdateMs = 43200000; //Every 12 hours
// const float versionNumber = 1.9; //

// IPAddress local_IP(192, 168, 1, 159);    // Desired static IP address
// IPAddress gateway(192, 168, 1, 1);         // Your network gateway
// IPAddress subnet(255, 255, 255, 0);        // Subnet mask
// IPAddress primaryDNS(192, 168, 1, 1);          // Primary DNS (optional)
// IPAddress secondaryDNS(8, 8, 4, 4);        // Secondary DNS (optional)

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "au.pool.ntp.org", utcOffsetInSeconds, ntpUpdateMs);  // Update every 60 sec

#define FAN_HIGH "000001011111"
#define FAN_MED "000001101111"
#define FAN_LOW "000001110111"
#define FAN_OFF "000001111101"
#define CHANGE_LIGHT_COLOR "000001110110"
#define FANLIGHT_ON_OFF "000001111110"

#define GREEN 0x0
#define RED 0x1

// ------------------------- RF Transmitter Configuration -------------------------
#define SHIFT_DATA_PIN 25   // DS of the shift register
#define SHIFT_CLOCK_PIN 19  // Clock of the shift register
#define SHIFT_LATCH_PIN 21  // Latch of the shift register
#define LED_BARGRAPH_LED_9 27 //Led 9 and 10 are connected directly to esp32 since the shift register is 8 bit only
#define LED_BARGRAPH_LED_10 14

#define TRANSMIT_STATUS 2
#define TX_PIN 23 // FS1000A transmitter connected to GPIO23
RCSwitch mySwitch = RCSwitch();

// ------------------------- Connectivity Indicator -------------------------
#define NTP_STATUS_LIGHT 17  // This pin is set HIGH when a valid NTP time is available

// ------------------------- DS18B20 Temperature Sensor Configuration -------------------------
#define ONE_WIRE_PIN 32  // DS18B20 data line is connected to GPIO34
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);
unsigned long lastNtpCheckMillis = 0;

unsigned long lastAnimationMillis = 0;
const unsigned long animationInterval = 500;  // Update animation every 500 milliseconds
int animationLEDCount = 1;  // Current LED position (1 to 10)


String fanOnTime = "18:00:00";  // Fan turns ON at 18:00:00
String fanOffTime = "07:00:00"; // Fan turns OFF at 07:00:00
// ------------------------- Fan Control Settings -------------------------
bool fanIsOnTemp = false; //Flags to stop repeatedly transmitting if temp is in range or time
bool fanIsOnTime = false;
bool automaticFanControl = true;

// Global variables to track fan state and time-based command execution
float FAN_ON_TEMP = 30.0;
float FAN_OFF_TEMP = FAN_ON_TEMP -2; //Delta 2c difference
float barGraphRange[2] = {15,30}; //Map the bargraph going from 15c to 30c order is important
unsigned long lastScheduledChange = 0;
const unsigned long scheduledChange = 60000; //Timer for How long to wait before applying temperature after time elapsed

String serialLog = ""; //Buffers for serial webpage and websocket Buffer. Websocket buffer helps to ensure data is sent immedialy in webpage
String wsBuffer = "";
// int lastFanOffDay = -1;  // To ensure the fan OFF command (07:00) is sent only once per day
// int lastFanOnDay  = -1;  // To ensure the fan ON command (18:00) is sent only once per day

AsyncWebServer server(80); //Define websocket and webpage
AsyncWebSocket ws("/ws");

//This class mirrors whatever we serial.print or whatever to the web serial console
String epochTimeToDateTime(unsigned long epoch){
  unsigned long JDN = epoch / 86400UL+2440588UL;
  // This converts the epoch time to an integer Julian Day Number.

  // Use the Fliegel-Van Flandern algorithm to convert JDN to Gregorian date.
  long L = JDN + 68569;
  long N = (4*L)/146097;
  L = L - (146097 * N + 3)/4;
  long I = (4000 *(L+1)) / 1461001;
  L = L - (1461*I)/4+31;
  long J = (80 * L) / 2447;
  int day = L - (2447 * J) / 80;
  L = J / 11;
  int month = J + 2 - 12 * L;
  int year = 100 * (N - 49) + I + L;

  unsigned long secondsInDay = epoch % 86400UL;
  int hour = secondsInDay / 3600;
  int minute = (secondsInDay % 3600) / 60;
  int second = secondsInDay % 60;

  char buf[30];
  sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d", year, month, day, hour, minute, second);
  return String(buf);
}

class MirrorSerial : public Stream{
  public:
    HardwareSerial* _serial;
    bool atLineStart;
    MirrorSerial(HardwareSerial* serial) : _serial(serial), atLineStart(true){}
    void begin(unsigned long baud, uint32_t config = SERIAL_8N1, int8_t rxPin = -1, int8_t txPin =-1, bool invert = false){
      _serial->begin(baud,config,rxPin,txPin,invert);
    }

    int available(){
      return _serial->available();
    }

    int read(){
      return _serial->read();
    }

    int peek(){
      return _serial->peek();
    }

    void flush(){
      _serial->flush();
    }

    String getTimestamp(){
      unsigned long epochTime = timeClient.getEpochTime();
      return epochTimeToDateTime(epochTime) + " ";
    }

    virtual size_t write(uint8_t c) override {
      size_t n = 0;
      if(atLineStart){
        String ts = getTimestamp();
        n += _serial->write((const uint8_t*)ts.c_str(), ts.length());
        serialLog += ts;
        wsBuffer += ts;
        atLineStart = false;
      }
      n += _serial->write(c);
      serialLog += (char)c;
      wsBuffer += (char) c;
      if(c == '\n') atLineStart = true;
      return n;
    }

    virtual size_t write(const uint8_t *buffer, size_t size) override {
      size_t total = 0;
      for(size_t i = 0; i < size; i++){
        // char ch = (char)buffer[i];
        // serialLog += ch;
        // wsBuffer += ch;
        total += write(buffer[i]);

      }
      // return _serial->write(buffer, size);
      return total;
    }
};

HardwareSerial &RealSerial = Serial;
MirrorSerial Mirror(&RealSerial);
#define Serial Mirror

// ----------------------------------------------------------------------
// Function: checkAndControlFanByTime
// Description: Uses the current NTP time to send scheduled commands.
//   - At 07:00 (within the first 10 seconds) turn the fan OFF.
//   - At 18:00 (within the first 10 seconds) turn the fan ON.
// Each command is sent only once per day using the day-of-year value.
// ----------------------------------------------------------------------


std::map<AsyncWebSocketClient*, IPAddress> clientIPMap;

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len){
  if(type==WS_EVT_CONNECT){
    IPAddress clientIP = client->remoteIP();
    Serial.println("Websocket client connected from IP: " + clientIP.toString());
    client->text(serialLog);
    clientIPMap[client] = clientIP;
  }
  else if(type==WS_EVT_DATA){
    String cmd = "";
    for(size_t i=0;i<len;i++){
      cmd += (char)data[i];
    }
    if(cmd.equals("ping")){
      client->text("pong");
    } else {
      handleCommands(cmd);
    }
    
  }
  else if(type==WS_EVT_DISCONNECT){
    if(clientIPMap.find(client) != clientIPMap.end()){
      Serial.println("Websocket client disconnected from IP: " + clientIPMap[client].toString());
      clientIPMap.erase(client);
    } else{
      Serial.println("Websocket client disconnected (IP UNKNOWN)");
    }
    
  }
}

void initWebSocket(){
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
}

void transmitFanCode(const char* code) {
  digitalWrite(TRANSMIT_STATUS, HIGH);
  mySwitch.send(code);
  digitalWrite(TX_PIN, LOW);
  digitalWrite(TRANSMIT_STATUS, LOW);
}

float getAveragedTemperature(bool reinitialize = false){
  static const int numSamples = 30;
  static float tempSamples[numSamples] = {0};
  static int sampleIndex = 0;
  static bool flag = false;
  static bool bufferFull = false;
  if(reinitialize){
    sampleIndex = 0;
    bufferFull = false;
  }
  sensors.requestTemperatures();
  float currentTemp = sensors.getTempCByIndex(0);
  if(currentTemp == DEVICE_DISCONNECTED_C){
    if(!flag){
      Serial.println("ERROR: Temperature sensor not connected");
      flag = true;
    }
    
    return DEVICE_DISCONNECTED_C;
  }
  tempSamples[sampleIndex] = currentTemp;
  sampleIndex = (sampleIndex + 1) % numSamples;
  if(sampleIndex == 0) bufferFull = true;
  int count = bufferFull ? numSamples:sampleIndex;
  float sum = 0;
  for(int x=0;x<count;x++){
    sum += tempSamples[x];
  }
  flag = false;
  return sum/count;
}

void displayLEDBar(int ledCount) {
  uint8_t shiftValue = 0;   // 8-bit value for the shift register (first 8 LEDs)
  bool led9State = LOW;     // Direct LED (pin 14)
  bool led10State = LOW;    // Direct LED (pin 27)
  
  if (ledCount <= 8) {
    // If 1 to 8 LEDs are to be lit, set the lowest 'ledCount' bits.
    // For example, if ledCount is 3, then (1 << 3) - 1 equals 0b00000111.
    shiftValue = (1 << ledCount) - 1;
  } else if (ledCount == 9) {
    // For 9 LEDs: all 8 shift register LEDs are ON and LED9 is ON.
    shiftValue = 0xFF;  // All 8 bits ON
    led9State = HIGH;
    led10State = LOW;
  } else if (ledCount >= 10) {
    // For 10 LEDs: all 8 shift register LEDs and both direct LEDs are ON.
    shiftValue = 0xFF;
    led9State = HIGH;
    led10State = HIGH;
  }
  
  // Update the shift register (74HC595)
  digitalWrite(SHIFT_LATCH_PIN, LOW);              // Prepare shift register for data
  shiftOut(SHIFT_DATA_PIN, SHIFT_CLOCK_PIN, MSBFIRST, shiftValue);
  digitalWrite(SHIFT_LATCH_PIN, HIGH);             // Latch the data to outputs
  
  // Update the direct LED outputs
  digitalWrite(LED_BARGRAPH_LED_9, led9State);
  digitalWrite(LED_BARGRAPH_LED_10, led10State);
}


// ------------------------- Temperature-Based Bargraph Update -------------------------
// This function maps the temperature (from TEMP_MIN to TEMP_MAX) to a LED count (1–10)
// and then displays that count on the bargraph.
void updateBarGraphFromTemperature(float temperature, float TEMP_MIN  = 18.0, float TEMP_MAX = 28.0) {
  // Clamp temperature to our defined range.
  if (temperature < TEMP_MIN) temperature = TEMP_MIN;
  if (temperature > TEMP_MAX) temperature = TEMP_MAX;
  
  // Map temperature linearly to a value between 1 and 10.
  // The formula calculates a fraction between 0 and 1, scales it to 0–9, rounds it, and then adds 1.
  int ledCount = (int)round(((temperature - TEMP_MIN) / (TEMP_MAX - TEMP_MIN)) * 9) + 1;
  ledCount = constrain(ledCount, 1, 10); // Ensure ledCount is between 1 and 10
  
  displayLEDBar(ledCount);
}

void checkAndControlFanByTime() {
  // time_t epochTime = timeClient.getEpochTime();
  // struct tm *ptm = localtime(&epochTime);
  // int currentHour   = ptm->tm_hour;
  // int currentMinute = ptm->tm_min;
  // int currentSecond = ptm->tm_sec;
  // int currentDay    = ptm->tm_yday;  // Day of year (0-365)
  // char currentTime[9];
  // sprintf(currentTime, "%02d:%02d:%02d", ptm->tm_hour,ptm->tm_min,ptm->tm_sec);
  String currentTimeStr = timeClient.getFormattedTime();

  // At 07:00 (within first 10 seconds) turn fan OFF
  if (currentTimeStr == fanOnTime && !fanIsOnTime) {
    Serial.println("Scheduled Time: Turning fan ON...");
    transmitFanCode(FAN_MED);
    fanIsOnTime = true;
    lastScheduledChange = millis();
  }

  // Check if it's time to turn the fan OFF
  else if (currentTimeStr == fanOffTime && fanIsOnTime) {
    Serial.println("Scheduled Time: Turning fan OFF...");
    transmitFanCode(FAN_OFF);
    fanIsOnTime = false;
    lastScheduledChange = millis();
  }
}

// ----------------------------------------------------------------------
// Function: checkAndControlFanByTemp
// Description: Reads the temperature from the DS18B20 sensor and,
//   using hysteresis, turns the fan ON if the temperature exceeds 28°C,
//   and turns it OFF if it drops below 25°C.
// If the sensor is not detected, an error is printed.
// ----------------------------------------------------------------------
void checkAndControlFanByTemp() {
  // Request temperature measurement
  // sensors.requestTemperatures();
  // float temperatureC = sensors.getTempCByIndex(0);
  
  // Verify that the sensor is connected.
  // The DallasTemperature library returns DEVICE_DISCONNECTED_C (typically -127°C)
  // if no sensor is detected.

  // static float lastTemperature = 0;
  // if(temperatureC !=lastTemperature){
  //   Serial.print("Temperature updated to ");
  //   Serial.print(temperatureC);
  //   Serial.println(" °C");
  //   lastTemperature = temperatureC;
  // }
  static bool flags[2] = {false};
  
  if (millis() - lastScheduledChange < scheduledChange){
    flags[1] = false;
    if(!flags[0]){
      Serial.println("Scheduled lock active. Skipping temperature control");
      flags[0] = true;
    }
    return;
  } else {
    flags[0] = false;
    if(!flags[1]){
      Serial.println("Scheduled lock disabled. Enabling temperature control");
      flags[1] = true;
    }
    

  }
  
  float averagedTemp = getAveragedTemperature();
  
  // Temperature-based control with hysteresis:
  if (averagedTemp > FAN_ON_TEMP && !fanIsOnTemp) {
    Serial.println("Temperature-based: Turning fan ON...");
    transmitFanCode(FAN_MED);
    fanIsOnTemp = true;
  } 
  else if (averagedTemp < FAN_OFF_TEMP && fanIsOnTemp) {
    Serial.println("Temperature-based: Turning fan OFF...");
    //transmitFanCode(FAN_OFF);
    fanIsOnTemp = false;
  }
}

template <typename T>
void updateConfig(const char* key, T newValue){
  //1st argument is the key and last argument is the value we want to save. This function saves to flash memory when serial command is set
  const char* configFileName = "/config.txt";

  if(!LittleFS.begin()){
    Serial.println("ERROR: LittleFS.begin() failed. File system not mounted");
    return;
  }
  if(!LittleFS.exists(configFileName)){
    File createFile = LittleFS.open(configFileName, "w");
    if(createFile){
      createFile.close();
      Serial.println("Config file could not be found. Creating new config file.");
    } else{
      Serial.println("ERROR: Failed to create config file :(");
      return;
    }
  }
  String configData = "";
  File file = LittleFS.open(configFileName, "r");
  if(file){
    while(file.available()){
      configData += file.readStringUntil('\n');
      configData += "\n";
    }
    file.close();
  }
  String newConfig = "";
  bool found = false;
  int start = 0;
  int newLineIndex = configData.indexOf('\n', start);
  String keyStr = String(key) + "=";
  while(newLineIndex != -1){
    String line = configData.substring(start, newLineIndex);
    if(line.startsWith(keyStr)){
      newConfig += keyStr + String(newValue) + "\n";
      found = true;
    } else {
      newConfig += line + "\n";
    }
    start = newLineIndex + 1;
    newLineIndex = configData.indexOf('\n', start);

  }
  if(!found){
    newConfig += keyStr + String(newValue) + "\n";
  }
  File configFile = LittleFS.open(configFileName, "w");
  if(!configFile){
    Serial.println("Failed to open config file for writing");
    return;
  }
  configFile.print(newConfig);
  configFile.close();
  Serial.println("Updated config key: " + String(key) + " to " + String(newValue));
}

void loadConfig(){
  //Load the confinguration on startup
  const char* configFileName = "/config.txt";
  if(!LittleFS.exists(configFileName)){
    Serial.println("No config file was found. Using default settings");
    return;
  }
  File file = LittleFS.open(configFileName, "r");
  if(!file){
    Serial.println("Failed to open config file for reading");
  }
  while (file.available()){
    String line = file.readStringUntil('\n');
    line.trim();
    if(line.length() == 0) continue;
    int equalIndex = line.indexOf('=');
    if(equalIndex == -1) continue;
    String key = line.substring(0, equalIndex);
    String value = line.substring(equalIndex + 1);
    if(key.equalsIgnoreCase("ssid")){
      ssid = value;
    }
      else if (key.equalsIgnoreCase("password")) {
      password = value;
    } else if (key.equalsIgnoreCase("sftempon")) {
      FAN_ON_TEMP = value.toFloat();
    } else if (key.equalsIgnoreCase("sftempoff")) {
      FAN_OFF_TEMP = value.toFloat();
    } else if (key.equalsIgnoreCase("sftimeon")) {
      fanOnTime = value;
    } else if (key.equalsIgnoreCase("sftimeoff")) {
      fanOffTime = value;
    } else if (key.equalsIgnoreCase("bgmlow")) {
      barGraphRange[0] = value.toFloat();
    } else if (key.equalsIgnoreCase("bgmhigh")) {
      barGraphRange[1] = value.toFloat();
    }
  }
  file.close();
  String passStr = String(password);
  String masked = "";
  for(int x=0;x<passStr.length(); x++){
    masked += "*";
  }
  
  Serial.println("Configuration loaded from flash:");
  Serial.println("  SSID: " + ssid); 
  Serial.println("  Password: " + masked); 
  Serial.println("  Fan ON Temp: " + String(FAN_ON_TEMP)); 
  Serial.println("  Fan OFF Temp: " + String(FAN_OFF_TEMP)); 
  Serial.println("  Fan ON Time: " + fanOnTime); 
  Serial.println("  Fan OFF Time: " + fanOffTime); 
  Serial.println("  BGM Low: " + String(barGraphRange[0])); 
  Serial.println("  BGM High: " + String(barGraphRange[1])); 
}

//This function defines the serial commands 
void handleCommands(String input) {
 // Read a line from Serial until newline
    input.trim();
    if (input.length() == 0) return; // No input

    // Find the first space to split command and argument
    int spaceIndex = input.indexOf(' ');
    String command;
    String argument;

    if (spaceIndex == -1) {
      // No space found, so the entire input is the command (no argument)
      command = input;
    } else {
      // Split the string into command and argument parts.
      command = input.substring(0, spaceIndex);
      argument = input.substring(spaceIndex + 1);
      argument.trim();
    }

    // Process commands:
    if (command.equalsIgnoreCase("help") || command.equals("?")) {
      Serial.println("Available commands:");
      Serial.println("  setssid <SSID>         - Set WiFi SSID");
      Serial.println("  help or ?              - Shows this menu");
      Serial.println("  setpass <PASSWORD>     - Set WiFi password");
      Serial.println("  fantempon <temp> <delta>       - Set fan ON temperature threshold and fan off using delta");
      //Serial.println("  sftempoff <temp>       - Set fan OFF temperature threshold");
      Serial.println("  bgmlow <value>         - Set led bargraph low mapping temperature");
      Serial.println("  bgmhigh <value>        - Set led bargraph high mapping temperature");
      Serial.println("  fantimeon <HH:MM:SS>    - Set fan ON time");
      Serial.println("  fantimeoff <HH:MM:SS>   - Set fan OFF time");
      Serial.println("  getTime                - Get current NTP time");
      Serial.println("  getUptime              - Uptime since esp32 started");
      Serial.println("  restartEsp             - Restarts the esp32");
      Serial.println("  getTemp                - Get current temperature from sensor");
      Serial.println("  manualfan <FAN_OFF|FAN_MED|FAN_HIGH|FAN_LOW> - Manually transmit code");
      Serial.println("  automaticfan <on|true|enable|off|false|disable> - Enable or disable fan control by temperature and time");
      
    }
    if(command.equalsIgnoreCase("clearlog")){
      serialLog = "";
      return;
    }
    else if (command.equalsIgnoreCase("setssid")) {
      if (argument.length() > 0) {
        ssid = argument.c_str();
        Serial.print("WiFi SSID set to: ");
        Serial.println(ssid);
        updateConfig("ssid", ssid);
      } else {
        Serial.println("Usage: setssid <SSID>");
      }
    }
    else if (command.equalsIgnoreCase("setpass")) {
      if (argument.length() > 0) {
        password = argument.c_str();
        Serial.print("WiFi password set to: ");
        Serial.println(password);
        updateConfig("password", password);
      } else {
        Serial.println("Usage: setpass <PASSWORD>");
      }
    }
    else if (command.equalsIgnoreCase("bgmlow")) {
      if (argument.length() > 0) {
        barGraphRange[0] = argument.toFloat();
        Serial.print("Bar graph low mapping value: ");
        Serial.println(barGraphRange[0]);
        updateConfig("bgmlow", barGraphRange[0]);
      } else {
        Serial.println("Usage: bmglow <Value>");
      }
    }
    else if(command.equalsIgnoreCase("getuptime")){
      unsigned long uptimeSeconds = millis() / 1000;
      int days = uptimeSeconds / 86400;
      uptimeSeconds %=86400;
      int hours = uptimeSeconds / 3600;
      uptimeSeconds %=3600;
      int minutes = uptimeSeconds / 60;
      int seconds = uptimeSeconds % 60;
      Serial.print("Uptime: ");
      if(days > 0){
        Serial.print(days);
        Serial.print(" days, ");
      }
      Serial.print(hours);
      Serial.print(" hours, ");
      Serial.print(minutes);
      Serial.print(" minutes, ");
      Serial.print(seconds);
      Serial.println(" seconds");
    }
    else if (command.equalsIgnoreCase("bgmhigh")) {
      if (argument.length() > 0) {
        barGraphRange[1] = argument.toFloat();
        Serial.print("Bar graph high mapping value: ");
        Serial.println(barGraphRange[1]);
        updateConfig("bgmhigh", barGraphRange[1]);
      } else {
        Serial.println("Usage: bmghigh <Value>");
      }
    }

    else if(command.equalsIgnoreCase("restartesp")){
      Serial.println("Restarting esp32...");
      delay(500);
      ESP.restart();
    }
    else if (command.equalsIgnoreCase("fanontemp")) {
      if (argument.length() > 0) {
        int spaceIndex2 = argument.indexOf(' ');
        if(spaceIndex2 == -1){
          Serial.println("Usage: fanontemp <value> <delta>");
        } else {
          String valueStr = argument.substring(0, spaceIndex2);
          String deltaStr = argument.substring(spaceIndex2 + 1);
          valueStr.trim();
          deltaStr.trim();
          if(valueStr.length() == 0 || deltaStr.length() == 0){
            Serial.println("Usage: fantempon <value> <delta>");
          } else {
            float onTemp = valueStr.toFloat();
            float delta = deltaStr.toFloat();

          if(onTemp < 0){
            Serial.println("ERROR: Value was set too low, defaulting to 0c. Why the fuck is it set lower anyway?");
            onTemp = 0;
          } else if(onTemp > 40){
            Serial.println("ERROR: Value was set too high, defaulting to 40c. Why the fuck is it set higher anyway?");
            onTemp = 40;
          } 
          FAN_ON_TEMP = onTemp;
          FAN_OFF_TEMP = onTemp - delta;
          if(FAN_OFF_TEMP < 0) FAN_OFF_TEMP = 0;
          fanIsOnTemp = false;
          updateConfig("sftempon", FAN_ON_TEMP);
          updateConfig("sftempoff", FAN_OFF_TEMP);
          Serial.print("Fan ON temperature threshold set to: ");
          Serial.println(FAN_ON_TEMP);
          Serial.print("Fan Off temperature threshold set to: ");
          Serial.println(FAN_OFF_TEMP);
          }
        }  
      } else {
        Serial.println("Usage: fanontemp <temp> <delta>");
      }
    }
    else if (command.equalsIgnoreCase("manualfan")) {
      if (argument.length() > 0) {
        String code;
        if(argument.equalsIgnoreCase("FAN_OFF")){
          code = FAN_OFF;
        }
        else if(argument.equalsIgnoreCase("FAN_MED")){
          code = FAN_MED;
        }
        else if(argument.equalsIgnoreCase("FAN_LOW")){
          code = FAN_LOW;
        }
        else if(argument.equalsIgnoreCase("FAN_HIGH")){
          code = FAN_HIGH;
        }
        else{
          Serial.println("Usage: manualfan <FAN_OFF|FAN_MED|FAN_HIGH|FAN_LOW>");
          return;
        }
        transmitFanCode(code.c_str());
         Serial.print("Fan code set to: ");
         Serial.println(code);
      } else {
        Serial.println("Usage: manualfan <FAN_OFF|FAN_MED|FAN_HIGH|FAN_LOW>");
      }
    }
    // else if (command.equalsIgnoreCase("sftempoff")) {
    //   if (argument.length() > 0) {
        
    //     if(tempVal < 0){
    //       Serial.println("ERROR: Value was set too low, defaulting to 0c. Why the fuck is it set lower anyway?");
    //       FAN_ON_TEMP = 0;
    //     } else if(tempVal > 40){
    //       Serial.println("ERROR: Value was set too high, defaulting to 40c. Why the fuck is it set higher anyway?");
    //       FAN_ON_TEMP = 40;
    //     } else{
    //       FAN_ON_TEMP = tempVal;
    //     }
    //     fanIsOnTemp = true;
    //     Serial.print("Fan OFF temperature threshold set to: ");
    //     Serial.println(FAN_OFF_TEMP);
    //     updateConfig("sftempoff", FAN_OFF_TEMP);
    //   } else {
    //     Serial.println("Usage: sftempoff <temp>");
    //   }
    // }
    else if (command.equalsIgnoreCase("fantimeon")) {
      if (argument.length() > 0) {
        fanOnTime = argument;
        fanIsOnTime = false;
        Serial.print("Fan ON time set to: ");
        Serial.println(fanOnTime);
        updateConfig("sftimeon", fanOnTime);
      } else {
        Serial.println("Usage: fantimeon <HH:MM:SS>");
      }
    }
    else if (command.equalsIgnoreCase("automaticfan")){
      if(argument.length() > 0){
        String argLower = argument;
        argLower.toLowerCase();
        if(argLower.equals("on") || argLower.equals("true") || argLower.equals("enable")){
          automaticFanControl = true;
          Serial.println("Automatic fan control enabled.");
        } else if(argLower.equals("off") || argLower.equals("false") || argLower.equals("disable")){
          automaticFanControl = false;
          Serial.println("Automatic fan control disabled.");
        } else {
          Serial.println("Usage: automaticfan <on|true|enable|off|false|disable>");
        }
      } else {
        Serial.println("Usage: automaticfan <on|true|enable|off|false|disable>");
      }
    }
    else if (command.equalsIgnoreCase("fantimeoff")) {
      if (argument.length() > 0) {
        fanIsOnTime = true;
        fanOffTime = argument;
        Serial.print("Fan OFF time set to: ");
        Serial.println(fanOffTime);
        updateConfig("sftimeoff", fanOffTime);
      } else {
        Serial.println("Usage: fantimeoff <HH:MM:SS>");
      }
    }
    else if (command.equalsIgnoreCase("getTime")) {
      String ntpTime = timeClient.getFormattedTime();
      Serial.print("Current NTP time: ");
      Serial.println(ntpTime);
    }
    else if (command.equalsIgnoreCase("getTemp")) {
      Serial.print(getAveragedTemperature());
      Serial.println("C");
      
    }
    else {
      Serial.println("Unknown command. Type 'help' or '?' for a list of commands.");
    }
    
}

void processSerialCommands() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() > 0) {
      handleCommands(input);
    }
  }
}

void serialWebPage(){
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      if(!request->authenticate("admin", "*")){
        //IPAddress clientIP = request->client()->remoteIP();
        //Serial.println("WARNING: Bad login attempt made from IP: " + clientIP.toString() + " either evil hacker or someone put in wrong credentials :P");
        return request->requestAuthentication();
      }
    request->send_P(200, "text/html", INDEX_HTML);
  });
    server.on("/logout", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(401, "text/plain", "Logged out. Goodbye and have a blessing day");
    });
    server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest *request){
      digitalWrite(TRANSMIT_STATUS, HIGH);
      float avgTemp = getAveragedTemperature();
      if(avgTemp == DEVICE_DISCONNECTED_C){
        request->send(500, "text/plain", "Error: Temperature sensor not connected");
      } else {
        char tempStr[16];
        snprintf(tempStr, sizeof(tempStr), "%.2f", avgTemp);
        request->send(200, "text/plain", tempStr);
      }
      digitalWrite(TRANSMIT_STATUS, LOW);
    });
  
  // server.on("/log", HTTP_GET, [](AsyncWebServerRequest *request) {
  //   request->send(200, "text/plain", serialLog);
  // });
  
  // server.on("/command", HTTP_GET, [](AsyncWebServerRequest *request) {
  //   if (request->hasParam("cmd")) {
  //     String cmd = request->getParam("cmd")->value();
  //     handleCommands(cmd);
  //     request->send(200, "text/plain", "Command executed");
  //   } else {
  //     request->send(400, "text/plain", "No command provided");
  //   }
  // });
  
  server.begin();
}

// Get specs of flash memory
void getFlashSpecs(){
  size_t flashSizeBytes = ESP.getFlashChipSize();
  float flashSizeMB = flashSizeBytes / (1024.0*1024.0);
  Serial.print("Flash chip size: ");
  Serial.print(flashSizeMB, 2);
  Serial.println(" MB");

  const esp_partition_t* otaPartition = esp_ota_get_running_partition();
  size_t otaPartitionSizeBytes = otaPartition->size;
  float otaPartitionSizeMB = otaPartitionSizeBytes / (1024.0 * 1024.0);

  size_t sketchSizeBytes = ESP.getSketchSize();
  float sketchSizeMB = sketchSizeBytes / (1024.0 * 1024.0);

  size_t otaFreeBytes = otaPartitionSizeBytes - sketchSizeBytes;
  float otaFreeMB = otaFreeBytes / (1024.0 * 1024.0);

  size_t LFStotalBytes = LittleFS.totalBytes();
  size_t LFSusedBytes = LittleFS.usedBytes();
  float LFStotalMB = LFStotalBytes / (1024.0 * 1024.0);
  float LSFusedMB = LFSusedBytes / (1024.0 * 1024.0);

  Serial.println("LittleFS total MB: " + String(LFStotalMB));
  Serial.println("LittleFS used MB: " + String(LSFusedMB));

  Serial.print("OTA Partition Size: ");
  Serial.print(otaPartitionSizeMB, 2);
  Serial.println(" MB");

  Serial.print("Sketch Size: ");
  Serial.print(sketchSizeMB, 2);
  Serial.println(" MB");

  Serial.print("Free Space in OTA Partition: ");
  Serial.print(otaFreeMB, 2);
  Serial.println(" MB");
  Serial.println("");
}



void setup() {
  Serial.begin(115200);
  delay(1000);

  if(!LittleFS.begin()){
    Serial.println("Failed to moutn littleFS");
  } else{
    Serial.println("LittleFS mounted successfully now loading settings");
    loadConfig();
  }

  // if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
  //   Serial.println("STA Failed to configure");
  // }
  // Initialize the connectivity indicator pin
  pinMode(NTP_STATUS_LIGHT, OUTPUT);
  pinMode(TRANSMIT_STATUS, OUTPUT);
  pinMode(SHIFT_DATA_PIN, OUTPUT);
  pinMode(SHIFT_CLOCK_PIN, OUTPUT);
  pinMode(SHIFT_LATCH_PIN, OUTPUT);
  digitalWrite(SHIFT_LATCH_PIN, LOW);
  shiftOut(SHIFT_DATA_PIN, SHIFT_CLOCK_PIN, MSBFIRST, 0);
  digitalWrite(SHIFT_LATCH_PIN, HIGH);

  pinMode(LED_BARGRAPH_LED_10, OUTPUT);
  pinMode(LED_BARGRAPH_LED_9, OUTPUT);
  digitalWrite(NTP_STATUS_LIGHT, RED);  // Start assuming no connectivity
  digitalWrite(TRANSMIT_STATUS, LOW);  // Start assuming no connectivity
  
  // Connect to WiFi
  Serial.printf("Connecting to WiFi network: %s\n", ssid);
  WiFi.begin(ssid, password);
  if(WiFi.status() == WL_CONNECTED){
    Serial.println("Connected to Wifi with ip: " + WiFi.localIP());
  } 
  
  
  // Initialize the NTP client and update time
  timeClient.begin();
  timeClient.update();
  
  // Set connectivity indicator based on NTP time validity.
  IPAddress ntpServerIP;
  if (WiFi.hostByName("au.pool.ntp.org", ntpServerIP)) {
    // Update time from the NTP server and check its validity.
    timeClient.update();
    if (timeClient.getEpochTime() > 100000) {
      digitalWrite(NTP_STATUS_LIGHT, GREEN);
      Serial.println("NTP time: " + timeClient.getFormattedTime());
    } else {
      digitalWrite(NTP_STATUS_LIGHT, RED);
      Serial.println("Failed to retrieve a valid NTP time.");
    }
  } else {
    digitalWrite(NTP_STATUS_LIGHT, RED);
    Serial.println("au.pool.ntp.org is not reachable.");
  }
  
  // Initialize the DS18B20 sensor
  sensors.begin();
  int sensorCount = sensors.getDeviceCount();
  if (sensorCount == 0) {
    Serial.println("Warning: No DS18B20 sensor detected on pin " + String(ONE_WIRE_PIN));
  } else {
    Serial.printf("DS18B20 sensor detected; count = %d\n", sensorCount);
  }
  
  // Initialize the RF transmitter
  mySwitch.enableTransmit(TX_PIN);
  mySwitch.setProtocol(6);
  mySwitch.setPulseLength(328);
  initWebSocket();
  serialWebPage();
  getFlashSpecs();
  Serial.println("Fan controller firmware version 2.0.7");
  Serial.println("Type 'help' or '?' for a list of commands.");
}


void loop() {
  // Update NTP client and the connectivity indicator
  unsigned long currentMillis = millis();
  if(currentMillis - lastNtpCheckMillis >= ntpUpdateMs){
    lastNtpCheckMillis = currentMillis;
    IPAddress ntpServerIP;
    if (WiFi.hostByName("au.pool.ntp.org", ntpServerIP)) {
    digitalWrite(NTP_STATUS_LIGHT, GREEN);
    Serial.println("Successful connection to au.pool.ntp.org");
    //Serial.println("NTP time: " + timeClient.getFormattedTime());
    // timeClient.update();
    // if (timeClient.getEpochTime() > 100000) {
    //   digitalWrite(NTP_STATUS_LIGHT, LOW);  // Valid NTP time received
    // } else {
    //   digitalWrite(NTP_STATUS_LIGHT, HIGH);
    // }
  } else {
    digitalWrite(NTP_STATUS_LIGHT, RED);     // Unable to resolve pool.ntp.org
    Serial.println("ERROR: cant connect to au.pool.ntp.org");
  }
  }

  // sensors.requestTemperatures();
  // float temperature = sensors.getTempCByIndex(0);
  float averageTempC = getAveragedTemperature();
  
  // If temperature sensor got disconnected run the led bargraph animation 
  if (averageTempC == DEVICE_DISCONNECTED_C) {
    static bool flag = false;
    // Sensor error: Run the LED animation (bargraph lights one LED at a time from left to right)
    unsigned long currentMillis = millis();
    if (currentMillis - lastAnimationMillis >= animationInterval) {
      lastAnimationMillis = currentMillis;
      
      // Update the bargraph with the current animation position.
      displayLEDBar(animationLEDCount);
      
      // Move to the next LED position.
      animationLEDCount++;
      if (animationLEDCount > 10) {
        animationLEDCount = 1;  // Reset back to the first LED after reaching the last one.
        sensors.begin();
        if(sensors.getDeviceCount()>0){
          Serial.println("Successfully found ds18b20 sensor");
          averageTempC = getAveragedTemperature(true);
        } else{
          Serial.println("Temperature sensor not connected. Running LED animation...");

        }
      }
    }

    
  } 
  else {
    // Sensor is connected: Update the bargraph based on the actual temperature.
    updateBarGraphFromTemperature(averageTempC,barGraphRange[0],barGraphRange[1]);
    if(automaticFanControl) checkAndControlFanByTemp();
    
  }
  
  
  // Execute the scheduled (time-based) fan control
  if(automaticFanControl) checkAndControlFanByTime();
  processSerialCommands();
  if(wsBuffer.length() >0){
    ws.textAll(wsBuffer);
    wsBuffer = "";
  }
  // Execute the temperature-based fan control
  
  
  // static String lastPrintedNtpTime = "";
  // String currentTime = timeClient.getFormattedTime();
  // if(currentTime != lastPrintedNtpTime){
  //   Serial.println("NTP Time: " + currentTime);
  //   lastPrintedNtpTime = currentTime;
  // }

  
  
  //delay(1000);  // Loop every second
}
