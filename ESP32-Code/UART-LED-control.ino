static const int RXD2 = 16;   // ESP32 UART2 RX pin
static const int TXD2 = 17;   // ESP32 UART2 TX pin
static const unsigned long BAUD = 115200;

static const int LED_PIN = 2; // onboard LED for ESP32
static bool ledState = false; // keep track of LED state

// Line buffer for text protocols coming from STM32
static char lineBuf[256];
static size_t lineLen = 0;

void setup() {
  // USB serial to PC
  Serial.begin(BAUD);
  while (!Serial) { }

  // UART2 to STM32
  Serial2.begin(BAUD, SERIAL_8N1, RXD2, TXD2);

  // LED output
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println("\n[ESP32] UART2 reader ready (GPIO16 RX, GPIO17 TX).");
  Serial.println("[ESP32] LED toggle enabled.");
}

void handleFromSTM32() {
  while (Serial2.available() > 0) {
    int b = Serial2.read();
    if (b < 0) break;

    char c = (char)b;

    if (c == '\r') {
      // ignore CR
    } else if (c == '\n') {
      // line complete → handle it
      lineBuf[lineLen] = '\0';

      Serial.print("[STM32] ");
      Serial.println(lineBuf);

      // --- LED TOGGLE ---
      if (strcmp(lineBuf, "BTN: PA0 pressed") == 0) {
        ledState = !ledState; // toggle state
        digitalWrite(LED_PIN, ledState ? HIGH : LOW);

        Serial.print("[ESP32] LED is now ");
        Serial.println(ledState ? "ON" : "OFF");
      }

      lineLen = 0;
    } else {
      if (lineLen < sizeof(lineBuf) - 1) {
        lineBuf[lineLen++] = c;
      } else {
        lineBuf[sizeof(lineBuf) - 1] = '\0';
        Serial.print("[STM32] (truncated) ");
        Serial.println(lineBuf);
        lineLen = 0;
      }
    }
  }
}

void handleFromPC() {
  while (Serial.available() > 0) {
    int b = Serial.read();
    if (b < 0) break;
    Serial2.write((uint8_t)b);
  }
}

void loop() {
  handleFromSTM32();
  handleFromPC();
}