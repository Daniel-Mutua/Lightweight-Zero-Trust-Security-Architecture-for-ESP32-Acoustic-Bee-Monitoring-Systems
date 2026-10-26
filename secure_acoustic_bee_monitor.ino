#include <WiFi.h>
#include <ESP_Mail_Client.h>
#include <WebServer.h>
#include <time.h>

// =====================================================
// CONFIGURATION — replace placeholders before uploading
// Keep this file private; do not commit real credentials.
// =====================================================
const char* WIFI_SSID     = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 465
const char* EMAIL_SENDER_ACCOUNT   = "your@gmail.com";
const char* EMAIL_SENDER_APP_PASS  = "YOUR_16_CHARACTER_GMAIL_APP_PASSWORD";
const char* EMAIL_RECIPIENT        = "recipient@gmail.com";

// HTTP Basic Authentication. Use a unique, long password.
// Basic Auth over plain HTTP is NOT encrypted; see setup notes.
const char* WEB_USERNAME = "beeadmin";
const char* WEB_PASSWORD = "CHANGE_THIS_TO_A_LONG_UNIQUE_PASSWORD";

// KY-037 analog output. GPIO 34 is input-only on a classic ESP32.
constexpr uint8_t SOUND_SENSOR_PIN = 34;
constexpr int ADC_MAX_VALUE = 4095;
constexpr int DISPLAY_MAX_VALUE = 1023;
constexpr int DEFAULT_THRESHOLD = 800;
constexpr int MIN_THRESHOLD = 0;
constexpr int MAX_THRESHOLD = 1023;
constexpr size_t MAX_ALERTS = 5;

WebServer server(80);
SMTPSession smtp;
int noiseLevel = 0;
int threshold = DEFAULT_THRESHOLD;
bool alertSent = false;
unsigned long lastThresholdChangeMs = 0;

struct AlertEntry {
  String recipient;
  String timestamp;
  String message;
  bool emailSent;
};
AlertEntry alertHistory[MAX_ALERTS];
size_t alertIndex = 0; // Next slot to write; also oldest slot after wraparound.

