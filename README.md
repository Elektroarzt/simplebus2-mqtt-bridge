# Simplebus2 MQTT Bridge

## Overview

This project focuses on integrating Comelit intercom systems running Simplebus2 protocol into a Smart Home, typically equipped with an MQTT broker for information exchange. The following functions have been implemented:

-   Doorbell signal for the main entrance
-   Doorbell signal for the apartment door
-   Opening the main entrance door
-   Ring-to-Open (automatic opening)
-   WiFi Manager
-   Configuration via Web Interface

The project originated from foundational work on the Simplebus2 protocol by [plusvic](https://github.com/plusvic). These fundamentals will not be reiterated and can be found [here](https://github.com/plusvic/simplebus2-intercom).

![Mittel (Simplebus2 MQTT Bridge V2 0 Pic5)](https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/eb228457-a56e-4270-bf16-d54564b8aaf9)
![Mittel (Simplebus2 MQTT Bridge V2 0 Pic2)](https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/6b3fbd68-7e7e-4de6-b161-e1b7b0db3095)
![Mittel (Simplebus2 MQTT Bridge V2 0 Pic3)](https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/e0feba6d-6ee0-4110-b9d2-6cd0f7c0bd5e)

## Software

## Electronics

The electronics draw power from the bus voltage and require no additional power source. A Seeed Studio XIAO ESP32C3 serves as the controller board.

![Schematics V2 1](https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/115db3b3-a06b-46df-984c-68052d103bfa)

## Printed Circuit Board (PCB)

The PCB was created with KiCAD using through-hole technology (THT) and surface-mount device technology (SMD) to match the limited space requirements.

<img width="732" alt="Layout front V2 1" src="https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/f5932b9b-821a-460e-b4a3-818edec00a40">

## Mechanics

The entire electronics assembly should fit into a flush-mounted housing, resulting in a streamlined enclosure. The latch was designed as a snap closure, eliminating the need for additional mechanical components beyond the housing shells.

The antenna is glued inside the housing with adhesive tape, and a small cable channel exists for the antenna cable within the housing.

The USB-C socket is externally routed, allowing for easy updates.

The onboard LED features a simple light guide, making it visible externally when the housing is closed. The onboard button is operated with a small axis embedded in the housing.

The housing is designed in Autodesk Fusion 360 and was 3D-printed in three colors on Bambu Lab P1S printer:

-   LED diffuser: transparent filament
-   Housing: black filament
-   Labeling: white filament

![Mittel (Housing V10)](https://github.com/Elektroarzt/simplebus2-mqtt-bridge/assets/61664171/8a876f20-7a97-4d8a-b7a7-971fe98ba21d)


