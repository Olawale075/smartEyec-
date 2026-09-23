#include <WiFi.h>
#include "esp_camera.h"
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "DHT.h"

// ============================================================
// DEVICE
// ============================================================

#define DEVICE_ID "camera_01"
#define DEVICE_TYPE "ESP32-CAM"
#define DEVICE_LOCATION "Field_A"
#define DEVICE_FOCUS "General"
#define FIRMWARE_VERSION "4.3.0"

// ============================================================
// DHT11
// ============================================================

#define DHTPIN 15
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);

float temperature = 0.0;
float humidity = 0.0;

bool sensorAvailable = false;
unsigned long lastSensorRead = 0;

// Last value actually pushed to server (for change detection)
float lastSentTemperature = -999.0;
float lastSentHumidity = -999.0;

// ============================================================
// WIFI
// ============================================================

const char* ssid = "Olawale";
const char* password = "olawalet075@G";

// ============================================================
// WEBSOCKET
// ============================================================

const char* websocketHost = "crop-disease-detector-8nqt.onrender.com";
const uint16_t websocketPort = 443;
const char* websocketPath = "/camera-stream";

WebSocketsClient webSocket;

bool wsConnected = false;
bool registered = false;
bool streamingEnabled = false;

// ============================================================
// CAMERA
// ============================================================

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#define FLASH_LED_PIN      4

#define FRAME_SIZE FRAMESIZE_VGA
#define JPEG_QUALITY 15

// 500 ms -> ~2 FPS
#define FRAME_INTERVAL 500

#define MAX_FRAME_SIZE 120000

unsigned long lastFrameTime = 0;

// ============================================================
// HEARTBEAT
// ============================================================

unsigned long lastHeartbeat = 0;

#define HEARTBEAT_INTERVAL 15000

// ============================================================
// STATUS
// ============================================================

unsigned long lastStatus = 0;

#define STATUS_INTERVAL 30000

unsigned long totalFrames = 0;
unsigned long droppedFrames = 0;
unsigned long sentFrames = 0;

unsigned long lastReconnectAttempt = 0;

#define RECONNECT_INTERVAL 5000

// ============================================================
// FUNCTION DECLARATIONS
// ============================================================

void connectWiFi();
void setupCamera();
void setupWebSocket();

void webSocketEvent(WStype_t type, uint8_t* payload, size_t length);

void sendDeviceRegistration();
void sendSensorData(bool force);
void sendHeartbeat();

void handleServerMessage(uint8_t* payload, size_t length);

void captureAndSendFrame();

void readSensor();
void printStatus();

void setResolution(const char* resolution);

void sendStatus();

void startStreaming();
void stopStreaming();

void flashOn();
void flashOff();

// ============================================================
// SETUP
// ============================================================

void setup() {

    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("==========================================");
    Serial.println("        SMART EYE ESP32-CAM");
    Serial.println("==========================================");

    pinMode(FLASH_LED_PIN, OUTPUT);
    digitalWrite(FLASH_LED_PIN, LOW);

    // --------------------------------------------------------
    // CAMERA
    // --------------------------------------------------------

    Serial.println();
    Serial.println("[1/4] Initializing camera...");

    setupCamera();

    // --------------------------------------------------------
    // DHT11
    // --------------------------------------------------------

    Serial.println();
    Serial.println("[2/4] Initializing DHT11...");

    dht.begin();
    delay(1000);

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (!isnan(t) && !isnan(h)) {

        temperature = t;
        humidity = h;
        sensorAvailable = true;

        Serial.println("DHT11 ready");
        Serial.print("Temperature: "); Serial.print(temperature); Serial.println(" C");
        Serial.print("Humidity: ");    Serial.print(humidity);    Serial.println(" %");

    } else {

        sensorAvailable = false;

        Serial.println("DHT11 not responding");
        Serial.println("Continuing without sensor...");
    }

    // --------------------------------------------------------
    // WIFI
    // --------------------------------------------------------

    Serial.println();
    Serial.println("[3/4] Connecting WiFi...");

    connectWiFi();

    // --------------------------------------------------------
    // WEBSOCKET
    // --------------------------------------------------------

    Serial.println();
    Serial.println("[4/4] Connecting WebSocket...");

    setupWebSocket();

    Serial.println();
    Serial.println("==========================================");
    Serial.println("SMART EYE READY");
    Serial.println("==========================================");
}

// ============================================================
// LOOP
// ============================================================

