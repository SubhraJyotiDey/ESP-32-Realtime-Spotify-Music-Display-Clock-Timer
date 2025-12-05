1. The "Story"
Introduction I wanted a desktop companion that didn't just show the time but actively helped me focus and relax. Using an ESP32, I built the ISO-Deck. Unlike standard Arduino loops that pause when fetching data from the internet, this project leverages the ESP32's Dual-Core architecture (FreeRTOS).

How it Works (The Dual-Core Engine)

Core 0 (The Network Brain): Handles heavy tasks like connecting to Wi-Fi, fetching API data from Spotify, and syncing NTP time. It runs independently, so slow internet never freezes the interface.

Core 1 (The UI Brain): Handles the Rotary Encoder, Button, and LCD drawing. It runs at lightning speed (~10ms refresh rate), ensuring the knob feels snappy and responsive instantly, even while the other core is downloading song data.

Key Features

🎵 Live Spotify Integration: Displays current Song and Artist with a "Carousel" mode that auto-switches between Time and Music.

⏱️ Pomodoro Timer: A built-in focus timer with a buzzer notification.

🎨 Custom "Inverted" UI: Uses custom bitmaps to create solid "Status Bar" style headers (TIME, SONG, WORK, ALRM) on a standard 16x2 LCD.

💡 Mood Lighting: RGB LED changes color based on the song name hash (seeded randoms).

⏰ Daily Alarm: Simple 07:00 AM alarm with buzzer.

2. Bill of Materials (Components)
1x ESP32 Development Board (Doit DevKit V1 or similar)

1x 16x2 LCD Display with I2C Module

1x Rotary Encoder (KY-040)

1x RGB LED (Common Cathode)

1x Active Buzzer

3x 220Ω Resistors (for LED)

Breadboard & Jumper Wires

3. The Wiring Diagram
Since you cannot upload an image file directly to code repositories, copy this Text Diagram:

ESP32 PINOUT MAPPING
+-----------------------------------+
| COMPONENT       | ESP32 PIN       |
|-----------------|-----------------|
| LCD SDA         | GPIO 21         |
| LCD SCL         | GPIO 22         |
| Encoder CLK     | GPIO 25         |
| Encoder DT      | GPIO 26         |
| Encoder SW      | GPIO 27         |
| RGB Red         | GPIO 16         |
| RGB Green       | GPIO 17         |
| RGB Blue        | GPIO 5          |
| Buzzer (+)      | GPIO 18         |
+-----------------------------------+
* Power: Connect LCD VCC to VIN (5V). 
* Encoder VCC can go to 3.3V.
