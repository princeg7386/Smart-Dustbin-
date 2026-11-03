#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>

// ==========================================
// 1. PIN CONFIGURATION
// ==========================================
// SENSOR 1: HAND SENSOR (OUTSIDE -> OPENS SERVO)
#define TRIG_HAND 5
#define ECHO_HAND 18

// SENSOR 2: FILL LEVEL SENSOR (INSIDE -> MEASURES DEPTH)
#define TRIG_FILL 19
#define ECHO_FILL 23

// Servo Motor Pin
#define SERVO_PIN 13

// IR Flame Sensor Pin (D0)
#define IR_FIRE_PIN 32

// Smoke Sensor Pin (Make sure A0 is connected!)
#define SMOKE_PIN 34

// Buzzer Pin
#define BUZZER_PIN 4

// ==========================================
// 2. SETTINGS
// ==========================================
const char* AP_SSID = "SmartDustbin"; // Open Wi-Fi

// Hand Trigger: 15cm ke andar aate hi KHULEGA!
const int HAND_TRIGGER_CM       = 15; 
const unsigned long LID_HOLD_MS = 10000; // 10 Seconds hold

// Servo Angles (Exact 90° Swing without buzzing)
const int SERVO_CLOSE_ANGLE = 10;   // Closed
const int SERVO_OPEN_ANGLE  = 100;  // Open (100 - 10 = 90 Degrees!)

const int TOTAL_BIN_HEIGHT_CM = 30; // Dustbin depth in cm

// ==========================================
// 3. GLOBAL VARIABLES
// ==========================================
WebServer server(80);
Servo lidServo;

bool isLidOpen = false;
unsigned long lidOpenedAt = 0;
unsigned long cooldownUntil = 0;

int fillPercent = 0;
long spaceLeftCm = TOTAL_BIN_HEIGHT_CM;
long handDistanceCm = 999;

int cleanAirPercent = 98;
int airPPM = 35;
int smokeBaseline = 500; // Auto-calibrated on boot
bool isFireDetected = false;
unsigned long lastSensorTime = 0;
unsigned long lastHandRead   = 0;
unsigned long lastFillRead   = 0;

// Read Single Ultrasonic with timeout protection
long readDistance(int trig, int echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);

  long duration = pulseIn(echo, HIGH, 25000);
  if (duration == 0) return 999;
  long cm = duration * 0.034 / 2;
  return (cm > 0 && cm < 400) ? cm : 999;
}

// Servo Actions
void openLid() {
  lidServo.write(SERVO_OPEN_ANGLE);
  isLidOpen = true;
  lidOpenedAt = millis();
  tone(BUZZER_PIN, 1800, 100);
  Serial.println("\n>>> [ACTION] SERVO OPENED 90°! (Holding 10 Seconds)");
}

void closeLid() {
  lidServo.write(SERVO_CLOSE_ANGLE);
  isLidOpen = false;
  cooldownUntil = millis() + 2000; // 2 second cooldown after close
  tone(BUZZER_PIN, 1200, 80);
  Serial.println("\n<<< [ACTION] SERVO CLOSED! (2s cooldown shuru...)");
}

