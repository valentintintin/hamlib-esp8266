/*
    LCD    -> ESP8266 12-Q
  following pinmap http://arduino.esp8266.com/versions/1.6.5-1160-gef26c5f/doc/esp12.png
  --------------------------
  4|RS | -> ESP2866  pin 4 D2
  6|E  | -> ESP2866 pin 5 D1
  11|D4 | -> ESP2866 pin 13 D7
  12|D5 | -> ESP2866 pin 12 D6
  13|D6 | -> ESP2866 pin 14 D5
  14|D7 | -> ESP2866 pin 16 D0

  D3 -> Rotary DT
  D4 -> Rotary CLK (not used)
*/

#include <ESPRotary.h>
#include <ESP8266WiFi.h>
#include <LiquidCrystal.h>
#include <WiFiClient.h>

#define BUTTON_PAUSE 250
#define TIME_UPDATE 250

enum Button {
  NO,
  SELECT,
  UP,
  DOWN,
  LEFT,
  RIGHT,
  MIDDLE
};

enum RadioMode {
  FM,
  LSB,
  USB,
  PKTUSB,
  PKTLSB,
  PKTFM,
  WFM,
  AM,
  CW,
  UNK
};

struct RadioData {
  unsigned long frequency;
  int signal;
  bool isTx;
  RadioMode mode;
  char vfo;
};

const char* ssid = "SSID";
const char* password = "PASSWORD";
const char * host = "192.168.1.36";
const unsigned int port = 4532;

LiquidCrystal lcd(D2, D1, D7, D6, D5, D0);
ESPRotary r;
WiFiClient client;

struct RadioData radioData;
RadioMode radioModeAvailable[] = { FM, LSB, USB };
short currentIncrement = 3;
bool incrementIsNegative = false;
unsigned long incrementAvailable[] = { 100, 1000, 2500, 5000, 10000, 100000, 1000000};
unsigned long lastUpdateRadio = 0, lastSendRadio = 0, lastLcdRefresh = 0;

void setup() {
  Serial.begin(115200);

  lcd.display();
  lcd.begin(16, 2);
  lcd.clear();

  r.begin(D3, D4);
  r.setIncrement(incrementAvailable[currentIncrement] / 100);
  r.setChangedHandler(rotate);

  radioData.frequency = 0;
  r.resetPosition(radioData.frequency, false);
  radioData.mode = UNK;
  radioData.signal = 0;
  radioData.isTx = false;
  radioData.vfo = '?';

  lcd.setCursor(0, 0);
  lcd.print(F("Connecting WiFi"));
  WiFi.begin(ssid, password);

  short i = 0;
  while (WiFi.status() != WL_CONNECTED) {
    lcd.setCursor(i++, 1);
    lcd.print(F("."));
    i = i % 16;
    delay(500);
  }

  i = 0;
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(F("Connecting Radio"));

  while (!client.connect(host, port)) {
    lcd.setCursor(i++, 1);
    lcd.print(F("."));
    i = i % 16;
    delay(500);
  }

  lcd.clear();
}

void loop() {
  r.loop();
  computeRadio();
  computeButton();
  showDataToLcd();

  delay(10);
}

void sendRadio(char command, const char * value) {
  if (millis ()- lastSendRadio < TIME_UPDATE) {
    return;
  }
  
  client.print(F("+"));
  client.print(command);
  client.print(F(" "));
  client.println(value);
  
  lastUpdateRadio = 0;
  lastSendRadio = millis();
}

void computeRadio() {
  if (millis() - lastUpdateRadio < TIME_UPDATE) {
    return;
  }
  
  client.println(F("+f +m +t +l STRENGTH +v"));

  /*
     get_freq:
    15:52:14.214 -> Frequency: 27555000
    15:52:14.214 -> RPRT 0
    15:52:14.214 -> get_mode:
    15:52:14.214 -> Mode: USB
    15:52:14.214 -> Passband: 0
    15:52:14.214 -> RPRT 0
    15:52:14.214 -> get_ptt:
    15:52:14.214 -> PTT: 0
    15:52:14.214 -> RPRT 0
    15:52:14.214 -> get_level: STRENGTH
    15:52:14.214 -> -36
    15:52:14.214 -> RPRT 0
    get_vfo:
    VFO: VFOA
  */

  while (client.available() > 0) {
    lastUpdateRadio = millis();
    
    String line = client.readStringUntil('\n');
    //Serial.println(line);

    if (line.startsWith(PSTR("Frequency: "))) {
      radioData.frequency = line.substring(11).toInt();
    } else if (line.startsWith(PSTR("PTT: "))) {
      radioData.isTx = line.substring(5).toInt();
    } else if (line.charAt(0) == '-' || (line.charAt(0) >= '0' && line.charAt(0) <= '9')) {
      radioData.signal = line.toInt();
    } else if (line.startsWith(PSTR("VFO: VFO"))) {
      radioData.vfo = line.charAt(8);
    } else if (line.startsWith(PSTR("Mode: "))) {
      if (line.endsWith("PKTFM")) {
        radioData.mode = PKTFM;
      } else if (line.endsWith("PKTUSB")) {
        radioData.mode = PKTUSB;
      } else if (line.endsWith("PKTLSB")) {
        radioData.mode = PKTLSB;
      } else if (line.endsWith("WFM")) {
        radioData.mode = WFM;
      } else if (line.endsWith("LSB")) {
        radioData.mode = LSB;
      } else if (line.endsWith("USB")) {
        radioData.mode = USB;
      } else if (line.endsWith("AM")) {
        radioData.mode = AM;
      } else if (line.endsWith("FM")) {
        radioData.mode = FM;
      } else if (line.endsWith("CW")) {
        radioData.mode = CW;
      }
    }
  }
}

