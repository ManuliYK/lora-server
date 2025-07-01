/*
  LoRa-WiFi Gateway Master Node
  (Enhanced with debug output and connection monitoring)
*/

#include <SPI.h>
#include <LoRa.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

void handleServerCommand(char* payload);

// ---------- pin / network / server config ----------
#define ss   15
#define rst  16
#define dio0 4

const char* ssid = "Ruwanya's S24";
const char* password = "dataMine";

const char* serverURL = "http://192.168.56.1:3000";
const char* websocketHost = "192.168.56.1";
const int   websocketPort = 3000;

// ---------- LoRa addressing ----------
byte MasterNode = 0xFF;
byte Node1      = 0xBB;
byte msgCount   = 0;

// ---------- runtime globals ----------
struct SensorData {
  float voltage;
  int   nodeId;
  unsigned long timestamp;
  int   rssi;
  float snr;
  String messageType;
};

HTTPClient        http;
WebSocketsClient  webSocket;
bool wifiConnected   = false;
bool serverConnected = false;
String Mymessage     = "";    // <-- put ADR command to be sent here

// Debug counters
unsigned long lastStatusPrint = 0;
unsigned long lastHeartbeat = 0;
int loopCounter = 0;

// ---------------------------------------------------
//  SET-UP
// ---------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== LoRa Gateway Starting ===");
  Serial.println("Initializing LoRa...");
  
  LoRa.setPins(ss, rst, dio0);
  if (!LoRa.begin(433E6)) { 
    Serial.println("ERROR: LoRa initialization failed!"); 
    while (1) {
      delay(1000);
      Serial.println("LoRa failed - system halted");
    }
  }
  Serial.println("LoRa initialized successfully");

  Serial.println("Connecting to WiFi...");
  Serial.printf("SSID: %s\n", ssid);
  
  WiFi.begin(ssid, password);
  int wifiTimeout = 0;
  while (WiFi.status() != WL_CONNECTED && wifiTimeout < 30) { 
    delay(1000); 
    Serial.print(".");
    wifiTimeout++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.printf("\nWiFi Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
  } else {
    Serial.println("\nERROR: WiFi connection failed!");
    Serial.printf("WiFi Status: %d\n", WiFi.status());
  }

  Serial.println("Initializing WebSocket...");
  Serial.printf("WebSocket Server: %s:%d\n", websocketHost, websocketPort);
  
  webSocket.begin(websocketHost, websocketPort, "/");
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
  
  Serial.println("=== Gateway Setup Complete ===");
  Serial.println("Starting main loop...\n");
}

// ---------------------------------------------------
//  MAIN LOOP
// ---------------------------------------------------
void loop() {
  loopCounter++;
  
  // Check WiFi status
  if (WiFi.status() != WL_CONNECTED) { 
    if (wifiConnected) {
      Serial.println("WiFi connection lost!");
      wifiConnected = false;
    }
    reconnectWiFi(); 
  }
  
  // Handle WebSocket
  webSocket.loop();
  
  // Check for LoRa packets
  onReceive(LoRa.parsePacket());
  
  // Print status every 10 seconds
  if (millis() - lastStatusPrint > 10000) {
    printStatus();
    lastStatusPrint = millis();
  }
  
  // Send heartbeat every 30 seconds
  if (millis() - lastHeartbeat > 30000) {
    sendHeartbeat();
    lastHeartbeat = millis();
  }
  
  delay(10);
}

// ---------------------------------------------------
//  STATUS MONITORING
// ---------------------------------------------------
void printStatus() {
  Serial.println("\n--- Gateway Status ---");
  Serial.printf("Loop count: %d\n", loopCounter);
  Serial.printf("WiFi: %s (RSSI: %d dBm)\n", 
                wifiConnected ? "Connected" : "Disconnected", 
                WiFi.RSSI());
  Serial.printf("WebSocket: %s\n", serverConnected ? "Connected" : "Disconnected");
  Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("Uptime: %lu ms\n", millis());
  Serial.println("---------------------\n");
  loopCounter = 0;
}

