#include <WiFi.h>
#include "esp_camera.h"
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "DHT.h"

// ============================================================
// SMART EYES ESP32-CAM v4.1.0
// AI-THINKER ESP32-CAM
// DHT11 + VGA JPEG WebSocket Streaming
// ============================================================

// ============================================================
// DHT11 CONFIGURATION
// ============================================================
#define DHTPIN 15
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);

// ============================================================
// WIFI CONFIGURATION
// ============================================================
const char* ssid = "Olawale";
const char* password = "olawalet075@G";

// ============================================================
// SERVER CONFIGURATION
// ============================================================
const char* websocket_server_host = "smarteye-lme3.onrender.com";
const uint16_t websocket_server_port = 443;
const char* websocket_url_path = "/camera-stream";

bool use_ssl = true;

// ============================================================
// DEVICE CONFIGURATION
// ============================================================
const char* device_id = "camera_01";
const char* device_location = "Field_A";
const char* device_type = "ESP32-CAM";

String focusMode = "General";

// ============================================================
// CAMERA CONFIGURATION
// ============================================================
#define FRAME_INTERVAL 150
#define JPEG_QUALITY 12
#define FRAME_SIZE FRAMESIZE_VGA
#define MAX_FRAME_SIZE 120000

// ============================================================
// IMAGE TUNING
// ============================================================
#define BRIGHTNESS 0
#define CONTRAST 0
#define SATURATION 0
#define EXPOSURE_VALUE 0

// ============================================================
// AI-THINKER ESP32-CAM PIN DEFINITIONS
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
#define Y2_GPIO_NUM       5

#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#define FLASH_LED_PIN     4

// ============================================================
// GLOBAL OBJECTS
// ============================================================
WebSocketsClient webSocket;

// ============================================================
// CONNECTION STATE
// ============================================================
bool wsConnected = false;
bool streamingEnabled = true;
bool cameraInitialized = false;

// ============================================================
// TIMERS
// ============================================================
unsigned long lastFrameTime = 0;
unsigned long lastHeartbeatTime = 0;
unsigned long lastStatsTime = 0;
unsigned long lastSensorReadTime = 0;
unsigned long lastWiFiCheck = 0;
unsigned long bootTime = 0;

// ============================================================
// STATISTICS
// ============================================================
unsigned long totalFramesSent = 0;
unsigned long failedFrames = 0;
unsigned long connectionAttempts = 0;

// ============================================================
// SENSOR DATA
// ============================================================
float lastTemperature = 0.0;
float lastHumidity = 0.0;

bool sensorValid = false;

// ============================================================
// FUNCTION PROTOTYPES
// ============================================================
void flashLED(int times, int delayMs);

void connectWiFi();

void initCamera();
void applyCameraSettings();

void captureAndSendFrame();
void captureAndSendSingleFrame();

void initDHTSensor();
void readDHT11Sensor();
void sendSensorData();

void setupWebSocket();

void webSocketEvent(
  WStype_t type,
  uint8_t* payload,
  size_t length
);

void handleServerCommand(String message);

void sendDeviceRegistration();
void sendHeartbeat();
void sendDeviceStatus();
void sendCommandResponse(String command, String status);
void sendStayAlive();

void printStats();

// ============================================================
// FLASH LED
// ============================================================
void flashLED(int times, int delayMs) {

  for (int i = 0; i < times; i++) {

    digitalWrite(
      FLASH_LED_PIN,
      HIGH
    );

    delay(delayMs);

    digitalWrite(
      FLASH_LED_PIN,
      LOW
    );

    delay(delayMs);
  }
}

