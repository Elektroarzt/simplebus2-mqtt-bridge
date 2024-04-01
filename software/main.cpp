#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h> 
#include <PubSubClient.h>  
#include <time.h>
#include <Wire.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <PinButton.h>
#include <Debounce.h>
#include <EEPROM.h>

void function_ISR();
bool interruptActive = false;
bool teachInActive = false;
long teachInTimeActivated;

struct Settings {
  char mqttServer[40];
  char mqttUser[40];
  char mqttPassword[40];
  int gain;
  int compVoltLvl;
  bool filter;
  bool ringAutomation;
  int intercomAddress;
} mySettings;
WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50];
int value = 0;
volatile int messageCode = 0;
bool messageReceived = false;

bool ringToOpenStarted = false;
unsigned int ringToOpenMillis = 45*60*1000;
unsigned int ringToOpenMillisStart = 0;
volatile bool pinTriggered = false;
int lastState = LOW;  
int currentState;    
unsigned long pressedTime  = 0;
unsigned long releasedTime = 0;
unsigned long lastHeartbeatMillis = 0;




///////////////////////////////////////////////////////////////////////////////
// Message cod
//
// Call Addrress from the intercom

// Hardware pin definition
#define LEDPIN 4
#define INTERCOM_READ_PIN 2
#define INTERCOM_WRITE_PIN 3
#define BUTTONPIN 8
#define SDA_1 6
#define SCL_1 7
#define SDA_2 9
#define SCL_2 10
// Hardware Settings
#define MCP4017_MAX_RESISTANCE 100000
#define MCP4017_I2C_ADDRESS  0x2F
#define I2C_FREQ 100000
#define OPV_FIXED_GAIN_RESISTOR 100000

// Sent from intercoms to call the main switchboard.
#define MSG_CALL_TO_MAIN_SWITCHBOARD         8

// Sent from intercoms to call the caretaker phone (K).
#define MSG_CALL_CARETAKER_DOOR_ENTRY_PHONE  9

// Open building's entry door (AP). Sent when the intercom's open door button 
// is pushed. User ID tells who's opening the door.
#define MSG_OPEN_DOOR                       16

// Sent when the user hooks off the intercom's handset.
#define MSG_HOOK_OFF                        17

// Sent when the user hooks on the intercom's handset.
#define MSG_HOOK_ON                         18

// Send from intercoms to call the secondary switchboard. In model the 
// secondary button can be configured to send this message by setting the JP1
// jumper to position C.
#define MSG_CALL_TO_SECONDARY_SWITCHBOARD   19

// Turns on the video camera and the intercom's screen (AI).
#define MSG_CAMERA_ON                       20

// Sent when the external switch connected to the CFP terminals is closed.
#define MSG_CALL_FROM_FLOOR_DOOR            21

// Sent by one intercom to call other intercomms (INT).
#define MSG_CALL_INTERCOM                   24

// After sending MSG_CALL_INTERCOM three consecutive
// MSG_CALL_INTERCOM_RESPONSE are sent.
#define MSG_CALL_INTERCOM_RESPONSE          26

// Activates a generic actuator. In model the secondary button can be
// configured to send this message by setting the JP1 jumper to position A.
#define MSG_GENERIC_ACTUATOR                29

// High prioritary call to main switchboard (PAN).
#define MSG_HIGH_PRIO_CALL_TO_MAIN_SWITCHBOARD  30

// Sent from switchboard for calling an intercom.
#define MSG_CALL_FROM_SWITCHBOARD_1         32

// After sending this message I got a MSG_HOOK_ON, but only once. What does
// this mean?.
#define MSG_UNKNOWN_1                       33


#define MSG_CALL_FROM_SWITCHBOARD_2             37
#define MSG_CALL_FROM_SWITCHBOARD_3             42
#define MSG_CALL_FROM_SWITCHBOARD_4             43
#define MSG_CALL_FROM_SWITCHBOARD_5_SCREEN_ON   45


// Sent when someone calls at the building's entry. The ring tone is played
// once per each message, this message is usually sent two times, followed by 
// MSG_CALL_FROM_ENTRY_DOOR_SCREEN_ON.
#define MSG_CALL_FROM_ENTRY_DOOR            48

// Sent when the screen is turned off ?????
#define MSG_SCREEN_OFF                      49

