# MQTT-SN Embed
## Overview

Library and example files providing MQTT-SN v1.2 capability across different embedded devices.
Builds primarily for Raspberry Pi on Linux, but can be built in the Arduino IDE with some copying of files (steps to be defined).

The primary goal is to provide a minimal viable implementation that allows a client to publish messages to the gateway server. The typical use-case is a small device with sensors sending to an always-on Raspberry Pi server. Small devices could be on battery and sleep for intervals to preserve power. Arduino compatible devices fit the small device use case with the Linux based Raspberry Pi able to consume and drive other automation.

Next goal will be device subscriptions to extend the automation capability to small devices and other systems attached to the gateway.

Tested devices
* Raspberry Pi 2 and Zero
* Teensy LC
* STM32 Blue Pill

Supported drivers
* Nordic RF24

The code was originally written for just RF24 radio comms and has since been refactored to be driver agnostic.

## How to build
The code depends on a driver framework (code avaialble from github).  
Required code should be cloned from github  
[Hardware Framework](https://github.com/AidanHolmes/PiHardware)  
`> git clone https://github.com/AidanHolmes/PiHardware hardware`

[Nordic RF24 Driver](https://github.com/AidanHolmes/NordicRF24)  
`> git clone https://github.com/AidanHolmes/NordicRF24 NordicRF24`

The standard Makefiles assume that these are all installed as siblings for the build. Adjust Makefile for alternative locations of files.  
Finally clone this repository (as a sibling to the other project directories)

Build the code from Linux in this repository by running  
`> make`

## Using example code
Two examples files are created
* mqttsnclient
* mqttsnserver

Both take parameters for the RF24 driver which gives some flexibility when wiring up. 

### Client and server parameters
Usage:  -c ce -i irq -a address -b address [-n clientname] [-o channel] [-s 250|1|2] [-x]

Options:  
-c GPIO CE pin for RF24  
-i Interrupt pin for RF24  
-a Address of device  
-b Broadcast address for whole radio network (should match other devices in network)  
-n Client name (optional, client only)  
-o Channel 0 to 125 for RF24 (optional)  
-s Speed 250KBit, 1MBit, 2MBit for RF24 (optional)  
-x Enable ACKs for RF24 (optional)  

## Limitations
Small AtMega 328 devices with only 2k SRAM are not big enough to run this code alongside an appropriate driver. Many optimisations can be made to shrink the memory footprint, but I suspect that even getting down to 2k will not allow enough room for any practical use of the code.

The code supports packets of data and not stream based implementations. Transmission is limited by supported packet sizes.

Server code is hardwired to mosquitto API to provide the gateway

The code is still work in-progress, but hoping to be complete soon following a huge amount of work to decouple from existing drivers and making the code as portable as possible.

## To-do
* Subscription to topics
* Sleeping clients (some implementation, but not fully tested)
* Forwarders
* Encryption (all plain text communication, can be intercepted, replayed and spoofed)

## Deviations from 1.2 protocol
* The code is liberal with the suggested field lengths where a driver only supports small data packets. Expect shorter available lengths for ClientId strings
* Server will provide ReturnCode responses using existing response codes as errors. The official protocol is silent for most error cases
* Clients will not attempt to respond to a gwinfo - this is done to prevent too much potential chatter
* Client and server does not implement any random delays in responses as suggested
* Congestion messages are not processed by the client code automatically, although the codes are exposed for user specific behaviour to be adjusted
* Clients will try any gateway available, regardless of any recent connection issues to a previously connected gateway