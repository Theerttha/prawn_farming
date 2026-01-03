This project is a pond water quality monitoring system built using an ESP32. 
It continuously measures important water parameters like temperature, pH, TDS, and ORP to understand the condition of pond water over long periods of time.
The system is designed for field deployment, so it works without WiFi and saves all readings with timestamps directly to an SD card.
To conserve power, the ESP32 operates in cycles — it wakes up, collects sensor data for a short active period, logs the data, and then goes into deep sleep for several hours before repeating the process.
Special care has been taken to handle real-world challenges such as sensor interference, long-term deployment drift, and power efficiency. 
This makes the project suitable for environmental monitoring, pond management, and experimental water analysis, especially in remote locations.
The wifi based working model has been tested and the prototype has run successfully.Using wifi access(in station mode),esp32 uploads the data to firebase,from where it is displayed to a website hosted in vercel.
All further details are provided in the notion link below :
https://www.notion.so/esp32-2baab831c867802d9486ee7d48555090
