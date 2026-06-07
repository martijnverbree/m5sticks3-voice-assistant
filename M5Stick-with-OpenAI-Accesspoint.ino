
#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>

static const int WIDTH  = 240;
static const int HEIGHT = 135;

const char* OPENAI_API_KEY = "sk-XXXX";  // add your key here

static const int SAMPLE_RATE    = 16000;
static const int RECORD_SECONDS = 5;
static const int RECORD_SAMPLES = SAMPLE_RATE * RECORD_SECONDS;
static int16_t*  audioBuffer    = nullptr;

Preferences prefs;
WebServer   server(80);
String      savedSSID, savedPass;
bool        apMode = false;
String      currentMessage = "Hold A\nto ask!";

const char* setupPage =
  "<html><body style='background:#1a1a2e;color:#fff;font-family:Arial;padding:20px'>"
  "<div style='max-width:300px;margin:0 auto'>"
  "<h1 style='color:#0f0;font-size:24px'>Voice Setup</h1>"
  "<form action='/save' method='POST'>"
  "<input type='text' name='ssid' placeholder='WiFi Name' required "
    "style='width:100%;padding:12px;margin:8px 0;box-sizing:border-box;border-radius:8px;border:none;font-size:16px'><br>"
  "<input type='password' name='pass' placeholder='WiFi Password' required "
    "style='width:100%;padding:12px;margin:8px 0;box-sizing:border-box;border-radius:8px;border:none;font-size:16px'><br>"
  "<input type='submit' value='Connect' "
    "style='width:100%;padding:12px;background:#0f0;color:#000;border:none;border-radius:8px;font-size:16px;font-weight:bold;cursor:pointer'>"
  "</form></div></body></html>";

const char* successPage =
  "<html><body style='background:#1a1a2e;color:#fff;font-family:Arial;"
  "padding:20px;text-align:center'>"
  "<h1 style='color:#0f0'>Saved!</h1><p>Restarting...</p>"
  "</body></html>";

void drawScreen(const String& text) {
  M5Canvas canvas(&M5.Display);
  canvas.createSprite(WIDTH, HEIGHT);
  canvas.fillSprite(TFT_BLACK);
  canvas.setTextColor(TFT_WHITE);
  canvas.setTextDatum(MC_DATUM);
  canvas.setTextFont(2);

  int lineCount = 1;
  for (char c : text) if (c == '\n') lineCount++;

  int lineH  = canvas.fontHeight() + 4;
  int startY = (HEIGHT - lineCount * lineH) / 2 + lineH / 2;

  int lineNum = 0, lineStart = 0;
  for (int i = 0; i <= (int)text.length(); i++) {
    if (i == (int)text.length() || text[i] == '\n') {
      canvas.drawString(text.substring(lineStart, i),
                        WIDTH / 2, startY + lineNum * lineH);
      lineNum++;
      lineStart = i + 1;
    }
  }
  canvas.pushSprite(0, 0);
  canvas.deleteSprite();
}

void handleRoot() { server.send(200, "text/html", setupPage); }

void handleSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  server.send(200, "text/html", successPage);
  delay(2000);
  ESP.restart();
}

void startAPMode() {
  apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP("M5Voice-Setup");
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
  drawScreen("WiFi Setup\nJoin: M5Voice-Setup\nVisit: 192.168.4.1");
  Serial.println("AP mode started");
}

bool connectToWiFi() {
  prefs.begin("wifi", true);
  savedSSID = prefs.getString("ssid", "");
  savedPass = prefs.getString("pass", "");
  prefs.end();

  if (savedSSID.length() == 0) return false;

  drawScreen("Connecting...\n" + savedSSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSSID.c_str(), savedPass.c_str());

  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi: " + WiFi.localIP().toString());
    return true;
  }
  return false;
}

void resetCredentials() {
  drawScreen("Resetting...");
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.end();
  delay(1000);
  ESP.restart();
}