// Similar to MSG_RING_TONE but also makes the intercom turn on the video 
// screen.
#define MSG_CALL_FROM_ENTRY_DOOR_SCREEN_ON  50


#define MSG_START_BLINKING_OPEN_DOOR_BTN    51
#define MSG_STOP_BLINKING_OPEN_DOOR_BTN     52


///////////////////////////////////////////////////////////////////////////////
// Global variables
// 



// Last time the door was opened, either with Blynk or the input button.

volatile bool messageOk = false;
  
unsigned long lastOpenDoorTime;
unsigned long pauseTime;
unsigned long startTime;
bool pauseTimeMeasured;
bool pauseTimeStart;
int count;
//PinButton settupButton(BUTTONPIN, INPUT_PULLUP,);
Debounce Button(BUTTONPIN);

int writeStringToEEPROM(int addrOffset, const char *strToWrite)
{
  byte len = strlen(strToWrite);
  EEPROM.put(addrOffset, len);
  for (int i = 0; i < len; i++)
  {
    EEPROM.put(addrOffset + 1 + i, strToWrite[i]);
  }
  
  return addrOffset + 1 + len;
}
int readStringFromEEPROM(int addrOffset, char *strToRead)
{
  int newStrLen = EEPROM.read(addrOffset);
  for (int i = 0; i < newStrLen; i++)
  {
    strToRead[i] = EEPROM.read(addrOffset + 1 + i);
  }
  strToRead[newStrLen] = '\0'; 
  
  return addrOffset + 1 + newStrLen;
}
void saveSettings() {
  EEPROM.put(0,mySettings);
  EEPROM.commit();
}
void loadSettings() {
  EEPROM.get(0,mySettings);
  // compVoltLvl = EEPROM.read(str3Offset+2);
  Serial.println(String(mySettings.mqttServer));
  Serial.println(String(mySettings.mqttUser));
  Serial.println(String(mySettings.mqttPassword));
  Serial.println(mySettings.gain);
  Serial.println(mySettings.compVoltLvl);
}
bool structInitialized() {
  return   mySettings.compVoltLvl != 0 && mySettings.gain != 0 && sizeof(mySettings.mqttPassword) > 1 && sizeof(mySettings.mqttServer) > 1 && sizeof(mySettings.mqttPassword)>1;
}
boolean isNumeric(String str) {
    unsigned int stringLength = str.length();
    if (stringLength == 0) {
        return false;
    }
    for(unsigned int i = 0; i < stringLength; ++i) {
        if (isDigit(str.charAt(i))) {
            continue;
        }
        return false;
    }
    return true;
}
bool shouldSaveConfig = false;

void setupI2C(int i2cNumber) {
  int sclPin = -1;
  int sdaPin = -1;
  switch(i2cNumber) {
    case 1: {
      sdaPin = SDA_1;
      sclPin = SCL_1;
      break;
    }
    case 2: {
      sdaPin = SDA_2;
      sclPin = SCL_2;
      break;
    }
    default:{
      break;
    }
  }
  Wire.begin(sdaPin, sclPin, I2C_FREQ);
  Wire.beginTransmission(0b111111111);
  Wire.endTransmission();
}

void setPotValue(byte byteValue) {
  Serial.print("write Pot");
  Serial.print(byteValue);

  const uint8_t reg = byteValue & 0x7F;
  //Serial.print(reg);
  Wire.beginTransmission(MCP4017_I2C_ADDRESS);
  Wire.write(byteValue);
  Wire.endTransmission();
  Wire.end();
}

void setPotPercentage(float percentage) {
  if(percentage < 0 || percentage > 100) {
    return;
  }
  int value = round((float(percentage)/100.0) * 127.0);
  Serial.println("Percentage value");
  Serial.print(value);
  setPotValue(value);
 
}

void setPotResistance(float resistance) {
  Serial.println("Resistor value:");
  Serial.print(resistance);
  if(resistance < 0 || resistance > MCP4017_MAX_RESISTANCE) {
    return;
  }
  float percentageToSet = (float(resistance-325)/float(MCP4017_MAX_RESISTANCE)) * 100.0;
  Serial.println("Percentage:");
  Serial.print(percentageToSet);
  setPotPercentage(percentageToSet);
}