void loop() {

    webSocket.loop();

    unsigned long now = millis();

    // --------------------------------------------------------
    // WIFI CHECK
    // --------------------------------------------------------

    if (WiFi.status() != WL_CONNECTED) {

        wsConnected = false;
        registered = false;
        streamingEnabled = false;

        if (now - lastReconnectAttempt >= RECONNECT_INTERVAL) {

            lastReconnectAttempt = now;

            Serial.println();
            Serial.println("WiFi disconnected");
            Serial.println("Reconnecting...");

            connectWiFi();
        }

        delay(10);
        return;
    }

    // --------------------------------------------------------
    // SENSOR
    // --------------------------------------------------------

    if (now - lastSensorRead >= 2000) {
        lastSensorRead = now;
        readSensor();
    }

    // --------------------------------------------------------
    // HEARTBEAT
    // --------------------------------------------------------

    if (wsConnected && registered &&
        now - lastHeartbeat >= HEARTBEAT_INTERVAL) {

        lastHeartbeat = now;
        sendHeartbeat();
    }

    // --------------------------------------------------------
    // STREAM
    // --------------------------------------------------------

    if (wsConnected && registered && streamingEnabled &&
        now - lastFrameTime >= FRAME_INTERVAL) {

        lastFrameTime = now;
        captureAndSendFrame();
    }

    // --------------------------------------------------------
    // STATUS
    // --------------------------------------------------------

    if (now - lastStatus >= STATUS_INTERVAL) {
        lastStatus = now;
        printStatus();
    }

    delay(2);
}

// ============================================================
// WIFI
// ============================================================

void connectWiFi() {

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    WiFi.begin(ssid, password);

    Serial.print("Connecting");

    unsigned long start = millis();

    while (WiFi.status() != WL_CONNECTED &&
           millis() - start < 20000) {

        delay(500);
        Serial.print(".");
    }

    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {

        Serial.println("WiFi connected");
        Serial.print("IP Address: "); Serial.println(WiFi.localIP());
        Serial.print("RSSI: ");       Serial.print(WiFi.RSSI()); Serial.println(" dBm");

    } else {

        Serial.println("WiFi connection failed");
    }
}

// ============================================================
// CAMERA
// ============================================================

void setupCamera() {

    camera_config_t config;

    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;

    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;

    config.pin_xclk  = XCLK_GPIO_NUM;
    config.pin_pclk  = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href  = HREF_GPIO_NUM;

    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;

    config.pin_pwdn  = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;

    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;

    if (psramFound()) {

        config.frame_size   = FRAME_SIZE;
        config.jpeg_quality = JPEG_QUALITY;
        config.fb_count     = 2;
        config.grab_mode    = CAMERA_GRAB_LATEST;
        config.fb_location  = CAMERA_FB_IN_PSRAM;

    } else {

        config.frame_size   = FRAMESIZE_QVGA;
        config.jpeg_quality = 18;
        config.fb_count     = 1;
        config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
        config.fb_location  = CAMERA_FB_IN_DRAM;
    }

    esp_err_t result = esp_camera_init(&config);

    if (result != ESP_OK) {

        Serial.print("Camera initialization failed: 0x");
        Serial.println(result, HEX);

        while (true) { delay(1000); }
    }

    sensor_t* sensor = esp_camera_sensor_get();

    if (sensor != nullptr) {

        sensor->set_brightness(sensor, 0);
        sensor->set_contrast(sensor, 0);
        sensor->set_saturation(sensor, 0);
        sensor->set_special_effect(sensor, 0);

        sensor->set_whitebal(sensor, 1);
        sensor->set_awb_gain(sensor, 1);
        sensor->set_wb_mode(sensor, 0);

        sensor->set_exposure_ctrl(sensor, 1);
        sensor->set_aec2(sensor, 0);
        sensor->set_ae_level(sensor, 0);

        sensor->set_gain_ctrl(sensor, 1);
        sensor->set_agc_gain(sensor, 0);

        sensor->set_bpc(sensor, 0);
        sensor->set_wpc(sensor, 1);

        sensor->set_raw_gma(sensor, 1);
        sensor->set_lenc(sensor, 1);
        sensor->set_dcw(sensor, 1);
    }

    Serial.println("Camera initialized successfully");
    Serial.println("Resolution: VGA (640x480)");
    Serial.print("JPEG Quality: ");   Serial.println(JPEG_QUALITY);
    Serial.print("Frame interval: "); Serial.print(FRAME_INTERVAL); Serial.println(" ms");
    Serial.print("PSRAM: ");          Serial.println(psramFound() ? "YES" : "NO");
}

