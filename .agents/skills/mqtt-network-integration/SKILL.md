---
name: mqtt-network-integration
description: Guidelines for integrating an external MAC/PHY or Wi-Fi coprocessor for MQTT Edge Connectivity.
---
# MQTT Network Integration
1. **Hardware Choice**: Use an ESP32 as a serial-to-WiFi bridge (AT commands over UART) or an SPI-based Ethernet MAC (e.g., ENC28J60/W5500).
2. **Architecture**: Implement a dedicated `Network_Task`.
3. **Queue Interface**: The Queue Manager should route outbound data to an `Outbound_MQTT_Queue` instead of directly to UART.
4. **Publishing**: The Network_Task reads from `Outbound_MQTT_Queue` and uses a lightweight MQTT client (like coreMQTT from FreeRTOS) to publish to the broker with QoS 1.
