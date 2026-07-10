#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// ==========================================
// CONFIGURATION & SETUP
// ==========================================

// WiFi Settings: Replace with your credentials, or leave empty to default to Access Point Mode
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Fallback Access Point Settings (if SSID is empty or connection fails)
const char* ap_ssid = "ESP32_Smart_Room";
const char* ap_password = "EnergySavingRoom"; // Must be at least 8 characters

// Hardware Simulation Flag:
// Set to 1 to run virtual sensors & relays (great for instant testing without wiring!).
// Set to 0 to read/write real ESP32 GPIO pins.
#define SIMULATE_HARDWARE 1

// GPIO Pin Definitions (Only used if SIMULATE_HARDWARE is 0)
#define PIR_PIN          13  // Input: PIR Motion Sensor
#define LDR_PIN          32  // Analog Input: Light Dependent Resistor (ADC1_CH6)
#define DHT_PIN          4   // Input: DHT11 or DHT22 Temperature/Humidity Sensor

#define LIGHT_RELAY_PIN  26  // Output: Room Light Relay
#define FAN_RELAY_PIN    25  // Output: Ventilation Fan Relay
#define AC_RELAY_PIN     27  // Output: Air Conditioner Relay
#define BUZZER_PIN       14  // Output: Active High Buzzer

// Sensor Library Selection: DHT sensor library by Adafruit is recommended
#if !SIMULATE_HARDWARE
  #include <DHT.h>
  #define DHTTYPE DHT22  // DHT 11 or DHT 22
  DHT dht(DHT_PIN, DHTTYPE);
#endif

// ==========================================
// SYSTEM STATE VARIABLES
// ==========================================
float temperature = 24.5;
int humidity = 48;
int lightValue = 250;
bool motionActive = false;

bool lightRelay = false;
bool fanRelay = false;
bool acRelay = false;

// Mode: true = Auto Mode (ESP32 automates appliances), false = Manual Mode (user clicks buttons)
bool autoMode = false;

// Automation parameters
unsigned long lastMotionTriggerTime = 0;
const unsigned long motionTimeoutMs = 15000; // 15 seconds motion timeout (for testing/demo)

// Timers
unsigned long lastSensorRead = 0;
const unsigned long sensorInterval = 2000; // Read sensors and broadcast every 2 seconds