//set gain factor of the OPV
void setOPVGain(int gain) {
  //Gain = 1+(R2/R1)
  if(gain < 2) {
    return;
  }
  float resistorValue = OPV_FIXED_GAIN_RESISTOR/(gain-1);
  setupI2C(1);
  setPotResistance(resistorValue);
}

//set ref voltage of the comparator in [mV]
void setComparatorVoltageLimit(int voltage) {
  Serial.println("Voltage to set:");
  Serial.print(voltage);
  if(voltage < 100 || voltage > 1500) {
    return;
  }
  setupI2C(2);
  float resistorValue = (float(voltage) * float(OPV_FIXED_GAIN_RESISTOR))/(3300.0-float(voltage));
  Serial.println("Resistor Value:");
  Serial.print(resistorValue);
  setPotResistance(resistorValue);
}

void intToBinary(unsigned int input,int startPos,int noOfBits, int* bits) {
  unsigned int mask = 1;
  //static int bits [noOfBits];
  int zeroedStartPos = startPos-1;
  for(int i = startPos; i < noOfBits+startPos; i++) {
    bits[i] = (input & (1<<(i-startPos))) != 0;
  }
}

unsigned int binaryToInt(int startPos, int numOfBits, int binaryArray []) {
  unsigned int integer = 0;
  unsigned int mask = 1;
  for(int i = startPos; i < numOfBits+startPos; i++) {
    if(binaryArray[i]) integer |= mask;
    mask = mask << 1;
  }
  return integer;
}

void setPowerSavings() {
  Serial.print("CPU Freq:");
  Serial.println(getCpuFrequencyMhz());
  setCpuFrequencyMhz(80);
  Serial.print("CPU Freq:");
  Serial.println(getCpuFrequencyMhz());
}
void sendPWM() {
  ledcWrite(0, 50);
  delay(3);
  ledcWrite(0,0);
}
void sendMessage(bool bitToSend) {
  if(bitToSend) {
    sendPWM();
    delay(6);
    
  } else {
    sendPWM();
    delay(3);
  }
}

void sendMessageStart() {
  sendPWM();
  delay(17);
}

void writeMessageToIntercom(int msgCode, int msgAddr) {
  detachInterrupt(INTERCOM_READ_PIN);
  if(msgCode = MSG_OPEN_DOOR) {
    lastOpenDoorTime = millis();
  }  
  int msgArray [18];
  int checksum = 0;

  checksum += __builtin_popcount(msgCode);
  checksum += __builtin_popcount(msgAddr);
  Serial.print(checksum);
  Serial.println(" ");
  intToBinary(msgCode, 0, 6, msgArray );
  intToBinary(msgAddr, 6, 8, msgArray );
  intToBinary(checksum, 14, 4, msgArray );
  sendMessageStart();
  for(int i = 0; i < 18; i++) {
    sendMessage(msgArray[i]);
  }
  sendPWM();
  attachInterrupt(INTERCOM_READ_PIN, function_ISR, RISING);
 
}
void startStopRingToOpen(bool start) {
  if(start) {
    client.publish("SimpleBus/RingToOpenStatus", "ON");
  } else {
    client.publish("SimpleBus/RingToOpenStatus", "OFF");
  }
  ringToOpenStarted = start;
  
}
static unsigned long lastInterrupt = 0;
static int messagePositon = 0;
static int messageBitArray [18];
static bool messageStarted  = false;
static bool busHigh = false;
static unsigned long lastPauseTime = 0;