// ==========================================
// 4. CLEAN MODERN DASHBOARD
// ==========================================
const char DASHBOARD_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Smart IoT Dustbin</title>
<style>
  :root {
    --bg: #090d16;
    --card: #131a29;
    --primary: #00e5ff;
    --danger: #ff1744;
    --warning: #ffab00;
    --success: #00e676;
    --text: #f0f4f8;
    --sub: #8fa0bd;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, sans-serif; }
  body { background: var(--bg); color: var(--text); padding: 18px 10px; display: flex; flex-direction: column; align-items: center; }
  h1 { font-size: 24px; color: var(--primary); text-align: center; margin-bottom: 4px; }
  p.subtitle { font-size: 13px; color: var(--sub); text-align: center; margin-bottom: 18px; }

  .grid { display: grid; gap: 14px; width: 100%; max-width: 420px; }
  .card { background: var(--card); border: 1px solid #1f2b42; border-radius: 14px; padding: 18px; box-shadow: 0 4px 20px rgba(0,0,0,0.4); }
  .card-top { display: flex; justify-content: space-between; font-size: 12px; color: var(--sub); text-transform: uppercase; font-weight: 700; }
  .val-row { display: flex; justify-content: space-between; align-items: center; margin-top: 10px; }
  .big-num { font-size: 42px; font-weight: 800; }
  
  .bar-bg { width: 100%; height: 14px; background: #0a0f1c; border-radius: 8px; overflow: hidden; margin-top: 12px; }
  .bar-fill { height: 100%; width: 0%; transition: width 0.3s ease, background-color 0.3s; }

  .fire-card { border-left: 5px solid var(--success); transition: all 0.3s; }
  .fire-card.active { border-left-color: var(--danger); background: #260d16; }

  .badge { padding: 4px 10px; border-radius: 20px; font-size: 11px; font-weight: 700; }
  .btn-row { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-top: 14px; }
  .btn { border: none; padding: 13px; border-radius: 10px; font-size: 14px; font-weight: 700; cursor: pointer; color: #fff; }
  .btn-open { background: linear-gradient(135deg, #ff9100, #ff6d00); }
  .btn-close { background: linear-gradient(135deg, #00b0ff, #0091ea); }
</style>
</head>
<body>

  <h1>SMART IOT DUSTBIN</h1>
  <p class="subtitle">Live Sensor & 90° Automated Lid System</p>

  <div class="grid">

    <!-- AIR CLEANLINESS -->
    <div class="card">
      <div class="card-top">
        <span>Air Cleanliness</span>
        <span class="badge" id="airBadge" style="background:rgba(0,230,118,0.15); color:var(--success);">PURE AIR</span>
      </div>
      <div class="val-row">
        <div class="big-num" id="airPct" style="color:var(--success);">98%</div>
        <div style="text-align:right; font-size:13px; color:var(--sub);">Smoke Density<br><b id="airPpm" style="color:#fff; font-size:16px;">35 PPM</b></div>
      </div>
      <div class="bar-bg"><div class="bar-fill" id="airBar" style="background:var(--success); width:98%;"></div></div>
    </div>

    <!-- CAPACITY FILL LEVEL -->
    <div class="card">
      <div class="card-top">
        <span>Capacity Fill Level</span>
        <span class="badge" id="fillBadge" style="background:rgba(0,229,255,0.15); color:var(--primary);">NORMAL</span>
      </div>
      <div class="val-row">
        <div class="big-num" id="fillPct" style="color:var(--primary);">0%</div>
        <div style="text-align:right; font-size:13px; color:var(--sub);">Space Left<br><b id="spaceLeft" style="color:#fff; font-size:16px;">30 cm</b></div>
      </div>
      <div class="bar-bg"><div class="bar-fill" id="fillBar" style="background:var(--primary);"></div></div>
    </div>

    <!-- FIRE DETECTION -->
    <div class="card fire-card" id="fireCard">
      <div class="card-top">
        <span>Fire & Flame Status</span>
        <span id="fireIcon">🛡️</span>
      </div>
      <div style="font-size: 20px; font-weight: 800; margin-top: 8px;" id="fireTxt" style="color:var(--success);">
        SAFE (NO FIRE)
      </div>
    </div>

    <!-- LID STATUS & CONTROLS -->
    <div class="card">
      <div class="card-top">
        <span>Dustbin Lid (90° Servo)</span>
        <span id="lidState" style="font-weight: 800; color: var(--primary);">CLOSED</span>
      </div>
      <div style="font-size: 13px; color: var(--sub); margin-top: 8px;">
        Hand Sensor: <b id="handStatus" style="color:#fff;">Waiting...</b>
      </div>
      <div class="btn-row">
        <button class="btn btn-open" onclick="fetch('/open')">👐 OPEN (10s)</button>
        <button class="btn btn-close" onclick="fetch('/close')">🔒 CLOSE</button>
      </div>
    </div>

  </div>

<script>
  function updateData() {
    fetch('/api/stats').then(res=>res.json()).then(d=>{
      // Air
      document.getElementById('airPct').innerText = d.clean_air + '%';
      document.getElementById('airPpm').innerText = d.ppm + ' PPM';
      const aBar = document.getElementById('airBar');
      aBar.style.width = d.clean_air + '%';
      const aBadge = document.getElementById('airBadge');
      if (d.clean_air >= 75) {
        aBar.style.backgroundColor = '#00e676';
        aBadge.innerText = 'PURE AIR';
        aBadge.style.color = '#00e676';
        document.getElementById('airPct').style.color = '#00e676';
      } else if (d.clean_air >= 45) {
        aBar.style.backgroundColor = '#ffab00';
        aBadge.innerText = 'MILD SMOKE';
        aBadge.style.color = '#ffab00';
        document.getElementById('airPct').style.color = '#ffab00';
      } else {
        aBar.style.backgroundColor = '#ff1744';
        aBadge.innerText = 'HEAVY SMOKE!';
        aBadge.style.color = '#ff1744';
        document.getElementById('airPct').style.color = '#ff1744';
      }

      // Fill
      document.getElementById('fillPct').innerText = d.fill + '%';
      document.getElementById('fillBar').style.width = d.fill + '%';
      document.getElementById('spaceLeft').innerText = d.space_cm + ' cm';
      const fBadge = document.getElementById('fillBadge');
      if (d.fill >= 80) {
        fBadge.innerText = 'FULL';
        fBadge.style.color = '#ff1744';
      } else {
        fBadge.innerText = 'NORMAL';
        fBadge.style.color = '#00e5ff';
      }

      // Fire
      const fCard = document.getElementById('fireCard');
      const fTxt = document.getElementById('fireTxt');
      if (d.fire) {
        fCard.className = 'card fire-card active';
        fTxt.innerText = '🔥 FIRE DETECTED!';
        fTxt.style.color = '#ff1744';
      } else {
        fCard.className = 'card fire-card';
        fTxt.innerText = 'SAFE (NO FIRE)';
        fTxt.style.color = '#00e676';
      }

      // Lid & Hand
      document.getElementById('lidState').innerText = d.lid ? 'OPEN (90°)' : 'CLOSED';
      document.getElementById('lidState').style.color = d.lid ? '#ffab00' : '#00e5ff';
      document.getElementById('handStatus').innerText = d.lid ? 'Open (Holding 10 Seconds...)' : 'Distance: ' + d.hand_cm + ' cm';
    }).catch(()=>{});
  }

  setInterval(updateData, 500);
  updateData();
</script>
</body>
</html>
)rawliteral";

// ==========================================
// 5. SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n--- Smart Dustbin System Booting ---");

  // Sensor Pins
  pinMode(TRIG_HAND, OUTPUT);
  pinMode(ECHO_HAND, INPUT);
  pinMode(TRIG_FILL, OUTPUT);
  pinMode(ECHO_FILL, INPUT);

  pinMode(IR_FIRE_PIN, INPUT);
  pinMode(SMOKE_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // Servo Setup
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  lidServo.setPeriodHertz(50);
  lidServo.attach(SERVO_PIN, 500, 2400);
  lidServo.write(SERVO_CLOSE_ANGLE); // Start at 10° (Closed)

  // Calibrate Smoke Sensor on Room Air (Take 5 quick samples)
  long sum = 0;
  for(int i = 0; i < 5; i++) {
    sum += analogRead(SMOKE_PIN);
    delay(50);
  }
  smokeBaseline = sum / 5;
  if (smokeBaseline < 100) smokeBaseline = 500; // Safe default
  Serial.print("Smoke Baseline Calibrated: ");
  Serial.println(smokeBaseline);

  // Wi-Fi Setup
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, NULL, 1, 0, 4);

  server.on("/", []() { server.send(200, "text/html", DASHBOARD_HTML); });
  server.on("/api/stats", []() {
    String json = "{";
    json += "\"fill\":" + String(fillPercent) + ",";
    json += "\"space_cm\":" + String(spaceLeftCm) + ",";
    json += "\"hand_cm\":" + String(handDistanceCm < 100 ? handDistanceCm : 99) + ",";
    json += "\"clean_air\":" + String(cleanAirPercent) + ",";
    json += "\"ppm\":" + String(airPPM) + ",";
    json += "\"fire\":" + String(isFireDetected ? "true" : "false") + ",";
    json += "\"lid\":" + String(isLidOpen ? "true" : "false");
    json += "}";
    server.send(200, "application/json", json);
  });

  server.on("/open", []() { openLid(); server.send(200, "text/plain", "OK"); });
  server.on("/close", []() { closeLid(); server.send(200, "text/plain", "OK"); });

  server.begin();
  tone(BUZZER_PIN, 2000, 150);
}

// ==========================================
// 6. MAIN LOOP
// ==========================================
void loop() {
  server.handleClient();
  unsigned long now = millis();

  // ---- HAND SENSOR: Har 120ms pe read karo ----
  if (now - lastHandRead >= 120) {
    lastHandRead = now;

    handDistanceCm = readDistance(TRIG_HAND, ECHO_HAND);

    // Fresh timestamp BAAD mein lo (delay() nahi hai toh yeh accurate hai)
    unsigned long nowFresh = millis();

    Serial.print("[HAND] dist="); Serial.print(handDistanceCm);
    Serial.print("cm | lid="); Serial.println(isLidOpen ? "OPEN(ignoring)" : "CLOSED");

    // Sirf tab trigger karo jab:
    // 1. Lid CLOSED ho  (open hone pe 10s tak sensor ignore)
    // 2. Cooldown khatam ho chuka ho
    // 3. Haath 15cm ke andar ho
    if (!isLidOpen && nowFresh > cooldownUntil) {
      if (handDistanceCm > 1 && handDistanceCm <= HAND_TRIGGER_CM) {
        Serial.println(">>> HAATH DETECT! Servo 90 degree OPEN ho raha hai...");
        openLid();
      }
    }
  }

  // ---- FILL SENSOR: Har 200ms pe read karo (alag timer) ----
  if (now - lastFillRead >= 200) {
    lastFillRead = now;

    long depthCm = readDistance(TRIG_FILL, ECHO_FILL);
    if (depthCm <= TOTAL_BIN_HEIGHT_CM && depthCm > 1) {
      spaceLeftCm = depthCm;
      int filledDepth = TOTAL_BIN_HEIGHT_CM - depthCm;
      fillPercent = constrain((filledDepth * 100) / (TOTAL_BIN_HEIGHT_CM - 4), 0, 100);
    }
  }

  // ---- SMOKE + IR SENSOR: Har 300ms pe read karo ----
  if (now - lastSensorTime >= 300) {
    lastSensorTime = now;

    // Smoke
    int rawSmoke = analogRead(SMOKE_PIN);
    if (rawSmoke <= smokeBaseline + 200) {
      cleanAirPercent = 98;
      airPPM = 35;
    } else {
      int excess = rawSmoke - smokeBaseline;
      cleanAirPercent = constrain(map(excess, 200, 2000, 95, 5), 5, 95);
      airPPM = constrain(map(excess, 200, 2000, 50, 650), 35, 800);
    }

    // IR Fire
    isFireDetected = (digitalRead(IR_FIRE_PIN) == LOW);
    if (isFireDetected) tone(BUZZER_PIN, 3000, 50);
  }

  // ---- AUTO-CLOSE: Fresh millis() use karo (stale 'now' se overflow bug tha!) ----
  if (isLidOpen && (millis() - lidOpenedAt >= LID_HOLD_MS)) {
    closeLid();
  }
}