// Web Server & WebSockets
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ==========================================
// EMBEDDED DASHBOARD HTML (index.html)
// ==========================================
// The HTML is stored in flash memory as a PROGMEM string literal.
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Smart Room Energy Controller</title>
    <style>
        /* CSS Variables & Theme Setup */
        :root {
            --bg-color: #0b0f19;
            --card-bg: rgba(22, 30, 49, 0.6);
            --card-border: rgba(255, 255, 255, 0.06);
            --text-primary: #f8fafc;
            --text-secondary: #94a3b8;
            --color-temp: #f97316;
            --color-humidity: #3b82f6;
            --color-light: #eab308;
            --color-motion: #ef4444;
            --color-success: #10b981;
            --color-danger: #f43f5e;
            --color-ac: #06b6d4;
            --accent-glow: rgba(59, 130, 246, 0.15);
        }

        /* Basic Reset & Typography */
        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif;
            -webkit-font-smoothing: antialiased;
        }

        body {
            background-color: var(--bg-color);
            background-image: 
                radial-gradient(at 0% 0%, rgba(30, 58, 138, 0.3) 0px, transparent 50%),
                radial-gradient(at 100% 100%, rgba(17, 94, 89, 0.2) 0px, transparent 50%),
                radial-gradient(at 50% 50%, rgba(88, 28, 135, 0.15) 0px, transparent 50%);
            background-attachment: fixed;
            color: var(--text-primary);
            min-height: 100vh;
            padding: 20px;
            display: flex;
            justify-content: center;
        }

        .container {
            width: 100%;
            max-width: 1100px;
            display: flex;
            flex-direction: column;
            gap: 24px;
            padding: 10px 0;
        }

        /* Header Layout */
        header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 16px 24px;
            background: rgba(15, 23, 42, 0.4);
            border: 1px solid var(--card-border);
            border-radius: 16px;
            backdrop-filter: blur(12px);
            -webkit-backdrop-filter: blur(12px);
        }

        .header-title h1 {
            font-size: 1.5rem;
            font-weight: 700;
            background: linear-gradient(135deg, #60a5fa, #34d399);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            letter-spacing: -0.5px;
        }

        .header-title p {
            font-size: 0.85rem;
            color: var(--text-secondary);
            margin-top: 2px;
        }

        .status-indicators {
            display: flex;
            align-items: center;
            gap: 12px;
        }

        .badge {
            display: inline-flex;
            align-items: center;
            gap: 6px;
            padding: 6px 12px;
            border-radius: 9999px;
            font-size: 0.75rem;
            font-weight: 600;
            text-transform: uppercase;
            letter-spacing: 0.5px;
            background: rgba(30, 41, 59, 0.6);
            border: 1px solid var(--card-border);
            transition: all 0.3s ease;
        }

        .badge-ws.connected {
            color: var(--color-success);
            border-color: rgba(16, 185, 129, 0.3);
            box-shadow: 0 0 10px rgba(16, 185, 129, 0.1);
        }

        .badge-ws.disconnected {
            color: var(--color-danger);
            border-color: rgba(244, 63, 94, 0.3);
            box-shadow: 0 0 10px rgba(244, 63, 94, 0.1);
        }

        .badge-ws .dot {
            width: 8px;
            height: 8px;
            border-radius: 50%;
            background-color: currentColor;
            display: inline-block;
        }

        .badge-ws.connected .dot {
            animation: pulse 2s infinite;
        }

        @keyframes pulse {
            0% { transform: scale(0.9); opacity: 0.6; }
            50% { transform: scale(1.2); opacity: 1; box-shadow: 0 0 8px currentColor; }
            100% { transform: scale(0.9); opacity: 0.6; }
        }

        /* Glassmorphic Grid Layout */
        .grid-sensors {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
            gap: 20px;
        }

        .grid-controls {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
            gap: 20px;
        }

        .card {
            background: var(--card-bg);
            border: 1px solid var(--card-border);
            border-radius: 20px;
            padding: 24px;
            backdrop-filter: blur(16px);
            -webkit-backdrop-filter: blur(16px);
            transition: all 0.4s cubic-bezier(0.16, 1, 0.3, 1);
            position: relative;
            overflow: hidden;
            display: flex;
            flex-direction: column;
            justify-content: space-between;
            min-height: 150px;
        }

        .card::before {
            content: '';
            position: absolute;
            top: 0;
            left: 0;
            right: 0;
            height: 100%;
            background: radial-gradient(circle at 10% 10%, rgba(255,255,255,0.03), transparent 60%);
            pointer-events: none;
        }

        .card:hover {
            transform: translateY(-5px);
            border-color: rgba(255, 255, 255, 0.12);
            box-shadow: 0 12px 30px rgba(0, 0, 0, 0.4);
        }

        /* Sensor Specific Card Styling */
        .sensor-header {
            display: flex;
            justify-content: space-between;
            align-items: flex-start;
            margin-bottom: 12px;
        }

        .sensor-title {
            font-size: 0.85rem;
            color: var(--text-secondary);
            font-weight: 500;
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }

        .sensor-icon {
            display: flex;
            align-items: center;
            justify-content: center;
            width: 40px;
            height: 40px;
            border-radius: 12px;
            background: rgba(255, 255, 255, 0.04);
            border: 1px solid rgba(255, 255, 255, 0.05);
            transition: all 0.3s ease;
        }

        .sensor-value {
            font-size: 2.25rem;
            font-weight: 700;
            letter-spacing: -1px;
            margin-top: auto;
            display: flex;
            align-items: baseline;
        }

        .sensor-unit {
            font-size: 1rem;
            color: var(--text-secondary);
            font-weight: 500;
            margin-left: 4px;
        }

        .sensor-footer {
            font-size: 0.75rem;
            color: var(--text-secondary);
            margin-top: 8px;
            display: flex;
            align-items: center;
            gap: 4px;
        }

        /* Ambient Indicator Accents */
        .card-temp:hover .sensor-icon {
            color: var(--color-temp);
            border-color: rgba(249, 115, 22, 0.3);
            background: rgba(249, 115, 22, 0.08);
            box-shadow: 0 0 12px rgba(249, 115, 22, 0.15);
        }
        .card-humidity:hover .sensor-icon {
            color: var(--color-humidity);
            border-color: rgba(59, 130, 246, 0.3);
            background: rgba(59, 130, 246, 0.08);
            box-shadow: 0 0 12px rgba(59, 130, 246, 0.15);
        }
        .card-light:hover .sensor-icon {
            color: var(--color-light);
            border-color: rgba(234, 179, 8, 0.3);
            background: rgba(234, 179, 8, 0.08);
            box-shadow: 0 0 12px rgba(234, 179, 8, 0.15);
        }

        /* Motion Card Custom Styles */
        .card-motion {
            transition: all 0.4s ease;
        }
        .card-motion.motion-detected {
            border-color: rgba(239, 68, 68, 0.4);
            background: radial-gradient(circle at 50% 50%, rgba(239, 68, 68, 0.08), rgba(22, 30, 49, 0.6));
            box-shadow: 0 0 20px rgba(239, 68, 68, 0.15);
        }
        .card-motion.motion-detected .sensor-icon {
            color: var(--color-motion);
            border-color: rgba(239, 68, 68, 0.4);
            background: rgba(239, 68, 68, 0.15);
            animation: pulse-red 1.5s infinite;
        }
        .motion-status-text {
            font-size: 1.5rem !important;
            font-weight: 600 !important;
        }

        @keyframes pulse-red {
            0% { box-shadow: 0 0 0 0 rgba(239, 68, 68, 0.4); }
            70% { box-shadow: 0 0 0 10px rgba(239, 68, 68, 0); }
            100% { box-shadow: 0 0 0 0 rgba(239, 68, 68, 0); }
        }

        /* Control Dashboard Section */
        .section-title {
            font-size: 1.1rem;
            font-weight: 600;
            color: var(--text-primary);
            letter-spacing: 0.5px;
            margin-bottom: 8px;
            display: flex;
            align-items: center;
            justify-content: space-between;
        }

        .section-desc {
            font-size: 0.8rem;
            color: var(--text-secondary);
            margin-top: -4px;
            margin-bottom: 8px;
        }

        /* Mode Select Card Styling */
        .card-mode {
            min-height: 100px;
            flex-direction: row;
            align-items: center;
            justify-content: space-between;
            padding: 20px 24px;
            background: linear-gradient(135deg, rgba(30, 41, 59, 0.7), rgba(15, 23, 42, 0.7));
            border-color: rgba(255,255,255,0.08);
        }

        .mode-info h3 {
            font-size: 1.1rem;
            font-weight: 600;
        }

        .mode-info p {
            font-size: 0.8rem;
            color: var(--text-secondary);
            margin-top: 4px;
        }

        /* Toggle Switches */
        .switch-container {
            position: relative;
            display: inline-block;
            width: 60px;
            height: 32px;
        }

        .switch-container input {
            opacity: 0;
            width: 0;
            height: 0;
        }

        .slider {
            position: absolute;
            cursor: pointer;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background-color: rgba(148, 163, 184, 0.2);
            transition: .4s cubic-bezier(0.16, 1, 0.3, 1);
            border-radius: 34px;
            border: 1px solid rgba(255, 255, 255, 0.1);
        }

        .slider:before {
            position: absolute;
            content: "";
            height: 24px;
            width: 24px;
            left: 3px;
            bottom: 3px;
            background-color: white;
            transition: .4s cubic-bezier(0.16, 1, 0.3, 1);
            border-radius: 50%;
            box-shadow: 0 2px 4px rgba(0,0,0,0.2);
        }

        input:checked + .slider {
            background-color: var(--color-humidity);
        }

        input:checked + .slider:before {
            transform: translateX(28px);
        }

        /* Appliance Card Styling */
        .card-control {
            min-height: 180px;
            display: flex;
            flex-direction: column;
            justify-content: space-between;
        }

        .control-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
        }

        .control-details h3 {
            font-size: 1.1rem;
            font-weight: 600;
        }

        .control-status {
            font-size: 0.8rem;
            color: var(--text-secondary);
            margin-top: 2px;
            font-weight: 500;
        }

        .control-action {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-top: auto;
            padding-top: 20px;
            border-top: 1px solid rgba(255, 255, 255, 0.05);
        }

        /* Appliance Active Glowing States */
        .card-control.active {
            border-color: rgba(255, 255, 255, 0.15);
        }

        .card-control.active-light {
            background: linear-gradient(135deg, rgba(234, 179, 8, 0.05), rgba(22, 30, 49, 0.6));
            border-color: rgba(234, 179, 8, 0.3);
            box-shadow: 0 0 20px rgba(234, 179, 8, 0.06);
        }
        .card-control.active-light .sensor-icon {
            color: var(--color-light);
            border-color: rgba(234, 179, 8, 0.4);
            background: rgba(234, 179, 8, 0.1);
            box-shadow: 0 0 12px rgba(234, 179, 8, 0.2);
        }
        .card-control.active-light input:checked + .slider {
            background-color: var(--color-light);
        }

        .card-control.active-fan {
            background: linear-gradient(135deg, rgba(16, 185, 129, 0.05), rgba(22, 30, 49, 0.6));
            border-color: rgba(16, 185, 129, 0.3);
            box-shadow: 0 0 20px rgba(16, 185, 129, 0.06);
        }
        .card-control.active-fan .sensor-icon {
            color: var(--color-success);
            border-color: rgba(16, 185, 129, 0.4);
            background: rgba(16, 185, 129, 0.1);
            box-shadow: 0 0 12px rgba(16, 185, 129, 0.2);
        }
        .card-control.active-fan .sensor-icon svg {
            animation: spin 3s linear infinite;
        }
        .card-control.active-fan input:checked + .slider {
            background-color: var(--color-success);
        }

        @keyframes spin {
            0% { transform: rotate(0deg); }
            100% { transform: rotate(360deg); }
        }

        .card-control.active-ac {
            background: linear-gradient(135deg, rgba(6, 182, 212, 0.05), rgba(22, 30, 49, 0.6));
            border-color: rgba(6, 182, 212, 0.3);
            box-shadow: 0 0 20px rgba(6, 182, 212, 0.06);
        }
        .card-control.active-ac .sensor-icon {
            color: var(--color-ac);
            border-color: rgba(6, 182, 212, 0.4);
            background: rgba(6, 182, 212, 0.1);
            box-shadow: 0 0 12px rgba(6, 182, 212, 0.2);
        }
        .card-control.active-ac input:checked + .slider {
            background-color: var(--color-ac);
        }

        /* Auto Mode Dim Overlay (Manual Controls Disabled) */
        .controls-wrapper {
            position: relative;
            transition: all 0.3s ease;
        }

        .controls-wrapper.disabled {
            opacity: 0.55;
            pointer-events: none;
            filter: grayscale(0.2);
        }

        .auto-mode-overlay-msg {
            display: none;
            text-align: center;
            font-size: 0.85rem;
            color: var(--color-humidity);
            padding: 8px 12px;
            background: rgba(59, 130, 246, 0.1);
            border: 1px dashed rgba(59, 130, 246, 0.2);
            border-radius: 8px;
            margin-top: -8px;
            margin-bottom: 8px;
            align-items: center;
            justify-content: center;
            gap: 6px;
            animation: fadeIn 0.4s ease forwards;
        }

        .controls-wrapper.disabled + .auto-mode-overlay-msg {
            display: flex;
        }

        @keyframes fadeIn {
            from { opacity: 0; transform: translateY(-5px); }
            to { opacity: 1; transform: translateY(0); }
        }

        /* Footer */
        footer {
            text-align: center;
            font-size: 0.75rem;
            color: var(--text-secondary);
            padding: 16px 0;
            border-top: 1px solid rgba(255, 255, 255, 0.05);
            margin-top: auto;
        }

        /* Responsiveness adjustments */
        @media (max-width: 640px) {
            body {
                padding: 12px;
            }
            .container {
                gap: 16px;
            }
            header {
                flex-direction: column;
                align-items: flex-start;
                gap: 12px;
                padding: 16px;
            }
            .status-indicators {
                width: 100%;
                justify-content: space-between;
            }
            .sensor-value {
                font-size: 1.85rem;
            }
        }
    </style>
