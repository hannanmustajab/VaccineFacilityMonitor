# Temperature And Humidity Sensor for Cold Chain Products - Cellular Connected Data Logger

![Project Banner](project_banner.png)

## Description

This project is an Internet of Things (IoT) based solution for monitoring temperature and humidity in cold chain products using Particle Boron Devices and a Carrier Board built by Chip McClellend. The data logger is designed to collect temperature and humidity data from the SHT31x sensor and transmit it over a cellular connection. 

Additionally, it also reports the battery level of the device. The project enables remote monitoring of cold chain products to ensure they are stored within specified temperature and humidity ranges, critical for preserving product quality and safety during transportation and storage.

## Author
**Chip McClelland**
- Email: chip@seeinsights.com


**Abdul Hannan Mustajab**
- Email: hannan@kumva.io




## Date

- Original Project Date: 16 April 2020
- Latest Revision Date: 27th July 2023

## Features

- Cellular connectivity for remote data transmission.
- Temperature and humidity sensing using the SHT31x sensor.
- Real-Time Clock (RTC) functionality with MCP79410RK library for accurate timestamping.
- Non-volatile memory for data storage using the MB85RC256V-FRAM-RK library.
- 20 Minutes reporting frequency.
- Use of third party sim. (Make sure to keep the KeepAlive value to 120).
- Particle functions for remote control:
  - **Measure Now:** Trigger an immediate temperature and humidity measurement.
  - **Verbose Mode:** Toggle verbose logging for detailed information (Should be used only for debugging). 
  - **KeepAlive:** Maintain the device connection with the Particle cloud. 
  - **Third Party Sim:** Support for using third-party SIM cards for cellular connectivity.

## Hardware Requirements

- Particle Boron Device: Used for cellular connectivity and remote management.
- Carrier Board (by Chip McClellend): Provides the necessary interfaces for the sensors and FRAM module.
- SHT31x Sensor: Measures temperature and humidity.
- Power Supply/Battery: Provides power to the data logger.

## Libraries Used

1. **Adafruit-sht31:** Library to interface with the SHT31x sensor and obtain temperature and humidity readings.
2. **PublishQueueAsyncRK:** Library for queuing messages and efficiently transmitting data over the cellular connection.
3. **MCP79410RK:** Library for interacting with the MCP79410 Real-Time Clock module.
4. **MB85RC256V-FRAM-RK:** Library for working with the MB85RC256V FRAM module to store data in non-volatile memory.

## Particle Functions

1. **Measure Now:**
   Call this function remotely to trigger an immediate measurement of temperature and humidity using the SHT31x sensor. The data will be sent over the cellular connection.

2. **Verbose Mode:**
   Toggle this function to enable or disable verbose logging. When verbose mode is on, the data logger provides more detailed information in its logs.

3. **KeepAlive:**
   Use this function to maintain the device connection with the Particle cloud, preventing disconnection due to inactivity.

4. **Third Party Sim:**
   This function enables the use of third-party SIM cards for cellular connectivity, providing flexibility in choosing a suitable data plan.

## Reporting Duration

The data logger is programmed to report temperature, humidity, and battery level data every 20 minutes. This duration can be adjusted as needed to meet specific monitoring requirements.
