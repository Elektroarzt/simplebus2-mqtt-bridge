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

## Functionality

## Software

## Electronics

The electronics draw power from the bus voltage and require no additional power source. A Seeeduino ESP32-C3 XIAO serves as the controller board.

## Printed Circuit Board (PCB)

The PCB was created using through-hole technology (THT) and surface-mount device technology (SMD) to match the limited space requirements using KiCAD.

## Mechanics

The entire electronics assembly should fit into a flush-mounted housing, resulting in a streamlined enclosure. The latch was designed as a snap closure, eliminating the need for additional mechanical components beyond the housing shells.

The antenna is glued inside the housing with adhesive tape, and a small cable channel exists for the antenna cable within the housing.

The USB-C socket is externally routed, allowing for easy updates.

The onboard LED features a simple light guide, making it visible externally when the housing is closed. The onboard button is operated with a small axis embedded in the housing.

The housing was 3D printed in three colors:

-   LED diffuser: transparent filament
-   Housing: black filament
-   Labeling: white filament
