# Lightweight-Zero-Trust-Security-Architecture-for-ESP32-Based-Smart-Systems
Practical security framework that protects communication between IoT devices and a monitoring server, even when devices operate with limited computing resources.
Secure Acoustic Bee Monitoring and Alert System
Project overview
This project uses an ESP32 and a KY-037 sound sensor to monitor acoustic activity around a beehive. When the measured noise level exceeds a configurable threshold, the system records an alert and sends an email through Gmail SMTP. A browser dashboard displays live readings, a chart, the current threshold, and the latest five alerts.
Current progress
- Existing ESP32 + KY-037 monitoring design retained.
- Web dashboard and live noise chart included.
- Adjustable threshold and five-entry in-memory alert history included.
- Gmail SMTP notifications included.
- Security improvements in the current sketch include HTTP authentication, threshold validation, safer alert rendering, and response headers.
- Network time is used for timestamps when available, with an uptime-based fallback.
Hardware
- ESP32 development board
- KY-037 sound sensor
Connections
- KY-037 VCC → ESP32 3.3V
- KY-037 GND → ESP32 GND
- KY-037 AO → ESP32 GPIO 34
Ensure the sensor output voltage does not exceed the ESP32's permitted input voltage.
Software and configuration
Use Arduino IDE with the ESP32 board package and the ESP Mail Client library. Configure Wi-Fi details, Gmail address, Gmail App Password, recipient address, and a strong dashboard password in the sketch before uploading it. Keep credentials private and out of public repositories.

After uploading, open Serial Monitor at 115200 baud, find the ESP32's local IP address, and visit http://ESP32_IP_ADDRESS/ from a device on the same Wi-Fi network.
Current security limitations
- The dashboard uses HTTP, so credentials and traffic are not encrypted. Local Wi-Fi access alone does not provide encryption.
- Alert history is stored in RAM and is lost when the ESP32 restarts.
- The sketch has not yet been confirmed as compiled and tested on physical hardware.
- HTTPS, stronger deployment hardening, and documented security tests remain future work.
Do not expose the ESP32 web server directly to the public internet.
Next steps
1. Compile the sketch and resolve any library/API errors.
2. Upload it and verify Wi-Fi connection and dashboard login.
3. Test live sensor readings, threshold updates, alert history, and Gmail delivery.
4. Test rejection of unauthenticated requests and invalid threshold values.
5. Plan HTTPS and additional security testing.
Research direction
Evaluate whether lightweight authentication and access controls improve the security of an ESP32-based acoustic monitoring system while keeping it responsive and reliable.
