# Hardware Components & Pin Mapping

This document outlines the complete list of hardware requirements and pin configurations for the system.

## 🧠 Microcontroller
* **ESP32 Development Board**: Standard dual-core variant (e.g., ESP32-WROOM-32). A dual-core board is required to support FreeRTOS operations, allowing tasks to be pinned to both Core 0 and Core 1 for efficient concurrent processing.

## 🌡️ Sensors (Inputs)
* **AM2302 (DHT22) Sensor**: Reads ambient temperature and humidity. 
    * **Pin**: `GPIO 33`
* **MQ-135 Gas Sensor**: Detects general air quality and hazardous gases.
    * **Pin**: `GPIO 34`
* **MQ-2 Gas Sensor**: Detects smoke and flammable gases; serves as the primary input for fire detection logic.
    * **Pin**: `GPIO 35`
* **PIR Motion Sensor (e.g., HC-SR501)**: Primary motion detection for when the security system is armed.
    * **Pin**: `GPIO 18`
* **Vibration Sensor (e.g., SW-420)**: Works in tandem with the PIR sensor for physical break-in and tamper detection.
    * **Pin**: `GPIO 23`

## ⚙️ Actuators & Relays (Outputs)
* **5V Relay Module**: 3-Channel or 4-Channel module used to safely isolate and control higher-voltage outputs from the ESP32.
* **Cooling Fan**: Activated dynamically based on AI thresholds.
    * **Pin**: `GPIO 16`
* **Water Pump (Sprinkler)**: Activated automatically via the fire and smoke detection logic.
    * **Pin**: `GPIO 17`
* **Exhaust Fan**: Triggered automatically when gas concentration limits exceed the 1200 threshold.
    * **Pin**: `GPIO 19`

## 📺 Display & Indicators
* **16x2 LCD Display with I2C Backpack**: Provides local system status updates.
    * **Pins**: `GPIO 21` (SDA), `GPIO 22` (SCL)
* **Onboard Blue LED**: Visual indicator for Wi-Fi connection status.
    * **Pin**: `GPIO 2`

## 🔌 Power & Wiring Requirements
*(Note: These components are not explicitly declared in the codebase but are essential for hardware assembly.)*
* **Dedicated 5V Power Supply**: Crucial for providing sufficient current to the relay module, the internal heaters of the MQ gas sensors, and the ESP32 itself.
* **Jumper Wires**: Assorted male-to-male, male-to-female, and female-to-female.
* **Breadboard / Custom PCB**: For prototyping and finalizing the circuit connections.

---

## 📌 Summary Pinout Table

| Component | Pin / Connection | Type | Function |
| :--- | :--- | :--- | :--- |
| **AM2302 (DHT22)** | GPIO 33 | Input | Temperature & Humidity |
| **MQ-135** | GPIO 34 | Input | Air Quality |
| **MQ-2** | GPIO 35 | Input | Smoke & Flammable Gas |
| **PIR Sensor** | GPIO 18 | Input | Motion Detection |
| **Vibration Sensor** | GPIO 23 | Input | Tamper Detection |
| **Relay 1 (Cooling Fan)** | GPIO 16 | Output | Temperature Control |
| **Relay 2 (Water Pump)** | GPIO 17 | Output | Fire Suppression |
| **Relay 3 (Exhaust Fan)** | GPIO 19 | Output | Gas Ventilation |
| **I2C LCD (SDA)** | GPIO 21 | I2C | Local Display Data |
| **I2C LCD (SCL)** | GPIO 22 | I2C | Local Display Clock |
| **Onboard LED** | GPIO 2 | Output | Wi-Fi Status |
