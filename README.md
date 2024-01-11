# Simplebus2 MQTT Bridge

## Overview

This project focuses on integrating Comelit intercom systems with Simplebus2 protocol into a Smart Home, typically equipped with an MQTT broker for information exchange. The following functions have been implemented:

-   Doorbell signal for the main entrance
-   Doorbell signal for the apartment door
-   Opening the main entrance door
-   Ring-to-Open (automatic opening)
-   WiFi Manager
-   Configuration via Web Interface

The project originated from foundational work on the Simplebus2 protocol by [plusvic](https://github.com/plusvic). These fundamentals will not be reiterated and can be found [here](https://github.com/plusvic/simplebus2-intercom).

![Simplebus2 MQTT Bridge V2 0 Pic4](https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/a39002f3-98a1-447f-a74c-cfa44858eafd)
![Simplebus2 MQTT Bridge V2 0 Pic5](https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/7691ce40-320a-439c-98d1-27f9c1b3ebde)
![Simplebus2 MQTT Bridge V2 0 Pic2](https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/87916f2a-d24a-4864-96a7-0b422110d9af)
![Simplebus2 MQTT Bridge V2 0 Pic3](https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/86c1f8c1-742a-4f7b-a3c1-db9a6a988922)
![Simplebus2 MQTT Bridge V2 0 Pic1](https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/835b414e-3ab6-4d20-94d2-c4cc7889a36d)

## Functionality

## Software

## Electronics

The electronics draw power from the bus voltage and require no additional power source. A Seeeduino ESP32-C3 XIAO serves as the controller board.

![Schematics V2 1](https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/115db3b3-a06b-46df-984c-68052d103bfa)

## Printed Circuit Board (PCB)

The PCB was created using through-hole technology (THT) and surface-mount device technology (SMD) to match the limited space requirements using KiCAD.

<img width="732" alt="Layout front V2 1" src="https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/f5932b9b-821a-460e-b4a3-818edec00a40">

## Mechanics

The entire electronics assembly should fit into a flush-mounted housing, resulting in a streamlined enclosure. The latch was designed as a snap closure, eliminating the need for additional mechanical components beyond the housing shells.

The antenna is glued inside the housing with adhesive tape, and a small cable channel exists for the antenna cable within the housing.

The USB-C socket is externally routed, allowing for easy updates.

The onboard LED features a simple light guide, making it visible externally when the housing is closed. The onboard button is operated with a small axis embedded in the housing.

The housing was 3D printed in three colors:

-   LED diffuser: transparent filament
-   Housing: black filament
-   Labeling: white filament
