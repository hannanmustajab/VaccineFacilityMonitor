/******************************************************/
//       THIS IS A GENERATED FILE - DO NOT EDIT       //
/******************************************************/

#include "Particle.h"
#line 1 "/Users/chipmc/Documents/Maker/Particle/Projects/IDD_Cold-Chain/src/VaccineFacilityMonitor.ino"
/*
* Project : Temperature And Humidity Sensor for Vaccine Facility. 
* Description: Cellular Connected Data Logger.
* Author: Abdul Hannan Mustajab

* Sponsor: Thom Harvey ID&D
* Date: 16 April 2020
*/

// v0.10 - Initial Release - BME680 functionality
// v1.00 - Added Temperature sensing and threshold logic.
// v1.01 - Fixed Zero temperature check. 
// v1.02 - Fixed epoch timestamp to fix error with aws quicksights. 
// v1.03 - Fixed AWS Response template.
// v1.04 - Sent alert to aws to count number of alerts
// v1.05 - Added a new branch for ubidots handler
// v1.06 - Added particle variables to show the current threshold values. 
// v1.07 - Testing alerting system for ubidots dashboard
// v1.08 - Added EEPROM 
// v1.09 - Added SHT31 code support.
// v1.10 - Changed reporting time to 5 minutes from 30. 
// v2.00 - Changed reporting time to 15 minutes and added battery support.
// v3.00 - Changed payload size to 100 bytes from 256 and turned verbose mode off. 
// v4.00 - Added product ID version as variable.
// v6.00 - Testing the sampling period fix.
// v7.00 - Set the sampling period to 20 mins.
// v8.00 - Testing the sampling period fix. 
// v9.00 - Added watchdog support and changed from semi automatic to automatic. 
// v10.00 - Added keepalive .
// v11.01 - Updated the reporting to send a one character packet every 5 minutes in addition to reporting every 20, moved from EEPROM to FRAM
// v11.02 - Added a timer to keep the connection alive added clock support as well
// v11.03 - Initial testing complete - removing comments on keepAlive for testing
// v12.00 - Pushed to repo - moving to Particle for dissemination
// v12.01 - Minor messaging updates
// v13.00 - Fixed threshold settings in default
// V14.00 - Still tweaking alerts and LED flashing - found the bug 
// v15.00 - Updated to set better defaults for new devices


/* 
  Todo : 
    Add alerting to EEPROM
*/



void setup();
void loop();
void loadSystemDefaults();
void loadAlertDefaults();
void checkSystemValues();
void checkAlertsValues();
void watchdogISR();
void petWatchdog();
void keepAliveMessage();
void sendEvent();
void UbidotsHandler(const char *event, const char *data);
bool takeMeasurements();
void blinkLED(int LED);
int measureNow(String command);
int setVerboseMode(String command);
void publishStateTransition(void);
int setUpperTempLimit(String value);
int setLowerTempLimit(String value);
int setUpperHumidityLimit(String value);
int setLowerHumidityLimit(String value);
int setThirdPartySim(String command);
int setKeepAlive(String command);
void updateThresholdValue();
void getBatteryContext();
#line 47 "/Users/chipmc/Documents/Maker/Particle/Projects/IDD_Cold-Chain/src/VaccineFacilityMonitor.ino"
PRODUCT_ID(12401);
PRODUCT_VERSION(15); 
const char releaseNumber[8] = "15.00";                                                      // Displays the release on the menu

// Define the memory map - note can be EEPROM or FRAM - moving to FRAM for speed and to avoid memory wear
namespace FRAM {                                                                         // Moved to namespace instead of #define to limit scope
  enum Addresses {
    versionAddr           = 0x00,                                                           // Where we store the memory map version number - 8 Bits
    sysStatusAddr         = 0x01,                                                           // This is the status of the device
    alertStatusAddr       = 0x50,                                                           // Where we store the status of the alerts in the system
    sensorDataAddr        = 0xA0                                                            // Where we store the latest sensor data readings
   };
};