void computeButton() {
  switch (getCurrentButton()) {
    case LEFT:
      if (radioData.mode == 0) {
        radioData.mode = radioModeAvailable[2];
      } else {
        radioData.mode = radioModeAvailable[radioData.mode - 1];
      }
      radioChangeMode();
      delay(BUTTON_PAUSE);
      break;
    case RIGHT:
      radioData.mode = radioModeAvailable[(radioData.mode + 1) % 3];
      delay(BUTTON_PAUSE);
      radioChangeMode();
      break;
    case UP:
      currentIncrement = (currentIncrement + 1) % 7;
      r.setIncrement(incrementAvailable[currentIncrement] * (incrementIsNegative ? -1 : 1) / 100);
      delay(BUTTON_PAUSE);
      break;
    case DOWN:
      if (currentIncrement == 0) {
        currentIncrement = 6;
      } else {
        currentIncrement--;
      }
      r.setIncrement(incrementAvailable[currentIncrement] * (incrementIsNegative ? -1 : 1) / 100);
      delay(BUTTON_PAUSE);
      break;
    case MIDDLE:
      incrementIsNegative = !incrementIsNegative;
      r.setIncrement(-r.getIncrement());
      delay(BUTTON_PAUSE);
      break;
    case SELECT:
      if (radioData.vfo == 'A') {
        radioData.vfo = 'B';
      } else {
        radioData.vfo = 'A';
      }
      radioChangeVfo();
      delay(BUTTON_PAUSE);
      break;
  }
}

void showDataToLcd() {
  if (millis ()- lastLcdRefresh < TIME_UPDATE) {
    return;
  }

  lastLcdRefresh = millis();
  
  lcd.setCursor(0, 0);
  lcd.print(F("         "));
  lcd.setCursor(0, 0);
  lcd.print(radioData.frequency);

  lcd.setCursor(10, 0);
  switch (radioData.mode) {
    case AM:
      lcd.print(F("    AM"));
      break;
    case FM:
      lcd.print(F("    FM"));
      break;
    case CW:
      lcd.print(F("    CW"));
      break;
    case UNK:
      lcd.print(F("   UNK"));
      break;
    case WFM:
      lcd.print(F("   WFM"));
      break;
    case USB:
      lcd.print(F("   USB"));
      break;
    case LSB:
      lcd.print(F("   LSB"));
      break;
    case PKTFM:
      lcd.print(F(" PKTFM"));
      break;
    case PKTLSB:
      lcd.print(F("PKTLSB"));
      break;
    case PKTUSB:
      lcd.print(F("PKTUSB"));
      break;
  }

  lcd.setCursor(0, 1);
  lcd.print(F("S"));
  lcd.setCursor(1, 1);

  if (radioData.signal < -48) {
    lcd.print("0   ");
  } else if (radioData.signal < -42) {
    lcd.print("1   ");
  } else if (radioData.signal < -36) {
    lcd.print("2   ");
  } else if (radioData.signal < -30) {
    lcd.print("3   ");
  } else if (radioData.signal < -24) {
    lcd.print("4   ");
  } else if (radioData.signal < -18) {
    lcd.print("5   ");
  } else if (radioData.signal < -12) {
    lcd.print("6   ");
  } else if (radioData.signal < -6) {
    lcd.print("7   ");
  } else if (radioData.signal < 0) {
    lcd.print("8   ");
  } else if (radioData.signal < 10) {
    lcd.print("9   ");
  } else if (radioData.signal < 20) {
    lcd.print("9+10");
  } else if (radioData.signal < 30) {
    lcd.print("9+20");
  } else if (radioData.signal < 40) {
    lcd.print("9+30");
  } else if (radioData.signal < 50) {
    lcd.print("9+40");
  } else {
    lcd.print("9+50");
  }

  lcd.setCursor(6, 1);
  if (r.getIncrement() >= 0) {
    lcd.print(F("+"));
  } else {
    lcd.print(F("-"));
  }
  lcd.setCursor(7, 1);
  lcd.print(F("     "));
  lcd.setCursor(7, 1);
  lcd.print(abs(r.getIncrement()));

  lcd.setCursor(13, 1);
  if (radioData.isTx) {
    lcd.print(F("TX"));
  } else {
    lcd.print(F("RX"));
  }

  lcd.setCursor(15, 1);
  lcd.print(radioData.vfo);
}

Button getCurrentButton() {
  short button = map(analogRead(A0), 0, 1023, 0, 255);

  //Serial.println(button);

  if (button <= 15) {
    return RIGHT;
  } else if (button <= 22) {
    return UP;
  } else if (button <= 30) {
    return MIDDLE;
  } else if (button <= 50) {
    return DOWN;
  } else if (button <= 70) {
    return LEFT;
  } else if (button <= 100) {
    return SELECT;
  }

  return NO;
}

void rotate(ESPRotary& r) {
  radioData.frequency += r.getIncrement() * 100;

  char frequency[10];
  sprintf(frequency, "%lu", radioData.frequency);
  sendRadio('F', frequency);
}

void radioChangeMode() {
  switch (radioData.mode) {
    case FM:
      sendRadio('M', "FM 0");
      break;
    case USB:
      sendRadio('M', "USB 0");
      break;
    case LSB:
      sendRadio('M', "LSB 0");
      break;
  }
}

void radioChangeVfo() {
  if (radioData.vfo == 'A') {
    sendRadio('V', "VFOA 0");
  } else {
    sendRadio('V', "VFOB 0");
  }
}
