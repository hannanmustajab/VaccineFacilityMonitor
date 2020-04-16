/*
* Project : Temperature And Humidity Sensor for Vaccine Facility. 
* Description: Cellular Connected Data Logger.
* Author: Abdul Hannan Mustajab

* Sponsor: Thom Harvey ID&D
* Date: 16 April 2020
*/

// v0.10 - Initial Release - BME680 functionality
// v1.00 - Added Temperature sensing and threshold logic.


#define SOFTWARERELEASENUMBER "1.00"                                                        // Keep track of release numbers

// Included Libraries
#include "Adafruit_BME680.h"
#include "math.h"



#define SEALEVELPRESSURE_HPA (1013.25)                                                     // Universal variables

Adafruit_BME680 bme;                                                                       // Instantiate the I2C library

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

const unsigned long webhookWait = 45000;                                                    // How long will we wair for a WebHook response
const unsigned long resetWait = 300000;                                                     // How long will we wait in ERROR_STATE until reset
const int publishFrequency = 1000;                                                          // We can only publish once a second
unsigned long webhookTimeStamp = 0;                                                         // Webhooks...
unsigned long resetTimeStamp = 0;                                                           // Resets - this keeps you from falling into a reset loop
unsigned long lastPublish = 0;                                                              // Can only publish 1/sec on avg and 4/sec burst
int sampleRate;                                                                             // Sample rate for idle state.

// Program Variables
int resetCount;                                                                             // Counts the number of times the Electron has had a pin reset
int alertCount;                                                                             // Keeps track of non-reset issues - think of it as an indication of health
bool ledState = LOW;                                                                        // variable used to store the last LED status, to toggle the light
bool waiting = false;
bool dataInFlight = true;
const char* releaseNumber = SOFTWARERELEASENUMBER;                                          // Displays the release on the menu
byte controlRegister;                                                                       // Stores the control register values
bool verboseMode;                                                                           // Enables more active communications for configutation and setup

// Variables related to alerting on temperature thresholds. 

bool upperTemperatureThresholdCrossed=false;                                                // Set this to true if the upper temp threshold is crossed
bool lowerTemperatureThresholdCrossed=false;                                                // Set this to true if the lower temp threshold is crossed
bool upperHumidityThresholdCrossed=false;                                                   // Set this to true if the upper humidty threshold is crossed
bool lowerHumidityThresholdCrossed=false;                                                   // Set this to true if the lower humidty threshold is crossed
bool thresholdCrossAcknowledged=false;                                                      // Once  sms is sent, Put all the variables to false again. 
float upperTemperatureThreshold = 30;
float lowerTemperatureThreshold = 2;
float upperHumidityThreshold = 90;
float lowerHumidityThreshold = 60;
char smsString[64];



// Variables Related To Particle Mobile Application Reporting
char TVOCString[16];                                                                        // Simplifies reading values in the Particle Mobile Application
char temperatureString[16];
char humidityString[16];
char altitudeString[16];
char pressureString[16];
char batteryString[16];

// Time Period Related Variables
static int thresholdTimeStamp;                                                              // Global time vairable
byte currentHourlyPeriod;                                                                   // This is where we will know if the period changed
byte currentDailyPeriod;                                                                    // We will keep daily counts as well as period counts


// This section is where we will initialize sensor specific variables, libraries and function prototypes
double temperatureInC = 0;
double relativeHumidity = 0;
double pressureHpa = 0;
double gasResistanceKOhms = 0;
double approxAltitudeInM = 0;

void setup()                                                                                // Note: Disconnected Setup()
{
  char StartupMessage[64] = "Startup Successful";                                           // Messages from Initialization
  state = IDLE_STATE;

  pinMode(HumidityLED, OUTPUT);                                                             // declare the Blue LED Pin as an output
  pinMode(tempLED,OUTPUT);
  
  char responseTopic[125];
  String deviceID = System.deviceID();                                                      // Multiple Electrons share the same hook - keeps things straight
  deviceID.toCharArray(responseTopic,125);
  Particle.subscribe(responseTopic, UbidotsHandler, MY_DEVICES);                            // Subscribe to the integration response event
       
  Particle.variable("Release",releaseNumber);
  Particle.variable("temperature", temperatureString);
  Particle.variable("humidity", humidityString);
  Particle.variable("pressure", pressureString);
  
  Particle.function("Measure-Now",measureNow);
  Particle.function("Verbose-Mode",setVerboseMode);
  Particle.function("Temp-Upper-Limit",setUpperTempLimit);
  Particle.function("Temp-Lower-Limit",setLowerTempLimit);
  Particle.function("Humidity-Lower-Limit",setLowerHumidityLimit);
  Particle.function("Humidty-upper-Limit",setUpperHumidityLimit);

  // And set the flags from the control register
  // controlRegister = EEPROM.read(MEM_MAP::controlRegisterAddr);                          // Read the Control Register for system modes so they stick even after reset
  // verboseMode     = (0b00001000 & controlRegister);                                     // Set the verboseMode
  if (!bme.begin()) {                                                                      // Start the BME680 Sensor
    resetTimeStamp = millis();
    snprintf(StartupMessage,sizeof(StartupMessage),"Error - BME680 Initialization");
    state = ERROR_STATE;
    resetTimeStamp = millis();
  }

  // Set up the sampling paramatures
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150); // 320*C for 150 ms

  takeMeasurements();                                                                      // For the benefit of monitoring the device

 
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
   
    static int TimePassed = 0;
    if (verboseMode && state != oldState) publishStateTransition();
   
    if (Time.hour() != currentHourlyPeriod || Time.minute() - TimePassed >= 10 ) {
      TimePassed = Time.minute();
      state = MEASURING_STATE;                                                     
      }
    
    else if ((upperTemperatureThresholdCrossed \
    || lowerTemperatureThresholdCrossed \
    || upperHumidityThresholdCrossed \
    || lowerHumidityThresholdCrossed)!= 0 && (Time.minute() - thresholdTimeStamp > 10))                 // Send threshold message after every 10 minutes.
    {
      state = THRESHOLD_CROSSED;
    }
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
  char data[256];                                                                           // Store the date in this character array - not global
  snprintf(data, sizeof(data), "{\"Temperature\":%4.1f, \"Humidity\":%4.1f, \"Pressure\":%4.1f}", temperatureInC, relativeHumidity, pressureHpa);
  Particle.publish("storage-facility-hook", data, PRIVATE);
  currentHourlyPeriod = Time.hour();                                                        // Change the time period
  currentDailyPeriod = Time.day();
  dataInFlight = true;                                                                      // set the data inflight flag
  webhookTimeStamp = millis();
}

