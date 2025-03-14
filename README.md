# Simplebus2 MQTT Bridge [![stability-mature](https://img.shields.io/badge/stability-mature-008000.svg)](https://github.com/mkenney/software-guides/blob/master/STABILITY-BADGES.md#mature)

### Table of contents
[1. Overview](#Overview)  
[2. Software](#Software)  
[3. ESPhome](#ESPhome)  
[4. Hardware](#Hardware)  
[5. Mechanics](#Mechanics)  
[6. Disclaimer](#Disclaimer)  
[7. Credits](#Credits) 

## Overview

This project focuses on integrating Comelit intercom systems running the Simplebus2 protocol into a Smart Home, typically equipped with an MQTT broker for information exchange. The following functions have been implemented:

-   Doorbell signal main entrance
-   Doorbell signal apartment door
-   Opening main entrance door
-   Ring-to-Open (automatic opening)
-   WiFi Manager
-   Configuration via Web Interface and MQTT
-   Teach-in of intercom adress
-   OTA updates

![Bridge front](https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/fad2f1d9-91a6-4b36-82e3-777d47f65b6b)
![Bridge front PCBA](https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/cac0c7ac-0554-418a-90bc-81e9a0638eaf)
![Bridge connector](https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/37c8e988-82e1-4e89-92e0-a10634ce108b)

## Software

### Configuration

A short push on the button (SW1) starts the configuration mode and the bridge opens a WiFi access point named "Config_MQTT_SimpleBus2" for 4 minutes. After connecting to this access point from any device, the following main menu will be shown. If the configured network can't be found or is out of range, the configuration mode will also be launched. The activated configuration mode is signaled by the LED. Alternatively the configuration mode can be started via MQTT.

<img width="500" alt="WiFi manager main menu" src="https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/7d9be9a3-b389-42de-b5eb-53c7c4b0da48">

After selecting "Configure WiFi" the credentials of the bridge and hardware / firmware configuration can be edited:

<img width="250" alt="WiFi manager configuration" src="https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/bd1f1602-053f-4ca7-87d4-2d1543fa69aa">

Keep in mind that the ESP32 is not equipped with 5GHz WiFi, only 2,4GHz will work.
The web interface is not supported by every browser in all functions (e.g. firmware update), for best compatibility use Chrome or Edge.

### Hardware tuning
"gain" and "voltage level" are parameters to tune in to the specific installation circumstances depending on cable lenght and resistance of the signal path where gain is the factor the OPV amplifies the line signal at the input and level is the threshold of the comparator before the S2 signal goes to the ESP32s GPIO. A gain of 20 and a voltage level of 600 works good from tests in a building with about 20m cable lenght.

### Open Automation
If selected, the automatic opening of the door after receiving a ring signal is possible. This has to be additionally activated and configured via MQTT. If not selected, the automatic open funcionality is completely disabled.

### Firmware update
The option "Update" in the main menu shows a dialog where a .bin file can be uploaded over the air. This is a good option if the bridge is buried in the switch box. The existing configuration will be kept.

### Adress adjustment
Each intercom unit has its own 8-bits address, which is configured via an 8-way DIP switch during installation. See the interior of your Comelit intercom with the DIP switch in red and translate the bits to your corresponding decimal number, which is usually your appartement or floor number. In some intercoms the DIP-switch can be found on the back, in others you need to open the housing. The address DIP switch is marked S1 and follows LSB logic like in the following table:

 Switch No.| 1 | 2 | 3 | 4 |  5 |  6 |  7 |  8  |
 --------- |:-:|:-:|:-:|:-:|:--:|:--:|:--:|:---:|
 Value     | 1 | 2 | 4 | 8 | 16 | 32 | 64 | 128 |

<img width="300" alt="DIP switch panel" src="https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/e777526b-f2ed-47c3-a666-8bb2cc70a9e0">

The above intercom for example is addressed to 12.

The adress of the intercom can be easiest teached by pressing the button (SW1) for 3...4s. The bridge will acknowledge with 2x blinking the LED and is then ready to be teached by the next ring that occurs on the bus in the next 3 minutes. The LED will stay on in this time. If the adress has successfully teached in, the LED shuts off and the bridge listens and talks from now on to this specific adress. Alternatively the teach-in process can be started via MQTT. As a third way the choice of the intercom adress can be done manually in the web interface.

### MQTT data structure
**Published topics**

 Topic                          | Values         | Notes
 ------------------------------ |:--------------:| ---------------------------------------------------
 SimpleBus/FloorDoor            | ON             | bell rings on floor door of apartement
 SimpleBus/EntryDoor            | ON             | bell rings on entry door of building
 SimpleBus/Reboot               | ON             | bridge has booted and is listening
 SimpleBus/RingToOpenStatus     | ON / OFF       | 'ring to open' status is on or off
 SimpleBus/RingToOpenOpened     | ON             | is sent if 'ring to open' function opened the door automatically
 SimpleBus/Heartbeat            | ON             | sent every 5min to show online status

**Subscribed topics**

 Topic                          | Values         | Notes
 ------------------------------ |:--------------:| ---------------------------------------------------
 SimpleBus/OpenDoor             | ON             | open the door
 SimpleBus/RingToOpen           | ON / OFF       | activate 'ring to open' (40 minutes default, automatically shut off after bell ring)
 SimpleBus/SetRingToOpenTime    | 1 ... 1440     | activate 'ring to open' for x minutes (max. 24hrs)
 SimpleBus/SetGain              | 2 ... 40       | set gain factor of the amplifier U4 (20 default)
 SimpleBus/SetComparatorVoltage | 100 ... 1500   | set threshold of the comparator U6 to x millivolts, e.g. 600 (default)
 SimpleBus/StartConfigMode      | ON             | start web configuration portal on IP address 192.168.4.1
 SimpleBus/StartTeachIn         | ON / OFF       | ON activates teach-in routine, learns intercom address from the bus; OFF cancels the teaching process
 SimpleBus/Restart              | ON             | restarts the ESP32

### Dependencies
The following components are required to build the firmware. Other versions may also work but are not tested.

 | Component    | Version     | Link                                             |
 | ------------ | :---------- | ------------------------------------------------ |
 | EEPROM       | 2.0.0       | default Arduino                                  |
 | WiFi         | 2.0.0       | default Arduino                                  |
 | Wire         | 2.0.0       | default Arduino                                  |
 | MultiButton  | 1.2.0       | https://github.com/poelstra/arduino-multi-button |
 | Debounce     | 1.2.0       | https://github.com/wkoch/Debounce                |
 | PubSubClient | 2.8         | https://github.com/knolleary/pubsubclient        |
 | WiFiManager  | 2.0.16-rc.2 | https://github.com/tzapu/WiFiManager             |

## ESPhome
If you feel more familiar with ESPhome instead of MQTT give [this companion project](https://github.com/se-bastiaan/esphome-simplebus2) a try. It is based on the hardware of this project and allows for easy integration in Home Assistant. Many thanks to [se-bastiaan](https://github.com/se-bastiaan) for his fabulous work!

## Hardware

The electronics draw power from the bus voltage and require no additional power source. A Seeed Studio XIAO ESP32C3 serves as the controller board.

![schematics V2 2](https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/e5383859-6b88-411f-a325-aa8435e5d2d8)

### Expansion pin header J2
For future use this pin header can connect to a piggy-back. The M2.5 hole in the neighborhood can be used to secure a sandwich PCBA.

 Signal Name   | Pin  | Notes
 ------------- |:----:| --------------------------------------------------------------------------------------
 D6            | 1    | GPIO 21 of ESP32 (D6 of XIAO module pinout)
 3V3           | 2    | directly connected to 3.3V plane
 D3            | 3    | GPIO 5 of ESP32 (D3 of XIAO module pinout, outputs PWM signal at boot, strapping pin)
 GND           | 4    | directly connected to GND plane
 
### Debug pin header J3
Meant for debugging, header can be populated optionally. The following signals can be measured against GND:

 Signal Name   | Pin  | Notes
 ------------- |:----:| ----------------------------------------------------------------------------
 D1            | 1    | voltage divider 1, reference voltage of OPV filter stage 1 ('gain' parameter)
 S2            | 2    | cleaned payload signal of Simplebus2
 D2            | 3    | voltage divider 2, reference voltage of comparator ('level' parameter)
 GND           | 4    | directly connected to GND plane

### Filter
In the schematics two filters can be found, one low pass (C5 and R5) and one high pass sallen key active filter with a gain of 2. Between those two filters there is signal amplifier which can be set individually to compensate for a long bus wire. The goal is to filter and amplify the incoming 25 kHz signal.

R1 and R11 are alternative positions to select signal conditioning by hardware (comparator U6) or direct input of the signal into the ESP32. In the second case the firmware should do the signal conditioning via DSP routines or similar. At the moment the DSP option is not implemented in the firmware and is meant for future use, so option "OPV" is default. To change this, desolder R11 and close R1 with a solder drop or a 0Ohms resistor.

## Printed Circuit Board (PCB)

The PCB was designed with KiCAD using through-hole technology (THT) and surface-mount device technology (SMD) to match the limited space requirements. Top layer is 3,3V plane and bottom is GND plane.
If someone is interested in a complete bridge including all parts (PCBA, ESP32, housing, etc.) you can contact me under elektroarzt@digital-filestore.de.

<img width="400" alt="PCB Layout front" src="https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/9e08688a-bc8e-422b-9aea-08cdf21ee501">

## Mechanics

The entire electronics assembly fits into a flush-mounted switch box, resulting in a streamlined enclosure. The latch was designed as a snap closure, eliminating the need for additional mechanical components beyond the housing shells.

<img width="400" alt="image" src="https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/c7216c4d-91a1-4187-8a82-3b373562678a">

A second option for the housing is a DIN rail enclosure. For this, I have created an alternative bottom shell with a builtin clip that can be directly clicked onto a DIN rail. It should not be printed in PLA, use a somewhat flexible filament like PETG. Otherwise, there is a risk of cracking the clip if released from the DIN rail again. The top part of the housing is identical to the flush-mounted version.

<img width="300" alt="Bottom DIN Rail Mount" src="https://github.com/user-attachments/assets/eba309a0-8a5d-4281-a596-97edbfd5641d" />
<img width="300" alt="Bottom DIN Rail Mount" src="https://github.com/Elektroarzt/simplebus2-mqtt-bridge/blob/main/mechanics/Housing%20on%20DIN%20Rail.png" />

### Antenna
The antenna is mounted inside the housing with adhesive tape. A small cable channel exists for the antenna cable within the housing. Be careful while inserting the PCB in the housing not to shear off the antenna cable if it is not inserted properly into the cable channel.

<img width="300" alt="Bridge Antenna" src="https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/4652c3fa-6144-4ea2-9211-f5a49fb67cec">

### USB connector
The USB-C socket is externally routed, allowing for easy firmware updates.

### LED and button
The onboard LED features a simple light guide, making it visible externally when the housing is closed. The onboard button is operated with a small axis embedded in the housing.

### Housing
The housing was designed in Autodesk Fusion 360 and 3D-printed in three colors on Bambu Lab P1S printer:
-   LED diffuser: transparent filament
-   Housing: black filament
-   Labeling: white filament

<img width="400" alt="image" src="https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/8a876f20-7a97-4d8a-b7a7-971fe98ba21d">

For the type of filament every material is suitable that has low stringing tendency. PETG works for me without needing supports. 0.2mm layer height does a good job on a 0.4mm standard nozzle.

<img width="1624" alt="Bambu Lab project" src="https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/841e8315-2a00-442a-89f9-4d012f876e1d">

## Disclaimer
> :warning: **WARNING**: You may only **USE THIS PROJECT AT YOUR OWN RISK**. The
  information provided is meant to be helpful but may contain errors or may be
  misinterpreted. Neither myself nor any of the contributors to this project
  provide any warranty or will assume any responsibility for any damage you
  cause yourself or others by using this project.

## Credits
The project originated from the excellent foundational work on the Simplebus2 protocol by [plusvic](https://github.com/plusvic). These fundamentals will not be reiterated and can be found [here](https://github.com/plusvic/simplebus2-intercom).