void sendHeartbeat() {
  if (serverConnected) {
    DynamicJsonDocument doc(200);
    doc["type"] = "heartbeat";
    doc["gateway_id"] = "LORA_GATEWAY_01";
    doc["timestamp"] = millis();
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["free_heap"] = ESP.getFreeHeap();
    
    String msg;
    serializeJson(doc, msg);
    webSocket.sendTXT(msg);
    Serial.println("Heartbeat sent to server");
  }
}

// ---------------------------------------------------
//  WEBSOCKET HANDLER
// ---------------------------------------------------
void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED: {
      Serial.printf("WebSocket Connected to: %s\n", payload);
      serverConnected = true;
      
      DynamicJsonDocument doc(200);
      doc["type"] = "gateway_connected";
      doc["gateway_id"] = "LORA_GATEWAY_01";
      doc["timestamp"] = millis();
      
      String msg;
      serializeJson(doc, msg);
      webSocket.sendTXT(msg);
      Serial.println("Gateway connection message sent");
      break;
    }
    
    case WStype_DISCONNECTED: 
      Serial.println("WebSocket Disconnected");
      serverConnected = false; 
      break;
      
    case WStype_TEXT: 
      Serial.printf("WebSocket message received: %s\n", payload);
      handleServerCommand((char*)payload); 
      break;
      
    case WStype_ERROR:
      Serial.printf("WebSocket Error: %s\n", payload);
      break;
      
    default: 
      Serial.printf("WebSocket event type: %d\n", type);
      break;
  }
}

void handleServerCommand(char* payload) {
  Serial.print("Processing server command: ");
  Serial.println(payload);

  // Parse JSON command
  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, payload);
  
  if (error) {
    Serial.printf("JSON parse error: %s\n", error.c_str());
    return;
  }

  String type = doc["type"];
  if (type == "send_to_node") {
    int nodeId = doc["nodeId"];
    String message = doc["message"];
    
    Serial.printf("Sending to node %d: %s\n", nodeId, message.c_str());
    Mymessage = message;  // Store for next ADR ACK
    
    // Or send immediately if you prefer
    // sendLoRaMessage(message, nodeId);
  }
}

// ---------------------------------------------------
//  LORA RECEIVE & ADR-ACK LOGIC
// ---------------------------------------------------
void onReceive(int packetSize) {
  if (!packetSize) return;

  Serial.printf("LoRa packet received, size: %d\n", packetSize);

  int  recipient     = LoRa.read();
  byte sender        = LoRa.read();
  byte msgId         = LoRa.read();
  int  declaredLen   = LoRa.read();

  String incoming;
  while (LoRa.available()) {
    incoming += (char)LoRa.read();
  }
  
  Serial.printf("Packet details - From: 0x%02X, To: 0x%02X, MsgID: %d, Len: %d\n", 
                sender, recipient, msgId, declaredLen);
  Serial.printf("Message: '%s'\n", incoming.c_str());

  if (declaredLen != incoming.length()) {
    Serial.println("Length mismatch - packet rejected");
    return;
  }
  
  if (recipient != MasterNode) {
    Serial.println("Not addressed to master node - packet ignored");
    return;
  }

  /* >>> ADR-ack branch <<< */
  if (incoming == "20") {                       // 20 = ADR ACK
    Serial.println("ADR ACK received");
    if (Mymessage.length()) {                   // send only if we have data
      sendLoRaMessage(Mymessage, Node1);
      Serial.printf("Command sent to node: %s\n", Mymessage.c_str());
      Mymessage = "";                           // clear after sending
    } else {
      Serial.println("No command queued to send");
    }
    return;                                     // do not forward to server
  }

  /* >>> normal sensor frame branch <<< */
  int rssi = LoRa.packetRssi();
  float snr = LoRa.packetSnr();
  Serial.printf("RSSI: %d dBm, SNR: %.2f dB\n", rssi, snr);
  
  processSensorData(incoming, sender, rssi, snr);
}

