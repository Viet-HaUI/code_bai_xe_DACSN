#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
#include <map>
#include <ESP32Servo.h>

// Chân và giá trị dùng cho LCD và RFID 
#define SS_PIN 5
#define RST_PIN 4
#define EEPROM_SIZE 512
#define UID_LENGTH 4
#define MAX_UIDS 32
#define BUZZER_PIN 15
#define so_cho 4
int cho_trong = so_cho;
int dieu_khien_barie;
String vi_tri[so_cho] = {""}; // Quản lý chỗ từ A1 đến A10, ưu tiên A10 -> A1

const int vat_can_out = 26;
const int vat_can_in = 25;
const int buzzer = 15;
const int chuyen_dong = 13;
const int led_ham_xe = 27;

Servo myServo;
const int servoPin = 14;
int tin_hieu_cu_servo = -1;

const int anh_sang = 34;
const int led_barie = 32;

// Khai bao cum RFID LCD
MFRC522 rfid(SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2);

unsigned long thoiGianBatDen = 0;
bool denDangBat = false;

bool setupMode = false;
int availableSlots = MAX_UIDS - 1;

std::map<String, bool> statusMap;

int trangThaiRFID = 0;
unsigned long rfidStartTime = 0;

// Hàm RFID && LCD
String uidToString(byte *uid) {
  String s = "";
  for (int i = 0; i < UID_LENGTH; i++) {
    if (uid[i] < 0x10) s += "0";
    s += String(uid[i], HEX);
  }
  return s;
}

void showMessage(String line1, String line2) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(line1);
  lcd.setCursor(0, 1); lcd.print(line2);
}

void showWaitingScreen() {
  if (cho_trong == 0) {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("Ham xe het cho!");
    lcd.setCursor(0, 1); lcd.print("Chi cho di ra");
  } else {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("Hay quet the");
    lcd.setCursor(0, 1); lcd.print("Con ");
    lcd.print(cho_trong);
    lcd.print(" cho trong");
  }
}

void showMasterWaiting() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Master mode");
  lcd.setCursor(0, 1); lcd.print("Hay quet the !");
}

bool compareUID(byte *uid1, byte *uid2) {
  for (int i = 0; i < UID_LENGTH; i++) {
    if (uid1[i] != uid2[i]) return false;
  }
  return true;
}

bool readUID(byte *buffer) {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return false;
  for (byte i = 0; i < UID_LENGTH; i++) buffer[i] = rfid.uid.uidByte[i];
  rfid.PICC_HaltA();
  return true;
}

void writeUID(int index, byte *uid) {
  for (int i = 0; i < UID_LENGTH; i++) {
    EEPROM.write(index * UID_LENGTH + i, uid[i]);
  }
  EEPROM.commit();
}

void readUIDFromEEPROM(int index, byte *uid) {
  for (int i = 0; i < UID_LENGTH; i++) {
    uid[i] = EEPROM.read(index * UID_LENGTH + i);
  }
}

int findUID(byte *uid) {
  for (int i = 1; i < MAX_UIDS; i++) {
    byte temp[UID_LENGTH];
    readUIDFromEEPROM(i, temp);
    if (temp[0] != 0xFF && compareUID(uid, temp)) return i;
  }
  return -1;
}

bool addUID(byte *uid) {
  if (findUID(uid) != -1) return false;
  for (int i = 1; i < MAX_UIDS; i++) {
    byte temp[UID_LENGTH];
    readUIDFromEEPROM(i, temp);
    if (temp[0] == 0xFF) {
      writeUID(i, uid);
      availableSlots--;
      return true;
    }
  }
  return false;
}

bool removeUID(byte *uid) {
  int index = findUID(uid);
  if (index != -1) {
    for (int i = 0; i < UID_LENGTH; i++) {
      EEPROM.write(index * UID_LENGTH + i, 0xFF);
    }
    EEPROM.commit();
    availableSlots++;
    return true;
  }
  return false;
}

void initEEPROM() {
  byte check[UID_LENGTH];
  readUIDFromEEPROM(0, check);
  bool empty = true;
  for (int i = 0; i < UID_LENGTH; i++) {
    if (check[i] != 0xFF) {
      empty = false;
      break;
    }
  }

  if (empty) {
    showMessage("Quet the chu", "Lam mac dinh");
    while (!readUID(check)) delay(100);
    writeUID(0, check);
    showMessage("Tao the chu", "Thanh cong!");
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    delay(100);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    delay(2000);
  } else {
    for (int i = 1; i < MAX_UIDS; i++) {
    byte temp[UID_LENGTH];
    readUIDFromEEPROM(i, temp);
    if (temp[0] != 0xFF) availableSlots--;
    }
  }
}

