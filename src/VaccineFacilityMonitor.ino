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
// v4.10 - Testing the sampling period fix.

/* 
  Todo : 
    Add alerting to EEPROM
*/


PRODUCT_ID(12401);
PRODUCT_VERSION(4);

#define PRODUCT_ID "12401"                                                        // Keep track of release numbers
#define SOFTWARERELEASENUMBER "4.10"                                                        // Keep track of release numbers

// Included Libraries
#include "math.h"
#include "adafruit-sht31.h"


Adafruit_SHT31 sht31 = Adafruit_SHT31();


// Prototypes and System Mode calls
SYSTEM_MODE(SEMI_AUTOMATIC);                                                               // This will enable user code to start executing automatically.
SYSTEM_THREAD(ENABLED);                                                                    // Means my code will not be held up by Particle processes.
STARTUP(System.enableFeature(FEATURE_RESET_INFO));

// State Machine Variables
enum State { INITIALIZATION_STATE, ERROR_STATE, IDLE_STATE, MEASURING_STATE,THRESHOLD_CROSSED, REPORTING_STATE, RESP_WAIT_STATE};
char stateNames[8][26] = {"Initialize", "Error", "Idle", "Measuring", "Threshold Crossed","Reporting", "Response Wait"};
State state = INITIALIZATION_STATE;
State oldState = INITIALIZATION_STATE;

// Pin Constants
const int HumidityLED =   D7;                                                               // This LED is on the Electron itself
const int tempLED     =   D5;

// Timing Variables

int publishInterval;                                                                        // Publish interval for sending data. 
const unsigned long webhookWait = 45000;                                                    // How long will we wair for a WebHook response
const unsigned long resetWait   = 300000;                                                   // How long will we wait in ERROR_STATE until reset
const int publishFrequency      = 1000;                                                     // We can only publish once a second
unsigned long webhookTimeStamp  = 0;                                                        // Webhooks...
unsigned long resetTimeStamp    = 0;                                                        // Resets - this keeps you from falling into a reset loop
unsigned long lastPublish       = 0;                                                        // Can only publish 1/sec on avg and 4/sec burst
int sampleRate;                                                                             // Sample rate for idle state.
time_t t;                                                                                   // Global time vairable

// Program Variables
int resetCount;                                                                             // Counts the number of times the Electron has had a pin reset
int alertCount;                                                                             // Keeps track of non-reset issues - think of it as an indication of health
bool dataInFlight = true;
const char* releaseNumber = SOFTWARERELEASENUMBER;                                          // Displays the release on the menu
byte controlRegister;                                                                       // Stores the control register values
bool verboseMode=false;                                                                      // Enables more active communications for configutation and setup
float voltage;                                                                              // Voltage level of the LiPo battery - 3.6-4.2V range
const char* productNumber = PRODUCT_ID;                                          // Displays the release on the menu


// Variables related to alerting on temperature thresholds. 

bool upperTemperatureThresholdCrossed = false;                                                // Set this to true if the upper temp threshold is crossed
bool lowerTemperatureThresholdCrossed = false;                                                // Set this to true if the lower temp threshold is crossed
bool upperHumidityThresholdCrossed    = false;                                                // Set this to true if the upper humidty threshold is crossed
bool lowerHumidityThresholdCrossed    = false;                                                // Set this to true if the lower humidty threshold is crossed
bool thresholdCrossAcknowledged       = false;                                                // Once  sms is sent, Put all the variables to false again. 
float upperTemperatureThreshold       = 30;
float lowerTemperatureThreshold       = 2;
float upperHumidityThreshold          = 90;
float lowerHumidityThreshold          = 60;
char smsString[256];