const int FRAMversionNumber = 5;                                                            // Increment this number each time the memory map is changed

struct systemStatus_structure {                     
  uint8_t structuresVersion;                                                                // Version of the data structures (system and data)
  bool thirdPartySim;                                                                       // If this is set to "true" then the keep alive code will be executed
  int keepAlive;                                                                            // Keep alive value for use with 3rd part SIMs
  uint8_t connectedStatus;
  uint8_t verboseMode;
  uint8_t lowBatteryMode;
  int stateOfCharge;                                                                        // Battery charge level
  uint8_t batteryState;                                                                     // Stores the current battery state
  int resetCount;                                                                           // reset count of device (0-256)
  unsigned long lastHookResponse;                                                           // Last time we got a valid Webhook response
} sysStatus;

struct alertsStatus_structure {
  bool upperTemperatureThresholdCrossed;                                                    // Set this to true if the upper temp threshold is crossed
  bool lowerTemperatureThresholdCrossed;                                                    // Set this to true if the lower temp threshold is crossed
  bool upperHumidityThresholdCrossed;                                                       // Set this to true if the upper humidty threshold is crossed
  bool lowerHumidityThresholdCrossed;                                                       // Set this to true if the lower humidty threshold is crossed
  bool thresholdCrossedFlag;                                                                // If any of the thresholds have been crossed
  float upperTemperatureThreshold;                                                          // Values set below that trigger alerts
  float lowerTemperatureThreshold;
  float upperHumidityThreshold;
  float lowerHumidityThreshold;
} alertsStatus;

struct sensor_data_struct {                                                               // Here we define the structure for collecting and storing data from the sensors
  bool validData;
  unsigned long timeStamp;
  float temperatureInC;
  float relativeHumidity; 
  int stateOfCharge;
} sensorData;


// Included Libraries
#include "math.h"
#include "adafruit-sht31.h"
#include "PublishQueueAsyncRK.h"                                                            // Async Particle Publish
#include "MB85RC256V-FRAM-RK.h"                                                             // Rickkas Particle based FRAM Library
#include "MCP79410RK.h"                                                                     // Real Time Clock

// Prototypes and System Mode calls
SYSTEM_MODE(AUTOMATIC);                                                                     // This will enable user code to start executing automatically.
SYSTEM_THREAD(ENABLED);                                                                     // Means my code will not be held up by Particle processes.
STARTUP(System.enableFeature(FEATURE_RESET_INFO));
Adafruit_SHT31 sht31 = Adafruit_SHT31();
MB85RC64 fram(Wire, 0);                                                                     // Rickkas' FRAM library
MCP79410 rtc;                                                                               // Rickkas MCP79410 libarary
retained uint8_t publishQueueRetainedBuffer[2048];                                          // Create a buffer in FRAM for cached publishes
PublishQueueAsync publishQueue(publishQueueRetainedBuffer, sizeof(publishQueueRetainedBuffer));
Timer keepAliveTimer(1000, keepAliveMessage);

// State Machine Variables
enum State { INITIALIZATION_STATE, ERROR_STATE, IDLE_STATE, MEASURING_STATE, REPORTING_STATE, RESP_WAIT_STATE};
char stateNames[8][26] = {"Initialize", "Error", "Idle", "Measuring","Reporting", "Response Wait"};
State state = INITIALIZATION_STATE;
State oldState = INITIALIZATION_STATE;

// Pin Constants
const int blueLED =   D7;                                                               // This LED is on the Electron itself
const int wakeUpPin = D8;  
const int donePin = D5;

volatile bool watchdogFlag=false;                                                           // Flag to let us know we need to pet the dog

// Timing Variables
const unsigned long webhookWait = 45000;                                                    // How long will we wair for a WebHook response
const unsigned long resetWait   = 300000;                                                   // How long will we wait in ERROR_STATE until reset

