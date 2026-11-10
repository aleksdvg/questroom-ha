\# Node-RED Automation



\## Overview



This project contains Node-RED flows developed for an automated escape room control system.



Node-RED handles the main automation logic and coordinates communication between hardware controllers and Home Assistant using MQTT.



The system executes predefined scenarios without requiring constant operator interaction.



\## System Architecture



\- \*\*Node-RED\*\* — automation logic and scenario execution.

\- \*\*Home Assistant\*\* — user interface and system monitoring.

\- \*\*Arduino\*\* — hardware control and sensor data acquisition.

\- \*\*MQTT\*\* — communication between controllers and automation services.



\## Main Functions



\- Processing commands from Home Assistant.

\- Coordinating automated scenarios.

\- Sending commands to Arduino controllers via MQTT.

\- Receiving sensor states and hardware status updates.

\- Controlling lighting and relay-based equipment.

\- Managing scenario transitions.



\## Project Files



\- `flows.json` — exported Node-RED flow configuration.



\## Installation



1\. Install Node-RED.

2\. Install the required Node-RED palette modules.

3\. Open the Node-RED editor.

4\. Select Menu → Import.

5\. Import `flows.json`.

6\. Configure MQTT broker connection settings.

7\. Deploy the flows.



\## Notes



Hardware configuration, MQTT broker settings and external device connections may require adjustment before deployment.