void processInterrupt() {
  unsigned long currentInterrupt = micros();
  unsigned long pauseTime = 0;
  //Serial.print("Processing ");
  if(true) { //currentInterrupt - lastInterrupt < 100 && currentInterrupt - lastInterrupt > 20
    pauseTime  = currentInterrupt - lastPauseTime;
    
    //Serial.println(pauseTime);
      
    if(pauseTime > 18000 && messageStarted) {
    messageStarted = false;
      //Serial.print("No Start Message Detected");
      
    }
  
    if(pauseTime >= 16000 && pauseTime <= 18000) {
      messageStarted = true;
      messagePositon = 0; 
    } 
        
      if(messageStarted) {
      
        switch(pauseTime) {
          case 2000 ... 4900: {
            count++;
            messageBitArray[messagePositon] = 0;
            ///Serial.print("0");
            messagePositon++;
            
            break;
          }
          case 5000 ... 9000: {
            count++;
            //Serial.print("1");
            messageBitArray[messagePositon] = 1;
            messagePositon++;
            
            
            break;
          }
          default:{
            break;
          }
        }
        if(messagePositon == 18) { 
          messageStarted = false;
          unsigned int msgCode = binaryToInt(0, 6, messageBitArray);
          //Serial.println(msgCode);
          unsigned int msgAddr = binaryToInt(6, 8, messageBitArray);
          //Serial.println(msgAddr);
          unsigned int msgChecksum = binaryToInt(14, 4, messageBitArray);
          int checksum = 0;
          checksum += __builtin_popcount(msgCode);
          checksum += __builtin_popcount(msgAddr);
          if(teachInActive) {
            mySettings.intercomAddress = msgAddr;
            saveSettings();
            teachInActive = false;
            digitalWrite(LEDPIN, LOW);
            ringToOpenStarted = true;
          }
          if(checksum == msgChecksum && msgAddr == mySettings.intercomAddress) {
            messageCode = msgCode;
            messageOk = true;
          } 
        }
    }
    lastPauseTime = currentInterrupt; 
  } else {
    

  }
  lastInterrupt = currentInterrupt;
  pinTriggered = false;
}
void processMessage(unsigned int msgCode) 
{
  switch (msgCode)
  {
    
    case MSG_CALL_TO_SECONDARY_SWITCHBOARD:
      break;
    case MSG_CALL_FROM_ENTRY_DOOR:
    case MSG_CALL_FROM_ENTRY_DOOR_SCREEN_ON:
      if(ringToOpenStarted) {
        // Waiting time in necessary otherwise the opening singal cant get through
        delay(2000);
        client.publish("SimpleBus/RingToOpenOpened", "ON");
        writeMessageToIntercom(MSG_OPEN_DOOR, mySettings.intercomAddress);
        startStopRingToOpen(false);
       
        
      } else {
        if ((lastOpenDoorTime > 0 && millis() - lastOpenDoorTime < 60000) && mySettings.ringAutomation) {
          // Wait for 2.5 seconds before opening the door. 
          delay(2000);
          client.publish("SimpleBus/RingToOpenOpened", "ON");
          writeMessageToIntercom(MSG_OPEN_DOOR, mySettings.intercomAddress);
          
        } else {
          delay(2000);
          client.publish("SimpleBus/EntryDoor", "ON");
        }
      }
      break;
    case MSG_CALL_FROM_FLOOR_DOOR:
      client.publish("SimpleBus/FloorDoor", "ON");
      break;
  case MSG_OPEN_DOOR:
    lastOpenDoorTime = millis();
    break;
  }

}

void function_ISR() {
  static unsigned long last_interrupt_time = 0;
  unsigned long interrupt_time = micros();
  // If interrupts come faster than 200ms, assume it's a bounce and ignore
  //this is a small filter which only accepts smaller a connected waveform
  if(!pinTriggered) {

      pinTriggered = true;
  }
  
  
 
 
  
  last_interrupt_time = interrupt_time;


}
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}
void addNullTermination(char *stringToSearch) {
  if(strchr(stringToSearch, '\0') == NULL ) {
    Serial.print(stringToSearch);
    Serial.print("Added null termination");
    stringToSearch[strlen(stringToSearch)] = '\0';

  }
}