bool recordAudio() {
  if (!audioBuffer) {
    size_t bytes = RECORD_SAMPLES * sizeof(int16_t);
    audioBuffer  = (int16_t*)(psramFound() ? ps_malloc(bytes) : malloc(bytes));
    if (!audioBuffer) {
      Serial.println("ERROR: audio buffer alloc failed");
      return false;
    }
    Serial.printf("Audio buffer: %u bytes in %s\n",
                  bytes, psramFound() ? "PSRAM" : "heap");
  }

  memset(audioBuffer, 0, RECORD_SAMPLES * sizeof(int16_t));

  // Must stop speaker before using mic
  M5.Speaker.end();

  auto mic_cfg = M5.Mic.config();
  mic_cfg.over_sampling      = 1;
  mic_cfg.magnification      = 4;  // reduce from default 16 to stop clipping
  mic_cfg.noise_filter_level = 0;
  M5.Mic.config(mic_cfg);

  M5.Mic.begin();

  drawScreen("Recording...\nSpeak now!");
  Serial.println("Recording...");

  // Record in small chunks exactly like the official M5Stack example
  static constexpr size_t chunkSize = 256;
  size_t totalChunks = RECORD_SAMPLES / chunkSize;

  for (size_t i = 0; i < totalChunks; i++) {
    // record() blocks until chunk is filled
    while (!M5.Mic.record(audioBuffer + (i * chunkSize), chunkSize, SAMPLE_RATE)) {
      delay(1);
    }
    int secsLeft = RECORD_SECONDS - (int)((i * chunkSize) / SAMPLE_RATE);
    drawScreen("Recording...\n" + String(secsLeft) + "s left");
  }

  M5.Mic.end();
  M5.Speaker.begin();

  int64_t sum    = 0;
  int16_t maxVal = 0;
  for (int i = 0; i < RECORD_SAMPLES; i++) {
    int16_t val = abs(audioBuffer[i]);
    sum += val;
    if (val > maxVal) maxVal = val;
  }
  Serial.printf("Avg level: %lld  Max level: %d\n", sum / RECORD_SAMPLES, maxVal);

  return true;
}

void createWavHeader(uint8_t* h, int dataSize) {
  auto w32 = [&](int o, uint32_t v) {
    h[o]=(v)&0xFF; h[o+1]=(v>>8)&0xFF; h[o+2]=(v>>16)&0xFF; h[o+3]=(v>>24)&0xFF;
  };
  auto w16 = [&](int o, uint16_t v) {
    h[o]=(v)&0xFF; h[o+1]=(v>>8)&0xFF;
  };
  memcpy(h,      "RIFF", 4); w32(4,  dataSize + 36);
  memcpy(h + 8,  "WAVE", 4);
  memcpy(h + 12, "fmt ", 4); w32(16, 16);
  w16(20, 1); w16(22, 1);
  w32(24, SAMPLE_RATE);
  w32(28, SAMPLE_RATE * 2);
  w16(32, 2); w16(34, 16);
  memcpy(h + 36, "data", 4); w32(40, dataSize);
}

String extractJsonString(const String& json, const String& key) {
  String needle = "\"" + key + "\"";
  int idx = json.indexOf(needle);
  if (idx < 0) return "";
  int colon = json.indexOf(':', idx + needle.length());
  if (colon < 0) return "";
  int s = json.indexOf('"', colon);
  if (s < 0) return "";
  s++;
  String result;
  bool esc = false;
  for (int i = s; i < (int)json.length(); i++) {
    char c = json[i];
    if (esc) {
      if      (c == 'n') result += '\n';
      else if (c == 't') result += ' ';
      else if (c == 'r') result += ' ';
      else if (c == 'u') i += 4;
      else               result += c;
      esc = false;
    } else if (c == '\\') { esc = true; }
      else if (c == '"')  { break; }
      else                { result += c; }
  }
  return result;
}

String transcribeAudio() {
  drawScreen("Transcribing...");
  Serial.println("Transcribing...");

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(60);

  if (!client.connect("api.openai.com", 443)) {
    Serial.println("Whisper: connect failed");
    return "";
  }

  int     audioBytes = RECORD_SAMPLES * sizeof(int16_t);
  uint8_t wavHeader[44];
  createWavHeader(wavHeader, audioBytes);

  const String boundary = "----M5Boundary99";

  String partHead =
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
    "Content-Type: audio/wav\r\n\r\n";

  String partModel =
    "\r\n--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"model\"\r\n\r\nwhisper-1\r\n"
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"language\"\r\n\r\nen\r\n"
    "--" + boundary + "--\r\n";

  int contentLen = partHead.length() + 44 + audioBytes + partModel.length();

  client.printf(
    "POST /v1/audio/transcriptions HTTP/1.1\r\n"
    "Host: api.openai.com\r\n"
    "Authorization: Bearer %s\r\n"
    "Content-Type: multipart/form-data; boundary=%s\r\n"
    "Content-Length: %d\r\n"
    "Connection: close\r\n\r\n",
    OPENAI_API_KEY, boundary.c_str(), contentLen);

  client.print(partHead);
  client.write(wavHeader, 44);

  uint8_t* ptr = (uint8_t*)audioBuffer;
  int remaining = audioBytes;
  while (remaining > 0) {
    int n = min(4096, remaining);
    client.write(ptr, n);
    ptr += n;
    remaining -= n;
    delay(1);
  }
  client.print(partModel);

  unsigned long t = millis();
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break;
    if (millis() - t > 60000) { client.stop(); return ""; }
  }

  String body = client.readString();
  client.stop();
  Serial.println("Whisper: " + body);

  String result = extractJsonString(body, "text");
  Serial.println("Transcription: " + result);
  return result;
}

