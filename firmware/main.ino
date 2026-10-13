
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>

// ---------- GPIO assignments ----------
constexpr uint8_t PIN_I2C_SDA = 4;
constexpr uint8_t PIN_I2C_SCL = 5;

constexpr uint8_t PIN_IR_RX   = 1;
constexpr uint8_t PIN_IR_TX   = 2;  // Proposed: verify against schematic

constexpr uint8_t PIN_UP      = 38;
constexpr uint8_t PIN_DOWN    = 39;
constexpr uint8_t PIN_SELECT  = 40;
constexpr uint8_t PIN_STATUS  = 21;

// ---------- Hardware ----------
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(
    U8G2_R0, U8X8_PIN_NONE
);

IRrecv irrecv(PIN_IR_RX, 1024, 50, true);
IRsend irsend(PIN_IR_TX);

decode_results results;

// Save raw timings in microseconds.
constexpr uint16_t MAX_RAW = 300;
uint16_t savedRaw[MAX_RAW];
uint16_t savedLength = 0;
bool hasCapture = false;

const char* menuItems[] = {
    "Learn IR",
    "Replay last",
    "Clear capture"
};
constexpr int MENU_COUNT = 3;
int selected = 0;

bool previousUp = HIGH;
bool previousDown = HIGH;
bool previousSelect = HIGH;

unsigned long lastButtonMs = 0;
unsigned long lastCaptureMs = 0;

bool buttonPressed(uint8_t pin, bool &previous) {
    bool current = digitalRead(pin);
    bool pressed = (previous == HIGH && current == LOW);
    previous = current;

    if (pressed && millis() - lastButtonMs > 160) {
        lastButtonMs = millis();
        return true;
    }
    return false;
}

void drawScreen(const char* message = nullptr) {
    oled.clearBuffer();
    oled.setFont(u8g2_font_6x12_tf);

    oled.drawStr(0, 11, "S3 Handheld | IR");

    for (int i = 0; i < MENU_COUNT; i++) {
        int y = 27 + i * 12;

        if (i == selected) {
            oled.drawBox(0, y - 10, 128, 12);
            oled.setDrawColor(0);
        }

        oled.drawStr(3, y, menuItems[i]);
        oled.setDrawColor(1);
    }

    oled.drawHLine(0, 52, 128);

    if (message) {
        oled.drawStr(0, 63, message);
    } else if (hasCapture) {
        oled.drawStr(0, 63, "IR signal saved in RAM");
    } else {
        oled.drawStr(0, 63, "No capture yet");
    }

    oled.sendBuffer();
}

void learnIR() {
    drawScreen("Waiting for remote...");

    unsigned long start = millis();

    while (millis() - start < 15000) {
        if (irrecv.decode(&results)) {
            uint16_t count = results.rawlen;

            // Ignore captures that exceed available storage.
            if (count > 1 && count - 1 <= MAX_RAW) {
                savedLength = count - 1;

                // rawbuf[0] is the leading gap.
                // Remaining entries are mark/space timings
                // in units of kRawTick microseconds.
                for (uint16_t i = 0; i < savedLength; i++) {
                    uint32_t us =
                        (uint32_t)results.rawbuf[i + 1]
                        * kRawTick;

                    savedRaw[i] = (uint16_t)
                        min(us, (uint32_t)65535);
                }

                hasCapture = true;
                lastCaptureMs = millis();

                Serial.printf(
                    "Captured protocol=%s, raw timings=%u\n",
                    typeToString(results.decode_type).c_str(),
                    savedLength
                );

                irrecv.resume();
                drawScreen("Capture successful!");
                delay(900);
                return;
            }

            irrecv.resume();
        }

        delay(5);
    }

    drawScreen("No usable signal");
    delay(900);
}

void replayIR() {
    if (!hasCapture || savedLength == 0) {
        drawScreen("Nothing captured");
        delay(900);
        return;
    }

    drawScreen("Transmitting IR...");
    irsend.sendRaw(savedRaw, savedLength, 38);

    drawScreen("Transmission sent");
    delay(700);
}

void handleSelection() {
    switch (selected) {
        case 0:
            learnIR();
            break;

        case 1:
            replayIR();
            break;

        case 2:
            hasCapture = false;
            savedLength = 0;
            drawScreen("Capture cleared");
            delay(700);
            break;
    }
}

void setup() {
    Serial.begin(115200);

    pinMode(PIN_UP, INPUT_PULLUP);
    pinMode(PIN_DOWN, INPUT_PULLUP);
    pinMode(PIN_SELECT, INPUT_PULLUP);

    pinMode(PIN_STATUS, OUTPUT);
    digitalWrite(PIN_STATUS, LOW);

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    oled.begin();

    irrecv.enableIRIn();
    irsend.begin();

    drawScreen("Ready");
    digitalWrite(PIN_STATUS, HIGH);

    Serial.println("ESP32-S3 handheld IR firmware ready");
}

void loop() {
    if (buttonPressed(PIN_UP, previousUp)) {
        selected = (selected + MENU_COUNT - 1) % MENU_COUNT;
        drawScreen();
    }

    if (buttonPressed(PIN_DOWN, previousDown)) {
        selected = (selected + 1) % MENU_COUNT;
        drawScreen();
    }

    if (buttonPressed(PIN_SELECT, previousSelect)) {
        handleSelection();
    }

    delay(5);
}
