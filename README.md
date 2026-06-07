# Smart Home RTOS Security System (Mixed-Criticality CPS)

![C](https://img.shields.io/badge/Language-C-blue.svg)
![Python](https://img.shields.io/badge/Language-Python-yellow.svg)
![RTOS](https://img.shields.io/badge/System-RTOS-orange.svg)
![OpenCV](https://img.shields.io/badge/Library-OpenCV-green.svg)

## Overview
This project is a comprehensive **Cyber-Physical System (CPS)** and **Embedded Smart Home Security Prototype**. It demonstrates the implementation of a Mixed-Criticality System utilizing a Real-Time Operating System (RTOS) architecture on a Raspberry Pi. 

The system seamlessly isolates high-priority, time-critical hardware tasks (sensors and actuators) written in **C (POSIX Threads)** from low-priority, resource-intensive AI image processing tasks written in **Python (Flask & OpenCV)**.

## Core Architecture & Academic Features
- **Mixed-Criticality Architecture:** Strict separation between hard real-time tasks (motor control, RFID) and soft real-time tasks (AI face detection, web streaming).
- **Priority Inheritance Protocol:** Implementation of POSIX Mutexes with Priority Inheritance (`PTHREAD_PRIO_INHERIT`) to prevent Priority Inversion and Deadlocks during shared resource access.
- **Finite State Machine (FSM):** Reactive system behavior modeling with defined states: `IDLE`, `WAITING_CARD`, and `ALARM`.
- **Inter-Process Communication (IPC):** Synchronized state passing between the C-based RTOS core and the Python-based web server.

## Hardware Components
- **Microcontroller:** Raspberry Pi (Running Preempt-RT / Standard Linux)
- **Camera:** Arducam IMX708 (Camera Module 3) with Autofocus
- **Sensors:** - RC522 RFID Module (SPI Interface)
  - PIR Motion Sensor
- **Actuators & Indicators:** - SG90 Servo Motor (Door Lock Mechanism)
  - Active Buzzer
  - LED Array (Yellow, Green, Red)

## Software Stack
- **C:** `pigpio` for hardware PWM/GPIO control, `pthread` for RTOS scheduling (`SCHED_FIFO`).
- **Python:** `Flask` for the Web Dashboard, `Picamera2` for ISP hardware-accelerated capture, `OpenCV` (Haar Cascades) for lightweight AI Face Detection, `psutil` for real-time telemetry.
- **Frontend:** HTML5, CSS3, JavaScript (`Chart.js` for real-time CPU/RAM/Temp monitoring).

## System Behavior (FSM Flow)
1. **IDLE:** System is armed. Camera is off to save resources.
2. **MOTION DETECTED:** PIR sensor triggers the Python AI camera. A 10-second timer starts. The Yellow LED blinks to indicate the waiting period.
3. **AUTHORIZED ACCESS:** If a valid RFID card is read within 10 seconds, the Green LED turns on, the servo opens the door for 3 seconds, and the system resets.
4. **ALARM TRIGGERED:** If the 10-second timer expires without a valid card, the Red LED turns solid, the buzzer sounds continuously, and the camera shuts down. The alarm can only be disarmed by scanning a valid RFID card.

## Real-Time Web Dashboard
The system features a live web dashboard running on port `5000` that provides:
- Live AI-augmented video feed (Bounding boxes around detected humans).
- Real-time performance telemetry (CPU Usage, RAM Usage, Core Temperature) visualized dynamically via Chart.js to prove system stability under load.

## Installation & Usage

**1. Hardware Daemon Setup:**
```bash

gcc -o smart_home main.c -lpigpio -lpthread -lrt
# Terminal 1: Start the AI Web Dashboard
sudo python3 camera.py

# Terminal 2: Start the RTOS Hardware Controller
sudo ./smart_home

sudo apt-get install pigpio python3-pigpio python3-picamera2 python3-opencv python3-psutil python3-flask
sudo systemctl enable pigpiod