String askGPT(const String& question) {
  drawScreen("Thinking...");
  Serial.println("Asking GPT: " + question);

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  http.begin(client, "https://api.openai.com/v1/chat/completions");
  http.addHeader("Content-Type",  "application/json");
  http.addHeader("Authorization", String("Bearer ") + OPENAI_API_KEY);
  http.setTimeout(30000);

  String q = question;
  q.replace("\\", "\\\\");
  q.replace("\"", "\\\"");
  q.replace("\n", " ");
  q.replace("\r", "");

  String body =
    "{\"model\":\"gpt-4o-mini\",\"max_tokens\":60,\"messages\":["
    "{\"role\":\"system\",\"content\":\"You are a friendly assistant for an 8-year-old child. "
    "Always answer in 20 words or fewer. Use simple, fun language. Never say anything scary or adult.\"},"
    "{\"role\":\"user\",\"content\":\"" + q + "\"}]}";

  int code = http.POST(body);
  Serial.printf("GPT HTTP %d\n", code);

  if (code != 200) {
    Serial.println("GPT error: " + http.getString());
    http.end();
    return "Oops! Try again.";
  }

  String resp = http.getString();
  http.end();
  Serial.println("GPT: " + resp);

  int msgIdx = resp.indexOf("\"message\"");
  if (msgIdx < 0) return "No answer.";

  String result = extractJsonString(resp.substring(msgIdx), "content");
  if (result.length() == 0) return "No answer.";

  Serial.println("Answer: " + result);
  return result;
}

String wordWrap(const String& text, int maxChars) {
  String result, word;
  int lineLen = 0;

  for (int i = 0; i <= (int)text.length(); i++) {
    char c = (i < (int)text.length()) ? text[i] : ' ';
    if (c == ' ' || c == '\n') {
      if (lineLen > 0 && lineLen + (int)word.length() > maxChars) {
        result += '\n'; lineLen = 0;
      }
      result += word;
      lineLen += word.length();
      word = "";
      if (c == ' ' && lineLen > 0 && lineLen < maxChars) { result += ' '; lineLen++; }
      if (c == '\n') { result += '\n'; lineLen = 0; }
    } else {
      word += c;
    }
  }
  return result;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  auto cfg = M5.config();
  cfg.internal_mic = true;
  M5.begin(cfg);
  M5.Display.setRotation(1);

  Serial.printf("Heap: %d  PSRAM: %d\n", ESP.getFreeHeap(), ESP.getFreePsram());

  if (!connectToWiFi()) {
    startAPMode();
    return;
  }

  drawScreen("Hold A\nto ask!");
  Serial.println("Ready.");
}

void loop() {
  M5.update();

  if (apMode) {
    server.handleClient();
    return;
  }

  if (M5.BtnA.pressedFor(3000)) {
    resetCredentials();
  }

  if (M5.BtnA.wasClicked()) {
    Serial.println("\n*** Button A ***");

    if (!recordAudio()) {
      drawScreen("Mic error.\nTry again.");
      delay(2000);
      drawScreen(currentMessage);
      return;
    }

    String question = transcribeAudio();
    if (question.length() < 2) {
      drawScreen("Didn't catch\nthat. Try again!");
      delay(2500);
      drawScreen(currentMessage);
      return;
    }

    String answer  = askGPT(question);
    currentMessage = wordWrap(answer, 22);
    drawScreen(currentMessage);

    Serial.println("Done. Heap: " + String(ESP.getFreeHeap()));
  }

  delay(20);
}