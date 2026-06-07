M5StickS3 AI Voice Assistant for Kids
A pocket-sized voice assistant built for my 8-year-old son, running on an M5StickS3 (ESP32-S3). Press a button, ask a question out loud, and get a simple answer on screen — no phone or screen required.
How it works:

Press the front button to record a 3-second voice clip
Audio is transcribed using OpenAI Whisper (speech-to-text)
The transcribed question is sent to GPT-4o-mini with a child-friendly system prompt
The answer (20 words or fewer) is displayed on the built-in screen

Features:

Single-button operation
WiFi setup via captive portal (no hardcoding required)
Automatically connects to saved WiFi on startup
Hold button 3 seconds to reset WiFi credentials
PSRAM-aware audio buffer allocation
Child-safe GPT system prompt

Hardware:

M5StickS3 (ESP32-S3, 1.14" LCD)

Dependencies:

M5Unified library
M5GFX library
OpenAI API key (Whisper + GPT-4o-mini)

Setup:

Add your OpenAI API key to the sketch
Flash to your M5StickS3 via Arduino IDE
On first boot, join the M5Voice-Setup WiFi network and visit 192.168.4.1 to enter your WiFi credentials