</head>
<body>

    <div class="container">
        
        <!-- Header -->
        <header>
            <div class="header-title">
                <h1>Smart Room</h1>
                <p>ESP32 Controller Dashboard</p>
            </div>
            <div class="status-indicators">
                <div class="badge badge-ws disconnected" id="connection-badge">
                    <span class="dot"></span>
                    <span id="connection-text">Disconnected</span>
                </div>
            </div>
        </header>

        <!-- Environmental Metrics Section -->
        <section>
            <div class="section-title">Sensors</div>
            <div class="grid-sensors">
                
                <!-- Temperature -->
                <div class="card card-temp" id="card-temp">
                    <div class="sensor-header">
                        <span class="sensor-title">Temperature</span>
                        <div class="sensor-icon">
                            <!-- Thermometer SVG -->
                            <svg xmlns="http://www.w3.org/2000/svg" width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M14 4v10.54a4 4 0 1 1-4 0V4a2 2 0 0 1 4 0Z"/></svg>
                        </div>
                    </div>
                    <div class="sensor-value">
                        <span id="val-temp">--.-</span><span class="sensor-unit">°C</span>
                    </div>
                    <div class="sensor-footer">
                        <span id="val-temp-f">--.-</span>°F
                    </div>
                </div>

                <!-- Humidity -->
                <div class="card card-humidity" id="card-humidity">
                    <div class="sensor-header">
                        <span class="sensor-title">Humidity</span>
                        <div class="sensor-icon">
                            <!-- Droplet SVG -->
                            <svg xmlns="http://www.w3.org/2000/svg" width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 22a7 7 0 0 0 7-7c0-4.3-7-11-7-11S5 10.7 5 15a7 7 0 0 0 7 7Z"/></svg>
                        </div>
                    </div>
                    <div class="sensor-value">
                        <span id="val-humidity">--</span><span class="sensor-unit">%</span>
                    </div>
                    <div class="sensor-footer">
                        <span id="val-humidity-desc">Normal humidity</span>
                    </div>
                </div>

                <!-- Light level -->
                <div class="card card-light" id="card-light">
                    <div class="sensor-header">
                        <span class="sensor-title">Ambient Light</span>
                        <div class="sensor-icon">
                            <!-- Sun/Light SVG -->
                            <svg xmlns="http://www.w3.org/2000/svg" width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="4"/><path d="M12 2v2"/><path d="M12 20v2"/><path d="M4.93 4.93l1.41 1.41"/><path d="M17.66 17.66l1.41 1.41"/><path d="M2 12h2"/><path d="M20 12h2"/><path d="M6.34 17.66l-1.41 1.41"/><path d="M19.07 4.93l-1.41 1.41"/></svg>
                        </div>
                    </div>
                    <div class="sensor-value">
                        <span id="val-light">---</span><span class="sensor-unit">lux</span>
                    </div>
                    <div class="sensor-footer">
                        <span id="val-light-level">Indoor Reading</span>
                    </div>
                </div>

                <!-- Motion sensor -->
                <div class="card card-motion" id="card-motion">
                    <div class="sensor-header">
                        <span class="sensor-title">Motion Activity</span>
                        <div class="sensor-icon">
                            <!-- Shield/Radar SVG -->
                            <svg xmlns="http://www.w3.org/2000/svg" width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/></svg>
                        </div>
                    </div>
                    <div class="sensor-value motion-status-text">
                        <span id="val-motion">NO MOTION</span>
                    </div>
                    <div class="sensor-footer">
                        <span>Last trigger:</span> <span id="val-motion-time">Never</span>
                    </div>
                </div>

            </div>
        </section>

        <!-- System Mode Section (Auto / Manual) -->
        <section>
            <div class="card card-mode">
                <div class="mode-info">
                    <h3>System Controller Mode</h3>
                    <p id="mode-desc">Manual control enabled. Appliances will respond directly to dashboard commands.</p>
                </div>
                <div class="switch-container">
                    <input type="checkbox" id="toggle-mode" onchange="sendModeToggle()">
                    <span class="slider"></span>
                </div>
            </div>
        </section>

        <!-- Appliance Controls Section -->
        <section>
            <div class="section-title">Manual Controls</div>
            
            <!-- Warning message appearing when Auto mode disables manual switches -->
            <div class="auto-mode-overlay-msg" id="auto-msg">
                <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><line x1="12" y1="8" x2="12" y2="12"/><line x1="12" y1="16" x2="12.01" y2="16"/></svg>
                Auto Mode is active. Manual toggles are disabled to let automation govern room energy saving.
            </div>

            <div class="controls-wrapper" id="controls-wrapper">
                <div class="grid-controls">

                    <!-- Light Appliance -->
                    <div class="card card-control" id="card-light-relay">
                        <div class="sensor-header">
                            <div class="control-details">
                                <h3>Room Light</h3>
                                <div class="control-status" id="status-light">OFF</div>
                            </div>
                            <div class="sensor-icon">
                                <!-- Lightbulb SVG -->
                                <svg xmlns="http://www.w3.org/2000/svg" width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M15 14c.2-1 .7-1.7 1.5-2.5 1-.9 1.5-2.2 1.5-3.5A5 5 0 0 0 8 8c0 1 .4 2.5 1.5 3.5.7.8 1.3 1.5 1.5 2.5"/><path d="M9 18h6"/><path d="M10 22h4"/></svg>
                            </div>
                        </div>
                        <div class="control-action">
                            <span>Power State</span>
                            <div class="switch-container">
                                <input type="checkbox" id="toggle-light" onchange="sendDeviceControl('light', this.checked)">
                                <span class="slider"></span>
                            </div>
                        </div>
                    </div>

                    <!-- Fan Appliance -->
                    <div class="card card-control" id="card-fan-relay">
                        <div class="sensor-header">
                            <div class="control-details">
                                <h3>Ventilation Fan</h3>
                                <div class="control-status" id="status-fan">OFF</div>
                            </div>
                            <div class="sensor-icon">
                                <!-- Fan SVG -->
                                <svg xmlns="http://www.w3.org/2000/svg" width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"/><path d="M12 2v7"/><path d="M12 15v7"/><path d="M2 12h7"/><path d="M15 12h7"/></svg>
                            </div>
                        </div>
                        <div class="control-action">
                            <span>Power State</span>
                            <div class="switch-container">
                                <input type="checkbox" id="toggle-fan" onchange="sendDeviceControl('fan', this.checked)">
                                <span class="slider"></span>
                            </div>
                        </div>
                    </div>

                    <!-- AC Appliance -->
                    <div class="card card-control" id="card-ac-relay">
                        <div class="sensor-header">
                            <div class="control-details">
                                <h3>Air Conditioner</h3>
                                <div class="control-status" id="status-ac">OFF</div>
                            </div>
                            <div class="sensor-icon">
                                <!-- AC/Snowflake SVG -->
                                <svg xmlns="http://www.w3.org/2000/svg" width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><line x1="2" y1="12" x2="22" y2="12"/><line x1="12" y1="2" x2="12" y2="22"/><path d="m20 16-4-4 4-4"/><path d="m4 8 4 4-4 4"/><path d="m16 4-4 4-4-4"/><path d="m8 20 4-4 4 4"/></svg>
                            </div>
                        </div>
                        <div class="control-action">
                            <span>Power State</span>
                            <div class="switch-container">
                                <input type="checkbox" id="toggle-ac" onchange="sendDeviceControl('ac', this.checked)">
                                <span class="slider"></span>
                            </div>
                        </div>
                    </div>

                </div>
            </div>
        </section>

        <!-- Footer -->
        <footer>
            Smart Room Controller © 2026 - Designed for High Performance ESP32 IoT Nodes
        </footer>

    </div>

    <script>
        // System States & WebSocket Settings
        let gateway = `ws://${window.location.host}/ws`;
        let websocket;
        let isAutoMode = false;
        let pollTimer = null;
        let motionTimeoutTimer = null;
        let lastMotionTime = null;

        // On Load initialization
        window.addEventListener('load', initDashboard);

        function initDashboard() {
            // Attempt to connect to WebSockets
            initWebSocket();
        }

        // --- WEB SOCKET COMMUNICATION ---
        function initWebSocket() {
            console.log('Connecting to WebSocket...');
            
            // Handle local testing on file protocol
            if (window.location.protocol === 'file:') {
                console.warn('Dashboard running locally via file://. WebSocket unavailable. Switching to mock/simulated mode.');
                updateBadge(false, "Simulated Mode");
                startMockingData();
                return;
            }

            websocket = new WebSocket(gateway);
            websocket.onopen = onWSOpen;
            websocket.onclose = onWSClose;
            websocket.onmessage = onWSMessage;
            websocket.onerror = onWSError;
        }

        function onWSOpen(event) {
            console.log('WebSocket Connection Opened.');
            updateBadge(true, "Connected");
            
            // If we had a polling timer, clear it
            if (pollTimer) {
                clearInterval(pollTimer);
                pollTimer = null;
            }
        }

        function onWSClose(event) {
            console.warn('WebSocket connection lost. Attempting HTTP polling fallback...');
            updateBadge(false, "Offline Polling");
            
            // Switch to HTTP REST Polling every 2.5 seconds
            if (!pollTimer) {
                pollDataHTTP();
                pollTimer = setInterval(pollDataHTTP, 2500);
            }

            // Retry WebSocket connection after 8 seconds
            setTimeout(initWebSocket, 8000);
        }

        function onWSError(event) {
            console.error('WebSocket Error detected:', event);
        }

        function onWSMessage(event) {
            console.log('WebSocket Message Received:', event.data);
            try {
                const data = JSON.parse(event.data);
                updateUI(data);
            } catch (err) {
                console.error("Error parsing WebSocket JSON payload:", err);
            }
        }

        // --- HTTP FALLBACK COMMUNICATION ---
        function pollDataHTTP() {
            fetch('/api/status')
                .then(response => {
                    if (!response.ok) throw new Error('HTTP Status response not ok');
                    return response.json();
                })
                .then(data => {
                    updateUI(data);
                    updateBadge(false, "HTTP Mode");
                })
                .catch(err => {
                    console.error("Error fetching REST API data:", err);
                    updateBadge(false, "Offline / Error");
                });
        }

        function sendHTTPControl(device, state) {
            // Converts true/false to 1/0
            const stateValue = state ? 1 : 0;
            fetch(`/api/control?device=${device}&state=${stateValue}`)
                .then(response => {
                    if (!response.ok) throw new Error('HTTP Control response not ok');
                    return response.json();
                })
                .then(data => {
                    updateUI(data);
                })
                .catch(err => console.error("HTTP Control Send Failed:", err));
        }

        // Send HTTP mode toggle
        function sendHTTPMode(autoMode) {
            const modeStr = autoMode ? "auto" : "manual";
            fetch(`/api/control?mode=${modeStr}`)
                .then(response => {
                    if (!response.ok) throw new Error('HTTP Mode response not ok');
                    return response.json();
                })
                .then(data => {
                    updateUI(data);
                })
                .catch(err => console.error("HTTP Mode Send Failed:", err));
        }

        // --- DYNAMIC CONTROL DISPATCHERS ---
        function sendDeviceControl(device, state) {
            console.log(`Command Device Control - ${device}: ${state}`);
            if (websocket && websocket.readyState === WebSocket.OPEN) {
                const msg = {
                    action: "control",
                    device: device,
                    value: state ? 1 : 0
                };
                websocket.send(JSON.stringify(msg));
            } else {
                sendHTTPControl(device, state);
            }
        }

        // Send System Mode Toggle
        function sendModeToggle() {
            const toggleElement = document.getElementById('toggle-mode');
            const autoChecked = toggleElement.checked;
            console.log(`Command System Mode Toggle: ${autoChecked ? 'AUTO' : 'MANUAL'}`);
            
            if (websocket && websocket.readyState === WebSocket.OPEN) {
                const msg = {
                    action: "mode",
                    value: autoChecked ? "auto" : "manual"
                };
                websocket.send(JSON.stringify(msg));
            } else {
                sendHTTPMode(autoChecked);
            }
        }

        // --- CORE UI RENDER ENGINE ---
        function updateUI(data) {
            // Update Sensor Values
            if (data.temperature !== undefined) {
                document.getElementById('val-temp').innerText = parseFloat(data.temperature).toFixed(1);
                // Convert to Fahrenheit for footer
                const tempF = (parseFloat(data.temperature) * 9/5) + 32;
                document.getElementById('val-temp-f').innerText = tempF.toFixed(1);
                
                // Color Code Temperature Card
                const cardTemp = document.getElementById('card-temp');
                if (data.temperature > 28) {
                    cardTemp.style.borderLeft = "4px solid var(--color-temp)";
                } else if (data.temperature < 20) {
                    cardTemp.style.borderLeft = "4px solid var(--color-humidity)";
                } else {
                    cardTemp.style.borderLeft = "none";
                }
            }

            if (data.humidity !== undefined) {
                document.getElementById('val-humidity').innerText = parseInt(data.humidity);
                const humDesc = document.getElementById('val-humidity-desc');
                if (data.humidity > 70) {
                    humDesc.innerText = "High humidity (wet)";
                    humDesc.style.color = "var(--color-humidity)";
                } else if (data.humidity < 35) {
                    humDesc.innerText = "Low humidity (dry)";
                    humDesc.style.color = "var(--color-temp)";
                } else {
                    humDesc.innerText = "Comfortable humidity";
                    humDesc.style.color = "var(--text-secondary)";
                }
            }

            if (data.light !== undefined) {
                document.getElementById('val-light').innerText = parseInt(data.light);
                const lightLabel = document.getElementById('val-light-level');
                if (data.light > 500) {
                    lightLabel.innerText = "Brightly Lit Room";
                } else if (data.light < 80) {
                    lightLabel.innerText = "Dimly Lit Room";
                } else {
                    lightLabel.innerText = "Ambient Workspace";
                }
            }

            // Update Motion Status
            if (data.motion !== undefined) {
                const motionCard = document.getElementById('card-motion');
                const motionValEl = document.getElementById('val-motion');
                const isMotionActive = (data.motion === 1 || data.motion === true || data.motion === "active");
                
                if (isMotionActive) {
                    motionCard.classList.add('motion-detected');
                    motionValEl.innerText = "MOTION ACTIVE";
                    lastMotionTime = new Date();
                    document.getElementById('val-motion-time').innerText = lastMotionTime.toLocaleTimeString([], {hour: '2-digit', minute:'2-digit', second:'2-digit'});
                } else {
                    motionCard.classList.remove('motion-detected');
                    motionValEl.innerText = "NO MOTION";
                }
            }

            // Update Mode (Auto/Manual)
            if (data.mode !== undefined) {
                isAutoMode = (data.mode === "auto" || data.mode === 1);
                document.getElementById('toggle-mode').checked = isAutoMode;
                
                const modeDesc = document.getElementById('mode-desc');
                const controlsWrapper = document.getElementById('controls-wrapper');
                
                if (isAutoMode) {
                    modeDesc.innerText = "Auto Mode Active. ESP32 runs onboard automation sensors & relays to save energy.";
                    controlsWrapper.classList.add('disabled');
                } else {
                    modeDesc.innerText = "Manual control enabled. Appliances will respond directly to dashboard commands.";
                    controlsWrapper.classList.remove('disabled');
                }
            }

            // Update Relays
            if (data.light_relay !== undefined) {
                const lightActive = (data.light_relay === 1 || data.light_relay === true);
                document.getElementById('toggle-light').checked = lightActive;
                document.getElementById('status-light').innerText = lightActive ? "ACTIVE / ON" : "INACTIVE / OFF";
                
                const card = document.getElementById('card-light-relay');
                if (lightActive) card.classList.add('active-light');
                else card.classList.remove('active-light');
            }

            if (data.fan_relay !== undefined) {
                const fanActive = (data.fan_relay === 1 || data.fan_relay === true);
                document.getElementById('toggle-fan').checked = fanActive;
                document.getElementById('status-fan').innerText = fanActive ? "ACTIVE / ON" : "INACTIVE / OFF";
                
                const card = document.getElementById('card-fan-relay');
                if (fanActive) card.classList.add('active-fan');
                else card.classList.remove('active-fan');
            }

            if (data.ac_relay !== undefined) {
                const acActive = (data.ac_relay === 1 || data.ac_relay === true);
                document.getElementById('toggle-ac').checked = acActive;
                document.getElementById('status-ac').innerText = acActive ? "ACTIVE / ON" : "INACTIVE / OFF";
                
                const card = document.getElementById('card-ac-relay');
                if (acActive) card.classList.add('active-ac');
                else card.classList.remove('active-ac');
            }
        }

        // Connection badge state controller
        function updateBadge(isConnected, customText) {
            const badge = document.getElementById('connection-badge');
            const text = document.getElementById('connection-text');
            badge.className = "badge badge-ws " + (isConnected ? "connected" : "disconnected");
            text.innerText = customText || (isConnected ? "Connected" : "Disconnected");
        }

        // --- SIMULATOR FOR LOCAL PREVIEWS (OFFLINE DEV) ---
        let mockState = {
            temperature: 24.5,
            humidity: 48,
            light: 250,
            motion: 0,
            mode: "manual",
            light_relay: 0,
            fan_relay: 0,
            ac_relay: 0
        };

        function startMockingData() {
            updateUI(mockState);
            
            // Regularly fluctuate sensor values to simulate real dashboard action
            setInterval(() => {
                mockState.temperature += (Math.random() - 0.5) * 0.3;
                mockState.humidity += Math.floor((Math.random() - 0.5) * 2);
                mockState.humidity = Math.max(20, Math.min(95, mockState.humidity));
                mockState.light += Math.floor((Math.random() - 0.5) * 25);
                mockState.light = Math.max(10, Math.min(800, mockState.light));
                
                // Randomly trigger simulated motion (10% chance)
                if (Math.random() > 0.9) {
                    mockState.motion = 1;
                    setTimeout(() => {
                        mockState.motion = 0;
                        updateUI(mockState);
                    }, 5000);
                }
                
                // If Auto mode, simulate some automation decisions
                if (mockState.mode === "auto") {
                    if (mockState.temperature > 27) {
                        mockState.ac_relay = 1;
                        mockState.fan_relay = 1;
                    } else if (mockState.temperature < 22) {
                        mockState.ac_relay = 0;
                        mockState.fan_relay = 0;
                    }
                    
                    if (mockState.light < 100 && mockState.motion === 1) {
                        mockState.light_relay = 1;
                    } else if (mockState.motion === 0) {
                        // Turn off light after a delay
                        mockState.light_relay = 0;
                    }
                }

                updateUI(mockState);
            }, 3000);

            // Re-bind the interface change functions for the simulation
            window.sendDeviceControl = function(device, state) {
                console.log(`[SIMULATOR] Controlled device '${device}' to: ${state}`);
                mockState[device + "_relay"] = state ? 1 : 0;
                updateUI(mockState);
            };

            window.sendModeToggle = function() {
                const autoChecked = document.getElementById('toggle-mode').checked;
                console.log(`[SIMULATOR] System mode set to: ${autoChecked ? 'AUTO' : 'MANUAL'}`);
                mockState.mode = autoChecked ? "auto" : "manual";
                updateUI(mockState);
            };
        }
    </script>
