# ESP32-S3 LILYGO T-Display S3 Thermometer

🌡️ DS18B20 Temperatursensor mit TFT-Display, Web-Interface und CraftBeerPi4-Integration.

## ✨ Features

- **Temperaturanzeige**: DS18B20 Sensor mit TFT-Display
- **CraftBeerPi4-Integration**: Sendet Daten per WiFi an HTTPSensor
- **Web-Konfiguration**: Alle Einstellungen über Browser änderbar
- **Power-Save-Modus**: Messintervalle 1-120 Min für längere Akkulaufzeit
- **NTP-Zeitstempel**: Echte Zeitangaben statt Uptime
- **Persistente Konfiguration**: Einstellungen bleiben nach Neustart erhalten
- **Min/Max-Tracking**: Mit Reset-Button
- **mDNS**: Erreichbar unter http://thermometer.local

## 📦 Hardware

| Komponente | Details |
|---|---|
| Board | LILYGO T-Display S3 (ESP32-S3 + ST7789 1.9" 170x320 IPS) |
| Sensor | DS18B20 Temperatursensor (wasserdicht oder TO-92) |
| Widerstand | 4.7 kΩ Pull-Up (zwischen DATA und VCC) |

## Verkabelung DS18B20

```
DS18B20          T-Display S3
─────────        ──────────────
VCC (rot)   ──→  3V (Pin oben rechts)
GND (schwarz)──→ GND
DATA (gelb) ──→  GPIO21

         ┌──── 4.7 kΩ ────┐
         │                 │
       DATA (GPIO21)      3V
```

### Pin-Belegung am Board (Draufsicht, USB unten)

```
         ┌─────────────────────────┐
    3V ──┤ 3V               GND   ├── GND
         │                  GND   │
  GPIO01 ┤ 1             43      ├ GPIO43
  GPIO02 ┤ 2             44      ├ GPIO44
  GPIO03 ┤ 3             18      ├ GPIO18
  GPIO10 ┤ 10            17      ├ GPIO17
  GPIO11 ┤ 11    ┌──────┐ 21     ├ GPIO21 ← DS18B20 DATA
  GPIO12 ┤ 12    │ TFT  │        │
  GPIO13 ┤ 13    │170x  │ NC     │
    NC   ┤       │320   │ NC     │
    NC   ┤       │      │ GND    ├── GND
   GND ──┤ GND   └──────┘ GND    ├── GND
    5V ──┤ 5V             3V     ├── 3V
         │                       │
  Boot○  │  [IO14]  USB-C  [Boot]│  ○IO14
         └─────────────────────────┘
```

> **Wichtig:** Der 4.7 kΩ Widerstand zwischen DATA (GPIO21) und 3V ist **zwingend erforderlich** für eine stabile Kommunikation mit dem DS18B20!

### DS18B20 Pinout (Flachseite zu dir)

```
     ┌───┐
     │   │
  1  2  3
  │  │  │
 GND DATA VCC
```

## Software-Setup

### 1. WLAN konfigurieren

**Wichtig**: WLAN-Credentials werden NICHT ins Repository übertragen!

```bash
# Kopiere die Beispiel-Datei
cp ESP32_Thermometer/credentials.h.example ESP32_Thermometer/credentials.h

# Bearbeite credentials.h und trage deine WLAN-Daten ein
```

In `ESP32_Thermometer/credentials.h`:

```cpp
const char* ssid     = "DEIN_WLAN_NAME";
const char* password = "DEIN_WLAN_PASSWORT";
```

### 2. CraftBeerPi4 Integration (optional)

**Im Web-Interface des ESP32**:
- CraftBeerPi Host: `192.168.x.x` (IP deines Raspberry Pi)
- CraftBeerPi Port: `8000`
- Sensor Key: `ds18b20` (muss mit CraftBeerPi übereinstimmen)
- CraftBeerPi aktiviert: ✓

**In CraftBeerPi4**:
- Hardware → Sensoren → "+" (Neuer Sensor)
- Name: `ESP32`
- Typ: `HTTPSensor`
- Key: `ds18b20` (muss übereinstimmen!)
- Timeout: Empfohlen 360s bei 5-Min-Intervall

### 3. Power-Save Modus

- **Messintervall**: 1-120 Minuten wählbar
- **Display Auto-Off**: Nach 30s Inaktivität
- **WiFi Light Sleep**: Automatisch zwischen Messungen

**Geschätzte Akkulaufzeit (1800mAh LiPo)**:
- Normal (1 Min): 10-15h
- 15 Min: 3-5 Tage
- 60 Min: 1-2 Wochen

### 4. Kompilieren & Flashen

```bash
# Mit PlatformIO CLI
pio run -t upload

# Serial Monitor öffnen
pio device monitor
```

Oder in VS Code mit PlatformIO-Extension: **Upload-Button** (→) klicken.

## Features

### Display (320x170 Querformat)
- **Große Temperaturanzeige** mit farblicher Kodierung (blau → grün → rot)
- **Min/Max** Werte
- **IP-Adresse** am unteren Rand
- **IO14-Button**: Min/Max zurücksetzen
- **Boot-Button** (GPIO0): Display-Helligkeit umschalten (4 Stufen)

### Webinterface
- Erreichbar unter `http://<IP-Adresse>` oder `http://thermometer.local`
- Responsive Design für Smartphone
- Live-Aktualisierung alle 5 Sekunden
- Temperaturverlauf-Graph (letzte 5 Minuten)
- Min/Max Reset-Button

### API Endpunkte
| Endpunkt | Beschreibung |
|---|---|
| `GET /` | Webseite mit Live-Daten |
| `GET /config` | Web-Konfigurationsseite |
| `GET /api/data` | JSON mit Temperatur, Min, Max, Uptime, RSSI |
| `GET /api/reset` | Min/Max Werte zurücksetzen |
| `GET /api/config` | Aktuelle Konfiguration als JSON |
| `POST /save-config` | Konfiguration speichern (JSON) |

## Troubleshooting

| Problem | Lösung |
|---|---|
| Sensor nicht gefunden | 4.7kΩ Pull-Up prüfen, Verkabelung kontrollieren |
| Display bleibt schwarz | LCD_Power_On (GPIO15) prüfen, Board-Typ in platformio.ini prüfen |
| WiFi verbindet nicht | SSID/Passwort prüfen, 2.4 GHz Netz verwenden |
| Falsche Temperatur (-127°C) | Kurzschluss/Wackelkontakt am Sensor |
| CraftBeerPi empfängt keine Daten | Key prüfen, Timeout erhöhen, Firewall prüfen |

## 🔗 Links

- [CraftBeerPi4 German UI](https://github.com/Hansi137/craftbeerpi4-german-ui)
- [LILYGO T-Display S3](https://github.com/Xinyuan-LilyGO/T-Display-S3)
- [DS18B20 Datasheet](https://datasheets.maximintegrated.com/en/ds/DS18B20.pdf)

## 📝 Lizenz

MIT License

## 🤝 Beiträge

Pull Requests sind willkommen! Für größere Änderungen bitte zuerst ein Issue öffnen.

---

**Erstellt**: 2026 | **Status**: ✅ Produktiv im Einsatz