unsigned long webhookTimeStamp  = 0;                                                        // Webhooks...
unsigned long resetTimeStamp    = 0;                                                        // Resets - this keeps you from falling into a reset loop
bool dataInFlight = false;

// Variables Related To Particle Mobile Application Reporting
// Simplifies reading values in the Particle Mobile Application
char temperatureString[16];
char humidityString[16];
char batteryContextStr[16];                                                                 // One word that describes whether the device is getting power, charging, discharging or too cold to charge
char batteryString[16];
char upperTemperatureThresholdString[24];                                                   // String to show the current threshold readings.                         
char lowerTemperatureThresholdString[24];                                                   // String to show the current threshold readings.                         
char upperHumidityThresholdString[24];                                                      // String to show the current threshold readings.                         
char lowerHumidityThresholdString[24];                                                      // String to show the current threshold readings.       
bool sysStatusWriteNeeded = false;                                                       // Keep track of when we need to write
bool alertsStatusWriteNeeded = false;         
bool sensorDataWriteNeeded = false; 


// Time Period Related Variables
const int wakeBoundary = 0*3600 + 20*60 + 0;                                                // 0 hour 20 minutes 0 seconds
const int keepAliveBoundary = 0*3600 + 5*60 +0;                                             // How often do we need to send a ping to keep the connection alive - start with 5 minutes - *** related to keepAlive value in Setup()! ***

void setup()                                                                                // Note: Disconnected Setup()
{
  pinMode(wakeUpPin,INPUT);                                                                 // This pin is active HIGH, 
  pinMode(donePin,OUTPUT);                                                                  // Allows us to pet the watchdog
  pinMode(blueLED, OUTPUT);                                                                 // declare the Blue LED Pin as an output

  petWatchdog();                                                                            // Pet the watchdog - This will reset the watchdog time period AND 
  attachInterrupt(wakeUpPin, watchdogISR, RISING);                                          // The watchdog timer will signal us and we have to respond

  char StartupMessage[64] = "Startup Successful";                                           // Messages from Initialization
  state = INITIALIZATION_STATE;

  char responseTopic[125];
  String deviceID = System.deviceID();                                                      // Multiple Electrons share the same hook - keeps things straight
  deviceID.toCharArray(responseTopic,125);
  Particle.subscribe(responseTopic, UbidotsHandler, MY_DEVICES);                            // Subscribe to the integration response event
  
  Particle.variable("Release",releaseNumber);
  Particle.variable("temperature", temperatureString);
  Particle.variable("humidity", humidityString);
  Particle.variable("temperature-Upper",upperTemperatureThresholdString);
  Particle.variable("temperature-lower",lowerTemperatureThresholdString);
  Particle.variable("humidity-upper",upperHumidityThresholdString);
  Particle.variable("humidity-lower",lowerHumidityThresholdString);
  Particle.variable("Battery", batteryString);                                              // Battery level in V as the Argon does not have a fuel cell
  Particle.variable("BatteryContext",batteryContextStr);
  Particle.variable("Keep Alive Sec",sysStatus.keepAlive);
  Particle.variable("3rd Party Sim", sysStatus.thirdPartySim);

  
  Particle.function("Measure-Now",measureNow);
  Particle.function("Verbose-Mode",setVerboseMode);
  Particle.function("Temp-Upper-Limit",setUpperTempLimit);
  Particle.function("Temp-Lower-Limit",setLowerTempLimit);
  Particle.function("Humidity-Lower-Limit",setLowerHumidityLimit);
  Particle.function("Humidty-upper-Limit",setUpperHumidityLimit);
  Particle.function("Keep Alive",setKeepAlive);
  Particle.function("3rd Party Sim", setThirdPartySim);

  rtc.setup();                                                        // Start the real time clock
  rtc.clearAlarm();                                                   // Ensures alarm is still not set from last cycle

  publishQueue.publish("Time",Time.timeStr(Time.now()), PRIVATE);


  if (!sht31.begin(0x44)) {                                                                 // Start the i2c connected SHT-31 sensor
    snprintf(StartupMessage,sizeof(StartupMessage),"Error - SHT31 Initialization");
    state = ERROR_STATE;
    resetTimeStamp = millis();
  }

  // Load FRAM and reset variables to their correct values
  fram.begin();                                                                             // Initialize the FRAM module

  byte tempVersion;
  fram.get(FRAM::versionAddr, tempVersion);
  if (tempVersion != FRAMversionNumber) {                                                   // Check to see if the memory map in the sketch matches the data on the chip
    fram.erase();                                                                           // Reset the FRAM to correct the issue
    fram.put(FRAM::versionAddr, FRAMversionNumber);                                         // Put the right value in
    fram.get(FRAM::versionAddr, tempVersion);                                               // See if this worked
    if (tempVersion != FRAMversionNumber) state = ERROR_STATE;                              // Device will not work without FRAM
    else {
      loadSystemDefaults();                                                                 // Out of the box, we need the device to be awake and connected
      loadAlertDefaults();
    }
  }
  else {
    fram.get(FRAM::sysStatusAddr,sysStatus);                                                // Loads the System Status array from FRAM
    fram.get(FRAM::alertStatusAddr,alertsStatus);                                           // Load the current values array from FRAM
  }

  checkSystemValues();                                                                      // Make sure System values are all in valid range
  checkAlertsValues();                                                                      // Make sure that Alerts values are all in a valid range

  if (sysStatus.thirdPartySim) {
    waitUntil(Particle.connected); 
    Particle.keepAlive(sysStatus.keepAlive);                                              // Set the keep alive value
    keepAliveTimer.changePeriod(sysStatus.keepAlive*1000);                                  // Will start the repeating timer
  }

  takeMeasurements();                                                                       // For the benefit of monitoring the device
  updateThresholdValue();                                                                   // For checking values of each device

  if(sysStatus.verboseMode) publishQueue.publish("Startup",StartupMessage,PRIVATE);                       // Let Particle know how the startup process went

  if (state == INITIALIZATION_STATE) state = IDLE_STATE;                                    // We made it throughgo let's go to idle
}

