/*
 * ═══════════════════════════════════════════════
 * CAMERA TEST — ESP32-S3 + OV5640
 * Flash this, connect to WiFi, open the IP
 * in your browser → live video stream
 * ═══════════════════════════════════════════════
 *
 * Board settings in Arduino IDE:
 * Board:    ESP32S3 Dev Module
 * PSRAM:    OPI PSRAM
 * USB CDC:  Enabled
 */

#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>

// ── CHANGE THESE ──────────────────────────────
const char* ssid     = "rinat";
const char* password = "12345678";
// ──────────────────────────────────────────────

// ── CAMERA PINS — ESP32-S3 FREENOVE-STYLE ─────
#define PWDN_GPIO    -1
#define RESET_GPIO   -1
#define XCLK_GPIO    15
#define SIOD_GPIO    4
#define SIOC_GPIO    5
#define Y9_GPIO      16
#define Y8_GPIO      17
#define Y7_GPIO      18
#define Y6_GPIO      12
#define Y5_GPIO      10
#define Y4_GPIO      8
#define Y3_GPIO      9
#define Y2_GPIO      11
#define VSYNC_GPIO   6
#define HREF_GPIO    7
#define PCLK_GPIO    13

WebServer server(80);

// ── HTML page with live stream ─────────────────
const char* HTML_PAGE = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Camera Test</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      background: #0a0a0f;
      color: #e8e8f0;
      font-family: 'Courier New', monospace;
      display: flex;
      flex-direction: column;
      align-items: center;
      padding: 20px;
      min-height: 100vh;
    }
    h1 {
      font-size: 14px;
      letter-spacing: 4px;
      color: #16a34a;
      margin-bottom: 8px;
    }
    .status {
      font-size: 11px;
      color: #6b6b80;
      margin-bottom: 16px;
      letter-spacing: 2px;
    }
    img {
      max-width: 100%;
      width: 640px;
      border: 1px solid rgba(255,255,255,0.1);
      border-radius: 4px;
      background: #111;
    }
    .controls {
      margin-top: 16px;
      display: flex;
      gap: 8px;
      flex-wrap: wrap;
      justify-content: center;
    }
    button {
      background: #1e293b;
      border: 1px solid rgba(255,255,255,0.1);
      color: #e8e8f0;
      padding: 8px 16px;
      font-family: 'Courier New', monospace;
      font-size: 11px;
      letter-spacing: 1px;
      cursor: pointer;
      border-radius: 4px;
    }
    button:hover { background: #334155; }
    button.active { border-color: #16a34a; color: #16a34a; }
    .info {
      margin-top: 20px;
      font-size: 10px;
      color: #333;
      text-align: center;
      letter-spacing: 1px;
    }
  </style>
</head>
<body>
  <h1>&#9889; CAMERA LIVE</h1>
  <div class="status" id="status">CONNECTING...</div>
  <img id="stream" src="" alt="Loading camera...">
  <div class="controls">
    <button onclick="setRes('QQVGA')">160x120</button>
    <button onclick="setRes('QVGA')" class="active">320x240</button>
    <button onclick="setRes('VGA')">640x480</button>
    <button onclick="setRes('SVGA')">800x600</button>
    <button onclick="location.href='/capture'">&#128247; Snapshot</button>
  </div>
  <div class="info">
    Stream URL: <span id="streamUrl"></span>
  </div>
  <script>
    const img = document.getElementById('stream');
    const status = document.getElementById('status');
    const streamUrl = document.getElementById('streamUrl');
    const host = window.location.origin;

    img.src = host + '/stream';
    streamUrl.textContent = host + '/stream';
    img.onload = () => { status.textContent = 'STREAM ACTIVE'; status.style.color = '#16a34a'; };
    img.onerror = () => { status.textContent = 'NO SIGNAL — CHECK PINS'; status.style.color = '#dc2626'; };

    function setRes(r) {
      fetch('/resolution?set=' + r).then(() => {
        img.src = '';
        setTimeout(() => { img.src = host + '/stream'; }, 500);
        document.querySelectorAll('button').forEach(b => b.classList.remove('active'));
        event.target.classList.add('active');
      });
    }
  </script>
</body>
</html>
)rawliteral";

// ── Resolution mapping ─────────────────────────
framesize_t getFrameSize(String name) {
  if (name == "QQVGA") return FRAMESIZE_QQVGA;
  if (name == "QVGA")  return FRAMESIZE_QVGA;
  if (name == "VGA")   return FRAMESIZE_VGA;
  if (name == "SVGA")  return FRAMESIZE_SVGA;
  return FRAMESIZE_QVGA;
}

// ── Camera init ────────────────────────────────
bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO;
  config.pin_d1       = Y3_GPIO;
  config.pin_d2       = Y4_GPIO;
  config.pin_d3       = Y5_GPIO;
  config.pin_d4       = Y6_GPIO;
  config.pin_d5       = Y7_GPIO;
  config.pin_d6       = Y8_GPIO;
  config.pin_d7       = Y9_GPIO;
  config.pin_xclk     = XCLK_GPIO;
  config.pin_pclk     = PCLK_GPIO;
  config.pin_vsync    = VSYNC_GPIO;
  config.pin_href     = HREF_GPIO;
  config.pin_sccb_sda = SIOD_GPIO;
  config.pin_sccb_scl = SIOC_GPIO;
  config.pin_pwdn     = PWDN_GPIO;
  config.pin_reset    = RESET_GPIO;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode    = CAMERA_GRAB_LATEST;  // always grab freshest frame
  config.fb_location  = CAMERA_FB_IN_PSRAM;
  config.frame_size   = FRAMESIZE_QVGA;
  config.jpeg_quality = 25;   // ← was 12 (lower=bigger); 25 is good for streaming
  config.fb_count     = 3;    // ← was 2; reduces stall waiting for free buffers

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] Init FAILED: 0x%x\n", err);
    return false;
  }
  Serial.println("[CAM] Init OK");
  return true;
}