void setup_wifi(bool autoconnect = true) {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  delay(10);
  WiFiManager wifiManager;
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setBreakAfterConfig(true);
  wifiManager.setConfigPortalTimeout(120);
  wifiManager.setConnectTimeout(60);
  WiFiManagerParameter custom_mqttServer("server", "mqtt server", mySettings.mqttServer, 40);
  wifiManager.addParameter(&custom_mqttServer);
  WiFiManagerParameter custom_mqtt_user("user", "mqtt user", mySettings.mqttUser, 40);
  wifiManager.addParameter(&custom_mqtt_user);
  WiFiManagerParameter custom_mqtt_password("password", "mqtt password", mySettings.mqttPassword, 40);
  wifiManager.addParameter(&custom_mqtt_password);
  char intercomAddress[2];
  itoa(mySettings.intercomAddress,intercomAddress,10);
  WiFiManagerParameter custom_intercomAddress("address", "Address Intercom", intercomAddress, 40);
  wifiManager.addParameter(&custom_intercomAddress);
  char gainChar[2];
  itoa(mySettings.gain,gainChar,10);
  WiFiManagerParameter custom_gain("Gain", "Gain", gainChar, 40);
  wifiManager.addParameter(&custom_gain);
  char compVoltChar[3];
  itoa(mySettings.compVoltLvl,compVoltChar,10);
  WiFiManagerParameter custom_voltage_level("voltagelevel", "Voltage level",compVoltChar, 40);
  wifiManager.addParameter(&custom_voltage_level); 
  char customhtmlFilter[24] = "type=\"checkbox\"";
  if (mySettings.filter) {
      strcat(customhtmlFilter, " checked");
  }
  WiFiManagerParameter custom_filter("filter", "Filter", "T", 2, customhtmlFilter, WFM_LABEL_AFTER);
  wifiManager.addParameter(&custom_filter);
  char customhtmlAutomation[24] = "type=\"checkbox\"";
  if (mySettings.ringAutomation) {
      strcat(customhtmlAutomation, " checked");
  }
  WiFiManagerParameter custom_automation("ringAutomation", "Open automation", "T", 2, customhtmlAutomation, WFM_LABEL_AFTER);
  wifiManager.addParameter(&custom_automation);

  if(autoconnect) {
    wifiManager.autoConnect("Config_MQTT_SimpleBus_2");
  } else  {
    wifiManager.startConfigPortal("Config_MQTT_SimpleBus_2");
  }
  if(shouldSaveConfig) {
    const char* voltageTmp = custom_voltage_level.getValue();
    const char* gainTmp = custom_gain.getValue();
    const char* intercomAddressTmp = custom_intercomAddress.getValue();
    strcpy(mySettings.mqttServer, custom_mqttServer.getValue());
    strcpy(mySettings.mqttUser, custom_mqtt_user.getValue());
    strcpy(mySettings.mqttPassword, custom_mqtt_password.getValue());
    Serial.println(custom_filter.getValue());
    mySettings.filter = (strncmp(custom_filter.getValue(), "T", 2) == 0);
    mySettings.ringAutomation = (strncmp(custom_automation.getValue(), "T", 2) == 0);
    
    Serial.println("Saving data to EEPROM");
    Serial.println("Server:"+String(mySettings.mqttServer));
    Serial.println("User:"+String(mySettings.mqttUser));
    Serial.println("MQTT Password:"+String(mySettings.mqttPassword));
    Serial.println("Comp Voltage:"+String(voltageTmp));
    Serial.println("Gain:"+String(gainTmp));
    Serial.println("Intercom Address:"+String(intercomAddressTmp));
    Serial.println( mySettings.ringAutomation);
    Serial.println( mySettings.filter);
    if(isNumeric(String(voltageTmp))) {
      mySettings.compVoltLvl = String(voltageTmp).toInt();
    } else {
      mySettings.compVoltLvl = 220;
    }
    if(isNumeric(String(gainTmp))) {
      mySettings.gain = String(gainTmp).toInt();
    } else {
      mySettings.gain = 33;
    }
    if(isNumeric(String(intercomAddressTmp))) {
      mySettings.intercomAddress = String(intercomAddressTmp).toInt();
    } else {
      mySettings.intercomAddress = 0;
    }
    saveSettings();
  }

  WiFi.setTxPower(WIFI_POWER_MINUS_1dBm);
  //WiFi.setSleep(true);
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 1);
}

