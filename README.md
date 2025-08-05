# 🏎️ F1 Desktop Pitwall

![F1_desktop_pitwall](https://github.com/user-attachments/assets/a59f7181-0cbe-482b-9f2c-5285a3a44482)


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
![drivers1976](https://github.com/user-attachments/assets/488be357-8bf6-40a4-9384-32cc298d381f)
![drivers2021](https://github.com/user-attachments/assets/38c55249-68c4-438b-92b5-89dbe9318674)

- STAGE 5 - Calendar (upcoming race)
![race1](https://github.com/user-attachments/assets/61e7b43b-bfe8-43f1-a17e-d11af61ebb4e)
![race2](https://github.com/user-attachments/assets/d689b8a7-29e4-476e-86d2-608f569f54c6)

 If there is no upcoming race selected season:
 ![raceInfErr](https://github.com/user-attachments/assets/2f45ad6b-a3e9-48be-8031-b1d85f299942)

- Notification
![notification](https://github.com/user-attachments/assets/78948388-9385-432b-a0d2-47bd3f99cfe7)



## Hardware:
**Components Used:**
- ESP32 DevKit V1
- 16x2 I2C LCD Display
- Buzzer
- Push Button
- Wires


## Schematics:

<img width="2697" height="1614" alt="circuit_image(3)" src="https://github.com/user-attachments/assets/4ef27d82-de1e-42fa-b519-412911e3d2f9" />


## How it works

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

### Setup -  Connecting to Wifi
On boot, the ESP32 will attempt to connect using saved WiFi credentials. If none are found, it opens a captive portal where you can enter WiFi details and (optionally) a season year for F1 standings.
![checkingWifi](https://github.com/user-attachments/assets/6e1ba82d-7747-44e1-8326-76fdbe75bfa6)

To reset WiFi credentials, power off the device, then hold the button while powering it back on. You’ll see this screen:
![resettingWifi](https://github.com/user-attachments/assets/dcddecf8-6267-47cc-896c-ad9900742d86)

Next, you’ll see the hotspot name you should connect to:
![connectToHotspot](https://github.com/user-attachments/assets/425a6b55-7521-404f-9b0b-c18d43e99fbc)


Once connected, open the captive portal and select "Configure WiFi". Choose your network from the list and enter the password:

(Optional) Enter an F1 season year. Used for past seasons data, if left empty it will be set to current year.
<img width="1849" height="914" alt="configureWifi2" src="https://github.com/user-attachments/assets/424ada13-cd1f-4db9-b1e0-8fd45c4bef43" />


If the connection is successful, the device will begin fetching data:
![fetchingData](https://github.com/user-attachments/assets/36b579f5-ea07-410a-9567-aa1b45754102)

If the WiFi setup fails, you’ll see an error screen. Try setting up again:
![WifiNotSet](https://github.com/user-attachments/assets/ff9424b1-b488-4f5b-b974-3594389e6c91)

There also can be an error while trying to fetch data from APIs. User will be informed if there is such failure and recommended to reboot the device.
![LocationNotSet](https://github.com/user-attachments/assets/229082b6-0f0b-4eed-8947-b00b3d96a050)


### Adjusting Screen Contrast

If the characters on the screen are not clearly visible, you can easily adjust the display contrast using the contrast screw on the back of the screen module.

![contrast](https://github.com/user-attachments/assets/3e134ffc-0c84-43ae-b740-3025b8ceeb37)

Adjust slowly until the text is clear and comfortable to read.

### Runtime Behavior

The display cycles through 5 stages: Clock, Weather, Constructor Standings, Driver Standings, and Race Info. 

- Every 15 minutes (or on button press), the device enters a cycle that shows all stages one by one, then returns to the Clock.

- Weather data is refreshed hourly.

- F1 data is refreshed automatically ~2 hours after a race ends.

- Notifications appear shortly before race or qualifying sessions.

- The LCD enters night mode (backlight off) between 23:00–06:00.

**Button controls:**

- Short press: manually cycle through stages

- Long press: temporarily toggle the backlight

## Power and Network Usage

**Power Consumption**

Based on testing with a 12,000 mAh power bank at 5 V, device operated continuously for 115 hours, consuming about 0.52 watts on average. 
 `
12,000 mAh × 5 V ÷ 1000 = 60 Wh  
60 Wh ÷ 115 h = 0.52 W
 `

 This means it would take over 1,917 hours (80 days) to consume just 1 kilowatt-hour of electricity.

 **Powering the Device**

 The device can be powered in two simple ways:

 - Via a 5V USB port – Practically any USB port (on a computer or power bank) can provide the required 5V.
 - Via a standard USB charging adapter – For example, a common phone charger will work perfectly.

 **Data Usage**
 
The device downloads less than 100 kb of data for a full update, making its data usage insignificant to your network.


## Credits

Huge shoutout to the folks at [jolpica-f1](https://github.com/jolpica/jolpica-f1) for awesome open-source F1 API! 🏆  

Thanks to [WiFiManager](https://github.com/tzapu/WiFiManager) for making WiFi super easy to setup. 🏆

>  I'm not much of software engineer and this is my first microcontroler project, so it is a bit rough :)

---

Made with 🧡 for Formula 1 fans.

Any feedback is welcome.