// =====================================================
// HTML DASHBOARD
// Chart.js is loaded from a CDN. For fully offline operation,
// download and host Chart.js locally instead.
// =====================================================
const char webpage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Acoustic Bee Monitoring and Alert System</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
<style>
body{font-family:Arial,sans-serif;background:linear-gradient(to bottom right,#ffecd2,#fcb69f);display:flex;flex-direction:column;align-items:center;margin:0;padding:20px;color:#333}
.display{background:linear-gradient(to right,#ffe259,#ffa751);padding:25px;border-radius:20px;box-shadow:0 8px 20px #0003;text-align:center;color:#fff;width:min(300px,85vw);margin-bottom:25px}
h1{font-size:1.45rem;text-shadow:2px 2px 5px #0003}.value{font-size:20px;margin:8px 0;font-weight:bold}
input{padding:8px;width:100px;margin:5px;border-radius:6px;border:none}button{padding:9px 12px;border:none;border-radius:6px;background:#ff6f61;color:white;cursor:pointer;font-weight:bold;margin:5px}button:hover{background:#ff3b2e}
.alert{color:#7a1700;font-weight:bold;margin-top:10px}.chart-container{background:#ffffff66;padding:15px;border-radius:15px;width:min(600px,90vw)}
#loggedAlertsDisplay{margin-top:12px;background:#ffffff44;padding:10px;border-radius:10px;max-height:180px;overflow-y:auto;text-align:left}#alertList{list-style:none;padding:0}#alertList li{border-bottom:1px solid #ffffff88;padding:8px 0;font-size:14px;overflow-wrap:anywhere}#alertList li:last-child{border-bottom:none}.status{font-size:13px;overflow-wrap:anywhere}
</style>
</head>
<body>
<div class="display">
<h1>Acoustic Bee Monitoring and Alert System</h1>
<p class="value">Noise Level: <span id="noiseValue">--</span></p>
<p class="value">Threshold: <span id="thresholdValue">--</span></p>
<label for="thresholdInput">New threshold (0–1023)</label><br>
<input id="thresholdInput" type="number" min="0" max="1023" step="1" value="800">
<button id="setThresholdButton" type="button">Set Threshold</button>
<p id="statusMsg" class="status" role="status"></p>
<p id="alertMsg" class="alert" role="alert"></p>
<button id="toggleAlertsButton" type="button">Logged Alerts</button>
<div id="loggedAlertsDisplay" hidden>
<p style="margin:0 0 5px 0;font-weight:bold">Logged Alerts (Newest First)</p>
<ul id="alertList"></ul>
</div>
</div>
<div class="chart-container"><canvas id="noiseChart" aria-label="Recent noise readings" role="img"></canvas></div>
<script>
const dataPoints=[]; const labels=[]; const maxPoints=20;
const chartCtx=document.getElementById('noiseChart').getContext('2d');
const noiseChart=new Chart(chartCtx,{type:'line',data:{labels,datasets:[{label:'Noise Level',data:dataPoints,borderColor:'#ff3b2e',backgroundColor:'rgba(255,59,46,.2)',tension:.3}]},options:{animation:false,responsive:true,scales:{y:{min:0,max:1023}}}});
const el=id=>document.getElementById(id);
let requestInProgress=false;
function renderAlerts(entries){
 const list=el('alertList'); list.replaceChildren();
 if(!entries || entries.length===0){const li=document.createElement('li');li.textContent='No alerts logged yet.';list.appendChild(li);return;}
 entries.forEach(a=>{if(!a)return;const li=document.createElement('li');
  const time=document.createElement('div');time.textContent='Time: '+a.time;
  const to=document.createElement('div');to.textContent='To: '+a.recipient;
  const msg=document.createElement('div');msg.textContent='Message: '+a.msg;
  li.append(time,to,msg);list.appendChild(li);
 });
}
async function refreshData(){
 if(requestInProgress)return; requestInProgress=true;
 try{
  const res=await fetch('/data',{cache:'no-store',credentials:'same-origin'});
  if(!res.ok)throw new Error('Request failed ('+res.status+'). Refresh the page and authenticate if asked.');
  const data=await res.json();
  el('noiseValue').textContent=data.noise;el('thresholdValue').textContent=data.threshold;
  el('thresholdInput').value=data.threshold;
  el('alertMsg').textContent=data.alert?'Noise exceeded!':'Noise level is below the alert threshold.';
  renderAlerts(data.alertHistory);
  dataPoints.push(data.noise);labels.push(new Date().toLocaleTimeString());
  if(dataPoints.length>maxPoints){dataPoints.shift();labels.shift();}
  noiseChart.update('none');
  el('statusMsg').textContent='Connected to the ESP32.';
 }catch(err){el('statusMsg').textContent=err.message;}
 finally{requestInProgress=false;}
}
el('setThresholdButton').addEventListener('click',async()=>{
 const raw=el('thresholdInput').value;const value=Number(raw);
 if(raw.trim()===''||!Number.isInteger(value)||value<0||value>1023){el('statusMsg').textContent='Enter a whole number from 0 to 1023.';return;}
 try{
  const res=await fetch('/threshold',{method:'POST',credentials:'same-origin',cache:'no-store',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'threshold='+encodeURIComponent(String(value))});
  if(!res.ok){const body=await res.text();throw new Error(body||('Threshold update failed ('+res.status+').'));}
  el('statusMsg').textContent='Threshold updated.';await refreshData();
 }catch(err){el('statusMsg').textContent=err.message;}
});
el('toggleAlertsButton').addEventListener('click',()=>{el('loggedAlertsDisplay').hidden=!el('loggedAlertsDisplay').hidden;});
refreshData();setInterval(refreshData,1000);
</script>
</body>
</html>
)rawliteral";

// =====================================================
// SMALL HELPERS
// =====================================================
int readNoise() {
  const int raw = analogRead(SOUND_SENSOR_PIN);
  return map(raw, 0, ADC_MAX_VALUE, 0, DISPLAY_MAX_VALUE);
}

String getTimestamp() {
  struct tm timeInfo;
  if (getLocalTime(&timeInfo, 50)) {
    char buffer[24];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeInfo);
    return String(buffer);
  }
  // Explicitly marked fallback: uptime, not a real wall-clock timestamp.
  return String("uptime-") + String(millis() / 1000) + "s";
}

String jsonEscape(const String& input) {
  String out;
  out.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); i++) {
    const char c = input[i];
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if ((uint8_t)c >= 0x20) out += c;
        break;
    }
  }
  return out;
}

bool sendEmailAlert(const String& messageText) {
  SMTPSession mailSession;
  SMTP_Message email;
  email.sender.name = "ESP32 Acoustic Bee Monitor";
  email.sender.email = EMAIL_SENDER_ACCOUNT;
  email.subject = "Acoustic Bee Monitoring - Noise Alert";
  email.addRecipient("Bee Monitor Recipient", EMAIL_RECIPIENT);
  email.text.content = messageText.c_str();
  email.text.charSet = "utf-8";

  ESP_Mail_Session session;
  session.server.host_name = SMTP_HOST;
  session.server.server_port = SMTP_PORT;
  session.login.email = EMAIL_SENDER_ACCOUNT;
  session.login.password = EMAIL_SENDER_APP_PASS;
  session.login.user_domain = "";

  if (!mailSession.connect(&session)) {
    Serial.println("SMTP connection failed.");
    return false;
  }
  const bool sent = MailClient.sendMail(&mailSession, &email);
  if (!sent) Serial.println("Email send failed: " + mailSession.errorReason());
  mailSession.closeSession();
  return sent;
}

void recordAlert(const String& timestamp, const String& messageText, bool emailSent) {
  alertHistory[alertIndex].recipient = EMAIL_RECIPIENT;
  alertHistory[alertIndex].timestamp = timestamp;
  alertHistory[alertIndex].message = messageText + (emailSent ? " [email sent]" : " [email delivery failed]");
  alertHistory[alertIndex].emailSent = emailSent;
  alertIndex = (alertIndex + 1) % MAX_ALERTS;
}

bool requireAuthentication() {
  if (server.authenticate(WEB_USERNAME, WEB_PASSWORD)) return true;
  server.requestAuthentication(BASIC_AUTH, "Acoustic Bee Monitor");
  return false;
}

void sendNoCacheHeader() {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("X-Content-Type-Options", "nosniff");
  server.sendHeader("X-Frame-Options", "DENY");
  server.sendHeader("Referrer-Policy", "no-referrer");
}

// =====================================================
// HTTP HANDLERS — authentication is enforced server-side
// =====================================================
void handleRoot() {
  if (!requireAuthentication()) return;
  sendNoCacheHeader();
  server.send_P(200, "text/html; charset=utf-8", webpage);
}

void handleThreshold() {
  if (!requireAuthentication()) return;

  // Same-origin check helps reduce cross-site request forgery on this local HTTP UI.
  // It is a defence-in-depth check, not a replacement for HTTPS.
  const String origin = server.header("Origin");
  const String expectedOrigin = String("http://") + WiFi.localIP().toString();
  if (origin != expectedOrigin) {
    sendNoCacheHeader();
    server.send(403, "text/plain", "Forbidden: invalid request origin.");
    return;
  }

  if (!server.hasArg("threshold")) {
    server.send(400, "text/plain", "Missing threshold value.");
    return;
  }
  const String valueText = server.arg("threshold");
  if (valueText.length() == 0 || valueText.length() > 4) {
    server.send(400, "text/plain", "Threshold must be an integer from 0 to 1023.");
    return;
  }
  for (size_t i = 0; i < valueText.length(); i++) {
    if (!isDigit(valueText[i])) {
      server.send(400, "text/plain", "Threshold must be an integer from 0 to 1023.");
      return;
    }
  }
  const long requested = valueText.toInt();
  if (requested < MIN_THRESHOLD || requested > MAX_THRESHOLD) {
    server.send(400, "text/plain", "Threshold must be from 0 to 1023.");
    return;
  }

  // Prevent rapid repeated configuration changes.
  const unsigned long now = millis();
  if (lastThresholdChangeMs != 0 && now - lastThresholdChangeMs < 500) {
    server.send(429, "text/plain", "Please wait before changing the threshold again.");
    return;
  }
  threshold = (int)requested;
  lastThresholdChangeMs = now;
  Serial.printf("Threshold changed to %d by an authenticated dashboard request.\n", threshold);
  sendNoCacheHeader();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleData() {
  if (!requireAuthentication()) return;

  noiseLevel = readNoise();
  const bool alert = noiseLevel > threshold;

  // Send one email on threshold crossing; re-arm after readings return below threshold.
  if (alert && !alertSent) {
    alertSent = true;
    const String timestamp = getTimestamp();
    const String messageText = String("Bee Hive Alert! Noise level exceeded. Noise level: ") +
                               String(noiseLevel) + " (threshold " + String(threshold) +
                               ") at " + timestamp;
    const bool emailSent = sendEmailAlert(messageText);
    recordAlert(timestamp, messageText, emailSent);
  } else if (!alert) {
    alertSent = false;
  }

  String historyJson = "[";
  bool first = true;
  // Iterate newest to oldest, including correctly ordered entries after ring-buffer wrap.
  for (size_t offset = 0; offset < MAX_ALERTS; offset++) {
    const size_t index = (alertIndex + MAX_ALERTS - 1 - offset) % MAX_ALERTS;
    if (alertHistory[index].timestamp.length() == 0) continue;
    if (!first) historyJson += ",";
    first = false;
    historyJson += "{\"recipient\":\"" + jsonEscape(alertHistory[index].recipient) +
                   "\",\"time\":\"" + jsonEscape(alertHistory[index].timestamp) +
                   "\",\"msg\":\"" + jsonEscape(alertHistory[index].message) +
                   "\",\"emailSent\":" + String(alertHistory[index].emailSent ? "true" : "false") + "}";
  }
  historyJson += "]";

  const String json = String("{\"noise\":") + String(noiseLevel) +
                      ",\"threshold\":" + String(threshold) +
                      ",\"alert\":" + String(alert ? "true" : "false") +
                      ",\"alertHistory\":" + historyJson + "}";
  sendNoCacheHeader();
  server.send(200, "application/json; charset=utf-8", json);
}

void handleNotFound() {
  if (!requireAuthentication()) return;
  sendNoCacheHeader();
  server.send(404, "text/plain", "Not found.");
}

// =====================================================
// SETUP AND LOOP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  analogReadResolution(12);
  for (size_t i = 0; i < MAX_ALERTS; i++) {
    alertHistory[i].recipient = "";
    alertHistory[i].timestamp = "";
    alertHistory[i].message = "";
    alertHistory[i].emailSent = false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();
  Serial.print("Wi-Fi connected. ESP32 IP: ");
  Serial.println(WiFi.localIP());
  Serial.println("Open this IP on a device connected to the same trusted Wi-Fi.");
  Serial.println("Warning: dashboard uses HTTP Basic Auth over HTTP; do not use on an untrusted network.");

  // East Africa Time (UTC+3), with NTP time sources.
  configTime(3 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  const char* headerKeys[] = {"Origin"};
  server.collectHeaders(headerKeys, 1);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleData);
  server.on("/threshold", HTTP_POST, handleThreshold);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started on port 80.");
}

void loop() {
  server.handleClient();
  delay(2);
}