// ============================================================
// WEBSOCKET SETUP
// ============================================================

void setupWebSocket() {

    webSocket.beginSSL(websocketHost, websocketPort, websocketPath);
    webSocket.onEvent(webSocketEvent);

    // Application-level reconnection only.
    webSocket.setReconnectInterval(5000);

    // IMPORTANT: disable native ping/pong.
    // The server uses JSON ping/pong instead.
    // webSocket.enableHeartbeat(...)   <-- removed on purpose

    webSocket.setExtraHeaders(
        "User-Agent: SmartEye-ESP32-CAM/4.3.0"
    );

    Serial.println("WebSocket configured");
}

// ============================================================
// WEBSOCKET EVENT
// ============================================================

void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {

    switch (type) {

        case WStype_DISCONNECTED:

            Serial.println();
            Serial.println("==========================================");
            Serial.println("WebSocket DISCONNECTED");
            Serial.println("==========================================");

            wsConnected = false;
            registered = false;
            streamingEnabled = false;

            Serial.print("WiFi status: ");
            Serial.println(WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED");

            Serial.print("RSSI: "); Serial.println(WiFi.RSSI());
            break;

        case WStype_CONNECTED:

            Serial.println();
            Serial.println("==========================================");
            Serial.println("WebSocket CONNECTED");
            Serial.println("==========================================");

            wsConnected = true;
            registered = false;
            streamingEnabled = false;

            lastHeartbeat = millis();

            sendDeviceRegistration();
            break;

        case WStype_TEXT:

            Serial.println();
            Serial.print("SERVER TEXT: ");

            if (payload != nullptr && length > 0) {
                Serial.println((char*)payload);
            }

            handleServerMessage(payload, length);
            break;

        case WStype_BIN:
            Serial.println("SERVER BINARY MESSAGE (ignored)");
            break;

        case WStype_ERROR:
            Serial.println();
            Serial.println("WebSocket ERROR");
            wsConnected = false;
            break;

        case WStype_PING:
            Serial.println("WebSocket PING");
            break;

        case WStype_PONG:
            Serial.println("WebSocket PONG");
            lastHeartbeat = millis();
            break;

        default:
            break;
    }
}

// ============================================================
// REGISTRATION
// ============================================================

void sendDeviceRegistration() {

    if (!wsConnected) return;

    StaticJsonDocument<1024> doc;

    doc["type"]            = "register";
    doc["deviceId"]        = DEVICE_ID;
    doc["deviceType"]      = DEVICE_TYPE;
    doc["location"]        = DEVICE_LOCATION;
    doc["focus"]           = DEVICE_FOCUS;
    doc["cropType"]        = DEVICE_FOCUS;

    doc["ipAddress"]       = WiFi.localIP().toString();
    doc["signalStrength"]  = WiFi.RSSI();
    doc["freeHeap"]        = ESP.getFreeHeap();
    doc["freePsram"]       = psramFound() ? ESP.getFreePsram() : 0;
    doc["firmwareVersion"] = FIRMWARE_VERSION;
    doc["timestamp"]       = millis();
    doc["hasSensor"]       = sensorAvailable;
    doc["resolution"]      = "VGA (640x480)";
    doc["jpegQuality"]     = JPEG_QUALITY;
    doc["frameInterval"]   = FRAME_INTERVAL;

    if (sensorAvailable) {
        doc["temperature"] = temperature;
        doc["humidity"]    = humidity;
    }

    String json;
    serializeJson(doc, json);

    if (webSocket.sendTXT(json)) {
        Serial.println();
        Serial.println("Device registration sent");
        Serial.println(json);
    } else {
        Serial.println("Failed to send registration");
    }
}

// ============================================================
// HANDLE SERVER MESSAGE
// ============================================================

void handleServerMessage(uint8_t* payload, size_t length) {

    if (payload == nullptr || length == 0) return;

    StaticJsonDocument<2048> doc;

    DeserializationError error = deserializeJson(doc, payload, length);

    if (error) {
        Serial.print("JSON parse error: ");
        Serial.println(error.c_str());
        return;
    }

    const char* type    = doc["type"]    | "";
    const char* command = doc["command"] | "";

    Serial.print("Type: "); Serial.println(type);

    // --------------------------------------------------------
    // CONNECTION CONFIRMATION
    // --------------------------------------------------------

    if (strcmp(type, "connected") == 0) {
        Serial.println("Server connection confirmed");
        return;
    }

    // --------------------------------------------------------
    // REGISTRATION SUCCESS
    // --------------------------------------------------------

    if (strcmp(type, "registration_success") == 0) {

        registered = true;

        // Auto-start streaming so the camera does not sit idle.
        streamingEnabled = true;

        Serial.println();
        Serial.println("==========================================");
        Serial.println("REGISTRATION SUCCESS - STREAMING ENABLED");
        Serial.println("==========================================");

        lastFrameTime = millis();
        return;
    }

    // --------------------------------------------------------
    // PONG
    // --------------------------------------------------------

    if (strcmp(type, "pong") == 0) {
        lastHeartbeat = millis();
        Serial.println("Heartbeat confirmed");
        return;
    }

    // --------------------------------------------------------
    // ERROR
    // --------------------------------------------------------

    if (strcmp(type, "error") == 0) {
        const char* message = doc["message"] | "Unknown server error";
        Serial.print("SERVER ERROR: "); Serial.println(message);
        return;
    }

    // --------------------------------------------------------
    // COMMAND ROUTING
    // --------------------------------------------------------

    String effectiveCommand;

    if (strlen(command) > 0) effectiveCommand = command;
    else                        effectiveCommand = type;

    Serial.print("Effective command: "); Serial.println(effectiveCommand);

    if (effectiveCommand == "start_stream")  { startStreaming();       return; }
    if (effectiveCommand == "stop_stream")   { stopStreaming();        return; }
    if (effectiveCommand == "capture")       { captureAndSendFrame();  return; }
    if (effectiveCommand == "flash_on")      { flashOn();              return; }
    if (effectiveCommand == "flash_off")     { flashOff();             return; }
    if (effectiveCommand == "status")        { sendStatus();           return; }
    if (effectiveCommand == "get_sensor")    { sendSensorData(true);   return; }
    if (effectiveCommand == "stay_alive")    { sendHeartbeat();        return; }

    if (effectiveCommand == "reboot") {
        Serial.println("Reboot command received");
        delay(100);
        ESP.restart();
        return;
    }

    if (effectiveCommand == "set_resolution") {
        const char* resolution = doc["resolution"] | "VGA";
        setResolution(resolution);
        return;
    }

    if (effectiveCommand == "set_focus") {
        const char* focus = doc["focus"] | "General";
        Serial.print("Focus changed to: "); Serial.println(focus);
        return;
    }

    Serial.print("Unknown message. Type=");
    Serial.print(type);
    Serial.print(" Command=");
    Serial.println(command);
}

// ============================================================
// SEND HEARTBEAT
// ============================================================

void sendHeartbeat() {

    if (!wsConnected || !registered) return;

    StaticJsonDocument<256> doc;

    doc["type"]      = "ping";
    doc["deviceId"]  = DEVICE_ID;
    doc["timestamp"] = millis();
    doc["freeHeap"]  = ESP.getFreeHeap();

    String json;
    serializeJson(doc, json);

    if (webSocket.sendTXT(json)) {
        Serial.println("Heartbeat sent");
    }
}

// ============================================================
// SENSOR
// ============================================================

void readSensor() {

    float h = dht.readHumidity();
    float t = dht.readTemperature();

    if (!isnan(h) && !isnan(t)) {

        temperature = t;
        humidity    = h;
        sensorAvailable = true;

        // Push to server only if values changed meaningfully.
        bool changed =
            fabs(temperature - lastSentTemperature) >= 0.5f ||
            fabs(humidity    - lastSentHumidity)    >= 1.0f;

        if (changed) {
            sendSensorData(false);
        }

    } else {
        sensorAvailable = false;
    }
}

// ============================================================
// SEND SENSOR DATA
// ============================================================

void sendSensorData(bool force) {

    if (!wsConnected || !registered) return;

    if (!force && !sensorAvailable) return;

    StaticJsonDocument<512> doc;

    doc["type"]        = "sensor_data";
    doc["deviceId"]    = DEVICE_ID;
    doc["temperature"] = temperature;
    doc["humidity"]    = humidity;
    doc["hasSensor"]   = sensorAvailable;
    doc["location"]    = DEVICE_LOCATION;
    doc["focus"]       = DEVICE_FOCUS;
    doc["timestamp"]   = millis();

    String json;
    serializeJson(doc, json);

    if (webSocket.sendTXT(json)) {

        lastSentTemperature = temperature;
        lastSentHumidity    = humidity;

        Serial.print("Sensor data sent: ");
        Serial.print(temperature); Serial.print(" C / ");
        Serial.print(humidity);    Serial.println(" %");
    }
}

// ============================================================
// CAPTURE FRAME
// ============================================================

void captureAndSendFrame() {

    if (!wsConnected || !registered) return;

    camera_fb_t* fb = esp_camera_fb_get();

    if (fb == nullptr) {
        Serial.println("Camera capture failed");
        return;
    }

    totalFrames++;

    size_t frameSize = fb->len;

    if (frameSize == 0 || frameSize > MAX_FRAME_SIZE) {

        droppedFrames++;
        Serial.print("Frame dropped, size="); Serial.println(frameSize);

        esp_camera_fb_return(fb);
        return;
    }

    bool result = webSocket.sendBIN(fb->buf, fb->len);

    if (result) {
        sentFrames++;
    } else {
        droppedFrames++;
        Serial.println("Failed to send frame");
    }

    esp_camera_fb_return(fb);
}

// ============================================================
// STREAM CONTROL
// ============================================================

void startStreaming() {

    streamingEnabled = true;
    lastFrameTime = millis();

    Serial.println();
    Serial.println("STREAMING STARTED");
}

void stopStreaming() {

    streamingEnabled = false;

    Serial.println();
    Serial.println("STREAMING STOPPED");
}

// ============================================================
// FLASH
// ============================================================

void flashOn()  { digitalWrite(FLASH_LED_PIN, HIGH); Serial.println("Flash ON"); }
void flashOff() { digitalWrite(FLASH_LED_PIN, LOW);  Serial.println("Flash OFF"); }

// ============================================================
// STATUS
// ============================================================

void sendStatus() {

    if (!wsConnected || !registered) return;

    StaticJsonDocument<768> doc;

    doc["type"]          = "status";
    doc["deviceId"]      = DEVICE_ID;
    doc["connected"]     = wsConnected;
    doc["registered"]    = registered;
    doc["streaming"]     = streamingEnabled;
    doc["temperature"]   = temperature;
    doc["humidity"]      = humidity;
    doc["hasSensor"]     = sensorAvailable;
    doc["freeHeap"]      = ESP.getFreeHeap();
    doc["freePsram"]     = psramFound() ? ESP.getFreePsram() : 0;
    doc["rssi"]          = WiFi.RSSI();
    doc["totalFrames"]   = totalFrames;
    doc["sentFrames"]    = sentFrames;
    doc["droppedFrames"] = droppedFrames;
    doc["timestamp"]     = millis();

    String json;
    serializeJson(doc, json);
    webSocket.sendTXT(json);
}

// ============================================================
// STATUS LOG
// ============================================================

void printStatus() {

    Serial.println();
    Serial.println("------------------------------------------");
    Serial.println("SMART EYE STATUS");
    Serial.println("------------------------------------------");

    Serial.print("WiFi: ");        Serial.println(WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED");
    Serial.print("WebSocket: ");   Serial.println(wsConnected      ? "CONNECTED" : "DISCONNECTED");
    Serial.print("Registered: ");  Serial.println(registered      ? "YES"       : "NO");
    Serial.print("Streaming: ");   Serial.println(streamingEnabled? "YES"       : "NO");

    Serial.print("RSSI: ");        Serial.print(WiFi.RSSI()); Serial.println(" dBm");
    Serial.print("Free heap: ");   Serial.println(ESP.getFreeHeap());

    if (psramFound()) {
        Serial.print("Free PSRAM: "); Serial.println(ESP.getFreePsram());
    }

    Serial.print("Temperature: ");    Serial.print(temperature); Serial.println(" C");
    Serial.print("Humidity: ");       Serial.print(humidity);    Serial.println(" %");
    Serial.print("Total frames: ");   Serial.println(totalFrames);
    Serial.print("Sent frames: ");    Serial.println(sentFrames);
    Serial.print("Dropped frames: "); Serial.println(droppedFrames);

    Serial.println("------------------------------------------");
}

// ============================================================
// RESOLUTION
// ============================================================

void setResolution(const char* resolution) {

    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor == nullptr) return;

    if (strcmp(resolution, "QVGA") == 0) {
        sensor->set_framesize(sensor, FRAMESIZE_QVGA);
        Serial.println("Resolution changed to QVGA");

    } else if (strcmp(resolution, "VGA") == 0) {
        sensor->set_framesize(sensor, FRAMESIZE_VGA);
        Serial.println("Resolution changed to VGA");

    } else if (strcmp(resolution, "SVGA") == 0) {
        sensor->set_framesize(sensor, FRAMESIZE_SVGA);
        Serial.println("Resolution changed to SVGA");
    }
}