</body>
</html>
)rawliteral";

// ==========================================
// UTILITY FUNCTIONS & EVENT HANDLERS
// ==========================================

// Build the system status JSON string
String getStatusJSON() {
  StaticJsonDocument<256> doc;
  
  doc["temperature"] = temperature;
  doc["humidity"] = humidity;
  doc["light"] = lightValue;
  doc["motion"] = motionActive ? 1 : 0;
  doc["mode"] = autoMode ? "auto" : "manual";
  doc["light_relay"] = lightRelay ? 1 : 0;
  doc["fan_relay"] = fanRelay ? 1 : 0;
  doc["ac_relay"] = acRelay ? 1 : 0;
  
  String output;
  serializeJson(doc, output);
  return output;
}

// Notify all connected WebSocket clients of a state change
void notifyClients() {
  ws.textAll(getStatusJSON());
}

// Handle incoming control request (both WebSocket and REST fallback)
void handleDeviceControl(String device, int value) {
  bool state = (value == 1);
  bool changed = false;

  if (device == "light" && lightRelay != state) {
    lightRelay = state;
    changed = true;
    #if !SIMULATE_HARDWARE
      digitalWrite(LIGHT_RELAY_PIN, lightRelay ? LOW : HIGH);
    #endif
  }
  else if (device == "fan" && fanRelay != state) {
    fanRelay = state;
    changed = true;
    #if !SIMULATE_HARDWARE
      digitalWrite(FAN_RELAY_PIN, fanRelay ? LOW : HIGH);
    #endif
  }
  else if (device == "ac" && acRelay != state) {
    acRelay = state;
    changed = true;
    #if !SIMULATE_HARDWARE
      digitalWrite(AC_RELAY_PIN, acRelay ? LOW : HIGH);
    #endif
  }

  if (changed) {
    Serial.printf("Device %s set to %s\n", device.c_str(), state ? "ON" : "OFF");
    notifyClients();
  }
}