void loop()
{
  switch(state) {
  case IDLE_STATE:                                                                          // Idle state - brackets only needed if a variable is defined in a state    
    if (sysStatus.verboseMode && state != oldState) publishStateTransition();

    if (!(Time.now() % wakeBoundary)) state = MEASURING_STATE;                                                     
    
    break;

  case MEASURING_STATE:                                                                     // Take measurements prior to sending
    if (sysStatus.verboseMode && state != oldState) publishStateTransition();

    if (takeMeasurements()) alertsStatus.thresholdCrossedFlag = true;                       // A return of a "true" value indicates that one of the thresholds have been crossed
    else {
      alertsStatus.thresholdCrossedFlag = false;
      digitalWrite(blueLED,LOW);                                                            // Just in case it was on an on-flash
    }
    alertsStatusWriteNeeded = true;

    state = REPORTING_STATE;
    break;

  case REPORTING_STATE: 
    if (sysStatus.verboseMode && state != oldState) publishStateTransition();               // Reporting - hourly or on command
    if (Particle.connected()) {
      if (Time.hour() == 12) Particle.syncTime();                                           // Set the clock each day at noon
      sendEvent();                                                                          // Send data to Ubidots
      state = RESP_WAIT_STATE;                                                              // Wait for Response
    }
    else {
      state = ERROR_STATE;
      resetTimeStamp = millis();
    }
    break;

  case RESP_WAIT_STATE:
    if (sysStatus.verboseMode && state != oldState) publishStateTransition();

    if (!dataInFlight && (Time.now() % wakeBoundary))                                       // Response received back to IDLE state - make sure we don't allow repetivie reporting events
    {
     state = IDLE_STATE;
    }
    else if (millis() - webhookTimeStamp > webhookWait) {                                   // If it takes too long - will need to reset
      resetTimeStamp = millis();
      publishQueue.publish("spark/device/session/end", "", PRIVATE);                        // If the device times out on the Webhook response, it will ensure a new session is started on next connect
      state = ERROR_STATE;                                                                  // Response timed out
      resetTimeStamp = millis();
    }
    break;

  
  case ERROR_STATE:                                                                         // To be enhanced - where we deal with errors
    if (state != oldState) publishStateTransition();
    if (millis() > resetTimeStamp + resetWait)
    {
      if (Particle.connected()) publishQueue.publish("State","Error State - Reset", PRIVATE); // Brodcast Reset Action
      delay(2000);
      System.reset();
    }
    break;
  }

  rtc.loop();                                                                               // keeps the clock up to date

  if (watchdogFlag) petWatchdog();                                                          // Watchdog flag is raised - time to pet the watchdog

  if (alertsStatus.thresholdCrossedFlag) blinkLED(blueLED);

  if (sysStatusWriteNeeded) {
    fram.put(FRAM::sysStatusAddr,sysStatus);
    sysStatusWriteNeeded = false;
  }
  if (alertsStatusWriteNeeded) {
    fram.put(FRAM::alertStatusAddr,alertsStatus);
    alertsStatusWriteNeeded = false;
  }
  if (sensorDataWriteNeeded) {
    fram.put(FRAM::sensorDataAddr,sensorData);
    sensorDataWriteNeeded = false;
  }

}