void callback(char* topic, byte* message, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  String messageTemp;
  
  for (int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  Serial.println();

  if (String(topic) == "SimpleBus/OpenDoor") {
    //Serial.print("OPENEING the door ");
    if(messageTemp == String("ON")) {
      Serial.print("OPENEING the door ");
      writeMessageToIntercom(MSG_OPEN_DOOR, mySettings.intercomAddress);  
   }
    
  }
  if (String(topic) == "SimpleBus/SetRingToOpenTime") {
    if(isNumeric(messageTemp)) {
      int rtoTime = messageTemp.toInt();
      //rto maximum time 24 hours, security feature
      if(rtoTime <= 1440 && rtoTime >= 1) {
          ringToOpenMillis = rtoTime*60*1000;
        } else {
          if(rtoTime > 1440) {
            ringToOpenMillis = 1440*60*1000;
          } else {
            ringToOpenMillis = 1*60*1000;
          }
          
        }
    } else {
      ringToOpenMillis = 45*60*1000;
    }
     //Change minutes to millis 

  }
  if (String(topic) == "SimpleBus/RingToOpen") {
    if(isNumeric(messageTemp)) {
        int rtoTime = messageTemp.toInt();
        //rto maximum time 24 hours, security feature
        if(rtoTime <= 1440 && rtoTime >= 1) {
          ringToOpenMillis = rtoTime*60*1000;
        } else {
          if(rtoTime > 1440) {
            ringToOpenMillis = 1440*60*1000;
          } else {
            ringToOpenMillis = 1*60*1000;
          }
          
        }
        startStopRingToOpen(true);
        ringToOpenMillisStart = millis();
    }
    if(messageTemp == String("ON")) {
      startStopRingToOpen(true);
      ringToOpenMillisStart = millis();
      
    }
    if(messageTemp == String("OFF")) {
      startStopRingToOpen(false);
    }
  }
  if (String(topic) == "SimpleBus/SetComparatorVoltage") {
    if(isNumeric(messageTemp)) {
      int compVoltage = messageTemp.toInt();
      //rto maximum time 24 hours, security feature
      
      
      if( compVoltage >= 100 && compVoltage < 1500) {
          mySettings.compVoltLvl = compVoltage;
          saveSettings();
          setComparatorVoltageLimit(compVoltage);
      }
    }
     //Change minutes to millis 

  }

  if (String(topic) == "SimpleBus/SetGain") {
    if(isNumeric(messageTemp)) {
      int gain = messageTemp.toInt();
      //rto maximum time 24 hours, security feature
      
      
      if( gain >= 2 && gain <= 40) {
          mySettings.gain = gain;
          saveSettings();
          setOPVGain(gain);
      }
    }
  }
  if (String(topic) == "SimpleBus/StartConfigMode") {
    if(messageTemp == String("ON")) {
      setup_wifi();
      setOPVGain(mySettings.gain);
      setComparatorVoltageLimit(mySettings.compVoltLvl);
    }
  }
  if (String(topic) == "SimpleBus/StartTeachIn") {
    if(messageTemp == String("ON")) {
      teachInActive = true;
      digitalWrite(LEDPIN, HIGH);
      delay(500);
      digitalWrite(LEDPIN, LOW);
      delay(500);
      digitalWrite(LEDPIN, HIGH);
      delay(500);
      digitalWrite(LEDPIN, LOW);
      delay(500);
      digitalWrite(LEDPIN, HIGH);
    }
  }
  if (String(topic) == "SimpleBus/Restart") {
    if(messageTemp == String("ON")) {
      ESP.restart();   
    }
    

  }
}

void reconnect() {
  // Loop until we're reconnected
  int cnt = 0;
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    client.setServer(mySettings.mqttServer, 1883);
    // Attempt to connect
    if (client.connect("ESP32Doorbell",mySettings.mqttUser,mySettings.mqttPassword)) {
      Serial.println("connected");
      // Subscribe
      client.subscribe("SimpleBus/OpenDoor");
      client.subscribe("SimpleBus/RingToOpen");
      client.subscribe("SimpleBus/SetRingToOpenTime");
      client.subscribe("SimpleBus/SetComparatorVoltage");
      client.subscribe("SimpleBus/SetGain");
      client.subscribe("SimpleBus/Restart");
      client.subscribe("SimpleBus/StartTeachIn");
      client.subscribe("SimpleBus/StartConfigMode");
      client.publish("SimpleBus","subscribed");
    } else {
      cnt++;
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
    if(cnt == 5) {
      digitalWrite(LEDPIN, HIGH);
      setup_wifi(false);
      setOPVGain(mySettings.gain);
      setComparatorVoltageLimit(mySettings.compVoltLvl);
      digitalWrite(LEDPIN, LOW);
    }
  }
}

void connectmqtt()
{
  if(client.connect("ESP32Doorbell",mySettings.mqttUser, mySettings.mqttPassword))   // ESP will connect to mqtt broker with clientID
  {
    Serial.println("connected to MQTT");
    client.subscribe("SimpleBus/OpenDoor");
    client.subscribe("SimpleBus/RingToOpen");
    client.subscribe("SimpleBus/SetRingToOpenTime");
    client.subscribe("SimpleBus/SetComparatorVoltage");
    client.subscribe("SimpleBus/SetGain");
    client.subscribe("SimpleBus/StartTeachIn");
    client.subscribe("SimpleBus/StartConfigMode");
    client.subscribe("SimpleBus/Restart");
  } else {
    reconnect();
  }
}


void setup() 
{
  digitalWrite(LEDPIN, HIGH);
  EEPROM.begin(500);
  Serial.begin(115200);
  delay(1000);
  loadSettings();
  setPowerSavings();
  
  pinMode(INTERCOM_READ_PIN, INPUT_PULLUP);
  pinMode(INTERCOM_WRITE_PIN, OUTPUT);
  pinMode(LEDPIN,OUTPUT);
  interruptActive = true;
  attachInterrupt(INTERCOM_READ_PIN, function_ISR, RISING);
  pauseTimeMeasured = false;
  pauseTimeStart = false;
  ledcSetup(0, 25000, 8);
  ledcAttachPin(INTERCOM_WRITE_PIN, 0);
  if(!structInitialized()) {
    strcpy(mySettings.mqttPassword, "PASSWORD\0");
    strcpy(mySettings.mqttServer, "SERVER\0");
    strcpy(mySettings.mqttUser, "USER\0");
    mySettings.gain = 10;
    mySettings.compVoltLvl = 220;
    mySettings.filter = true;
    mySettings.ringAutomation = false;
    mySettings.intercomAddress = 0;
    saveSettings();
    setup_wifi(false);
  } else {
    setup_wifi();
  }
  
  client.setServer(mySettings.mqttServer, 1883);
  client.setCallback(callback);
  connectmqtt();
  client.loop();
  client.publish("SimpleBus/Reboot", "ON");
  setOPVGain(mySettings.gain);
  setComparatorVoltageLimit(mySettings.compVoltLvl);
  

  lastOpenDoorTime = 0;
  digitalWrite(LEDPIN, LOW);

}


void loop()
{
  if(millis()-lastHeartbeatMillis >= 60000*5) {
    client.publish("SimpleBus/Heartbeat", "ON");
    lastHeartbeatMillis = millis();
  }
  currentState = Button.read();
  if(teachInActive) {
    long diffTimeTeachIn = millis() - teachInTimeActivated;
    if(diffTimeTeachIn > 180000) {
      teachInActive = false;
      digitalWrite(LEDPIN, LOW);
    } 
  }

  if(lastState == LOW && currentState == HIGH)        // button is pressed
    pressedTime = millis();
  else if(lastState == HIGH && currentState == LOW) { // button is released
    releasedTime = millis();

    long pressDuration = releasedTime - pressedTime;

    if( pressDuration < 1000 && pressDuration > 100 ) {
      pressDuration = 0;
     
      digitalWrite(LEDPIN, HIGH);
      setup_wifi(false);
      setOPVGain(mySettings.gain);
      setComparatorVoltageLimit(mySettings.compVoltLvl);
      digitalWrite(LEDPIN, LOW);
      if (!client.connected())
      {
        reconnect();
      }

    }
      
    if( pressDuration > 2000 && pressDuration < 5000 ) {
      pressDuration = 0;
      teachInTimeActivated = millis();
      teachInActive = !teachInActive;
      if(teachInActive) {
        digitalWrite(LEDPIN, HIGH);
        delay(500);
        digitalWrite(LEDPIN, LOW);
        delay(500);
        digitalWrite(LEDPIN, HIGH);
        delay(500);
        digitalWrite(LEDPIN, LOW);
        delay(500);
        digitalWrite(LEDPIN, HIGH);
      } else {
        digitalWrite(LEDPIN, LOW);
      }
      
    }
  }
  lastState = currentState;
  client.loop();
  
  if (!client.connected())
  {
    reconnect();
  }
  if(ringToOpenStarted) {
    if(millis() - ringToOpenMillisStart > ringToOpenMillis) {
      startStopRingToOpen(false);
    }
  }
  if(pinTriggered) {
    processInterrupt();
  }
  if(messageOk) {
    processMessage(messageCode);
    messageOk = false;
  }
}