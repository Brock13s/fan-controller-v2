This project is a temperature monitoring and wireless transmission system built using an ESP32 microcontroller. The system reads temperature data from a Dallas DS18B20 temperature sensor, maps the temperature range on an LED bargraph from 15c to 30c using a 74HC595 shift register, and transmits the temperature readings wirelessly using an FS1000A 433MHz RF transmitter. 
The transmit happens at a time from 7 in morning to fan off and 18 so 6pm to turn fan on or either the fan turns on if the temperature is high 30c.

Additionally, the system includes two status LEDs (red and green) to indicate the success or failure of connecting to an NTP (Network Time Protocol) server and fetching the current time. 

Key Features
Temperature Sensing: Uses a Dallas DS18B20 temperature sensor to measure ambient temperature.
LED Bargraph Visualization: Displays the temperature range from 15°C to 30°C using a bargraph controlled by a 74HC595 shift register.
RF Transmission: Sends the temperature data wirelessly via an FS1000A 433MHz RF transmitter and therefore control ceiling fan with either temperature or ntp time. 
NTP Synchronization: Sync with a time server pool every 12 hours without using a rtc module
Green LED: Indicates successful NTP connection and time retrieval.
Red LED: Indicates failure to connect to the NTP server.
ESP32-based Wi-Fi Connectivity: Enables the system to connect to the internet for NTP synchronization.

;)