void loadSystemDefaults() {                                                                 // Default settings for the device - connected, not-low power and always on
  if (Particle.connected()) publishQueue.publish("Mode","Loading System Defaults", PRIVATE);
  sysStatus.thirdPartySim = 1;
  sysStatus.keepAlive = 120;
  sysStatus.structuresVersion = 1;
  sysStatus.verboseMode = false;
  sysStatus.lowBatteryMode = false;
  fram.put(FRAM::sysStatusAddr,sysStatus);                                                  // Write it now since this is a big deal and I don't want values over written
}

void loadAlertDefaults() {                                                                  // Default settings for the device - connected, not-low power and always on
  if (Particle.connected()) publishQueue.publish("Mode","Loading Alert Defaults", PRIVATE);
  alertsStatus.upperTemperatureThreshold = 30;
  alertsStatus.lowerTemperatureThreshold = 2;
  alertsStatus.upperHumidityThreshold = 90;
  alertsStatus.lowerHumidityThreshold= 5;
  fram.put(FRAM::alertStatusAddr,alertsStatus);                                             // Write it now since this is a big deal and I don't want values over written
}

void checkSystemValues() {                                                                  // Checks to ensure that all system values are in reasonable range 
  if (sysStatus.connectedStatus < 0 || sysStatus.connectedStatus > 1) {
    if (Particle.connected()) sysStatus.connectedStatus = true;
    else sysStatus.connectedStatus = false;
  }
  if (sysStatus.keepAlive < 0 || sysStatus.keepAlive > 1200) sysStatus.keepAlive = 600;
  if (sysStatus.verboseMode < 0 || sysStatus.verboseMode > 1) sysStatus.verboseMode = false;
  if (sysStatus.lowBatteryMode < 0 || sysStatus.lowBatteryMode > 1) sysStatus.lowBatteryMode = 0;
  if (sysStatus.resetCount < 0 || sysStatus.resetCount > 255) sysStatus.resetCount = 0;
  sysStatusWriteNeeded = true;
}