// ============================================================
// WIFI
// ============================================================
void connectWiFi() {

  Serial.println();
  Serial.println("==========================================");
  Serial.println("CONNECTING TO WIFI");
  Serial.println("==========================================");

  WiFi.mode(WIFI_STA);

  // Disable WiFi sleep for more stable streaming
  WiFi.setSleep(false);

  WiFi.setAutoReconnect(true);

  WiFi.begin(
    ssid,
    password
  );

  int attempts = 0;

  while (
    WiFi.status() != WL_CONNECTED &&
    attempts < 40
  ) {

    delay(500);

    Serial.print(".");

    attempts++;
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {

    Serial.println("WiFi CONNECTED");

    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    Serial.print("RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");

    Serial.print("Server: ");

    Serial.print(
      use_ssl ? "wss://" : "ws://"
    );

    Serial.print(
      websocket_server_host
    );

    Serial.print(":");

    Serial.print(
      websocket_server_port
    );

    Serial.println(
      websocket_url_path
    );

    if (psramFound()) {

      Serial.print("PSRAM: ");

      Serial.print(
        ESP.getPsramSize() / 1024
      );

      Serial.println(" KB");

    } else {

      Serial.println("PSRAM NOT FOUND");
    }

  } else {

    Serial.println(
      "WiFi connection failed"
    );

    Serial.println(
      "Restarting in 5 seconds..."
    );

    delay(5000);

    ESP.restart();
  }

  Serial.println();
}

// ============================================================
// DHT11 INITIALIZATION
// ============================================================
void initDHTSensor() {

  Serial.println(
    "Initializing DHT11..."
  );

  dht.begin();

  delay(2000);

  float temp =
    dht.readTemperature();

  float hum =
    dht.readHumidity();

  if (
    isnan(temp) ||
    isnan(hum)
  ) {

    Serial.println(
      "DHT11 not responding"
    );

    sensorValid = false;

  } else {

    lastTemperature = temp;
    lastHumidity = hum;

    sensorValid = true;

    Serial.printf(
      "DHT11 OK - Temperature: %.1f C | Humidity: %.1f %%\n",
      temp,
      hum
    );
  }
}

// ============================================================
// READ DHT11
// ============================================================
void readDHT11Sensor() {

  float temp =
    dht.readTemperature();

  float hum =
    dht.readHumidity();

  if (
    isnan(temp) ||
    isnan(hum)
  ) {

    if (sensorValid) {

      Serial.println(
        "DHT11 read error"
      );
    }

    sensorValid = false;

    return;
  }

  lastTemperature = temp;
  lastHumidity = hum;

  sensorValid = true;

  static unsigned long lastPrintTime = 0;

  if (
    millis() - lastPrintTime >= 5000
  ) {

    Serial.printf(
      "Temperature: %.1f C | Humidity: %.1f %%\n",
      temp,
      hum
    );

    lastPrintTime = millis();
  }

  if (wsConnected) {

    sendSensorData();
  }
}

// ============================================================
// SEND SENSOR DATA
// ============================================================
void sendSensorData() {

  if (!sensorValid) {
    return;
  }

  if (!wsConnected) {
    return;
  }

  if (!webSocket.isConnected()) {
    return;
  }

  StaticJsonDocument<320> doc;

  doc["type"] =
    "sensor_data";

  doc["deviceId"] =
    device_id;

  doc["focus"] =
    focusMode;

  doc["temperature"] =
    lastTemperature;

  doc["humidity"] =
    lastHumidity;

  doc["timestamp"] =
    millis();

  String jsonString;

  serializeJson(
    doc,
    jsonString
  );

  if (
    webSocket.sendTXT(
      jsonString
    )
  ) {

    Serial.print(
      "Sensor data: "
    );

    Serial.println(
      jsonString
    );

  } else {

    Serial.println(
      "Failed to send sensor data"
    );
  }
}

// ============================================================
// CAMERA INITIALIZATION
// ============================================================
void initCamera() {

  Serial.println();
  Serial.println("==========================================");
  Serial.println("INITIALIZING CAMERA");
  Serial.println("==========================================");

  camera_config_t config;

  config.ledc_channel =
    LEDC_CHANNEL_0;

  config.ledc_timer =
    LEDC_TIMER_0;

  config.pin_d0 =
    Y2_GPIO_NUM;

  config.pin_d1 =
    Y3_GPIO_NUM;

  config.pin_d2 =
    Y4_GPIO_NUM;

  config.pin_d3 =
    Y5_GPIO_NUM;

  config.pin_d4 =
    Y6_GPIO_NUM;

  config.pin_d5 =
    Y7_GPIO_NUM;

  config.pin_d6 =
    Y8_GPIO_NUM;

  config.pin_d7 =
    Y9_GPIO_NUM;

  config.pin_xclk =
    XCLK_GPIO_NUM;

  config.pin_pclk =
    PCLK_GPIO_NUM;

  config.pin_vsync =
    VSYNC_GPIO_NUM;

  config.pin_href =
    HREF_GPIO_NUM;

  config.pin_sscb_sda =
    SIOD_GPIO_NUM;

  config.pin_sscb_scl =
    SIOC_GPIO_NUM;

  config.pin_pwdn =
    PWDN_GPIO_NUM;

  config.pin_reset =
    RESET_GPIO_NUM;

  config.xclk_freq_hz =
    20000000;

  config.pixel_format =
    PIXFORMAT_JPEG;

  config.frame_size =
    FRAME_SIZE;

  config.jpeg_quality =
    JPEG_QUALITY;

  if (psramFound()) {

    config.fb_count = 2;

    config.fb_location =
      CAMERA_FB_IN_PSRAM;

    config.grab_mode =
      CAMERA_GRAB_LATEST;

  } else {

    config.fb_count = 1;

    config.fb_location =
      CAMERA_FB_IN_DRAM;

    config.grab_mode =
      CAMERA_GRAB_WHEN_EMPTY;
  }

  esp_err_t err =
    esp_camera_init(
      &config
    );

  if (err != ESP_OK) {

    Serial.printf(
      "Camera initialization failed: 0x%x\n",
      err
    );

    delay(5000);

    ESP.restart();
  }

  cameraInitialized = true;

  applyCameraSettings();

  Serial.println(
    "Camera initialized successfully"
  );

  Serial.println(
    "Resolution: VGA (640x480)"
  );

  Serial.printf(
    "JPEG Quality: %d\n",
    JPEG_QUALITY
  );

  Serial.printf(
    "Maximum JPEG size: %lu bytes\n",
    (unsigned long)MAX_FRAME_SIZE
  );

  Serial.printf(
    "Frame interval: %d ms\n",
    FRAME_INTERVAL
  );
}

// ============================================================
// CAMERA SETTINGS
// ============================================================
void applyCameraSettings() {

  sensor_t* sensor =
    esp_camera_sensor_get();

  if (!sensor) {

    Serial.println(
      "Camera sensor unavailable"
    );

    return;
  }

  sensor->set_framesize(
    sensor,
    FRAME_SIZE
  );

  sensor->set_quality(
    sensor,
    JPEG_QUALITY
  );

  sensor->set_brightness(
    sensor,
    BRIGHTNESS
  );

  sensor->set_contrast(
    sensor,
    CONTRAST
  );

  sensor->set_saturation(
    sensor,
    SATURATION
  );

  sensor->set_whitebal(
    sensor,
    1
  );

  sensor->set_awb_gain(
    sensor,
    1
  );

  sensor->set_wb_mode(
    sensor,
    0
  );

  sensor->set_exposure_ctrl(
    sensor,
    1
  );

  sensor->set_aec2(
    sensor,
    1
  );

  sensor->set_ae_level(
    sensor,
    0
  );

  sensor->set_aec_value(
    sensor,
    EXPOSURE_VALUE
  );

  sensor->set_gain_ctrl(
    sensor,
    1
  );

  sensor->set_agc_gain(
    sensor,
    0
  );

  sensor->set_gainceiling(
    sensor,
    GAINCEILING_4X
  );

  sensor->set_bpc(
    sensor,
    1
  );

  sensor->set_wpc(
    sensor,
    1
  );

  sensor->set_raw_gma(
    sensor,
    1
  );

  sensor->set_lenc(
    sensor,
    1
  );

  sensor->set_dcw(
    sensor,
    1
  );

  sensor->set_colorbar(
    sensor,
    0
  );
}

// ============================================================
// SEND STREAM FRAME
// ============================================================
void captureAndSendFrame() {

  if (!wsConnected) {
    return;
  }

  if (!cameraInitialized) {
    return;
  }

  if (!streamingEnabled) {
    return;
  }

  if (!webSocket.isConnected()) {

    wsConnected = false;

    return;
  }

  camera_fb_t* fb =
    esp_camera_fb_get();

  if (!fb) {

    failedFrames++;

    Serial.println(
      "Camera frame capture failed"
    );

    return;
  }

  if (fb->len == 0) {

    failedFrames++;

    Serial.println(
      "Camera returned empty frame"
    );

    esp_camera_fb_return(
      fb
    );

    return;
  }

  if (
    fb->len > MAX_FRAME_SIZE
  ) {

    failedFrames++;

    Serial.printf(
      "Dropping frame: %lu bytes | limit: %lu\n",
      (unsigned long)fb->len,
      (unsigned long)MAX_FRAME_SIZE
    );

    esp_camera_fb_return(
      fb
    );

    return;
  }

  size_t frameSize =
    fb->len;

  bool sent =
    webSocket.sendBIN(
      fb->buf,
      frameSize
    );

  if (sent) {

    totalFramesSent++;

    if (
      totalFramesSent % 10 == 0
    ) {

      Serial.printf(
        "Frame %lu sent - %lu bytes | focus=%s\n",
        totalFramesSent,
        (unsigned long)frameSize,
        focusMode.c_str()
      );
    }

  } else {

    failedFrames++;

    Serial.println(
      "Failed to send camera frame"
    );
  }

  esp_camera_fb_return(
    fb
  );
}

// ============================================================
// SINGLE FRAME CAPTURE
// ============================================================
void captureAndSendSingleFrame() {

  if (!wsConnected) {
    return;
  }

  if (!cameraInitialized) {
    return;
  }

  if (!webSocket.isConnected()) {
    return;
  }

  camera_fb_t* fb =
    esp_camera_fb_get();

  if (!fb) {

    Serial.println(
      "Single frame capture failed"
    );

    return;
  }

  if (fb->len == 0) {

    Serial.println(
      "Single frame is empty"
    );

    esp_camera_fb_return(
      fb
    );

    return;
  }

  if (
    fb->len <= MAX_FRAME_SIZE
  ) {

    size_t frameSize =
      fb->len;

    if (
      webSocket.sendBIN(
        fb->buf,
        frameSize
      )
    ) {

      Serial.printf(
        "Single frame sent: %lu bytes\n",
        (unsigned long)frameSize
      );

    } else {

      Serial.println(
        "Single frame send failed"
      );
    }

  } else {

    Serial.printf(
      "Single frame too large: %lu bytes\n",
      (unsigned long)fb->len
    );
  }

  esp_camera_fb_return(
    fb
  );
}

// ============================================================
// WEBSOCKET SETUP
// ============================================================
void setupWebSocket() {

  Serial.println();
  Serial.println("==========================================");
  Serial.println("SETTING UP WEBSOCKET");
  Serial.println("==========================================");

  webSocket.onEvent(
    webSocketEvent
  );

  webSocket.setReconnectInterval(
    7000
  );

  webSocket.enableHeartbeat(
    30000,
    5000,
    2
  );

  String extraHeader =
    "Device-ID: " +
    String(device_id) +
    "\r\n" +
    "Focus: " +
    focusMode +
    "\r\n" +
    "Crop-Type: " +
    focusMode;

  webSocket.setExtraHeaders(
    extraHeader.c_str()
  );

  if (use_ssl) {

    webSocket.beginSSL(
      websocket_server_host,
      websocket_server_port,
      websocket_url_path
    );

  } else {

    webSocket.begin(
      websocket_server_host,
      websocket_server_port,
      websocket_url_path
    );
  }

  Serial.println(
    "WebSocket configured"
  );

  Serial.print(
    "Endpoint: "
  );

  Serial.print(
    use_ssl ? "wss://" : "ws://"
  );

  Serial.print(
    websocket_server_host
  );

  Serial.print(":");

  Serial.print(
    websocket_server_port
  );

  Serial.println(
    websocket_url_path
  );
}

// ============================================================
// WEBSOCKET EVENT
// ============================================================
void webSocketEvent(
  WStype_t type,
  uint8_t* payload,
  size_t length
) {

  switch (type) {

    // --------------------------------------------------------
    // DISCONNECTED
    // --------------------------------------------------------
    case WStype_DISCONNECTED:

      wsConnected = false;

      digitalWrite(
        FLASH_LED_PIN,
        LOW
      );

      Serial.println(
        "WebSocket DISCONNECTED"
      );

      break;

    // --------------------------------------------------------
    // CONNECTED
    // --------------------------------------------------------
    case WStype_CONNECTED:

      wsConnected = true;

      connectionAttempts = 0;

      Serial.println();
      Serial.println(
        "=========================================="
      );

      Serial.println(
        "WebSocket CONNECTED"
      );

      Serial.println(
        "=========================================="
      );

      // Short connection indication
      digitalWrite(
        FLASH_LED_PIN,
        HIGH
      );

      delay(100);

      digitalWrite(
        FLASH_LED_PIN,
        LOW
      );

      sendDeviceRegistration();

      streamingEnabled = true;

      if (sensorValid) {

        sendSensorData();
      }

      break;

    // --------------------------------------------------------
    // TEXT
    // --------------------------------------------------------
    case WStype_TEXT:

      if (
        payload != nullptr &&
        length > 0
      ) {

        String message;

        for (
          size_t i = 0;
          i < length;
          i++
        ) {

          message +=
            (char)payload[i];
        }

        Serial.print(
          "SERVER TEXT: "
        );

        Serial.println(
          message
        );

        handleServerCommand(
          message
        );
      }

      break;

    // --------------------------------------------------------
    // BINARY
    // --------------------------------------------------------
    case WStype_BIN:

      Serial.printf(
        "Received binary data: %u bytes\n",
        (unsigned int)length
      );

      break;

    // --------------------------------------------------------
    // ERROR
    // --------------------------------------------------------
    case WStype_ERROR:

      wsConnected = false;

      Serial.println(
        "WebSocket ERROR"
      );

      if (
        payload != nullptr &&
        length > 0
      ) {

        Serial.printf(
          "WebSocket error payload: %.*s\n",
          (int)length,
          (char*)payload
        );
      }

      break;

    default:

      break;
  }
}

// ============================================================
// SERVER COMMAND HANDLER
// ============================================================
void handleServerCommand(
  String message
) {

  StaticJsonDocument<1024> doc;

  DeserializationError error =
    deserializeJson(
      doc,
      message
    );

  if (error) {

    Serial.print(
      "Invalid JSON received: "
    );

    Serial.println(
      error.c_str()
    );

    return;
  }

  // ==========================================================
  // IMPORTANT:
  // Always read "type" separately from "command".
  // ==========================================================
  const char* type =
    doc["type"] | "";

  const char* command =
    doc["command"] | "";

  Serial.print(
    "Type: "
  );

  Serial.println(
    type
  );

  if (
    strlen(command) > 0
  ) {

    Serial.print(
      "Command: "
    );

    Serial.println(
      command
    );
  }

  // ==========================================================
  // SYSTEM
  // ==========================================================
  if (
    strcmp(type, "system") == 0
  ) {

    const char* status =
      doc["status"] | "";

    Serial.printf(
      "System message: %s\n",
      status
    );

    return;
  }

  // ==========================================================
  // PONG
  // ==========================================================
  if (
    strcmp(type, "pong") == 0
  ) {

    return;
  }

  // ==========================================================
  // FOCUS CONFIRMATION
  // ==========================================================
  if (
    strcmp(type, "focus_set") == 0
  ) {

    const char* f =
      doc["focus"] | "";

    if (
      strlen(f) > 0
    ) {

      focusMode =
        String(f);

      Serial.printf(
        "Focus confirmed: %s\n",
        focusMode.c_str()
      );
    }

    return;
  }

  // ==========================================================
  // COMMAND RESPONSE
  // ==========================================================
  if (
    strcmp(type, "command_response") == 0
  ) {

    const char* cmd =
      doc["command"] | "";

    const char* status =
      doc["status"] | "";

    Serial.printf(
      "Command response: %s -> %s\n",
      cmd,
      status
    );

    return;
  }

  // ==========================================================
  // SENSOR UPDATE
  // ==========================================================
  if (
    strcmp(type, "sensor_update") == 0
  ) {

    return;
  }

  // ==========================================================
  // STAY ALIVE RESPONSE
  // ==========================================================
  if (
    strcmp(type, "stay_alive_response") == 0
  ) {

    return;
  }

  // ==========================================================
  // ERROR
  // ==========================================================
  if (
    strcmp(type, "error") == 0
  ) {

    const char* msg =
      doc["message"] | "";

    Serial.printf(
      "Server error: %s\n",
      msg
    );

    return;
  }

  // ==========================================================
  // ECHO
  // ==========================================================
  if (
    strcmp(type, "echo") == 0
  ) {

    return;
  }

  // ==========================================================
  // CAPTURE
  // ==========================================================
  if (
    strcmp(command, "capture") == 0
  ) {

    captureAndSendSingleFrame();

    sendCommandResponse(
      "capture",
      "success"
    );

    return;
  }

  // ==========================================================
  // START STREAM
  // ==========================================================
  if (
    strcmp(command, "start_stream") == 0
  ) {

    streamingEnabled = true;

    sendCommandResponse(
      "start_stream",
      "success"
    );

    Serial.println(
      "Streaming ENABLED"
    );

    return;
  }

  // ==========================================================
  // STOP STREAM
  // ==========================================================
  if (
    strcmp(command, "stop_stream") == 0
  ) {

    streamingEnabled = false;

    sendCommandResponse(
      "stop_stream",
      "success"
    );

    Serial.println(
      "Streaming DISABLED"
    );

    return;
  }

  // ==========================================================
  // FLASH ON
  // ==========================================================
  if (
    strcmp(command, "flash_on") == 0
  ) {

    digitalWrite(
      FLASH_LED_PIN,
      HIGH
    );

    sendCommandResponse(
      "flash_on",
      "success"
    );

    return;
  }

  // ==========================================================
  // FLASH OFF
  // ==========================================================
  if (
    strcmp(command, "flash_off") == 0
  ) {

    digitalWrite(
      FLASH_LED_PIN,
      LOW
    );

    sendCommandResponse(
      "flash_off",
      "success"
    );

    return;
  }

  // ==========================================================
  // STATUS
  // ==========================================================
  if (
    strcmp(command, "status") == 0
  ) {

    sendDeviceStatus();

    return;
  }

  // ==========================================================
  // GET SENSOR
  // ==========================================================
  if (
    strcmp(command, "get_sensor") == 0
  ) {

    readDHT11Sensor();

    if (sensorValid) {

      sendSensorData();
    }

    sendCommandResponse(
      "get_sensor",
      sensorValid
        ? "success"
        : "sensor_error"
    );

    return;
  }

  // ==========================================================
  // REBOOT
  // ==========================================================
  if (
    strcmp(command, "reboot") == 0
  ) {

    sendCommandResponse(
      "reboot",
      "executing"
    );

    delay(1000);

    ESP.restart();

    return;
  }

  // ==========================================================
  // SET RESOLUTION
  // ==========================================================
  if (
    strcmp(command, "set_resolution") == 0
  ) {

    const char* res =
      doc["value"] | "";

    sensor_t* s =
      esp_camera_sensor_get();

    if (!s) {

      sendCommandResponse(
        "set_resolution",
        "sensor_unavailable"
      );

      return;
    }

    if (
      strcmp(res, "qvga") == 0
    ) {

      s->set_framesize(
        s,
        FRAMESIZE_QVGA
      );

      sendCommandResponse(
        "set_resolution",
        "qvga"
      );

      Serial.println(
        "Resolution changed to QVGA"
      );

    } else if (
      strcmp(res, "vga") == 0
    ) {

      s->set_framesize(
        s,
        FRAMESIZE_VGA
      );

      sendCommandResponse(
        "set_resolution",
        "vga"
      );

      Serial.println(
        "Resolution changed to VGA"
      );

    } else if (
      strcmp(res, "svga") == 0
    ) {

      s->set_framesize(
        s,
        FRAMESIZE_SVGA
      );

      sendCommandResponse(
        "set_resolution",
        "svga"
      );

      Serial.println(
        "Resolution changed to SVGA"
      );

    } else {

      sendCommandResponse(
        "set_resolution",
        "unknown_value"
      );
    }

    return;
  }

  // ==========================================================
  // SET FOCUS
  // ==========================================================
  if (
    strcmp(command, "set_focus") == 0
  ) {

    const char* newFocus =
      doc["focus"] |
      doc["cropType"] |
      "General";

    focusMode =
      String(newFocus);

    Serial.printf(
      "Focus changed -> %s\n",
      focusMode.c_str()
    );

    sendCommandResponse(
      "set_focus",
      String("ok:") +
      focusMode
    );

    StaticJsonDocument<192> conf;

    conf["type"] =
      "focus_set";

    conf["deviceId"] =
      device_id;

    conf["focus"] =
      focusMode;

    conf["timestamp"] =
      millis();

    String out;

    serializeJson(
      conf,
      out
    );

    if (
      wsConnected &&
      webSocket.isConnected()
    ) {

      webSocket.sendTXT(
        out
      );
    }

    return;
  }

  // ==========================================================
  // STAY ALIVE
  // ==========================================================
  if (
    strcmp(command, "stay_alive") == 0
  ) {

    sendStayAlive();

    return;
  }

  // ==========================================================
  // UNKNOWN
  // ==========================================================
  Serial.print(
    "Unknown message. Type="
  );

  Serial.print(
    type
  );

  Serial.print(
    " Command="
  );

  Serial.println(
    command
  );
}

// ============================================================
// DEVICE REGISTRATION
// ============================================================
void sendDeviceRegistration() {

  if (!wsConnected) {
    return;
  }

  if (!webSocket.isConnected()) {
    return;
  }

  StaticJsonDocument<768> doc;

  doc["type"] =
    "register";

  doc["deviceId"] =
    device_id;

  doc["deviceType"] =
    device_type;

  doc["location"] =
    device_location;

  doc["focus"] =
    focusMode;

  doc["cropType"] =
    focusMode;

  doc["ipAddress"] =
    WiFi.localIP().toString();

  doc["signalStrength"] =
    WiFi.RSSI();

  doc["freeHeap"] =
    ESP.getFreeHeap();

  doc["freePsram"] =
    ESP.getFreePsram();

  doc["firmwareVersion"] =
    "4.1.0";

  doc["timestamp"] =
    millis();

  doc["hasSensor"] =
    sensorValid;

  doc["resolution"] =
    "VGA (640x480)";

  doc["jpegQuality"] =
    JPEG_QUALITY;

  doc["frameInterval"] =
    FRAME_INTERVAL;

  if (sensorValid) {

    doc["temperature"] =
      lastTemperature;

    doc["humidity"] =
      lastHumidity;
  }

  String jsonString;

  serializeJson(
    doc,
    jsonString
  );

  if (
    webSocket.sendTXT(
      jsonString
    )
  ) {

    Serial.println(
      "Device registration sent"
    );

    Serial.println(
      jsonString
    );

  } else {

    Serial.println(
      "Device registration failed"
    );
  }
}

// ============================================================
// CUSTOM HEARTBEAT
// ============================================================
void sendHeartbeat() {

  if (!wsConnected) {
    return;
  }

  if (!webSocket.isConnected()) {
    return;
  }

  StaticJsonDocument<384> doc;

  doc["type"] =
    "ping";

  doc["deviceId"] =
    device_id;

  doc["focus"] =
    focusMode;

  doc["timestamp"] =
    millis();

  doc["framesSent"] =
    totalFramesSent;

  doc["failedFrames"] =
    failedFrames;

  doc["freeHeap"] =
    ESP.getFreeHeap();

  doc["freePsram"] =
    ESP.getFreePsram();

  doc["signalStrength"] =
    WiFi.RSSI();

  doc["streaming"] =
    streamingEnabled;

  doc["sensorValid"] =
    sensorValid;

  if (sensorValid) {

    doc["temperature"] =
      lastTemperature;

    doc["humidity"] =
      lastHumidity;
  }

  String jsonString;

  serializeJson(
    doc,
    jsonString
  );

  webSocket.sendTXT(
    jsonString
  );
}

// ============================================================
// STAY ALIVE
// ============================================================
void sendStayAlive() {

  if (!wsConnected) {
    return;
  }

  if (!webSocket.isConnected()) {
    return;
  }

  StaticJsonDocument<192> doc;

  doc["type"] =
    "stay_alive";

  doc["deviceId"] =
    device_id;

  doc["timestamp"] =
    millis();

  String jsonString;

  serializeJson(
    doc,
    jsonString
  );

  webSocket.sendTXT(
    jsonString
  );
}

// ============================================================
// DEVICE STATUS
// ============================================================
void sendDeviceStatus() {

  if (!wsConnected) {
    return;
  }

  if (!webSocket.isConnected()) {
    return;
  }

  StaticJsonDocument<512> doc;

  doc["type"] =
    "status";

  doc["deviceId"] =
    device_id;

  doc["connected"] =
    wsConnected;

  doc["location"] =
    device_location;

  doc["focus"] =
    focusMode;

  doc["cropType"] =
    focusMode;

  doc["ipAddress"] =
    WiFi.localIP().toString();

  doc["signalStrength"] =
    WiFi.RSSI();

  doc["freeHeap"] =
    ESP.getFreeHeap();

  doc["freePsram"] =
    ESP.getFreePsram();

  doc["framesSent"] =
    totalFramesSent;

  doc["failedFrames"] =
    failedFrames;

  doc["uptime"] =
    millis() / 1000;

  doc["streaming"] =
    streamingEnabled;

  doc["sensorValid"] =
    sensorValid;

  doc["resolution"] =
    "VGA (640x480)";

  doc["jpegQuality"] =
    JPEG_QUALITY;

  if (sensorValid) {

    doc["temperature"] =
      lastTemperature;

    doc["humidity"] =
      lastHumidity;
  }

  String jsonString;

  serializeJson(
    doc,
    jsonString
  );

  webSocket.sendTXT(
    jsonString
  );
}

// ============================================================
// COMMAND RESPONSE
// ============================================================
void sendCommandResponse(
  String command,
  String status
) {

  if (!wsConnected) {
    return;
  }

  if (!webSocket.isConnected()) {
    return;
  }

  StaticJsonDocument<256> doc;

  doc["type"] =
    "command_response";

  doc["deviceId"] =
    device_id;

  doc["command"] =
    command;

  doc["status"] =
    status;

  doc["timestamp"] =
    millis();

  String jsonString;

  serializeJson(
    doc,
    jsonString
  );

  webSocket.sendTXT(
    jsonString
  );
}

// ============================================================
// STATISTICS
// ============================================================
void printStats() {

  Serial.println();

  Serial.println(
    "=========================================="
  );

  Serial.println(
    "SYSTEM STATISTICS"
  );

  Serial.println(
    "=========================================="
  );

  Serial.printf(
    "WebSocket: %s\n",
    wsConnected
      ? "CONNECTED"
      : "DISCONNECTED"
  );

  Serial.printf(
    "Streaming: %s\n",
    streamingEnabled
      ? "ACTIVE"
      : "STOPPED"
  );

  Serial.printf(
    "Camera: %s\n",
    cameraInitialized
      ? "READY"
      : "NOT READY"
  );

  Serial.printf(
    "Focus: %s\n",
    focusMode.c_str()
  );

  Serial.printf(
    "Frames Sent: %lu\n",
    totalFramesSent
  );

  Serial.printf(
    "Failed Frames: %lu\n",
    failedFrames
  );

  Serial.printf(
    "Signal: %d dBm\n",
    WiFi.RSSI()
  );

  Serial.printf(
    "Free Heap: %lu bytes\n",
    (unsigned long)
      ESP.getFreeHeap()
  );

  Serial.printf(
    "Free PSRAM: %lu bytes\n",
    (unsigned long)
      ESP.getFreePsram()
  );

  Serial.printf(
    "Uptime: %lu seconds\n",
    millis() / 1000
  );

  Serial.printf(
    "Frame Interval: %d ms\n",
    FRAME_INTERVAL
  );

  Serial.printf(
    "JPEG Quality: %d\n",
    JPEG_QUALITY
  );

  if (sensorValid) {

    Serial.printf(
      "Temperature: %.1f C\n",
      lastTemperature
    );

    Serial.printf(
      "Humidity: %.1f %%\n",
      lastHumidity
    );

  } else {

    Serial.println(
      "DHT11: INVALID"
    );
  }

  Serial.println(
    "=========================================="
  );
}

// ============================================================
// SETUP
// ============================================================
void setup() {

  Serial.begin(
    115200
  );

  delay(1000);

  Serial.println();

  Serial.println(
    "=========================================="
  );

  Serial.println(
    "smartEyes ESP32-CAM v4.1.0"
  );

  Serial.println(
    "=========================================="
  );

  bootTime =
    millis();

  pinMode(
    FLASH_LED_PIN,
    OUTPUT
  );

  digitalWrite(
    FLASH_LED_PIN,
    LOW
  );

  // ==========================================================
  // WIFI
  // ==========================================================
  Serial.println(
    "[1/4] Connecting to WiFi..."
  );

  connectWiFi();

  Serial.println(
    "[1/4] WiFi done"
  );

  // ==========================================================
  // CAMERA
  // ==========================================================
  Serial.println(
    "[2/4] Initializing camera..."
  );

  initCamera();

  Serial.println(
    "[2/4] Camera done"
  );

  // ==========================================================
  // DHT11
  // ==========================================================
  Serial.println(
    "[3/4] Initializing DHT11..."
  );

  initDHTSensor();

  Serial.println(
    "[3/4] DHT done"
  );

  // ==========================================================
  // WEBSOCKET
  // ==========================================================
  Serial.println(
    "[4/4] Setting up WebSocket..."
  );

  setupWebSocket();

  Serial.println(
    "[4/4] WebSocket done"
  );

  Serial.println();

  Serial.println(
    "=========================================="
  );

  Serial.println(
    "SYSTEM READY"
  );

  Serial.println(
    "=========================================="
  );

  Serial.print(
    "IP: "
  );

  Serial.println(
    WiFi.localIP()
  );

  Serial.println(
    "Resolution: VGA (640x480)"
  );

  Serial.print(
    "JPEG Quality: "
  );

  Serial.println(
    JPEG_QUALITY
  );

  Serial.print(
    "Frame interval: "
  );

  Serial.print(
    FRAME_INTERVAL
  );

  Serial.println(
    " ms"
  );

  Serial.print(
    "Maximum JPEG: "
  );

  Serial.print(
    MAX_FRAME_SIZE
  );

  Serial.println(
    " bytes"
  );

  Serial.print(
    "Default focus: "
  );

  Serial.println(
    focusMode
  );

  Serial.print(
    "PSRAM: "
  );

  Serial.println(
    psramFound()
      ? "AVAILABLE"
      : "NOT AVAILABLE"
  );

  Serial.println();

  Serial.println(
    "Waiting for WebSocket connection..."
  );
}

// ============================================================
// LOOP
// ============================================================
void loop() {

  // ==========================================================
  // WEBSOCKET
  // ==========================================================
  webSocket.loop();

  unsigned long now =
    millis();

  // ==========================================================
  // WIFI MONITORING
  // ==========================================================
  if (
    now - lastWiFiCheck >= 10000
  ) {

    lastWiFiCheck =
      now;

    if (
      WiFi.status() != WL_CONNECTED
    ) {

      Serial.println(
        "WiFi disconnected"
      );

      wsConnected = false;

      connectWiFi();
    }
  }

  // ==========================================================
  // DHT11
  // ==========================================================
  if (
    now - lastSensorReadTime >= 2000
  ) {

    lastSensorReadTime =
      now;

    readDHT11Sensor();
  }

  // ==========================================================
  // CAMERA STREAM
  // ==========================================================
  if (
    wsConnected &&
    streamingEnabled &&
    cameraInitialized &&
    now - lastFrameTime >= FRAME_INTERVAL
  ) {

    lastFrameTime =
      now;

    captureAndSendFrame();
  }

  // ==========================================================
  // CUSTOM HEARTBEAT
  // ==========================================================
  if (
    wsConnected &&
    now - lastHeartbeatTime >= 25000
  ) {

    lastHeartbeatTime =
      now;

    sendHeartbeat();
  }

  // ==========================================================
  // STATISTICS
  // ==========================================================
  if (
    now - lastStatsTime >= 60000
  ) {

    lastStatsTime =
      now;

    printStats();
  }

  delay(2);
}