// Setup vị trí
String capViTri(String uid) {
  for (int i = so_cho - 1; i >= 0; i--) { // A10 đến A1
    if (vi_tri[i] == "") {
      vi_tri[i] = uid;
      return "A" + String(i + 1);
    }
  }
  return ""; // Hết chỗ
}

void xoaViTri(String uid) {
  for (int i = 0; i < so_cho; i++) {
    if (vi_tri[i] == uid) {
      vi_tri[i] = "";
      break;
    }
  }
}

int xuLyTheHopLe(byte *currentUID) {
  String uidStr = uidToString(currentUID);
  bool status = statusMap[uidStr];
  int doc_vat_can_in = digitalRead(vat_can_in);
  int doc_vat_can_out = digitalRead(vat_can_out);

  // Kiểm tra nếu cả hai cảm biến đều không có tín hiệu
  if (doc_vat_can_in == 1 && doc_vat_can_out == 1) {
    showMessage("Do sai vi tri", "Kiem tra lai");
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    delay(100);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    delay(2000);
    showWaitingScreen();
    return 0; // Không cho phép quẹt thẻ
  }

  if (!status) { // Xe muốn vào
    if (cho_trong == 0) {
      showMessage("Ham xe het cho!", "Chi cho di ra");
      digitalWrite(BUZZER_PIN, HIGH);
      delay(100);
      digitalWrite(BUZZER_PIN, LOW);
      delay(100);
      digitalWrite(BUZZER_PIN, HIGH);
      delay(100);
      digitalWrite(BUZZER_PIN, LOW);
      delay(2000);
      showWaitingScreen();
      return 0; // Không cho vào
    }
    if (doc_vat_can_in == 0) { // Có tín hiệu từ vat_can_in
      String viTri = capViTri(uidStr);
      showMessage("Quet thanh cong", "Moi vao " + viTri);
      statusMap[uidStr] = true;
      cho_trong--;
    } else { // Không có tín hiệu từ vat_can_in
      showMessage("Chi duoc di vao", "Hay kiem tra lai");
      digitalWrite(BUZZER_PIN, HIGH);
      delay(100);
      digitalWrite(BUZZER_PIN, LOW);
      delay(100);
      digitalWrite(BUZZER_PIN, HIGH);
      delay(100);
      digitalWrite(BUZZER_PIN, LOW);
      delay(2000);
      showWaitingScreen();
      return 0; // Không cho vào
    }
  } else { // Xe muốn ra
    if (doc_vat_can_out == 0) { // Có tín hiệu từ vat_can_out
      xoaViTri(uidStr);
      showMessage("Quet thanh cong", "Tam biet");
      statusMap[uidStr] = false;
      cho_trong++;
    } else { // Không có tín hiệu từ vat_can_out
      showMessage("Chi duoc di ra", "Hay kiem tra lai");
      digitalWrite(BUZZER_PIN, HIGH);
      delay(100);
      digitalWrite(BUZZER_PIN, LOW);
      delay(100);
      digitalWrite(BUZZER_PIN, HIGH);
      delay(100);
      digitalWrite(BUZZER_PIN, LOW);
      delay(2000);
      showWaitingScreen();
      return 0; // Không cho ra
    }
  }

  // Bật còi trong 1 giây
  digitalWrite(BUZZER_PIN, HIGH);
  delay(1000);
  digitalWrite(BUZZER_PIN, LOW);

  trangThaiRFID = 1;
  rfidStartTime = millis();
  Serial.print("Trang thai RFID: ");
  Serial.println(trangThaiRFID);

  return 1;
}

// Hàm clear EPPROm để reset thẻ master
void clearEEPROM() {
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0xFF);
  }
  EEPROM.commit();
}

// Cam bien LM393
int cam_bien_vat_can() {
  int doc_vat_can_in = digitalRead(vat_can_in);
  int doc_vat_can_out = digitalRead(vat_can_out);
  if (doc_vat_can_in == 0 || doc_vat_can_out == 0)
    return 1;
  else
    return 0;
}