void checkAlertsValues() {                                                                  // Checks to ensure that all system values are in reasonable range 
  if (alertsStatus.lowerTemperatureThreshold < 0.0  || alertsStatus.lowerTemperatureThreshold > 20.0) alertsStatus.lowerTemperatureThreshold = 3.0;
  if (alertsStatus.upperTemperatureThreshold < 20.0 || alertsStatus.upperTemperatureThreshold > 90.0) alertsStatus.upperTemperatureThreshold = 33.0;
  if (alertsStatus.lowerHumidityThreshold < 0.0     || alertsStatus.lowerHumidityThreshold > 50.0)    alertsStatus.lowerHumidityThreshold = 13.0;
  if (alertsStatus.upperHumidityThreshold < 20.0    || alertsStatus.upperHumidityThreshold > 90.0)    alertsStatus.upperHumidityThreshold = 63.0;
  alertsStatusWriteNeeded = true;
}

void watchdogISR()
{
  watchdogFlag = true;
}

void petWatchdog()
{
  digitalWrite(donePin, HIGH);                                                              // Pet the watchdog
  digitalWrite(donePin, LOW);
  watchdogFlag = false;
}

void keepAliveMessage() {
  Particle.publish("*", PRIVATE,NO_ACK);
}

void sendEvent()
{
  char data[100];                 
  snprintf(data, sizeof(data), "{\"Temperature\":%4.1f, \"Humidity\":%4.1f,\"Battery\":%i}", sensorData.temperatureInC, sensorData.relativeHumidity,sensorData.stateOfCharge);
  publishQueue.publish("storage-facility-hook", data, PRIVATE);
  dataInFlight = true;                                                                      // set the data inflight flag
  webhookTimeStamp = millis();
}

void UbidotsHandler(const char *event, const char *data)                                    // Looks at the response from Ubidots - Will reset Photon if no successful response
{                                                                                           // Response Template: "{{hourly.0.status_code}}" so, I should only get a 3 digit number back
  // Response Template: "{{hourly.0.status_code}}"
  if (!data) {                                                                    // First check to see if there is any data
    if (sysStatus.verboseMode) {
      publishQueue.publish("Ubidots Hook", "No Data", PRIVATE);
    }
    return;
  }
  int responseCode = atoi(data);                                                  // Response is only a single number thanks to Template
  if ((responseCode == 200) || (responseCode == 201))
  {
    if (sysStatus.verboseMode) {
      publishQueue.publish("State", "Response Received", PRIVATE);
    }
    alertsStatus.upperHumidityThresholdCrossed = false;
    alertsStatus.lowerHumidityThresholdCrossed = false;
    alertsStatus.upperTemperatureThresholdCrossed = false;
    alertsStatus.lowerTemperatureThresholdCrossed = false;
    alertsStatusWriteNeeded = true;
    dataInFlight = false;    

  }
  else if (sysStatus.verboseMode) {  
    publishQueue.publish("Ubidots Hook", data, PRIVATE);                              // Publish the response code
  }


}

// These are the functions that are part of the takeMeasurements call