// Handle mode switch (Auto/Manual)
void handleModeChange(String mode) {
  bool newAutoMode = (mode == "auto");
  if (autoMode != newAutoMode) {
    autoMode = newAutoMode;
    Serial.printf("System Mode changed to: %s\n", autoMode ? "AUTO" : "MANUAL");
    notifyClients();
  }
}

// Process WebSocket data events
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    String message = (char*)data;
    Serial.print("WS Received: ");
    Serial.println(message);

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, message);
    if (error) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.c_str());
      return;
    }

    String action = doc["action"] | "";
    if (action == "control") {
      String device = doc["device"] | "";
      int value = doc["value"] | 0;
      // Controls are ignored in Auto Mode to preserve system override protection
      if (!autoMode) {
        handleDeviceControl(device, value);
      }
    } 
    else if (action == "mode") {
      String value = doc["value"] | "manual";
      handleModeChange(value);
    }
  }
}

// WebSocket Event Listener
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      // Send initial status on connection
      client->text(getStatusJSON());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

// ==========================================
// ARDUINO CORE HOOKS
// ==========================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\nInitializing Smart Room Controller...");

  // Initialize GPIO Pins
  #if !SIMULATE_HARDWARE
    pinMode(PIR_PIN, INPUT);
    pinMode(LIGHT_RELAY_PIN, OUTPUT);
    pinMode(FAN_RELAY_PIN, OUTPUT);
    pinMode(AC_RELAY_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    
    // Start with relays OFF (Active LOW relays are OFF when HIGH)
    digitalWrite(LIGHT_RELAY_PIN, HIGH);
    digitalWrite(FAN_RELAY_PIN, HIGH);
    digitalWrite(AC_RELAY_PIN, HIGH);
    digitalWrite(BUZZER_PIN, LOW); // Start with buzzer OFF
    
    // Start DHT Sensor
    dht.begin();
  #endif

  // WiFi Setup (Station or AP fallback)
  if (strlen(ssid) > 0 && strcmp(ssid, "YOUR_WIFI_SSID") != 0) {
    WiFi.begin(ssid, password);
    Serial.printf("Connecting to WiFi: %s ", ssid);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 15) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi Connected!");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("\nWiFi Connection failed. Activating AP Mode fallback...");
    }
  }

  // AP Mode configuration (if credentials fail or are not supplied)
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.softAP(ap_ssid, ap_password);
    Serial.println("Access Point Started.");
    Serial.print("AP IP Address: ");
    Serial.println(WiFi.softAPIP());
    Serial.printf("Connect to SSID '%s' with password '%s' to access dashboard.\n", ap_ssid, ap_password);
  }

  // Initialize WebSocket handler
  ws.onEvent(onEvent);
  server.addHandler(&ws);

  // Configure REST Web Server Routes
  
  // 1. Server Home Page (Serve HTML from flash)
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  // 2. API Status Endpoint (REST fallback)
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "application/json", getStatusJSON());
  });

  // 3. API Control Endpoint (REST fallback)
  server.on("/api/control", HTTP_GET, [](AsyncWebServerRequest *request){
    bool commandHandled = false;
    
    // Check if mode is being changed
    if (request->hasParam("mode")) {
      String mode = request->getParam("mode")->value();
      handleModeChange(mode);
      commandHandled = true;
    }
    
    // Check if device state is being changed (only allowed in manual mode)
    if (!autoMode && request->hasParam("device") && request->hasParam("state")) {
      String device = request->getParam("device")->value();
      int state = request->getParam("state")->value().toInt();
      handleDeviceControl(device, state);
      commandHandled = true;
    }
    
    if (commandHandled) {
      request->send(200, "application/json", getStatusJSON());
    } else {
      request->send(400, "application/json", "{\"error\":\"Invalid arguments or manual control locked in Auto mode\"}");
    }
  });

  // Start Asynchronous Web Server
  server.begin();
  Serial.println("HTTP and WebSocket Server is Running.");
}