// Variables Related To Particle Mobile Application Reporting
char TVOCString[16];                                                                        // Simplifies reading values in the Particle Mobile Application
char temperatureString[16];
char humidityString[16];
char altitudeString[16];
char pressureString[16];
char batteryContextStr[16];                                                                 // One word that describes whether the device is getting power, charging, discharging or too cold to charge
char batteryString[16];
char upperTemperatureThresholdString[24];                                                     // String to show the current threshold readings.                         
char lowerTemperatureThresholdString[24];                                                     // String to show the current threshold readings.                         
char upperHumidityThresholdString[24];                                                        // String to show the current threshold readings.                         
char lowerHumidityThresholdString[24];                                                        // String to show the current threshold readings.                         

// Time Period Related Variables
static int thresholdTimeStamp;                                                                // Global time vairable
byte currentHourlyPeriod;                                                                     // This is where we will know if the period changed
time_t currentCountTime;                                                                      // Global time vairable
byte currentMinutePeriod;                                                                     // control timing when using 5-min samp intervals


// This section is where we will initialize sensor specific variables, libraries and function prototypes
double temperatureInC = 0;
double relativeHumidity = 0;
double pressureHpa = 0;
double gasResistanceKOhms = 0;
double approxAltitudeInM = 0;

// Define the memory map - note can be EEPROM or FRAM
namespace MEM_MAP {                                                                       // Moved to namespace instead of #define to limit scope
  enum Addresses {
    versionAddr           = 0x00,                                                         // Where we store the memory map version number - 8 Bits
    alertCountAddr        = 0x01,                                                         // Where we store our current alert count - 8 Bits
    resetCountAddr        = 0x02,                                                         // This is where we keep track of how often the Argon was reset - 8 Bits
    currentCountsTimeAddr = 0x03,                                                         // Time of last report - 32 bits
    sensorData1Object     = 0x08                                                          // The first data object - where we start writing data
   };
};

// Keypad struct for mapping buttons, notes, note values, LED array index, and default color
struct sensor_data_struct {                                                               // Here we define the structure for collecting and storing data from the sensors
  bool validData;
  unsigned long timeStamp;
  float batteryVoltage;
  double temperatureInC;
  double relativeHumidity;
  float upperTemperatureThreshold;     
  float lowerTemperatureThreshold;       
  float upperHumidityThreshold;          
  float lowerHumidityThreshold;   
  int stateOfCharge;
};

sensor_data_struct sensor_data;



#define MEMORYMAPVERSION 2                          // Lets us know if we need to reinitialize the memory map


void setup()                                                                                // Note: Disconnected Setup()
{
  Serial.begin(115200);
  Serial.println("SHT31 test");
  char StartupMessage[64] = "Startup Successful";                                           // Messages from Initialization
  state = IDLE_STATE;
  Cellular.on();
  pinMode(HumidityLED, OUTPUT);                                                             // declare the Blue LED Pin as an output
  pinMode(tempLED,OUTPUT);
  
  char responseTopic[125];
  String deviceID = System.deviceID();                                                      // Multiple Electrons share the same hook - keeps things straight
  deviceID.toCharArray(responseTopic,125);
  Particle.subscribe(responseTopic, UbidotsHandler, MY_DEVICES);                            // Subscribe to the integration response event
  
  Particle.variable("Product Version",productNumber);
  Particle.variable("Release",releaseNumber);
  Particle.variable("temperature", temperatureString);
  Particle.variable("humidity", humidityString);
  Particle.variable("temperature-Upper",upperTemperatureThresholdString);
  Particle.variable("temperature-lower",lowerTemperatureThresholdString);
  Particle.variable("humidity-upper",upperHumidityThresholdString);
  Particle.variable("humidity-lower",lowerHumidityThresholdString);
  Particle.variable("Battery", batteryString);                                    // Battery level in V as the Argon does not have a fuel cell
  Particle.variable("BatteryContext",batteryContextStr);

  
  Particle.function("Measure-Now",measureNow);
  Particle.function("Verbose-Mode",setVerboseMode);
  Particle.function("Temp-Upper-Limit",setUpperTempLimit);
  Particle.function("Temp-Lower-Limit",setLowerTempLimit);
  Particle.function("Humidity-Lower-Limit",setLowerHumidityLimit);
  Particle.function("Humidty-upper-Limit",setUpperHumidityLimit);

  // And set the flags from the control register
  // controlRegister = EEPROM.read(MEM_MAP::controlRegisterAddr);                          // Read the Control Register for system modes so they stick even after reset
  // verboseMode     = (0b00001000 & controlRegister);                                     // Set the verboseMode
  if (! sht31.begin(0x44)) {                                                                      // Start the BME680 Sensor
    resetTimeStamp = millis();
    snprintf(StartupMessage,sizeof(StartupMessage),"Error - SHT31 Initialization");
    Serial.println("Couldn't find SHT31");
    state = ERROR_STATE;
    resetTimeStamp = millis();
  }

  takeMeasurements();                                                                      // For the benefit of monitoring the device
  updateThresholdValue();                                                                  // For checking values of each device
  Check_Available_Credit();
  
  if(!connectToParticle()) {
    state = ERROR_STATE;                                                                   // We failed to connect can reset here or go to the ERROR state for remediation
    resetTimeStamp = millis();
    snprintf(StartupMessage, sizeof(StartupMessage), "Failed to connect");
  }

  if(verboseMode) Particle.publish("Startup",StartupMessage,PRIVATE);                      // Let Particle know how the startup process went
  lastPublish = millis();
}