bool takeMeasurements() {
  char thresholdMessage[64] = "All within thresholds";
  bool haveAnyAlertsBeenSet = false;
  sensorData.validData = false;

  if (sht31.readTemperature()){
    sensorData.temperatureInC = sht31.readTemperature();
    snprintf(temperatureString,sizeof(temperatureString),"%4.1f*C", sensorData.temperatureInC);

    sensorData.relativeHumidity = sht31.readHumidity();
    snprintf(humidityString,sizeof(humidityString),"%4.1f%%", sensorData.relativeHumidity);

    sensorData.stateOfCharge = int(System.batteryCharge());
    snprintf(batteryString, sizeof(batteryString), "%i %%", sensorData.stateOfCharge);

    // If lower temperature threshold is crossed, Set the flag true. 
    if (sensorData.temperatureInC < alertsStatus.lowerTemperatureThreshold) {
      alertsStatus.lowerTemperatureThresholdCrossed = true;
      snprintf(thresholdMessage, sizeof(thresholdMessage), "Low Temp Alert %4.2f < %4.2f", sensorData.temperatureInC, alertsStatus.lowerTemperatureThreshold);
      haveAnyAlertsBeenSet = true;
    }

    // If upper temperature threshold is crossed, Set the flag true. 
    if (sensorData.temperatureInC > alertsStatus.upperTemperatureThreshold) {
      alertsStatus.upperTemperatureThresholdCrossed = true;
      snprintf(thresholdMessage, sizeof(thresholdMessage), "High Temp Alert %4.2f > %4.2f", sensorData.temperatureInC, alertsStatus.upperTemperatureThreshold);
      haveAnyAlertsBeenSet = true;
    }

    // If lower humidity threshold is crossed, Set the flag true. 
    if (sensorData.relativeHumidity < alertsStatus.lowerHumidityThreshold) {
      alertsStatus.lowerHumidityThresholdCrossed = true;
      snprintf(thresholdMessage, sizeof(thresholdMessage), "Low Humidity Alert %4.2f < %4.2f", sensorData.relativeHumidity, alertsStatus.lowerHumidityThreshold);
      haveAnyAlertsBeenSet = true;
    }

    // If upper humidity threshold is crossed, Set the flag true. 
    if (sensorData.relativeHumidity > alertsStatus.upperHumidityThreshold) {
      alertsStatus.upperHumidityThresholdCrossed = true;
      snprintf(thresholdMessage, sizeof(thresholdMessage), "High Humidity Alert %4.2f < %4.2f", sensorData.relativeHumidity, alertsStatus.upperHumidityThreshold);
      haveAnyAlertsBeenSet = true;
    }
  }

    getBatteryContext();                                                                    // Check what the battery is doing.

    // Indicate that this is a valid data array and store it
    sensorData.validData = true;
    sensorData.timeStamp = Time.now();
    sensorDataWriteNeeded = true;
    alertsStatusWriteNeeded = true;  

    if (haveAnyAlertsBeenSet) publishQueue.publish("Alerts", thresholdMessage,PRIVATE);

    return haveAnyAlertsBeenSet;
}

// Function to Blink the LED for alerting. 
void blinkLED(int LED)                                                                      // Non-blocking LED flashing routine
{
  const int flashingFrequency = 1000;
  static unsigned long lastStateChange = 0;

  if (millis() - lastStateChange > flashingFrequency) {
    digitalWrite(LED,!digitalRead(LED));
    lastStateChange = millis();
  }
}

// These are the particle functions that allow you to configure and run the device
// They are intended to allow for customization and control during installations
// and to allow for management.


int measureNow(String command) // Function to force sending data in current hour
{
  if (command == "1") {
    state = MEASURING_STATE;
    return 1;
  }
  else return 0;
}

int setVerboseMode(String command) // Function to force sending data in current hour
{
  if (command == "1")
  {
    sysStatus.verboseMode = true;
    publishQueue.publish("Mode","Set Verbose Mode",PRIVATE);
    sysStatusWriteNeeded = true;
    return 1;
  }
  else if (command == "0")
  {
    sysStatus.verboseMode = false;
    publishQueue.publish("Mode","Cleared Verbose Mode",PRIVATE);
    sysStatusWriteNeeded = true;
    return 1;
  }
  else return 0;
}


void publishStateTransition(void)
{
  char stateTransitionString[40];
  snprintf(stateTransitionString, sizeof(stateTransitionString), "From %s to %s", stateNames[oldState],stateNames[state]);
  oldState = state;
  if(Particle.connected()) publishQueue.publish("State Transition",stateTransitionString, PRIVATE);
}

// These function will allow to change the upper and lower limits for alerting the customer. 

int setUpperTempLimit(String value)
{
  alertsStatus.upperTemperatureThreshold = value.toFloat();
  publishQueue.publish("Upper Temperature Threshold Set",String(value),PRIVATE);
  updateThresholdValue();
  return 1;
}