// ---------------------------------------------------
//  SENSOR DATA → SERVER
// ---------------------------------------------------
void processSensorData(String data, byte nodeId, int rssi, float snr) {
  Serial.println("Processing sensor data...");
  
  SensorData s;
  s.nodeId = nodeId; 
  s.timestamp = millis();
  s.rssi = rssi; 
  s.snr = snr;

  if (data.indexOf(',') > 0) {                    // "voltage,"
    s.voltage = data.substring(0, data.indexOf(',')).toFloat();
    s.messageType = "voltage_reading";
  } else {
    s.voltage = 0; 
    s.messageType = "unknown";
  }
  
  Serial.printf("Parsed data - Node: %d, Voltage: %.2f, Type: %s\n", 
                s.nodeId, s.voltage, s.messageType.c_str());
  
  sendToServerHTTP(s);
  sendToServerWebSocket(s);
}

// ---------------------------------------------------
//  HTTP / WS FORWARDERS
// ---------------------------------------------------
void sendToServerHTTP(const SensorData& d) {
  if (!wifiConnected) {
    Serial.println("Cannot send HTTP - WiFi not connected");
    return;
  }
  
  Serial.println("Sending data via HTTP...");
  
  WiFiClient client;
  http.begin(client, String(serverURL) + "/api/sensor-data");
  http.addHeader("Content-Type", "application/json");
  
  DynamicJsonDocument doc(256);
  doc["nodeId"] = d.nodeId; 
  doc["voltage"] = d.voltage; 
  doc["messageType"] = d.messageType;
  doc["timestamp"] = d.timestamp; 
  doc["rssi"] = d.rssi; 
  doc["snr"] = d.snr; 
  doc["gatewayId"] = "LORA_GATEWAY_01";
  
  String body;
  serializeJson(doc, body);
  Serial.printf("HTTP payload: %s\n", body.c_str());
  
  int httpCode = http.POST(body);
  Serial.printf("HTTP response code: %d\n", httpCode);
  
  if (httpCode > 0) {
    String response = http.getString();
    Serial.printf("HTTP response: %s\n", response.c_str());
  }
  
  http.end();
}

void sendToServerWebSocket(const SensorData& d) {
  if (!serverConnected) {
    Serial.println("Cannot send WebSocket - not connected to server");
    return;
  }
  
  Serial.println("Sending data via WebSocket...");
  
  DynamicJsonDocument doc(256);
  doc["type"] = "sensor_data"; 
  doc["nodeId"] = d.nodeId; 
  doc["voltage"] = d.voltage; 
  doc["messageType"] = d.messageType;
  doc["timestamp"] = d.timestamp; 
  doc["rssi"] = d.rssi; 
  doc["snr"] = d.snr; 
  doc["gatewayId"] = "LORA_GATEWAY_01";
  
  String msg;
  serializeJson(doc, msg);
  Serial.printf("WebSocket payload: %s\n", msg.c_str());
  
  webSocket.sendTXT(msg);
  Serial.println("Data sent via WebSocket");
}

// ---------------------------------------------------
//  HELPERS
// ---------------------------------------------------
void sendLoRaMessage(String payload, byte dest) {
  Serial.printf("Sending LoRa message to 0x%02X: %s\n", dest, payload.c_str());
  
  LoRa.beginPacket();
  LoRa.write(dest);
  LoRa.write(MasterNode);
  LoRa.write(msgCount++);
  LoRa.write(payload.length());
  LoRa.print(payload);
  LoRa.endPacket();
  
  Serial.println("LoRa message sent");
}

void reconnectWiFi() {
  static unsigned long lastTry = 0;
  if (millis() - lastTry < 30000) return;
  
  lastTry = millis();
  Serial.println("Attempting WiFi reconnection...");
  
  WiFi.disconnect();
  delay(1000);
  WiFi.begin(ssid, password);
  
  int timeout = 15;
  while (WiFi.status() != WL_CONNECTED && timeout > 0) {
    delay(1000);
    Serial.print(".");
    timeout--;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.printf("\nWiFi reconnected! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\nWiFi reconnection failed");
  }
}