void loop()
{

  switch(state) {
  
  case IDLE_STATE:
  {
    static int TimePassed = 0;
    if (verboseMode && state != oldState) publishStateTransition();
   
    if (Time.hour() != currentHourlyPeriod || Time.minute() - TimePassed >= 15) {
      TimePassed = Time.minute();
      state = MEASURING_STATE;                                                     
      }
    
    else if ((upperTemperatureThresholdCrossed \
    || lowerTemperatureThresholdCrossed \
    || upperHumidityThresholdCrossed \
    || lowerHumidityThresholdCrossed)!= 0 && (Time.minute() - thresholdTimeStamp > 4))                 // Send threshold message after every 10 minutes.
    {
     
      state = THRESHOLD_CROSSED;
    }
  }s
    break;

  case THRESHOLD_CROSSED:
    if (verboseMode && state != oldState) publishStateTransition();
    
    if(takeMeasurements()!=0){                                                                          // Take measurements again before reporting.       
      ThresholdCrossed();
      state = IDLE_STATE;
    }else
    {
      state= ERROR_STATE;
    }
    break;

  case MEASURING_STATE:                                                                     // Take measurements prior to sending
    if (verboseMode && state != oldState) publishStateTransition();
    if (!takeMeasurements())
    {
      state = ERROR_STATE;
      resetTimeStamp = millis();
      if (verboseMode) {
        waitUntil(meterParticlePublish);
        Particle.publish("State","Error taking Measurements",PRIVATE);
        lastPublish = millis();
      }
    }
    else state = REPORTING_STATE;
    break;

  case REPORTING_STATE:
    if (verboseMode && state != oldState) publishStateTransition();                         // Reporting - hourly or on command
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
    if (verboseMode && state != oldState) publishStateTransition();
    if (!dataInFlight)                                                // Response received back to IDLE state
    {
     state = IDLE_STATE;
    }
    else if (millis() - webhookTimeStamp > webhookWait) {             // If it takes too long - will need to reset
      resetTimeStamp = millis();
      Particle.publish("spark/device/session/end", "", PRIVATE);      // If the device times out on the Webhook response, it will ensure a new session is started on next connect
      state = ERROR_STATE;                                            // Response timed out
      resetTimeStamp = millis();
    }
    break;

  
  case ERROR_STATE:                                                                         // To be enhanced - where we deal with errors
    if (verboseMode && state != oldState) publishStateTransition();
    if (millis() > resetTimeStamp + resetWait)
    {
      if (Particle.connected()) Particle.publish("State","Error State - Reset", PRIVATE);    // Brodcast Reset Action
      delay(2000);
      System.reset();
    }
    break;
  }
}

