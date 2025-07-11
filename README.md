# 🏎️ F1 Desktop Pitwall

A desktop gadget powered by ESP32 that shows live F1 standings, race countdown, weather info, and local time — with automatic updates and notifications.

 

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


## Demo:

- STAGE 1 - Clock
![clock](https://github.com/user-attachments/assets/ae5bdd79-e20c-441e-b0d2-5abbc29cd828)

- STAGE 2 - Weather data
![weather](https://github.com/user-attachments/assets/961131d6-b606-4780-9fb1-bf9bf2337a8a)

- STAGE 3 - Constructors standings
![constructors](https://github.com/user-attachments/assets/e74b194a-3e97-42cd-8104-cdb90de9ef1f)
![constructorsBody](https://github.com/user-attachments/assets/4f9f0856-ff92-4627-b4eb-b3e01d2656ab)

- STAGE 4 - Drivers standings
![drivers](https://github.com/user-attachments/assets/22f30f33-da14-4ad4-8e7e-fe8f13d3240d)
![driversBody](https://github.com/user-attachments/assets/a8b86491-5943-4a12-b4ac-9ad88757cf02)

- STAGE 5 - Calendar (upcoming race)
![race1](https://github.com/user-attachments/assets/61e7b43b-bfe8-43f1-a17e-d11af61ebb4e)
![race2](https://github.com/user-attachments/assets/d689b8a7-29e4-476e-86d2-608f569f54c6)


## Hardware:
**Components Used:**
- ESP32 DevKit V1
- 16x4 I2C LCD Display (green & blue)
- Buzzer
- Push Button


## Scematics:

<img width="2696" height="1614" alt="circuit" src="https://github.com/user-attachments/assets/71d610ac-b5ee-45d6-b277-b2d08eedb4fc" />


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

>  I'm not much of software engineer and this is my first microcontroler project, so it is a bit rough :)

---

Made with 🧡 for Formula 1 fans.

Any feedback is welcome.
