/*
 * ESP32-S3 LILYGO T-Display S3 Thermometer
 * ==========================================
 * DS18B20 Temperatursensor mit TFT-Display und Webserver
 * 
 * Hardware: LILYGO T-Display S3 (ESP32-S3 + ST7789 1.9" 170x320)
 * Sensor:   DS18B20 an GPIO21
 * 
 * Features:
 *   - Temperaturanzeige auf dem TFT-Display
 *   - Min/Max Temperatur-Tracking
 *   - Webserver mit Live-Aktualisierung (alle 5 Sek)
 *   - mDNS: http://thermometer.local
 *   - Button IO14: Min/Max zurücksetzen
 *   - Button Boot (GPIO0): Display-Helligkeit umschalten
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <TFT_eSPI.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <time.h>

// ========== KONFIGURATION ==========
// WLAN-Zugangsdaten aus credentials.h
#include "credentials.h"

// DS18B20 Datenleitung an GPIO21
#define ONE_WIRE_BUS 21

// Buttons
#define BUTTON1 14  // IO14 Button
#define BUTTON2  0  // Boot Button

// Display
#define BACKLIGHT_PIN  38  // LCD Backlight
#define LCD_POWER_PIN  15  // LCD Power Enable
uint8_t brightnessLevels[] = {64, 128, 200, 255};
uint8_t brightnessIndex = 2;

// Batterie (3.7V LiPo, 6.66Wh / 1800mAh)
#define BAT_ADC_PIN    4   // LCD_BAT_VOLT auf GPIO4
#define BAT_CAPACITY_WH 6.66
#define BAT_VOLTAGE_MAX 4.20  // Voll geladen
#define BAT_VOLTAGE_MIN 3.00  // Entladen (Schutz)
#define BAT_READ_INTERVAL 5000 // Alle 5 Sek messen

// Display-Dimensionen (Querformat)
#define SCREEN_W 320
#define SCREEN_H 170

// Aktualisierungsintervall (ms) - Normal-Modus
#define TEMP_READ_INTERVAL_NORMAL 2000
#define DISPLAY_UPDATE_INTERVAL 1000

// ========== GLOBALE OBJEKTE ==========
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
TFT_eSPI tft = TFT_eSPI();
WebServer server(80);
Preferences preferences;

// ========== GLOBALE VARIABLEN ==========
float currentTemp = -127.0;
float minTemp = 999.0;
float maxTemp = -999.0;
bool sensorFound = false;
unsigned long lastTempRead = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastButtonPress1 = 0;
unsigned long lastButtonPress2 = 0;
unsigned long lastBatRead = 0;
String ipAddress = "";

// Batterie
float batteryVoltage = 0.0;
int batteryPercent = 0;
bool batteryCharging = false;

// CraftBeerPi Push-Konfiguration
bool cbpiPushEnabled = false;
String cbpiHost = "";
int cbpiPort = 8000;
String cbpiSensorKey = "ds18b20";
unsigned long lastCbpiPush = 0;

// Power-Save / Gär-Modus
bool powerSaveEnabled = false;
int measureIntervalMin = 1;  // Messintervall in Minuten (Gär-Modus)
bool displayAutoOff = true;
unsigned long displayOffTime = 0;
unsigned long lastUserInteraction = 0;
#define DISPLAY_TIMEOUT 30000  // Display nach 30s ausschalten im Power-Save
#define CBPI_PUSH_INTERVAL_NORMAL 10000  // Alle 10 Sekunden (Normal)
unsigned long currentMeasureInterval = TEMP_READ_INTERVAL_NORMAL;
unsigned long currentPushInterval = CBPI_PUSH_INTERVAL_NORMAL;

// Farben
#define BG_COLOR      TFT_BLACK
#define TEMP_COLOR    TFT_WHITE
#define LABEL_COLOR   TFT_CYAN
#define MIN_COLOR     0x07FF  // Hellblau
#define MAX_COLOR     0xFD20  // Orange
#define ERROR_COLOR   TFT_RED
#define WIFI_COLOR    TFT_GREEN
#define UNIT_COLOR    TFT_LIGHTGREY
#define BAT_COLOR     0x07E0  // Gruen
#define BAT_LOW_COLOR 0xFBE0  // Gelb
#define BAT_CRIT_COLOR TFT_RED

// ========== WEBSEITE (HTML) ==========
const char MAIN_page[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="de">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Thermometer</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: linear-gradient(135deg, #0f0c29, #302b63, #24243e);
            color: #fff;
            min-height: 100vh;
            display: flex;
            flex-direction: column;
            align-items: center;
            padding: 20px;
        }
        h1 {
            font-size: 1.5em;
            margin-bottom: 20px;
            text-align: center;
            opacity: 0.9;
        }
        .card {
            background: rgba(255,255,255,0.08);
            backdrop-filter: blur(10px);
            border-radius: 20px;
            padding: 30px;
            margin: 10px;
            width: 100%;
            max-width: 400px;
            text-align: center;
            border: 1px solid rgba(255,255,255,0.1);
        }
        .temp-display {
            font-size: 5em;
            font-weight: 200;
            line-height: 1.1;
            margin: 10px 0;
        }
        .temp-unit { font-size: 0.4em; vertical-align: super; opacity: 0.7; }
        .status { font-size: 0.85em; opacity: 0.6; margin-top: 5px; }
        .minmax {
            display: flex;
            justify-content: space-around;
            margin-top: 15px;
        }
        .minmax-item { text-align: center; }
        .minmax-label { font-size: 0.75em; opacity: 0.5; text-transform: uppercase; letter-spacing: 1px; }
        .minmax-value { font-size: 1.8em; font-weight: 300; }
        .min-val { color: #4fc3f7; }
        .max-val { color: #ff7043; }
        .chart-container {
            width: 100%;
            height: 150px;
            margin-top: 10px;
            position: relative;
        }
        canvas { width: 100%; height: 100%; }
        .btn {
            background: rgba(255,255,255,0.15);
            border: 1px solid rgba(255,255,255,0.2);
            color: #fff;
            padding: 10px 24px;
            border-radius: 10px;
            cursor: pointer;
            font-size: 0.9em;
            margin-top: 15px;
            transition: background 0.3s;
        }
        .btn:hover { background: rgba(255,255,255,0.25); }
        .indicator {
            display: inline-block;
            width: 8px; height: 8px;
            border-radius: 50%;
            margin-right: 6px;
            animation: pulse 2s infinite;
        }
        .indicator.ok { background: #4caf50; }
        .indicator.err { background: #f44336; animation: none; }
        @keyframes pulse {
            0%, 100% { opacity: 1; }
            50% { opacity: 0.3; }
        }
        .footer { margin-top: 20px; font-size: 0.75em; opacity: 0.4; }
        .battery-card {
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 15px;
        }
        .bat-icon {
            position: relative;
            width: 50px; height: 24px;
            border: 2px solid rgba(255,255,255,0.5);
            border-radius: 4px;
        }
        .bat-icon::after {
            content: '';
            position: absolute;
            right: -6px; top: 6px;
            width: 4px; height: 10px;
            background: rgba(255,255,255,0.5);
            border-radius: 0 2px 2px 0;
        }
        .bat-fill {
            height: 100%;
            border-radius: 2px;
            transition: width 0.5s, background 0.5s;
        }
        .bat-text { font-size: 1.2em; font-weight: 300; }
        .bat-voltage { font-size: 0.85em; opacity: 0.5; }
        .bat-charging { color: #4fc3f7; font-size: 0.85em; }
        .api-box {
            margin-top: 8px;
            padding: 12px;
            border-radius: 10px;
            background: rgba(255,255,255,0.06);
            border: 1px solid rgba(255,255,255,0.12);
            text-align: left;
        }
        .api-line { font-size: 0.82em; margin: 4px 0; opacity: 0.9; word-break: break-all; }
        .api-label { color: #7de3c7; }
        .pinout-card { display: none; }
        .pinout-wrap {
            margin-top: 8px;
            padding: 12px;
            border-radius: 10px;
            background: rgba(255,255,255,0.06);
            border: 1px solid rgba(255,255,255,0.12);
            text-align: left;
        }
        .pinout-grid {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 12px;
            align-items: start;
        }
        .pinout-title { font-size: 0.8em; opacity: 0.7; margin-bottom: 6px; }
        .pin-row { font-size: 0.84em; margin: 5px 0; }
        .wire-vcc { color: #52ff9a; }
        .wire-gnd { color: #f0f0f0; }
        .wire-data { color: #59d6ff; }
        .wire-pull { color: #ffd452; }
        .pinout-hint { margin-top: 8px; font-size: 0.78em; opacity: 0.85; }
    </style>
</head>
<body>
    <h1>&#x1F321;&#xFE0F; ESP32 Thermometer</h1>
    
    <div class="card">
        <div class="temp-display" id="temp">--.-<span class="temp-unit">&deg;C</span></div>
        <div class="status">
            <span class="indicator ok" id="indicator"></span>
            <span id="statusText">Verbinde...</span>
        </div>
        <div class="minmax">
            <div class="minmax-item">
                <div class="minmax-label">Minimum</div>
                <div class="minmax-value min-val" id="minTemp">--.-&deg;</div>
            </div>
            <div class="minmax-item">
                <div class="minmax-label">Maximum</div>
                <div class="minmax-value max-val" id="maxTemp">--.-&deg;</div>
            </div>
        </div>
    </div>

    <div class="card">
        <div class="minmax-label" style="margin-bottom:10px;">Temperaturverlauf (letzte 5 Min)</div>
        <div class="chart-container">
            <canvas id="chart"></canvas>
        </div>
    </div>

    <div class="card">
        <div class="minmax-label" style="margin-bottom:10px;">Akku (3.7V LiPo &middot; 6.66Wh)</div>
        <div class="battery-card">
            <div class="bat-icon">
                <div class="bat-fill" id="batFill" style="width:0%;background:#4caf50;"></div>
            </div>
            <div>
                <div class="bat-text" id="batPct">--%</div>
                <div class="bat-voltage" id="batVolt">-.--V</div>
            </div>
        </div>
        <div class="bat-charging" id="batStatus"></div>
    </div>

    <div class="card">
        <div class="minmax-label" style="margin-bottom:10px;">CraftBeerPi4 HTTP Sensor</div>
        <div class="api-box">
            <div class="api-line"><span class="api-label">JSON URL:</span> <span id="cbpiJsonUrl">/api/cbpi</span></div>
            <div class="api-line"><span class="api-label">JSON Key:</span> value</div>
            <div class="api-line"><span class="api-label">Plain Value URL:</span> <span id="cbpiValueUrl">/api/cbpi/value</span></div>
        </div>
    </div>

    <div class="card">
        <div class="minmax-label" style="margin-bottom:10px;">CraftBeerPi Push Einstellungen</div>
        <div class="api-box">
            <div style="margin: 8px 0;">
                <label style="display:flex;align-items:center;cursor:pointer;">
                    <input type="checkbox" id="cbpiEnabled" style="width:18px;height:18px;margin-right:8px;cursor:pointer;">
                    <span style="font-size:0.9em;">Automatisch an CBPi senden</span>
                </label>
            </div>
            <div style="margin: 12px 0;">
                <label style="font-size:0.8em;opacity:0.7;display:block;margin-bottom:4px;">CBPi Host/IP:</label>
                <input type="text" id="cbpiHost" placeholder="192.168.178.93" style="width:100%;padding:8px;border-radius:6px;border:1px solid rgba(255,255,255,0.2);background:rgba(255,255,255,0.1);color:#fff;font-size:0.9em;">
            </div>
            <div style="margin: 12px 0;">
                <label style="font-size:0.8em;opacity:0.7;display:block;margin-bottom:4px;">Port:</label>
                <input type="number" id="cbpiPort" value="8000" style="width:100%;padding:8px;border-radius:6px;border:1px solid rgba(255,255,255,0.2);background:rgba(255,255,255,0.1);color:#fff;font-size:0.9em;">
            </div>
            <div style="margin: 12px 0;">
                <label style="font-size:0.8em;opacity:0.7;display:block;margin-bottom:4px;">Sensor Key:</label>
                <input type="text" id="cbpiKey" value="ds18b20" placeholder="ds18b20" style="width:100%;padding:8px;border-radius:6px;border:1px solid rgba(255,255,255,0.2);background:rgba(255,255,255,0.1);color:#fff;font-size:0.9em;">
            </div>
            <div style="font-size:0.75em;opacity:0.6;margin:8px 0;" id="cbpiIntervalHint">Sendet Werte alle 10 Sek an: http://HOST:PORT/httpsensor/KEY/WERT</div>
            <div id="cbpiStatus" style="font-size:0.8em;margin-top:8px;padding:6px;border-radius:4px;background:rgba(255,255,255,0.05);">Status: Deaktiviert</div>
        </div>
    </div>

    <div class="card">
        <div class="minmax-label" style="margin-bottom:10px;">🔋 Power-Save Modus (Gärung)</div>
        <div class="api-box">
            <div style="margin: 8px 0;">
                <label style="display:flex;align-items:center;cursor:pointer;">
                    <input type="checkbox" id="powerSaveEnabled" style="width:18px;height:18px;margin-right:8px;cursor:pointer;">
                    <span style="font-size:0.9em;">Energiesparmodus aktivieren</span>
                </label>
            </div>
            <div style="margin: 12px 0;">
                <label style="font-size:0.8em;opacity:0.7;display:block;margin-bottom:4px;">Messintervall (Minuten):</label>
                <input type="number" id="measureInterval" value="15" min="1" max="120" style="width:100%;padding:8px;border-radius:6px;border:1px solid rgba(255,255,255,0.2);background:rgba(255,255,255,0.1);color:#fff;font-size:0.9em;">
            </div>
            <div style="margin: 8px 0;">
                <label style="display:flex;align-items:center;cursor:pointer;">
                    <input type="checkbox" id="displayAutoOff" style="width:18px;height:18px;margin-right:8px;cursor:pointer;">
                    <span style="font-size:0.9em;">Display automatisch ausschalten</span>
                </label>
            </div>
            <div style="font-size:0.75em;opacity:0.6;margin:12px 0;line-height:1.4;">
                <div>✓ Temperatur nur alle X Minuten messen</div>
                <div>✓ Light Sleep zwischen Messungen</div>
                <div>✓ Display aus (Button weckt für 30s)</div>
                <div>✓ Akkulaufzeit: mehrere Tage statt Stunden</div>
            </div>
            <div id="powerSaveStatus" style="font-size:0.8em;margin-top:8px;padding:6px;border-radius:4px;background:rgba(255,255,255,0.05);">Status: Normal-Modus</div>
        </div>
        <button class="btn" onclick="saveCbpiConfig()" style="margin-top:12px;">Konfiguration speichern</button>
    </div>

    <div class="card pinout-card" id="pinoutCard">
        <div class="minmax-label" style="margin-bottom:10px;">DS18B20 PIN Layout (kein Sensor erkannt)</div>
        <div class="pinout-wrap">
            <div class="pinout-grid">
                <div>
                    <div class="pinout-title">T-Display S3</div>
                    <div class="pin-row wire-vcc">3V</div>
                    <div class="pin-row wire-gnd">GND</div>
                    <div class="pin-row wire-data">GPIO21 (OneWire DATA)</div>
                </div>
                <div>
                    <div class="pinout-title">DS18B20</div>
                    <div class="pin-row wire-vcc">VCC</div>
                    <div class="pin-row wire-gnd">GND</div>
                    <div class="pin-row wire-data">DATA</div>
                </div>
            </div>
            <div class="pinout-hint wire-vcc">3V  -> VCC</div>
            <div class="pinout-hint wire-gnd">GND -> GND</div>
            <div class="pinout-hint wire-data">GPIO21 -> DATA</div>
            <div class="pinout-hint wire-pull">4.7k Pullup zwischen 3V und DATA</div>
        </div>
    </div>

    <button class="btn" onclick="resetMinMax()">Min/Max zur&uuml;cksetzen</button>

    <div class="footer">Aktualisierung alle 5 Sekunden</div>

    <script>
        let history = [];
        const maxPoints = 60;

        function fetchData() {
            fetch('/api/data')
                .then(r => r.json())
                .then(d => {
                    const t = d.temperature;
                    const ind = document.getElementById('indicator');
                    const st = document.getElementById('statusText');
                    const sensorOk = (d.sensor === true || d.sensor === 'true') && t > -100;
                    const pinout = document.getElementById('pinoutCard');
                    
                    if (sensorOk) {
                        document.getElementById('temp').innerHTML = 
                            t.toFixed(1) + '<span class="temp-unit">&deg;C</span>';
                        document.getElementById('minTemp').textContent = d.min.toFixed(1) + '\u00B0';
                        document.getElementById('maxTemp').textContent = d.max.toFixed(1) + '\u00B0';
                        ind.className = 'indicator ok';
                        st.textContent = 'Sensor aktiv \u00B7 ' + new Date().toLocaleTimeString('de-DE');
                        pinout.style.display = 'none';
                        
                        history.push(t);
                        if (history.length > maxPoints) history.shift();
                        drawChart();
                    } else {
                        ind.className = 'indicator err';
                        st.textContent = 'Sensor nicht gefunden!';
                        pinout.style.display = 'block';
                    }
                    
                    // Batterie aktualisieren
                    if (d.battery !== undefined) {
                        const bp = d.battery_pct;
                        const bv = d.battery;
                        document.getElementById('batPct').textContent = bp + '%';
                        document.getElementById('batVolt').textContent = bv.toFixed(2) + 'V';
                        const fill = document.getElementById('batFill');
                        fill.style.width = bp + '%';
                        if (bp > 30) fill.style.background = '#4caf50';
                        else if (bp > 10) fill.style.background = '#ffb74d';
                        else fill.style.background = '#f44336';
                        const bs = document.getElementById('batStatus');
                        if (d.charging) bs.textContent = '\u26A1 Wird geladen (USB)';
                        else if (bp <= 10) bs.textContent = '\u26A0 Akku fast leer!';
                        else bs.textContent = '';
                    }
                })
                .catch(() => {
                    document.getElementById('indicator').className = 'indicator err';
                    document.getElementById('statusText').textContent = 'Verbindung verloren';
                });
        }

        function drawChart() {
            const canvas = document.getElementById('chart');
            const ctx = canvas.getContext('2d');
            canvas.width = canvas.offsetWidth * 2;
            canvas.height = canvas.offsetHeight * 2;
            ctx.scale(2, 2);
            const w = canvas.offsetWidth;
            const h = canvas.offsetHeight;
            
            ctx.clearRect(0, 0, w, h);
            
            if (history.length < 2) return;
            
            const min = Math.floor(Math.min(...history) - 1);
            const max = Math.ceil(Math.max(...history) + 1);
            const range = max - min || 1;
            
            ctx.strokeStyle = 'rgba(255,255,255,0.1)';
            ctx.lineWidth = 0.5;
            for (let i = 0; i <= 4; i++) {
                const y = (i / 4) * h;
                ctx.beginPath();
                ctx.moveTo(0, y);
                ctx.lineTo(w, y);
                ctx.stroke();
                
                ctx.fillStyle = 'rgba(255,255,255,0.3)';
                ctx.font = '10px sans-serif';
                ctx.fillText((max - (i/4) * range).toFixed(1) + '\u00B0', 2, y - 2);
            }
            
            const gradient = ctx.createLinearGradient(0, 0, 0, h);
            gradient.addColorStop(0, '#ff7043');
            gradient.addColorStop(1, '#4fc3f7');
            
            ctx.beginPath();
            ctx.strokeStyle = gradient;
            ctx.lineWidth = 2;
            ctx.lineJoin = 'round';
            
            for (let i = 0; i < history.length; i++) {
                const x = (i / (maxPoints - 1)) * w;
                const y = h - ((history[i] - min) / range) * h;
                if (i === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
            }
            ctx.stroke();
            
            const lastX = ((history.length - 1) / (maxPoints - 1)) * w;
            ctx.lineTo(lastX, h);
            ctx.lineTo(0, h);
            ctx.closePath();
            const fillGrad = ctx.createLinearGradient(0, 0, 0, h);
            fillGrad.addColorStop(0, 'rgba(255,112,67,0.15)');
            fillGrad.addColorStop(1, 'rgba(79,195,247,0.05)');
            ctx.fillStyle = fillGrad;
            ctx.fill();
        }

        function resetMinMax() {
            fetch('/api/reset').then(() => fetchData());
        }

        function initCbpiLinks() {
            const base = window.location.origin;
            document.getElementById('cbpiJsonUrl').textContent = base + '/api/cbpi';
            document.getElementById('cbpiValueUrl').textContent = base + '/api/cbpi/value';
        }

        function loadCbpiConfig() {
            fetch('/api/config')
                .then(r => r.json())
                .then(d => {
                    document.getElementById('cbpiEnabled').checked = d.enabled;
                    document.getElementById('cbpiHost').value = d.host || '';
                    document.getElementById('cbpiPort').value = d.port || 8000;
                    document.getElementById('cbpiKey').value = d.key || 'ds18b20';
                    document.getElementById('powerSaveEnabled').checked = d.powerSave || false;
                    document.getElementById('measureInterval').value = d.measureInterval || 15;
                    document.getElementById('displayAutoOff').checked = d.displayAutoOff !== false;
                    updateCbpiStatus(d);
                    updatePowerSaveStatus(d);
                })
                .catch(() => console.log('Config load failed'));
        }

        function saveCbpiConfig() {
            const config = {
                enabled: document.getElementById('cbpiEnabled').checked,
                host: document.getElementById('cbpiHost').value,
                port: parseInt(document.getElementById('cbpiPort').value),
                key: document.getElementById('cbpiKey').value,
                powerSave: document.getElementById('powerSaveEnabled').checked,
                measureInterval: parseInt(document.getElementById('measureInterval').value),
                displayAutoOff: document.getElementById('displayAutoOff').checked
            };
            fetch('/api/config', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(config)
            })
            .then(r => r.json())
            .then(d => {
                updateCbpiStatus(d);
                updatePowerSaveStatus(d);
                let msg = '';
                if (d.powerSave) msg = 'Power-Save aktiviert! ESP wird neu gestartet...';
                else if (d.enabled) msg = 'Push aktiviert!';
                else msg = 'Konfiguration gespeichert';
                alert(msg);
                if (d.powerSave) setTimeout(() => location.reload(), 2000);
            })
            .catch(() => alert('Speichern fehlgeschlagen'));
        }

        function updateCbpiStatus(config) {
            const status = document.getElementById('cbpiStatus');
            const hint = document.getElementById('cbpiIntervalHint');
            const interval = config.powerSave ? config.measureInterval + ' Min' : '10 Sek';
            hint.textContent = 'Sendet Werte alle ' + interval + ' an: http://HOST:PORT/httpsensor/KEY/WERT';
            if (config.enabled && config.host) {
                status.textContent = 'Status: Aktiv → http://' + config.host + ':' + config.port + '/httpsensor/' + config.key + '/WERT';
                status.style.background = 'rgba(76, 175, 80, 0.15)';
            } else if (config.enabled && !config.host) {
                status.textContent = 'Status: Host fehlt!';
                status.style.background = 'rgba(255, 152, 0, 0.15)';
            } else {
                status.textContent = 'Status: Deaktiviert';
                status.style.background = 'rgba(255,255,255,0.05)';
            }
        }

        function updatePowerSaveStatus(config) {
            const status = document.getElementById('powerSaveStatus');
            if (config.powerSave) {
                const runtime = estimateBatteryRuntime(config.measureInterval);
                status.textContent = 'Status: Power-Save (alle ' + config.measureInterval + ' Min) → ~' + runtime;
                status.style.background = 'rgba(76, 175, 80, 0.15)';
            } else {
                status.textContent = 'Status: Normal-Modus (alle 2 Sek) → ~10-15 Std';
                status.style.background = 'rgba(255,255,255,0.05)';
            }
        }

        function estimateBatteryRuntime(intervalMin) {
            // Schätzung basierend auf 1800mAh Akku
            if (intervalMin >= 60) return '1-2 Wochen';
            if (intervalMin >= 30) return '4-7 Tage';
            if (intervalMin >= 15) return '3-5 Tage';
            if (intervalMin >= 5) return '2-3 Tage';
            return '1-2 Tage';
        }

        initCbpiLinks();
        loadCbpiConfig();
        fetchData();
        setInterval(fetchData, 5000);
    </script>
</body>
</html>
)rawliteral";

// ========== FUNKTIONEN ==========

// Temperatur-abhaengige Farbe (blau -> gruen -> rot)
uint16_t getTempColor(float temp) {
    if (temp < -10) return tft.color565(0, 100, 255);
    if (temp < 0)   return tft.color565(0, 180, 255);
    if (temp < 10)  return tft.color565(0, 220, 200);
    if (temp < 20)  return tft.color565(50, 255, 100);
    if (temp < 25)  return tft.color565(200, 255, 0);
    if (temp < 30)  return tft.color565(255, 200, 0);
    if (temp < 35)  return tft.color565(255, 120, 0);
    return tft.color565(255, 40, 0);
}

void setBacklight(uint8_t brightness) {
    analogWrite(BACKLIGHT_PIN, brightness);
}

// Batteriespannung lesen (Spannungsteiler 1:2 auf GPIO4)
void readBattery() {
    // Mehrfach messen und mitteln fuer stabilen Wert
    uint32_t adcSum = 0;
    for (int i = 0; i < 16; i++) {
        adcSum += analogRead(BAT_ADC_PIN);
    }
    float adcAvg = adcSum / 16.0;
    
    // ESP32-S3 ADC: 12-Bit (0-4095), Referenz ~3.3V
    // Spannungsteiler 1:2, also x2
    batteryVoltage = (adcAvg / 4095.0) * 3.3 * 2.0;
    
    // Prozent berechnen (linear zwischen MIN und MAX)
    float pct = (batteryVoltage - BAT_VOLTAGE_MIN) / (BAT_VOLTAGE_MAX - BAT_VOLTAGE_MIN) * 100.0;
    batteryPercent = constrain((int)pct, 0, 100);
    
    // Lade-Erkennung: Spannung > 4.25V deutet auf Laden hin (USB angeschlossen)
    batteryCharging = (batteryVoltage > 4.25);
}

// Batterie-Farbe je nach Ladestand
uint16_t getBatColor() {
    if (batteryCharging) return LABEL_COLOR;
    if (batteryPercent > 30) return BAT_COLOR;
    if (batteryPercent > 10) return BAT_LOW_COLOR;
    return BAT_CRIT_COLOR;
}

// Batterie-Icon auf dem Display zeichnen (oben rechts)
void drawBatteryIcon() {
    int x = SCREEN_W - 45;
    int y = 3;
    int w = 30;
    int h = 12;
    
    // Hintergrund loeschen
    tft.fillRect(x - 2, y - 1, w + 8, h + 2, BG_COLOR);
    
    // Batterie-Rahmen
    tft.drawRect(x, y, w, h, tft.color565(100, 100, 100));
    // Batterie-Pol (rechts)
    tft.fillRect(x + w, y + 3, 3, h - 6, tft.color565(100, 100, 100));
    
    // Fuellstand
    uint16_t batCol = getBatColor();
    int fillW = (w - 4) * batteryPercent / 100;
    if (fillW > 0) {
        tft.fillRect(x + 2, y + 2, fillW, h - 4, batCol);
    }
    
    // Lade-Blitz wenn USB
    if (batteryCharging) {
        tft.setTextColor(TFT_WHITE, BG_COLOR);
        tft.setTextDatum(MC_DATUM);
        tft.setTextFont(1);
        tft.drawString("~", x + w/2, y + h/2 + 1);
    }
    
    // Prozent-Text darunter
    tft.fillRect(x - 5, y + h + 1, w + 10, 10, BG_COLOR);
    tft.setTextFont(1);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(getBatColor(), BG_COLOR);
    char batStr[10];
    sprintf(batStr, "%d%%", batteryPercent);
    tft.drawString(batStr, x + w/2, y + h + 2);
}

void drawStaticUI() {
    tft.fillScreen(BG_COLOR);
    
    tft.setTextColor(LABEL_COLOR, BG_COLOR);
    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(2);
    tft.drawString("Temperatur", SCREEN_W / 2, 8);
    
    tft.drawFastHLine(15, 30, SCREEN_W - 30, tft.color565(40, 40, 60));
    tft.drawFastHLine(15, 120, SCREEN_W - 30, tft.color565(40, 40, 60));
    
    tft.setTextFont(1);
    tft.setTextColor(MIN_COLOR, BG_COLOR);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("MIN", 15, 126);
    
    tft.setTextColor(MAX_COLOR, BG_COLOR);
    tft.setTextDatum(TR_DATUM);
    tft.drawString("MAX", SCREEN_W - 15, 126);
    
    tft.setTextColor(tft.color565(80, 80, 100), BG_COLOR);
    tft.setTextDatum(BC_DATUM);
    tft.setTextFont(1);
}

void drawNoSensorWiringGuide() {
    int x = 10;
    int y = 36;
    int w = SCREEN_W - 20;
    int h = 86;

    tft.fillRect(x, y, w, h, BG_COLOR);
    tft.drawRect(x, y, w, h, tft.color565(70, 30, 30));

    tft.setTextFont(2);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(ERROR_COLOR, BG_COLOR);
    tft.drawString("Kein DS18B20 gefunden", SCREEN_W / 2, y + 10);

    // Mini-Schema: Board links, Sensor rechts
    int boardX = x + 8;
    int boardY = y + 24;
    int boardW = 122;
    int boardH = 54;
    int sensorX = x + w - 96;
    int sensorY = y + 28;
    int sensorW = 82;
    int sensorH = 46;

    tft.drawRoundRect(boardX, boardY, boardW, boardH, 4, tft.color565(70, 70, 90));
    tft.setTextFont(1);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(tft.color565(140, 160, 255), BG_COLOR);
    tft.drawString("T-Display S3", boardX + boardW / 2, boardY + 4);

    // Board-Pins
    int p3vY = boardY + 16;
    int pgndY = boardY + 30;
    int pdataY = boardY + 44;
    int boardPinX = boardX + boardW;

    tft.fillCircle(boardPinX, p3vY, 2, TFT_GREEN);
    tft.fillCircle(boardPinX, pgndY, 2, TFT_WHITE);
    tft.fillCircle(boardPinX, pdataY, 2, TFT_CYAN);

    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(TFT_GREEN, BG_COLOR);
    tft.drawString("3V", boardPinX - 6, p3vY - 1);
    tft.setTextColor(TFT_WHITE, BG_COLOR);
    tft.drawString("GND", boardPinX - 6, pgndY - 1);
    tft.setTextColor(TFT_CYAN, BG_COLOR);
    tft.drawString("GPIO21", boardPinX - 6, pdataY - 1);

    // DS18B20
    tft.drawRoundRect(sensorX, sensorY, sensorW, sensorH, 4, tft.color565(90, 90, 90));
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(tft.color565(220, 220, 220), BG_COLOR);
    tft.drawString("DS18B20", sensorX + sensorW / 2, sensorY + 4);

    int sPinX = sensorX;
    int svccY = sensorY + 14;
    int sdataY = sensorY + 24;
    int sgndY = sensorY + 34;

    tft.fillCircle(sPinX, svccY, 2, TFT_GREEN);
    tft.fillCircle(sPinX, sdataY, 2, TFT_CYAN);
    tft.fillCircle(sPinX, sgndY, 2, TFT_WHITE);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_GREEN, BG_COLOR);
    tft.drawString("VCC", sPinX + 6, svccY - 3);
    tft.setTextColor(TFT_CYAN, BG_COLOR);
    tft.drawString("DATA", sPinX + 6, sdataY - 3);
    tft.setTextColor(TFT_WHITE, BG_COLOR);
    tft.drawString("GND", sPinX + 6, sgndY - 3);

    // Leitungen Board -> Sensor
    tft.drawLine(boardPinX + 2, p3vY, sPinX - 2, svccY, TFT_GREEN);
    tft.drawLine(boardPinX + 2, pgndY, sPinX - 2, sgndY, TFT_WHITE);
    tft.drawLine(boardPinX + 2, pdataY, sPinX - 2, sdataY, TFT_CYAN);

    // Pullup-Widerstand zwischen 3V und DATA
    int rx = boardPinX + 30;
    tft.drawLine(rx, p3vY, rx, sdataY, tft.color565(255, 210, 0));
    tft.drawRect(rx - 4, p3vY + 8, 8, 10, tft.color565(255, 210, 0));
    tft.setTextDatum(BC_DATUM);
    tft.setTextColor(tft.color565(255, 210, 0), BG_COLOR);
    tft.drawString("4.7k Pullup", SCREEN_W / 2, y + h - 2);
}

void updateDisplay() {
    int centerX = SCREEN_W / 2;
    
    tft.setTextDatum(MC_DATUM);
    
    if (sensorFound && currentTemp > -100) {
        tft.fillRect(5, 35, SCREEN_W - 10, 80, BG_COLOR);
        
        tft.setTextColor(getTempColor(currentTemp), BG_COLOR);
        tft.setTextFont(7);
        
        char tempStr[10];
        if (currentTemp >= 0 && currentTemp < 100) {
            dtostrf(currentTemp, 4, 1, tempStr);
        } else {
            dtostrf(currentTemp, 5, 1, tempStr);
        }
        tft.drawString(tempStr, centerX - 15, 75);
        
        tft.setTextFont(4);
        tft.setTextColor(UNIT_COLOR, BG_COLOR);
        tft.setTextDatum(TL_DATUM);
        tft.drawString("C", centerX + 85, 48);
        tft.drawCircle(centerX + 80, 51, 3, UNIT_COLOR);
        
        tft.fillRect(5, 135, SCREEN_W / 2 - 10, 20, BG_COLOR);
        tft.fillRect(SCREEN_W / 2 + 5, 135, SCREEN_W / 2 - 10, 20, BG_COLOR);
        
        tft.setTextFont(2);
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(MIN_COLOR, BG_COLOR);
        char minStr[12];
        dtostrf(minTemp, 5, 1, minStr);
        strcat(minStr, "'C");
        tft.drawString(minStr, 15, 138);
        
        tft.setTextDatum(TR_DATUM);
        tft.setTextColor(MAX_COLOR, BG_COLOR);
        char maxStr[12];
        dtostrf(maxTemp, 5, 1, maxStr);
        strcat(maxStr, "'C");
        tft.drawString(maxStr, SCREEN_W - 15, 138);
        
    } else {
        drawNoSensorWiringGuide();

        tft.fillRect(5, 135, SCREEN_W / 2 - 10, 20, BG_COLOR);
        tft.fillRect(SCREEN_W / 2 + 5, 135, SCREEN_W / 2 - 10, 20, BG_COLOR);
        tft.setTextFont(2);
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(MIN_COLOR, BG_COLOR);
        tft.drawString("--.-'C", 15, 138);
        tft.setTextDatum(TR_DATUM);
        tft.setTextColor(MAX_COLOR, BG_COLOR);
        tft.drawString("--.-'C", SCREEN_W - 15, 138);
    }
    
    tft.fillRect(0, SCREEN_H - 12, SCREEN_W, 12, BG_COLOR);
    tft.setTextDatum(BC_DATUM);
    tft.setTextFont(1);
    tft.setTextColor(tft.color565(80, 80, 100), BG_COLOR);
    if (WiFi.status() == WL_CONNECTED) {
        tft.drawString(ipAddress, centerX, SCREEN_H - 2);
    } else {
        tft.setTextColor(ERROR_COLOR, BG_COLOR);
        tft.drawString("WiFi getrennt", centerX, SCREEN_H - 2);
    }
}

void connectWiFi() {
    int centerX = SCREEN_W / 2;
    int centerY = SCREEN_H / 2;
    
    tft.fillScreen(BG_COLOR);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(WIFI_COLOR, BG_COLOR);
    tft.setTextFont(2);
    tft.drawString("WiFi verbinden...", centerX, centerY - 30);
    tft.setTextFont(1);
    tft.setTextColor(LABEL_COLOR, BG_COLOR);
    tft.drawString(ssid, centerX, centerY);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        tft.drawString(".", 40 + (attempts * 6), centerY + 25);
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        ipAddress = WiFi.localIP().toString();
        Serial.println("\nWiFi verbunden: " + ipAddress);
        
        // NTP-Zeitserver konfigurieren (GMT+1 = 3600, Sommerzeit GMT+2 = 7200)
        configTime(3600, 3600, "pool.ntp.org", "time.nist.gov");
        Serial.println("NTP-Zeit wird synchronisiert...");
        
        tft.fillScreen(BG_COLOR);
        tft.setTextColor(WIFI_COLOR, BG_COLOR);
        tft.setTextFont(2);
        tft.drawString("Verbunden!", centerX, centerY - 40);
        tft.setTextFont(4);
        tft.drawString(ipAddress, centerX, centerY);
        tft.setTextFont(1);
        tft.setTextColor(LABEL_COLOR, BG_COLOR);
        tft.drawString("thermometer.local", centerX, centerY + 35);
        delay(2000);
    } else {
        Serial.println("\nWiFi Fehler!");
        tft.fillScreen(BG_COLOR);
        tft.setTextColor(ERROR_COLOR, BG_COLOR);
        tft.setTextFont(2);
        tft.drawString("WiFi Fehler!", centerX, centerY - 15);
        tft.setTextFont(1);
        tft.drawString("Pruefe SSID & Passwort", centerX, centerY + 15);
        delay(3000);
    }
}

void setupWebServer() {
    server.on("/", HTTP_GET, []() {
        server.send(200, "text/html", MAIN_page);
    });
    
    server.on("/api/data", HTTP_GET, []() {
        String json = "{";
        json += "\"temperature\":" + String(currentTemp, 1) + ",";
        json += "\"min\":" + String(minTemp, 1) + ",";
        json += "\"max\":" + String(maxTemp, 1) + ",";
        json += "\"sensor\":" + String(sensorFound ? "true" : "false") + ",";
        json += "\"battery\":" + String(batteryVoltage, 2) + ",";
        json += "\"battery_pct\":" + String(batteryPercent) + ",";
        json += "\"charging\":" + String(batteryCharging ? "true" : "false") + ",";
        json += "\"uptime\":" + String(millis() / 1000) + ",";
        json += "\"wifi_rssi\":" + String(WiFi.RSSI());
        json += "}";
        server.send(200, "application/json", json);
    });
    
    server.on("/api/reset", HTTP_GET, []() {
        minTemp = currentTemp;
        maxTemp = currentTemp;
        server.send(200, "application/json", "{\"status\":\"ok\"}");
        drawStaticUI();
        updateDisplay();
    });

    // CraftBeerPi kompatibel: JSON + Key "value"
    server.on("/api/cbpi", HTTP_GET, []() {
        String json = "{";
        json += "\"value\":" + String(currentTemp, 2) + ",";
        json += "\"unit\":\"C\",";
        json += "\"sensor\":\"ds18b20\",";
        json += "\"online\":" + String((sensorFound && currentTemp > -100) ? "true" : "false");
        json += "}";
        server.send(200, "application/json", json);
    });

    // CraftBeerPi kompatibel: reiner Zahlenwert
    server.on("/api/cbpi/value", HTTP_GET, []() {
        server.send(200, "text/plain", String(currentTemp, 2));
    });

    // Config laden
    server.on("/api/config", HTTP_GET, []() {
        String json = "{";
        json += "\"enabled\":" + String(cbpiPushEnabled ? "true" : "false") + ",";
        json += "\"host\":\"" + cbpiHost + "\",";
        json += "\"port\":" + String(cbpiPort) + ",";
        json += "\"key\":\"" + cbpiSensorKey + "\",";
        json += "\"powerSave\":" + String(powerSaveEnabled ? "true" : "false") + ",";
        json += "\"measureInterval\":" + String(measureIntervalMin) + ",";
        json += "\"displayAutoOff\":" + String(displayAutoOff ? "true" : "false");
        json += "}";
        server.send(200, "application/json", json);
    });

    // Config speichern
    server.on("/api/config", HTTP_POST, []() {
        if (server.hasArg("plain")) {
            String body = server.arg("plain");
            Serial.println("=== Config empfangen ===");
            Serial.println(body);
            
            // Robustes JSON-Parsing
            int enabledPos = body.indexOf("\"enabled\":");
            int hostPos = body.indexOf("\"host\":\"");
            int portPos = body.indexOf("\"port\":");
            int keyPos = body.indexOf("\"key\":\"");
            
            if (enabledPos >= 0) {
                String val = body.substring(enabledPos + 10);
                cbpiPushEnabled = val.startsWith("true");
                Serial.printf("Parse enabled: %d\n", cbpiPushEnabled);
            }
            if (hostPos >= 0) {
                int hostEnd = body.indexOf("\"", hostPos + 8);
                cbpiHost = body.substring(hostPos + 8, hostEnd);
                Serial.printf("Parse host: %s\n", cbpiHost.c_str());
            }
            if (portPos >= 0) {
                String val = body.substring(portPos + 7);
                cbpiPort = val.toInt();
                Serial.printf("Parse port: %d\n", cbpiPort);
            }
            if (keyPos >= 0) {
                int keyEnd = body.indexOf("\"", keyPos + 7);
                cbpiSensorKey = body.substring(keyPos + 7, keyEnd);
                Serial.printf("Parse key: %s\n", cbpiSensorKey.c_str());
            }
            
            // Power-Save Parameter
            int powerSavePos = body.indexOf("\"powerSave\":");
            int intervalPos = body.indexOf("\"measureInterval\":");
            int displayPos = body.indexOf("\"displayAutoOff\":");
            
            if (powerSavePos >= 0) {
                String val = body.substring(powerSavePos + 12);
                powerSaveEnabled = val.startsWith("true");
                Serial.printf("Parse powerSave: %d\n", powerSaveEnabled);
            }
            if (intervalPos >= 0) {
                String val = body.substring(intervalPos + 18);
                measureIntervalMin = val.toInt();
                if (measureIntervalMin < 1) measureIntervalMin = 1;
                if (measureIntervalMin > 120) measureIntervalMin = 120;
                Serial.printf("Parse interval: %d\n", measureIntervalMin);
            }
            if (displayPos >= 0) {
                String val = body.substring(displayPos + 17);
                displayAutoOff = val.startsWith("true");
                Serial.printf("Parse displayAutoOff: %d\n", displayAutoOff);
            }
            
            // Intervalle berechnen
            if (powerSaveEnabled) {
                currentMeasureInterval = measureIntervalMin * 60000UL;  // Minuten -> ms
                currentPushInterval = currentMeasureInterval;  // Push = Messung im Power-Save
            } else {
                currentMeasureInterval = TEMP_READ_INTERVAL_NORMAL;
                currentPushInterval = CBPI_PUSH_INTERVAL_NORMAL;
            }
            
            // In Preferences speichern
            preferences.begin("cbpi", false);
            preferences.putBool("enabled", cbpiPushEnabled);
            preferences.putString("host", cbpiHost);
            preferences.putUInt("port", cbpiPort);
            preferences.putString("key", cbpiSensorKey);
            preferences.putBool("powerSave", powerSaveEnabled);
            preferences.putUInt("interval", measureIntervalMin);
            preferences.putBool("dispOff", displayAutoOff);
            preferences.end();
            
            Serial.println("Config gespeichert:");
            Serial.printf("  CBPi Enabled: %d\n", cbpiPushEnabled);
            Serial.printf("  Host: %s:%d\n", cbpiHost.c_str(), cbpiPort);
            Serial.printf("  Key: %s\n", cbpiSensorKey.c_str());
            Serial.printf("  Power-Save: %d\n", powerSaveEnabled);
            Serial.printf("  Interval: %d Min\n", measureIntervalMin);
            Serial.printf("  Display Auto-Off: %d\n", displayAutoOff);
            
            String json = "{";
            json += "\"enabled\":" + String(cbpiPushEnabled ? "true" : "false") + ",";
            json += "\"host\":\"" + cbpiHost + "\",";
            json += "\"port\":" + String(cbpiPort) + ",";
            json += "\"key\":\"" + cbpiSensorKey + "\",";
            json += "\"powerSave\":" + String(powerSaveEnabled ? "true" : "false") + ",";
            json += "\"measureInterval\":" + String(measureIntervalMin) + ",";
            json += "\"displayAutoOff\":" + String(displayAutoOff ? "true" : "false");
            json += "}";
            server.send(200, "application/json", json);
            
            // Neustart für saubere Aktivierung der neuen Intervalle
            delay(500);  // Zeit für Response
            Serial.println("Config gespeichert - ESP wird neu gestartet...");
            Serial.flush();
            ESP.restart();
        } else {
            server.send(400, "text/plain", "Bad Request");
        }
    });
    
    server.begin();
    Serial.println("Webserver gestartet auf Port 80");
}

// ========== SETUP ==========
void setup() {
    Serial.begin(115200);
    Serial.println("\n=== ESP32 Thermometer ===");
    
    // Preferences laden
    preferences.begin("cbpi", true);
    cbpiPushEnabled = preferences.getBool("enabled", false);
    cbpiHost = preferences.getString("host", "");
    cbpiPort = preferences.getUInt("port", 8000);
    cbpiSensorKey = preferences.getString("key", "ds18b20");
    powerSaveEnabled = preferences.getBool("powerSave", false);
    measureIntervalMin = preferences.getUInt("interval", 15);
    displayAutoOff = preferences.getBool("dispOff", true);
    preferences.end();
    
    // Intervalle setzen
    if (powerSaveEnabled) {
        currentMeasureInterval = measureIntervalMin * 60000UL;
        currentPushInterval = currentMeasureInterval;
    } else {
        currentMeasureInterval = TEMP_READ_INTERVAL_NORMAL;
        currentPushInterval = CBPI_PUSH_INTERVAL_NORMAL;
    }
    
    Serial.println("Config geladen:");
    Serial.printf("  CBPi: %d | %s:%d | %s\n", cbpiPushEnabled, cbpiHost.c_str(), cbpiPort, cbpiSensorKey.c_str());
    Serial.printf("  Power-Save: %d | Interval: %d Min | Display: %d\n", powerSaveEnabled, measureIntervalMin, displayAutoOff);
    Serial.printf("  Measure: %lu ms | Push: %lu ms\n", currentMeasureInterval, currentPushInterval);
    
    // Buttons
    pinMode(BUTTON1, INPUT_PULLUP);
    pinMode(BUTTON2, INPUT_PULLUP);
    
    // Batterie-ADC
    pinMode(BAT_ADC_PIN, INPUT);
    analogReadResolution(12);
    readBattery();  // Erste Messung
    
    // LCD Power einschalten
    pinMode(LCD_POWER_PIN, OUTPUT);
    digitalWrite(LCD_POWER_PIN, HIGH);
    delay(50);
    
    // Display initialisieren
    tft.init();
    tft.setRotation(1);  // Querformat (320x170)
    tft.fillScreen(BG_COLOR);
    
    // Hintergrundbeleuchtung
    pinMode(BACKLIGHT_PIN, OUTPUT);
    setBacklight(brightnessLevels[brightnessIndex]);
    
    // Splash Screen
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TEMP_COLOR, BG_COLOR);
    tft.setTextFont(4);
    tft.drawString("Thermometer", SCREEN_W / 2, SCREEN_H / 2 - 20);
    tft.setTextFont(2);
    tft.setTextColor(LABEL_COLOR, BG_COLOR);
    tft.drawString("T-Display S3 + DS18B20", SCREEN_W / 2, SCREEN_H / 2 + 15);
    delay(1500);
    
    // DS18B20 initialisieren
    sensors.begin();
    int deviceCount = sensors.getDeviceCount();
    sensorFound = (deviceCount > 0);
    
    Serial.print("DS18B20 Sensoren gefunden: ");
    Serial.println(deviceCount);
    
    if (sensorFound) {
        sensors.setResolution(12);
        sensors.setWaitForConversion(false);
        sensors.requestTemperatures();
    }
    
    // WiFi verbinden
    connectWiFi();
    
    // WiFi Sleep Mode setzen
    if (powerSaveEnabled) {
        WiFi.setSleep(true);
        Serial.println("WiFi Light Sleep aktiviert");
    } else {
        WiFi.setSleep(false);
    }
    
    // mDNS starten
    if (MDNS.begin("thermometer")) {
        Serial.println("mDNS: http://thermometer.local");
        MDNS.addService("http", "tcp", 80);
    }
    
    // Webserver starten
    setupWebServer();
    
    // Display-UI zeichnen
    drawStaticUI();
    drawBatteryIcon();
    updateDisplay();
    
    // Im Power-Save Display nach Timeout ausschalten
    if (powerSaveEnabled && displayAutoOff) {
        lastUserInteraction = millis();
    }
}

// Hilfsfunktion: Echte Uhrzeit von NTP (statt Uptime)
String getTimestamp() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        // Fallback auf Uptime wenn NTP noch nicht sync
        unsigned long totalSec = millis() / 1000;
        int hours = totalSec / 3600;
        int minutes = (totalSec % 3600) / 60;
        int seconds = totalSec % 60;
        char buf[12];
        sprintf(buf, "%02d:%02d:%02d", hours, minutes, seconds);
        return String(buf);
    }
    char buf[20];
    sprintf(buf, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    return String(buf);
}

// Funktion: Temperatur an CraftBeerPi senden
void sendToCraftBeerPi(float temp) {
    if (!cbpiPushEnabled || cbpiHost.length() == 0) return;
    if (WiFi.status() != WL_CONNECTED) return;
    
    HTTPClient http;
    String url = "http://" + cbpiHost + ":" + String(cbpiPort) + "/httpsensor/" + cbpiSensorKey + "/" + String(temp, 2);
    
    Serial.printf("[%s] Sende an CBPi: %s\n", getTimestamp().c_str(), url.c_str());
    
    http.begin(url);
    http.setTimeout(3000);
    int httpCode = http.GET();
    
    if (httpCode > 0) {
        if (httpCode == 204 || httpCode == 200) {
            Serial.printf("[%s] ✓ CBPi Push erfolgreich (Code %d)\n", getTimestamp().c_str(), httpCode);
        } else {
            Serial.printf("[%s] ! CBPi Response: %d\n", getTimestamp().c_str(), httpCode);
        }
    } else {
        Serial.printf("[%s] ✗ CBPi Push Fehler: %s\n", getTimestamp().c_str(), http.errorToString(httpCode).c_str());
    }
    
    http.end();
}

// ========== LOOP ==========
void loop() {
    server.handleClient();
    
    unsigned long now = millis();
    
    // Display Auto-Off im Power-Save-Modus
    if (powerSaveEnabled && displayAutoOff) {
        if (displayOffTime == 0 && (now - lastUserInteraction > DISPLAY_TIMEOUT)) {
            setBacklight(0);
            displayOffTime = millis();
            Serial.println("Display ausgeschaltet (Power-Save)");
        }
    }
    
    // Temperatur lesen
    if (now - lastTempRead >= currentMeasureInterval) {
        lastTempRead = now;
        
        if (sensorFound) {
            float temp = sensors.getTempCByIndex(0);
            
            if (temp != DEVICE_DISCONNECTED_C && temp > -100 && temp < 125) {
                currentTemp = temp;
                if (temp < minTemp) minTemp = temp;
                if (temp > maxTemp) maxTemp = temp;
                
                Serial.printf("[%s] Temperatur gemessen: %.2f°C\n", getTimestamp().c_str(), temp);
                
                // Display kurz an im Power-Save bei neuer Messung
                if (powerSaveEnabled && displayAutoOff && displayOffTime > 0) {
                    setBacklight(brightnessLevels[brightnessIndex]);
                    displayOffTime = 0;
                    lastUserInteraction = millis();
                    drawStaticUI();
                    updateDisplay();
                }
                
                // An CraftBeerPi senden (Intervall abhängig vom Modus)
                if (now - lastCbpiPush >= currentPushInterval) {
                    lastCbpiPush = now;
                    sendToCraftBeerPi(currentTemp);
                }
            } else {
                sensors.begin();
                sensorFound = (sensors.getDeviceCount() > 0);
            }
            
            sensors.requestTemperatures();
        } else {
            sensors.begin();
            sensorFound = (sensors.getDeviceCount() > 0);
            if (sensorFound) {
                sensors.setResolution(12);
                sensors.setWaitForConversion(false);
                sensors.requestTemperatures();
            }
        }
    }
    
    // Batterie lesen
    if (now - lastBatRead >= BAT_READ_INTERVAL) {
        lastBatRead = now;
        readBattery();
        drawBatteryIcon();
    }
    
    // Display aktualisieren
    if (now - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
        lastDisplayUpdate = now;
        updateDisplay();
    }
    
    // Button 1 (IO14): Min/Max zuruecksetzen + Display wecken
    if (digitalRead(BUTTON1) == LOW && (now - lastButtonPress1 > 500)) {
        lastButtonPress1 = now;
        lastUserInteraction = now;
        
        // Display wecken im Power-Save
        if (powerSaveEnabled && displayAutoOff && displayOffTime > 0) {
            setBacklight(brightnessLevels[brightnessIndex]);
            displayOffTime = 0;
            Serial.println("Display aktiviert (Button)");
        }
        
        minTemp = currentTemp;
        maxTemp = currentTemp;
        Serial.println("Min/Max zurueckgesetzt");
        drawStaticUI();
        updateDisplay();
    }
    
    // Button 2 (Boot/GPIO0): Helligkeit umschalten + Display wecken
    if (digitalRead(BUTTON2) == LOW && (now - lastButtonPress2 > 500)) {
        lastButtonPress2 = now;
        lastUserInteraction = now;
        
        // Display wecken im Power-Save
        if (powerSaveEnabled && displayAutoOff && displayOffTime > 0) {
            displayOffTime = 0;
        }
        
        brightnessIndex = (brightnessIndex + 1) % (sizeof(brightnessLevels) / sizeof(brightnessLevels[0]));
        setBacklight(brightnessLevels[brightnessIndex]);
        Serial.print("Helligkeit: ");
        Serial.println(brightnessLevels[brightnessIndex]);
    }
    
    // WiFi Reconnect
    static unsigned long lastWiFiCheck = 0;
    if (now - lastWiFiCheck > 30000) {
        lastWiFiCheck = now;
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi Reconnect...");
            WiFi.reconnect();
        }
    }
}