void loop() {
  unsigned long currentMillis = millis();

  // Clean up disconnected WebSocket clients periodically
  ws.cleanupClients();

  // Read Sensors & Update states
  if (currentMillis - lastSensorRead >= sensorInterval) {
    lastSensorRead = currentMillis;

    // --- SENSOR READING & SIMULATION ENGINE ---
    #if SIMULATE_HARDWARE
      // Fluctuate values to simulate a dynamic environment
      temperature += random(-2, 3) * 0.1;
      temperature = constrain(temperature, 15.0, 35.0);
      
      humidity += random(-2, 3);
      humidity = constrain(humidity, 30, 90);
      
      lightValue += random(-20, 21);
      lightValue = constrain(lightValue, 50, 750);

      // 10% chance to simulate motion detection if currently inactive
      if (!motionActive && random(0, 100) < 10) {
        motionActive = true;
        lastMotionTriggerTime = currentMillis;
        Serial.println("[Simulated Sensor] PIR Motion Triggered!");
        Serial.println("[Simulated Output] Buzzer BEEP!");
      } 
      // If motion is active, turn off after motionTimeoutMs
      else if (motionActive && (currentMillis - lastMotionTriggerTime >= motionTimeoutMs)) {
        motionActive = false;
        Serial.println("[Simulated Sensor] PIR Motion Timeout. No activity.");
      }
    #else
      // Actual sensor readings
      float t = dht.readTemperature();
      float h = dht.readHumidity();
      int l = analogRead(LDR_PIN); // Reads 0 to 4095 on ESP32 ADC
      // Convert raw LDR to a simplified Lux approximation for the dashboard
      int lux = map(l, 4095, 0, 10, 1000); 
      bool motion = (digitalRead(PIR_PIN) == HIGH);

      bool dataChanged = false;

      if (!isnan(t)) {
        temperature = t;
      }
      if (!isnan(h)) {
        humidity = h;
      }
      lightValue = lux;
      
      if (motion != motionActive) {
        motionActive = motion;
        if (motionActive) {
          lastMotionTriggerTime = currentMillis;
          Serial.println("[Sensor PIR] Motion Detected!");
          // Beep buzzer
          #if !SIMULATE_HARDWARE
            digitalWrite(BUZZER_PIN, HIGH);
            delay(100);
            digitalWrite(BUZZER_PIN, LOW);
          #endif
        } else {
          Serial.println("[Sensor PIR] Motion Clear.");
        }
      }
    #endif

    // --- AUTOPILOT SMART ENERGY SAVER RULES ---
    if (autoMode) {
      bool ruleTriggered = false;

      // Rule 1: Comfort Cooling (Air Conditioner & Fan Control)
      // Fan Control: ON if Temp > 30°C, else OFF.
      if (temperature > 30.0) {
        if (!fanRelay) {
          fanRelay = true;
          ruleTriggered = true;
          #if !SIMULATE_HARDWARE
            digitalWrite(FAN_RELAY_PIN, LOW); // ON (Active LOW)
          #endif
          Serial.println("[Autopilot] Temperature > 30C: Fan ON");
        }
      } 
      else {
        if (fanRelay) {
          fanRelay = false;
          ruleTriggered = true;
          #if !SIMULATE_HARDWARE
            digitalWrite(FAN_RELAY_PIN, HIGH); // OFF (Active LOW)
          #endif
          Serial.println("[Autopilot] Temperature <= 30C: Fan OFF");
        }
      }

      // AC Control: ON if Temp > 36°C, else OFF.
      if (temperature > 36.0) {
        if (!acRelay) {
          acRelay = true;
          ruleTriggered = true;
          #if !SIMULATE_HARDWARE
            digitalWrite(AC_RELAY_PIN, LOW); // ON (Active LOW)
          #endif
          Serial.println("[Autopilot] Temperature > 36C: AC ON");
        }
      } 
      else {
        if (acRelay) {
          acRelay = false;
          ruleTriggered = true;
          #if !SIMULATE_HARDWARE
            digitalWrite(AC_RELAY_PIN, HIGH); // OFF (Active LOW)
          #endif
          Serial.println("[Autopilot] Temperature <= 36C: AC OFF");
        }
      }

      // Rule 2: Smart Lighting Control
      // Turn lights ON if there is motion and ambient light level < 1000 lux.
      // Turn lights OFF otherwise.
      if (motionActive && lightValue < 1000) {
        if (!lightRelay) {
          lightRelay = true;
          ruleTriggered = true;
          #if !SIMULATE_HARDWARE
            digitalWrite(LIGHT_RELAY_PIN, LOW); // ON (Active LOW)
          #endif
          Serial.println("[Autopilot] Room dark & motion active: Light ON");
        }
      } 
      else {
        if (lightRelay) {
          lightRelay = false;
          ruleTriggered = true;
          #if !SIMULATE_HARDWARE
            digitalWrite(LIGHT_RELAY_PIN, HIGH); // OFF (Active LOW)
          #endif
          Serial.println("[Autopilot] Conditions not met: Light OFF");
        }
      }

      // Rule 3: Empty Room Auto Shutdown
      // If no motion is detected for a long duration, shut down everything (Fan & AC) to save power.
      if (!motionActive && (currentMillis - lastMotionTriggerTime >= motionTimeoutMs)) {
        if (fanRelay || acRelay) {
          fanRelay = false;
          acRelay = false;
          ruleTriggered = true;
          #if !SIMULATE_HARDWARE
            digitalWrite(FAN_RELAY_PIN, HIGH); // OFF (Active LOW)
            digitalWrite(AC_RELAY_PIN, HIGH);  // OFF (Active LOW)
          #endif
          Serial.println("[Autopilot] Long unoccupied room timeout: AC and Fan Shutdown");
        }
      }
    }

    // Print status in serial monitor
    String lightStatusStr = (lightRelay) ? "ON" : "OFF";
    String fanStatusStr = (fanRelay) ? "ON" : "OFF";
    String acStatusStr = (acRelay) ? "ON" : "OFF";
    Serial.printf("Temp: %.1fC | Hum: %d%% | Light: %d | Motion: %d | LightRelay: %s | FanRelay: %s | ACRelay: %s\n", 
                  temperature, humidity, lightValue, motionActive ? 1 : 0, lightStatusStr.c_str(), fanStatusStr.c_str(), acStatusStr.c_str());

    // Broadcast updated state to all connected web interfaces
    notifyClients();
  }
}