// Den ham xe
void den_ham_xe() {
  int doc_chuyen_dong = digitalRead(chuyen_dong);
  unsigned long hienTai = millis();

  if (doc_chuyen_dong == 1) {
    digitalWrite(led_ham_xe, HIGH);
    thoiGianBatDen = hienTai;
    denDangBat = true;
  }

  if (denDangBat && (hienTai - thoiGianBatDen >= 3000)) {
    digitalWrite(led_ham_xe, LOW);
    denDangBat = false;
  }
}

// Dieu khien servo
void tin_hieu_Servo(int tin_hieu) {
  Serial.println(tin_hieu);
  if (tin_hieu != tin_hieu_cu_servo) {
    if (tin_hieu == 0) {
      for (int i = 0; i <= 90; i++) {
        myServo.write(i);
        delay(15);
      }
    } else {
      for (int i = 90; i >= 0; i--) {
        myServo.write(i);
        delay(15);
      }
    }
    tin_hieu_cu_servo = tin_hieu;
  }
}

// Den barie
void den_barie() {
  int doc_anh_sang = digitalRead(anh_sang);
  if (doc_anh_sang == 0)
    digitalWrite(led_barie, LOW);
  else
    digitalWrite(led_barie, HIGH);
}

void setup() {
  pinMode(vat_can_out, INPUT);
  pinMode(vat_can_in, INPUT);
  pinMode(chuyen_dong, INPUT);
  pinMode(led_ham_xe, OUTPUT);
  pinMode(buzzer, OUTPUT);
  myServo.setPeriodHertz(50);    // SG90 chạy tần số PWM 50Hz
  myServo.attach(servoPin, 500, 2400); // Gán chân servo, xung min-max (micro giây)
  myServo.write(0);
  pinMode(anh_sang, INPUT);
  pinMode(led_barie, OUTPUT);

  // Set up LCD và RFID
  Wire.begin(21, 22);
  lcd.init(); lcd.backlight();
  SPI.begin();
  rfid.PCD_Init();
  EEPROM.begin(EEPROM_SIZE);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  // clearEEPROM();  // Khi muốn thay thẻ master, bỏ comment dòng này để reset bộ nhớ EPPROM
  initEEPROM();
  showWaitingScreen();
  Serial.begin(115200);
}

void loop() {
  den_ham_xe();
  den_barie();
  if (trangThaiRFID == 1 && cam_bien_vat_can() == 1)
    dieu_khien_barie = 0;
  if (cam_bien_vat_can() == 0 && trangThaiRFID == 0) {
    dieu_khien_barie = 1;
  }
  tin_hieu_Servo(dieu_khien_barie);
  if (trangThaiRFID == 1 && millis() - rfidStartTime >= 5000) {
    trangThaiRFID = 0;
    // Serial.println("Da het 5 giay, trangThaiRFID ve dong");
    showWaitingScreen();
  }
  byte currentUID[UID_LENGTH];
  if (!readUID(currentUID)) return;

  byte masterUID[UID_LENGTH];
  readUIDFromEEPROM(0, masterUID);

  if (compareUID(currentUID, masterUID)) {
    setupMode = !setupMode;
    if (setupMode)
      showMasterWaiting();
    else
      showWaitingScreen();
    delay(1500);
    return;
  }

  if (setupMode) {
    if (findUID(currentUID) == -1) {
      if (addUID(currentUID)) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(100);
        digitalWrite(BUZZER_PIN, LOW);
        delay(100);
        digitalWrite(BUZZER_PIN, HIGH);
        delay(100);
        digitalWrite(BUZZER_PIN, LOW);
        showMessage("CD the chu", "Da them the");
      } else {
        showMessage("CD the chu", "Khong them dc");
      }
    } else {
      removeUID(currentUID);
      statusMap.erase(uidToString(currentUID));
      digitalWrite(BUZZER_PIN, HIGH);
      delay(100);
      digitalWrite(BUZZER_PIN, LOW);
      delay(100);
      digitalWrite(BUZZER_PIN, HIGH);
      delay(100);
      digitalWrite(BUZZER_PIN, LOW);
      showMessage("CD the chu", "Da xoa the");
    }
    delay(2000);
    showMasterWaiting();
    return;
  }

  if (findUID(currentUID) != -1) {
    xuLyTheHopLe(currentUID);
  } else {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    delay(100);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    showMessage("Quet that bai", "The chua them");
    delay(2000);
    showWaitingScreen();
  }
}