void sendEvent()
{
  char data[100];           
   for (int i = 0; i < 4; i++) {
    sensor_data = EEPROM.get(8 + i*100,sensor_data);                  // This spacing of the objects - 100 - must match what we put in the takeMeasurements() function
  }          
  snprintf(data, sizeof(data), "{\"Temperature\":%4.1f, \"Humidity\":%4.1f,\"Battery\":%i}", sensor_data.temperatureInC, sensor_data.relativeHumidity,sensor_data.stateOfCharge);
  Particle.publish("storage-facility-hook", data, PRIVATE);
  Check_Available_Credit();
  currentCountTime = Time.now();
  EEPROM.write(MEM_MAP::currentCountsTimeAddr, currentCountTime);
  currentHourlyPeriod = Time.hour();                                                        // Change the time period
  dataInFlight = true;                                                                      // set the data inflight flag
  webhookTimeStamp = millis();
}

void UbidotsHandler(const char *event, const char *data)                                    // Looks at the response from Ubidots - Will reset Photon if no successful response
{                                                                                           // Response Template: "{{hourly.0.status_code}}" so, I should only get a 3 digit number back
    // Response Template: "{{hourly.0.status_code}}"
  if (!data) {                                                                    // First check to see if there is any data
    if (verboseMode) {
      waitUntil(meterParticlePublish);
      Particle.publish("Ubidots Hook", "No Data", PRIVATE);
    }
    return;
  }
  int responseCode = atoi(data);                                                  // Response is only a single number thanks to Template
  if ((responseCode == 200) || (responseCode == 201))
  {
    if (verboseMode) {
      waitUntil(meterParticlePublish);
      Particle.publish("State", "Response Received", PRIVATE);
      
    }
    dataInFlight = false;    
  }
  else if (verboseMode) {
    waitUntil(meterParticlePublish);      
    Particle.publish("Ubidots Hook", data, PRIVATE);                              // Publish the response code
  }

}

// These are the functions that are part of the takeMeasurements call

bool takeMeasurements() {

  // bme.setGasHeater(320, 150);                                                                 // 320*C for 150 ms
  
  sensor_data.validData = false;

  if (sht31.readTemperature()){
    
    int reportCycle;                                                    // Where are we in the sense and report cycle
    currentCountTime = Time.now();
    int currentMinutes = Time.minute();                                // So we only have to check once
    switch (currentMinutes) {
      case 15:
        reportCycle = 0;                                                // This is the first of the sample-only periods
        break;  
      case 30:
        reportCycle = 1;                                                // This is the second of the sample-only periods
        break; 
      case 45:
        reportCycle = 2;                                                // This is the third of the sample-only periods
        break; 
      case 0:
        reportCycle = 3;                                                // This is the fourth of the sample-only periods
        break; 
      default:
        reportCycle = 3;  
        break;                                                          // just in case
  }

    sensor_data.temperatureInC = sht31.readTemperature();
    snprintf(temperatureString,sizeof(temperatureString),"%4.1f*C", sensor_data.temperatureInC);

    sensor_data.relativeHumidity = sht31.readHumidity();
    snprintf(humidityString,sizeof(humidityString),"%4.1f%%", sensor_data.relativeHumidity);

    sensor_data.stateOfCharge = int(System.batteryCharge());
    snprintf(batteryString, sizeof(batteryString), "%i %%", sensor_data.stateOfCharge);

    // Get battery voltage level
    // sensor_data.batteryVoltage = analogRead(BATT) * 0.0011224;                   // Voltage level of battery
    // snprintf(batteryString, sizeof(batteryString), "%4.1fV", sensor_data.batteryVoltage);  // *** Volts not percent

    // If lower temperature threshold is crossed, Set the flag true. 
    if (temperatureInC < sensor_data.lowerTemperatureThreshold) lowerTemperatureThresholdCrossed = true;

    // If upper temperature threshold is crossed, Set the flag true. 
    if (temperatureInC > sensor_data.upperTemperatureThreshold) upperTemperatureThresholdCrossed = true;

    // If lower temperature threshold is crossed, Set the flag true. 
    if (relativeHumidity < sensor_data.lowerHumidityThreshold) lowerHumidityThresholdCrossed = true;

    // If lower temperature threshold is crossed, Set the flag true. 
    if (relativeHumidity > sensor_data.upperHumidityThreshold) upperHumidityThresholdCrossed = true;

     getBatteryContext();                   // Check what the battery is doing.

    // Indicate that this is a valid data array and store it
    sensor_data.validData = true;
    sensor_data.timeStamp = Time.now();
    EEPROM.put(8 + 100*reportCycle,sensor_data);     
    return 1;
  }                                                                       // Take measurement from all the sensors
  else {
        Particle.publish("Log", "Failed to perform reading :(");
        Serial.println("Failed to take reading!");
        return 0;

  }
 
}

