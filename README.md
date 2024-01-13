# Simplebus2 MQTT Bridge

### Table of contents
[1. Overview](#Overview)  
[2. Software](#Software)  
[3. Hardware](#Hardware)  
[4. Mechanics](#Mechanics)  
[5. Disclaimer](#Disclaimer)  

## Overview

This project focuses on integrating Comelit intercom systems running the Simplebus2 protocol into a Smart Home, typically equipped with an MQTT broker for information exchange. The following functions have been implemented:

-   Doorbell signal for the main entrance
-   Doorbell signal for the apartment door
-   Opening the main entrance door
-   Ring-to-Open (automatic opening)
-   WiFi Manager
-   Configuration via Web Interface
-   ...

The project originated from foundational work on the Simplebus2 protocol by [plusvic](https://github.com/plusvic). These fundamentals will not be reiterated and can be found [here](https://github.com/plusvic/simplebus2-intercom).

![Mittel (Simplebus2 MQTT Bridge V2 0 Pic5)](https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/eb228457-a56e-4270-bf16-d54564b8aaf9)
![Mittel (Simplebus2 MQTT Bridge V2 0 Pic2)](https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/6b3fbd68-7e7e-4de6-b161-e1b7b0db3095)
![Mittel (Simplebus2 MQTT Bridge V2 0 Pic3)](https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/e0feba6d-6ee0-4110-b9d2-6cd0f7c0bd5e)

## Software

Topics to describe:
- calibration process
- wifi manager
- web interface
- debug console
- ...

## Hardware

The electronics draw power from the bus voltage and require no additional power source. A Seeed Studio XIAO ESP32C3 serves as the controller board.

![Schematics V2 1](https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/115db3b3-a06b-46df-984c-68052d103bfa)

Topics to describe:
- filter topology
- hardware filter vs. DSP
- i2c digital potentiometer / voltage divider
- debug pin header
- extension pin header
- ...

## Printed Circuit Board (PCB)

The PCB was created with KiCAD using through-hole technology (THT) and surface-mount device technology (SMD) to match the limited space requirements.

<img width="732" alt="Layout front V2 1" src="https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/f5932b9b-821a-460e-b4a3-818edec00a40">

## Mechanics

The entire electronics assembly should fit into a flush-mounted switch box, resulting in a streamlined enclosure. The latch was designed as a snap closure, eliminating the need for additional mechanical components beyond the housing shells.

<img width="791" alt="image" src="https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/c7216c4d-91a1-4187-8a82-3b373562678a">

### Antenna
The antenna is glued inside the housing with adhesive tape, and a small cable channel exists for the antenna cable within the housing.

### USB Connector
The USB-C socket is externally routed, allowing for easy updates.

### LED
The onboard LED features a simple light guide, making it visible externally when the housing is closed. The onboard button is operated with a small axis embedded in the housing.

### Housing
The housing was designed in Autodesk Fusion 360 and 3D-printed in three colors on Bambu Lab P1S printer:

-   LED diffuser: transparent filament
-   Housing: black filament
-   Labeling: white filament

![Mittel (Housing V10)](https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/8a876f20-7a97-4d8a-b7a7-971fe98ba21d)

For the filament every material is suitable that has low stringing tendency. PLA or PETG works both for me without needing supports. 0.2mm layer height does a good job on a 0.4mm standard nozzle.

<img width="1624" alt="Bambu Lab project" src="https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/63c5a7bc-3bb4-44be-bc23-8f8ee1acf185">

## Disclaimer
> :warning: **WARNING**: You may only **USE THIS PROJECT AT YOUR OWN RISK**. The
  information provided is meant to be helpful but may contain errors or may be
  misinterpreted. Neither myself nor any of the contributors to this project
  provide any warranty or will assume any responsibility for any damage you
  cause yourself or others by using this project.