void UbidotsHandler(const char *event, const char *data)                                    // Looks at the response from Ubidots - Will reset Photon if no successful response
{                                                                                           // Response Template: "{{hourly.0.status_code}}" so, I should only get a 3 digit number back
  char dataCopy[strlen(data)+1];                                                            // data needs to be copied since if (Particle.connected()) Particle.publish() will clear it
  strncpy(dataCopy, data, sizeof(dataCopy));                                                // Copy - overflow safe
  if (!strlen(dataCopy)) {                                                                  // First check to see if there is any data
    if (Particle.connected()) Particle.publish("Ubidots Hook", "No Data", PRIVATE);
    return;
  }
  int responseCode = atoi(dataCopy);                                                        // Response is only a single number thanks to Template
  if ((responseCode == 200) || (responseCode == 201))
  {
    if (Particle.connected()) Particle.publish("State","Response Received", PRIVATE);
    lastPublish = millis();
    dataInFlight = false;                                                                   // Data has been received
  }
  else if (Particle.connected()) Particle.publish("Ubidots Hook", dataCopy, PRIVATE);       // Publish the response code
}

// These are the functions that are part of the takeMeasurements call

bool takeMeasurements() {

  bme.setGasHeater(320, 150);                                                                 // 320*C for 150 ms
  bme.performReading();                                                                       // Take measurement from all the sensors

  temperatureInC = bme.temperature;
  snprintf(temperatureString,sizeof(temperatureString),"%4.1f*C", temperatureInC);

  relativeHumidity = bme.humidity;
  snprintf(humidityString,sizeof(humidityString),"%4.1f%%", relativeHumidity);

  pressureHpa = bme.pressure / 100.0;
  snprintf(pressureString,sizeof(pressureString),"%4.1fHPa", pressureHpa);

  // If lower temperature threshold is crossed, Set the flag true. 
  if (temperatureInC < lowerTemperatureThreshold) lowerTemperatureThresholdCrossed = true;

  // If upper temperature threshold is crossed, Set the flag true. 
  if (temperatureInC > upperTemperatureThreshold) upperTemperatureThresholdCrossed = true;

  // If lower temperature threshold is crossed, Set the flag true. 
  if (relativeHumidity < lowerHumidityThreshold) lowerHumidityThresholdCrossed = true;

  // If lower temperature threshold is crossed, Set the flag true. 
  if (temperatureInC < lowerTemperatureThreshold) lowerTemperatureThresholdCrossed = true;

  return 1;
}

// Function to send sms for threshold values

bool ThresholdCrossed(){
  
  if ((lowerTemperatureThreshold || upperTemperatureThreshold)!=0){                               // If lower or upper threshold conditions are True. 
    
    BlinkLED(tempLED);                                                                            // Start Blinking LED
    snprintf(smsString,sizeof(smsString),"ALERT FROM KumvaIoT: Temperature Threshold Crossed. Current Temperature is: %4.1f",temperatureInC);
    Particle.publish("sms-webhook",smsString,PRIVATE);                                            // Send the webhook . 
    thresholdCrossAcknowledged = true;                                                            // Once, Published the data. Set all flags to false again . 
  }

  if ((upperHumidityThresholdCrossed || lowerHumidityThresholdCrossed)!=0){                       // If lower or upper threshold conditions are True. 
    
    BlinkLED(HumidityLED);                                                                        // Start Blinking LED
    snprintf(smsString,sizeof(smsString),"ALERT FROM KumvaIoT: Humidity Threshold Crossed. Current Humidity is: %4.1f and Current Temperature is: %4.1f",temperatureInC,relativeHumidity);
    Particle.publish("sms-webhook",smsString,PRIVATE);
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
  upperTemperatureThreshold = value.toFloat();
  waitUntil(meterParticlePublish);
  Particle.publish("Upper Threshold Set",String(value),PRIVATE);
  return 1;
}

int setLowerTempLimit(String value)
{
  lowerTemperatureThreshold = value.toFloat();
  waitUntil(meterParticlePublish);
  Particle.publish("Lower Threshold Set",String(value),PRIVATE);
  return 1;

}

int setUpperHumidityLimit(String value)
{
  upperHumidityThreshold = value.toFloat();
  waitUntil(meterParticlePublish);
  Particle.publish("Upper Threshold Set",String(value),PRIVATE);
  return 1;
}

int setLowerHumidityLimit(String value)
{
  lowerHumidityThreshold = value.toFloat();
  waitUntil(meterParticlePublish);
  Particle.publish("Lower Threshold Set",String(value),PRIVATE);
  return 1;
}