// Function to send sms for threshold values

bool ThresholdCrossed(){
  
  if ((lowerTemperatureThresholdCrossed || upperTemperatureThresholdCrossed)!=0){                               // If lower or upper threshold conditions are True. 
    char data[32];
    snprintf(data,sizeof(data),"{\"alert-temperature\":%4.1f}",temperatureInC);
    BlinkLED(tempLED);                                                                            // Start Blinking LED
    // snprintf(smsString,sizeof(smsString),"ALERT FROM KumvaIoT: Temperature Threshold Crossed. Current Temperature is: %4.1f",temperatureInC);
    // Particle.publish("sms-webhook",smsString,PRIVATE);                                            // Send the webhook . 
    waitUntil(meterParticlePublish);
    Particle.publish("cc-alert-webhook",data,PRIVATE);
    thresholdCrossAcknowledged = true;                                                            // Once, Published the data. Set all flags to false again . 
  }

  if ((upperHumidityThresholdCrossed || lowerHumidityThresholdCrossed)!=0){                       // If lower or upper threshold conditions are True. 
    
    char humidity_data[32];
    snprintf(humidity_data,sizeof(humidity_data),"{\"alert-humidity\":%4.1f}",relativeHumidity);
    BlinkLED(HumidityLED);                                                                        // Start Blinking LED
    // snprintf(smsString,sizeof(smsString),"ALERT FROM KumvaIoT: Humidity Threshold Crossed. Current Humidity is: %4.1f and Current Temperature is: %4.1f",temperatureInC,relativeHumidity);
    // Particle.publish("sms-webhook",smsString,PRIVATE);
    waitUntil(meterParticlePublish);
    Particle.publish("cc-alert-webhook",humidity_data,PRIVATE);
    thresholdCrossAcknowledged = true; 
  }

  thresholdTimeStamp = Time.minute();

  if (thresholdCrossAcknowledged == true)
  {
    upperHumidityThresholdCrossed = false;
    lowerHumidityThresholdCrossed = false;
    upperTemperatureThreshold     = false;
    lowerHumidityThresholdCrossed = false;
  }
  return 1;
}


// These functions control the connection and disconnection from Particle
bool connectToParticle() {
  Particle.connect();
  // wait for *up to* 5 minutes
  for (int retry = 0; retry < 300 && !waitFor(Particle.connected,1000); retry++) {
    Particle.process();
  }
  if (Particle.connected()) return 1;                               // Were able to connect successfully
  else return 0;                                                    // Failed to connect
}

bool disconnectFromParticle()
{
  Particle.disconnect();                                          // Otherwise Electron will attempt to reconnect on wake
  delay(1000);                                                    // Bummer but only should happen once an hour
  return true;
}

bool notConnected() {
  return !Particle.connected();                             // This is a requirement to use waitFor
}

// Function to Blink the LED for alerting. 
void BlinkLED(int LED){
  digitalWrite(LED,HIGH);
  delay(1000);
  digitalWrite(LED,LOW);
  delay(1000);
}

