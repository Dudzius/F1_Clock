# 🏎️ Desktop F1 Pitwall

A desktop gadget powered by ESP32 that shows live F1 standings, race countdown, weather info, and local time — with automatic updates and notifications.

 
---
## Features

- **Stage 1** – Current date and time  
- **Stage 2** – Current and upcoming weather  
- **Stages 3 & 4** – Constructors & Drivers championship standings (current or any past year)  
- **Stage 5** – Next race name, date, countdown, and local start times  
- Automatically cycles between stages every 15 minutes or on short button press  
- Notifications (with sound) before race and qualifying (1 hour and 5 min before start)  
- Night mode: screen off between 23:00–6:00; can be toggled manually with long press  
- Auto-refresh after each race  
- Easy one-time Wi-Fi setup  
- On-screen error messages when data fetching fails  

---
## Demo:

> (Coming soon – screenshots and short video)

- STAGE 1 - Clock insert photo/gif
- STAGE 2 - Weather insert photo/gif
- STAGE 3 - Constructors standings insert photo/gif
- STAGE 4 - Drivers standings insert photo/gif\
- STAGE 5 - Calendar insert photo/gif

---
## Hardware:
**Components Used:**
- ESP32 DevKit V1
- 16x4 I2C LCD Display (green & blue)
- Buzzer
- Push Button

---
## Scematics:

> (Coming soon – image)

---
## How it works

### Setup

On boot, the ESP32 attempts to connect to saved WiFi credentials. If none exist, it opens a captive portal for the user to enter WiFi details and (optionally) a season year for F1 standings. WiFi details are stored in flash memory.

### Data Sources

The device fetches data from the following public APIs:

- **F1 data** from [jolpi.ca/ergast](https://api.jolpi.ca/ergast/)
  - Race schedule, qualifying times, constructor & driver standings

- **IP Geolocation** from [ip-api.com](https://ip-api.com/)
  - Used to determine latitude/longitude and IP for timezone lookup

- **Timezone info** from [worldtimeapi.org](http://worldtimeapi.org/)
  - Raw and DST offsets based on IP, used for setting system time

- **Weather data** from [open-meteo.com](https://open-meteo.com/en/docs)
  - Hourly temperature, cloud coverage, and precipitation probability

Functions for fetching data are with retry logic and limit, for handling faiure on API calls.

### Runtime Behavior

The display cycles through 5 stages: Clock, Weather, Constructor Standings, Driver Standings, and Race Info. Stage switches automatically every 15 minutes or on button press.

- Weather is refreshed every hour
- F1 data is refreshed 2 hours after a race completes
- Notifications are triggered before race/quali sessions
- LCD enters night mode (off) between 23:00–06:00 unless overridden

Short and long button presses are used for navigation and toggling night mode. No user interaction is required after setup.

## Credits

Huge shoutout to the folks at [jolpica-f1](https://github.com/jolpica/jolpica-f1) for awesome open-source F1 API! 🏆  

Thanks to [WiFiManager](https://github.com/tzapu/WiFiManager) for making Wi-Fi setup super friendly. 🏆

> This is my first month playing with electronics and Arduino IDE, so the project’s a bit rough.

---

Made with 🧡 for Formula 1 fans.

Contributions, bug reports, and feedback are welcome.  
Open an issue or get in touch!