int setLowerTempLimit(String value)
{
  alertsStatus.lowerTemperatureThreshold = value.toFloat();
  publishQueue.publish("Lower Temperature Threshold Set",String(value),PRIVATE);
  updateThresholdValue();
  return 1;

}

int setUpperHumidityLimit(String value)
{
  alertsStatus.upperHumidityThreshold = value.toFloat();
  publishQueue.publish("Upper Humidity Threshold Set",String(value),PRIVATE);
  updateThresholdValue();
  return 1;
}

int setLowerHumidityLimit(String value)
{
  alertsStatus.lowerHumidityThreshold = value.toFloat();
  publishQueue.publish("Lower Humidity Threshold Set",String(value),PRIVATE);
  updateThresholdValue();
  return 1;
}

int setThirdPartySim(String command) // Function to force sending data in current hour
{
  if (command == "1")
  {
    sysStatus.thirdPartySim = true;
    Particle.keepAlive(sysStatus.keepAlive);                                                // Set the keep alive value
    keepAliveTimer.changePeriod(sysStatus.keepAlive*1000);                                  // Will start the repeating timer
    if (Particle.connected()) publishQueue.publish("Mode","Set to 3rd Party Sim", PRIVATE);
    sysStatusWriteNeeded = true;
    return 1;
  }
  else if (command == "0")
  {
    sysStatus.thirdPartySim = false;
    if (Particle.connected()) publishQueue.publish("Mode","Set to Particle Sim", PRIVATE);
    sysStatusWriteNeeded = true;
    return 1;
  }
  else return 0;
}


int setKeepAlive(String command)
{
  char * pEND;
  char data[256];
  int tempTime = strtol(command,&pEND,10);                                                  // Looks for the first integer and interprets it
  if ((tempTime < 0) || (tempTime > 1200)) return 0;                                        // Make sure it falls in a valid range or send a "fail" result
  sysStatus.keepAlive = tempTime;
  Particle.keepAlive(sysStatus.keepAlive);                                                // Set the keep alive value
  keepAliveTimer.changePeriod(sysStatus.keepAlive*1000);                                  // Will start the repeating timer
  snprintf(data, sizeof(data), "Keep Alive set to %i sec",sysStatus.keepAlive);
  publishQueue.publish("Keep Alive",data, PRIVATE);
  sysStatusWriteNeeded = true;                                                           // Need to store to FRAM back in the main loop
  return 1;
}

// This function updates the threshold value string in the console. 
void updateThresholdValue()
{
    snprintf(upperTemperatureThresholdString,sizeof(upperTemperatureThresholdString),"Temp_Max : %3.1f", alertsStatus.upperTemperatureThreshold);
    snprintf(lowerTemperatureThresholdString,sizeof(lowerTemperatureThresholdString),"Temp_Min : %3.1f",alertsStatus.lowerTemperatureThreshold);
    snprintf(upperHumidityThresholdString,sizeof(upperHumidityThresholdString),"Humidity_Max: %3.1f",alertsStatus.upperHumidityThreshold);
    snprintf(lowerHumidityThresholdString,sizeof(lowerHumidityThresholdString),"Humidity_Min : %3.1f",alertsStatus.lowerHumidityThreshold);
    alertsStatusWriteNeeded = true;                                                         // This function is called when there is a change so, we need to update the FRAM
} 


void getBatteryContext() 
{
  const char* batteryContext[7] ={"Unknown","Not Charging","Charging","Charged","Discharging","Fault","Diconnected"};
  // Battery conect information - https://docs.particle.io/reference/device-os/firmware/boron/#batterystate-
  sysStatus.batteryState = System.batteryState();
  snprintf(batteryContextStr, sizeof(batteryContextStr),"%s", batteryContext[sysStatus.batteryState]);
  sysStatusWriteNeeded = true;
}