// These are the particle functions that allow you to configure and run the device
// They are intended to allow for customization and control during installations
// and to allow for management.


int measureNow(String command) // Function to force sending data in current hour
{
  if (command == "1") {
    state = MEASURING_STATE;
    Check_Available_Credit();
    return 1;
  }
  else return 0;
}

int setVerboseMode(String command) // Function to force sending data in current hour
{
  if (command == "1")
  {
    verboseMode = true;
    Particle.publish("Mode","Set Verbose Mode",PRIVATE);
    return 1;
  }
  else if (command == "0")
  {
    verboseMode = false;
    Particle.publish("Mode","Cleared Verbose Mode",PRIVATE);
    return 1;
  }
  else return 0;
}


void publishStateTransition(void)
{
  char stateTransitionString[40];
  snprintf(stateTransitionString, sizeof(stateTransitionString), "From %s to %s", stateNames[oldState],stateNames[state]);
  oldState = state;
  if(Particle.connected()) {
    waitUntil(meterParticlePublish);
    Particle.publish("State Transition",stateTransitionString, PRIVATE);
    lastPublish = millis();
  }
  Serial.println(stateTransitionString);
}


bool meterParticlePublish(void)
{
  if(millis() - lastPublish >= publishFrequency) return 1;
  else return 0;
}

// These function will allow to change the upper and lower limits for alerting the customer. 

int setUpperTempLimit(String value)
{
  sensor_data.upperTemperatureThreshold = value.toFloat();
  waitUntil(meterParticlePublish);
  Particle.publish("Upper Threshold Set",String(value),PRIVATE);
  updateThresholdValue();
  return 1;
}

int setLowerTempLimit(String value)
{
  sensor_data.lowerTemperatureThreshold = value.toFloat();
  waitUntil(meterParticlePublish);
  Particle.publish("Lower Threshold Set",String(value),PRIVATE);
  updateThresholdValue();

  return 1;

}

int setUpperHumidityLimit(String value)
{
  sensor_data.upperHumidityThreshold = value.toFloat();
  waitUntil(meterParticlePublish);
  Particle.publish("Upper Threshold Set",String(value),PRIVATE);
  updateThresholdValue();

  return 1;
}

int setLowerHumidityLimit(String value)
{
  sensor_data.lowerHumidityThreshold = value.toFloat();
  waitUntil(meterParticlePublish);
  Particle.publish("Lower Threshold Set",String(value),PRIVATE);
  updateThresholdValue();
  return 1;
}

// This function updates the threshold value string in the console. 
void updateThresholdValue(){
    snprintf(upperTemperatureThresholdString,sizeof(upperTemperatureThresholdString),"Temp_Max : %3.1f",sensor_data.upperTemperatureThreshold);
    snprintf(lowerTemperatureThresholdString,sizeof(lowerTemperatureThresholdString),"Temp_Mix : %3.1f",sensor_data.lowerTemperatureThreshold);
    snprintf(upperHumidityThresholdString,sizeof(upperHumidityThresholdString),"Humidity_Max: %3.1f",sensor_data.upperHumidityThreshold);
    snprintf(lowerHumidityThresholdString,sizeof(lowerHumidityThresholdString),"Humidity_Min : %3.1f",sensor_data.lowerHumidityThreshold);
} 

// void getBatteryCharge()
// {
//   voltage = analogRead(BATT) * 0.0011224;
//   snprintf(batteryString, sizeof(batteryString), "%3.1f V", voltage);
// }

void getBatteryContext() {
  const char* batteryContext[7] ={"Unknown","Not Charging","Charging","Charged","Discharging","Fault","Diconnected"};
  // Battery conect information - https://docs.particle.io/reference/device-os/firmware/boron/#batterystate-

  snprintf(batteryContextStr, sizeof(batteryContextStr),"%s", batteryContext[System.batteryState()]);

}