// ── MJPEG stream task (runs on Core 1) ────────
void streamTask(void* arg) {
  WiFiClient client = server.client();

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
  client.println("Access-Control-Allow-Origin: *");
  client.println();

  while (client.connected()) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("[CAM] Capture failed");
      vTaskDelay(10);
      continue;
    }

    client.printf("--frame\r\n");
    client.printf("Content-Type: image/jpeg\r\n");
    client.printf("Content-Length: %u\r\n\r\n", fb->len);
    client.write(fb->buf, fb->len);
    client.println();

    esp_camera_fb_return(fb);

    vTaskDelay(1);  // yield to WiFi stack — prevents starving the radio driver

    if (!client.connected()) break;
  }

  Serial.println("[STREAM] Client disconnected");
  vTaskDelete(NULL);
}

// ── MJPEG stream handler ───────────────────────
void handleStream() {
  xTaskCreatePinnedToCore(streamTask, "stream", 8192, NULL, 5, NULL, 1);
}

// ── Single capture handler ─────────────────────
void handleCapture() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "text/plain", "Capture failed");
    return;
  }
  server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
  server.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

// ── Resolution change handler ──────────────────
void handleResolution() {
  String res = server.arg("set");
  sensor_t* s = esp_camera_sensor_get();
  s->set_framesize(s, getFrameSize(res));
  Serial.printf("[CAM] Resolution → %s\n", res.c_str());
  server.send(200, "text/plain", "OK");
}

// ══════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n===========================");
  Serial.println("  CAMERA TEST — ESP32-S3");
  Serial.println("===========================\n");

  if (!initCamera()) {
    Serial.println("[FATAL] Camera init failed. Halting.");
    while (true) delay(1000);
  }

  WiFi.begin(ssid, password);
  Serial.print("[WiFi] Connecting");
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    Serial.print(".");
    tries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" OK!");
    Serial.print("[WiFi] Open: http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(" FAILED — falling back to AP mode");
    WiFi.softAP("POSIT_CAM", "12345678");
    Serial.print("[WiFi] Open: http://");
    Serial.println(WiFi.softAPIP());
  }

  server.on("/",           []() { server.send(200, "text/html", HTML_PAGE); });
  server.on("/stream",     handleStream);
  server.on("/capture",    handleCapture);
  server.on("/resolution", handleResolution);
  server.begin();

  Serial.println("[SERVER] Ready\n===========================\n");
}

void loop() {
  server.handleClient();
}
