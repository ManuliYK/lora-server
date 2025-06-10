/*
  LoRa Node
  The IoT Projects

*/
#include <SPI.h>              // include libraries
#include <LoRa.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define ss 15  //GPIO 15
#define rst 16  //GPIO 16
#define dio0 4  //GPIO 4

#define VOLTAGE_PIN A0   // analog pin for voltage sensor
#define CURRENT_PIN A3   // analog pin for current sensor

// #define SCREEN_WIDTH 128 // OLED display width, in pixels
// #define SCREEN_HEIGHT 64 // OLED display height, in pixels
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
// #define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
// Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

byte MasterNode = 0xFF;
byte Node1 = 0xBB;
byte Node2 = 0xCC;

String SenderNode = "";
String outgoing;              // outgoing message

byte msgCount = 0;            // count of outgoing messages
String incoming = "";

// Tracks the time since last event fired
unsigned long previousMillis = 0;
unsigned long int previoussecs = 0;
unsigned long int currentsecs = 0;
unsigned long currentMillis = 0;
int interval = 1 ; // updated every 1 second
int Secs = 0;

int TP;
int SF;
// int soilmoisturepercent;
// int soilMoistureValue;

void setup() {
  Serial.begin(115200);                   // initialize serial
  //display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  delay(500);
  //display.clearDisplay();
  //display.setTextColor(WHITE);

  while (!Serial);

  Serial.println("LoRa Master Node");

  LoRa.setPins(ss, rst, dio0);

  if (!LoRa.begin(433E6)) {
    Serial.println("Starting LoRa failed!");
    while (1);
  }
}

void loop() {


  currentMillis = millis();
  currentsecs = currentMillis / 1000;
  if ((unsigned long)(currentsecs - previoussecs) >= interval) {
    
    //Serial.println(Secs);
    if ( Secs > 5 )
    {
      Secs = 0;
    }
    if ( (Secs >= 0) && (Secs < 5) )
    {

      float voltage = readVoltage();
      float current = readCurrent();


      String message = String(voltage) + "," + String(current); // Current and Voltage Measurement
      sendMessage(message, Node1, MasterNode);
    }

    if ( Secs == 6  )
    {

      String message = "20"; // ADR acknolewdgement
      sendMessage(message, Node1, MasterNode);
    }
    Secs = Secs + 1;
    previoussecs = currentsecs;
  }

  // sendMessage(message, Node1, MasterNode)

  // parse for a packet, and call onReceive with the result:
  onReceive(LoRa.parsePacket());

}


float readVoltage() {
  int raw = analogRead(VOLTAGE_PIN); // Read raw ADC value
  float voltage = (raw / 4095.0) * 3.3;  // Convert to voltage (3.3V ref for ESP32)
  voltage = voltage * (230.0 / 0.5);    // Scale based on your voltage divider
  return voltage;
}

float readCurrent() {
  int raw = analogRead(CURRENT_PIN);
  float voltage = (raw / 4095.0) * 3.3;
  float current = (voltage - 2.5) / 0.185;  // for ACS712 5A version
  return current;
}

void sendMessage(String outgoing, byte senderNode, byte MasterNode) {
  LoRa.beginPacket();                   // start packet
  LoRa.write(MasterNode);              // add destination address
  LoRa.write(senderNode);             // add sender address
  LoRa.write(msgCount);                 // add message ID
  LoRa.write(outgoing.length());        // add payload length
  LoRa.print(outgoing);                 // add payload
  LoRa.endPacket();                     // finish packet and send it
  msgCount++;                           // increment message ID
}

void onReceive(int packetSize) {
  if (packetSize == 0) return;          // if there's no packet, return

  // read packet header bytes:
  int recipient = LoRa.read();          // recipient address
  byte sender = LoRa.read();            // sender address
  if ( sender == 0XBB )
    SenderNode = "MasterNode1:";
  if ( sender == 0XCC )
    SenderNode = "Node2:";
  byte incomingMsgId = LoRa.read();     // incoming msg ID
  byte incomingLength = LoRa.read();    // incoming msg length


  while (LoRa.available()) {
    incoming += (char)LoRa.read();
  }

  if (incomingLength != incoming.length()) {   // check length for error
    //Serial.println("error: message length does not match length");
    ;
    return;                             // skip rest of function
  }

  // if the recipient isn't this device or broadcast,
  if (recipient != Node1) {
    // Serial.println("This message is not for me.");
    ;
    return;                             // skip rest of function
  }

  // if message is for this device, or broadcast, print details:
  //Serial.println("Received from: 0x" + String(sender, HEX));
  //Serial.println("Sent to: 0x" + String(recipient, HEX));
  //Serial.println("Message ID: " + String(incomingMsgId));
  // Serial.println("Message length: " + String(incomingLength));
  // Serial.println("Message: " + incoming);
  //Serial.println("RSSI: " + String(LoRa.packetRssi()));
  // Serial.println("Snr: " + String(LoRa.packetSnr()));
  // Serial.println();

  if ( sender == 0XBB )
  {
    String t = getValue(incoming, ',', 0); // TP
    String h = getValue(incoming, ',', 1); // SF

    TP = t.toInt();
    SF = h.toInt();
    incoming = "";
    //clear display
    // display.clearDisplay();
    // display.setTextSize(2);
    // display.setCursor(30, 0);
    // display.print(SenderNode);
    // // display temperature
    // display.setTextSize(2);
    // display.setCursor(0, 20);
    // display.print("Temp: ");
    // display.setCursor(60, 20);
    // display.print(temperature);
    // display.print(" ");
    // display.setTextSize(1);
    // display.cp437(true);
    // display.write(167);
    // display.setTextSize(2);
    // display.print("C");

    // display humidity
    // display.setTextSize(2);
    // display.setCursor(0, 45);
    // display.print("Humi: ");
    // display.setCursor(60, 45);
    // display.print(humidity);
    // display.print(" %");
  }

  // if ( sender == 0XBB )
  // {

  //   String v = getValue(incoming, ',', 0); // Soil Moisture Value
  //   String p = getValue(incoming, ',', 1); // Soil Moisture percentage

  //   soilMoistureValue = v.toInt();
  //   soilmoisturepercent = p.toInt();


  //   incoming = "";

  //   //clear display
  //   display.clearDisplay();

  //   display.setTextSize(2);
  //   display.setCursor(35, 0);
  //   display.print(SenderNode);

  //   display.setCursor(20, 20); //oled display
  //   display.setTextSize(2);
  //   display.println("Soil RH:");

  //   display.setCursor(35, 40); //oled display
  //   display.setTextSize(3);
  //   display.print(soilmoisturepercent);
  //   display.setCursor(70, 40);
  //   display.setTextSize(3);
  //   display.println(" %");
  //   display.display();
  // }

  // display.display();
}
String getValue(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = { 0, -1 };
  int maxIndex = data.length() - 1;

  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }
  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}
