/**
 * IoT-Based Automated Permit Notification System (single device version for prototyping)
 * This single ESP8266/MFRC522 device operates in two modes:
 * 1. WRITE_MODE: Simulates the issuing office. Encrypts an application number,
 * writes it to an RFID tag, and updates Firebase/sends an "Issued" SMS.
 * 2. READ_MODE: Simulates the collection office. Reads an encrypted tag,
 * queries Firebase, and sends a "Ready for Collection" SMS.
 *
 * Tech Stack:
 * - Microcontroller: Wemos D1 R2(ESP8266)
 * - Board's name TO BE SELECTED in Arduino IDE: LOLIN(WEMOS) D1 R2 & MINI
 * - RFID: MFRC522 module with Mifare Classic 1K chip functional RFID card/ key fob
 * - Cloud Database: Google Firebase Realtime Database
 * - SMS Gateway: Twilio
 * - Encryption: AES-128 (using Crypto library by rweather)
 * - IDE: Arduino IDE
 *
 * IMPORTANT: Before compiling, ensure you have:
 * 1. REMOVED the "AESLib" library from your Arduino libraries folder if you already have it installed.
 * 2. INSTALLED the "Crypto" library by Rhys Weatherley via the Library Manager.
 * 3. INSTALLED the "Base64" library by Xander Electronics via the Library Manager.
 * 4. INSTALLED the "ArduinoJson" library by Benoit Blanchon via the Library Manager.
 * 5. INSTALLED the "MFRC522" library by GithubCommunity via the Library Manager.
 *
 * How to Use:
 * 1. Fill in all the "__REPLACE THIS__" sections below.
 * 2. Upload the code to your ESP8266.
 * 3. Open the Serial Monitor at 115200 baud.
 * 4. Type 'WRITE' to enter Write Mode or 'READ' to enter Read Mode.
 * 5. Follow the on-screen prompts.
 */

// --- LIBRARIES ---
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Crypto.h>
#include <AES.h>
#include <ArduinoJson.h>
#include <Base64.h>

// ****** 1. WIFI CREDENTIALS ******
const char* WIFI_SSID = "__REPLACE THIS__";
const char* WIFI_PASSWORD = "__REPLACE THIS__";

// ****** 2. FIREBASE CONFIG ******
#define FIREBASE_HOST "__REPLACE THIS__"

// ****** 3. TWILIO CONFIG ******
const char* TWILIO_ACCOUNT_SID = "__REPLACE THIS__";
const char* TWILIO_AUTH_TOKEN = "__REPLACE THIS__";
const char* TWILIO_FROM_NUMBER = "__REPLACE THIS__";

// ****** 4. AES ENCRYPTION KEY (MUST be 16 bytes for AES-128) ******
uint8_t aesKey[] = { 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
                     0x39, 0x30, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46 };

#define RST_PIN D3
#define SS_PIN D2

// --- GLOBAL OBJECTS & VARIABLES ---
MFRC522 mfrc522(SS_PIN, RST_PIN);
AES128 aesCipher;  // Using AES128 class from Crypto library

enum Mode { IDLE,
            WRITE_MODE,
            READ_MODE };
Mode currentMode = IDLE;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\n--- Automated Permit Notification System ---");
  Serial.print("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected! IP: " + WiFi.localIP().toString());

  SPI.begin();
  mfrc522.PCD_Init();

  printMenu();
}

void loop() {
  // Checking for serial input to change modes
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    command.toUpperCase();

    if (command == "WRITE") {
      currentMode = WRITE_MODE;
      Serial.println("\n--- Switched to WRITE MODE ---");
      Serial.println("Enter the Application Number (e.g., 12345678) to process:");
    } else if (command == "READ") {
      currentMode = READ_MODE;
      Serial.println("\n--- Switched to READ MODE ---");
      Serial.println("Place an issued RFID tag on the reader to process for collection...");
    } else if (command == "MENU") {
      currentMode = IDLE;
      printMenu();
    } else if (currentMode == WRITE_MODE && command.length() > 0) {
      handleWriteMode(command);
    }
  }
  if (currentMode == READ_MODE) {
    handleReadMode();
  }
}

/**
 * Processes the application number input in WRITE_MODE
 */
void handleWriteMode(String appNumStr) {
  if (appNumStr.length() == 0) {
    Serial.println("DEBUG: Empty application number received");
    return;
  }

  Serial.println("Processing Application Number: " + appNumStr);

  // 1. Encrypt the application number
  byte encryptedData[16];
  if (!encryptAppNumber(appNumStr, encryptedData)) {
    Serial.println("Encryption failed! Aborting.");
    currentMode = IDLE;
    printMenu();
    return;
  }

  String encryptedBase64 = toBase64(encryptedData, 16);
  String safeKey = makeKeySafe(encryptedBase64);
  Serial.println("Encrypted (Base64 for DB): " + encryptedBase64);
  Serial.println("Firebase Safe Key: " + safeKey);  // Log the new safe key


  // 2. Fetch record from Firebase to get phone number
  String mobileNumber = "";
  String currentStatus = "";
  Serial.println("DEBUG: Fetching permit info from Firebase...");

  if (!getPermitInfo(safeKey, mobileNumber, currentStatus)) {
    Serial.println("Error: Could not find record in Firebase for this application number.");
    Serial.println("Please ensure data is pre-populated in Firebase with the encrypted Base64 string as the key.");
    Serial.println("Expected Firebase key: " + safeKey);
    currentMode = IDLE;
    printMenu();
    return;
  }

  Serial.println("DEBUG: Found record - Mobile: " + mobileNumber + ", Status: " + currentStatus);

  if (currentStatus != "under processing") {
    Serial.println("Warning: This permit's status is '" + currentStatus + "', not 'under processing'. Aborting.");
    currentMode = IDLE;
    printMenu();
    return;
  }

  // 3. Prompt to write to RFID tag
  Serial.println("Place a blank RFID tag on the reader and type 'Y' to write...");

  // Wait for confirmation
  waitForConfirmation(appNumStr, encryptedData, safeKey, mobileNumber);
}

/**
 * Waits for user confirmation to write to RFID tag
 */
void waitForConfirmation(String appNumStr, byte* encryptedData, String encryptedBase64, String mobileNumber) {
  while (true) {
    if (Serial.available() > 0) {
      String confirm = Serial.readStringUntil('\n');
      confirm.trim();
      confirm.toUpperCase();
      Serial.println("DEBUG: Confirmation received: '" + confirm + "'");

      if (confirm == "Y" || confirm == "YES") {
        Serial.println("DEBUG: User confirmed, attempting to write to RFID...");

        if (writeDataToRFID(encryptedData)) {
          Serial.println("RFID tag written successfully!");

          // 4. Update status in Firebase to "issued"
          if (updateStatusInFirebase(encryptedBase64, "issued")) {
            Serial.println("Firebase status updated to 'issued'.");

            // 5. Send "RP Issued" SMS via Twilio
            String message = "Dear Applicant, your Resident Permit for (Application no: " + appNumStr + ") has been issued and will be sent soon.";
            if (sendTwilioSms(mobileNumber, message)) {
              Serial.println("SMS notification sent successfully to " + mobileNumber);
            } else {
              Serial.println("Failed to send SMS notification.");
            }
          } else {
            Serial.println("Failed to update Firebase status.");
          }
        } else {
          Serial.println("Failed to write to RFID tag. Please try again.");
        }
        break;
      } else if (confirm == "N" || confirm == "NO") {
        Serial.println("Write operation cancelled.");
        break;
      } else {
        Serial.println("Please enter 'Y' to confirm or 'N' to cancel.");
      }
    }
    delay(100);
  }

  currentMode = IDLE;
  printMenu();
}

/**
 * Handles the logic for the Collection Office.
 * Continuously scans for RFID tags.
 */
void handleReadMode() {
  byte readData[16];
  if (readDataFromRFID(readData)) {
    Serial.println("Tag detected and read!");
    String encryptedBase64 = toBase64(readData, 16);
    String safeKey = makeKeySafe(encryptedBase64);
    Serial.println("Read Encrypted (Base64): " + encryptedBase64);
    Serial.println("Querying Firebase with safe key: " + safeKey);

    // 1. Get permit info from Firebase
    String mobileNumber = "";
    String currentStatus = "";
    if (getPermitInfo(safeKey, mobileNumber, currentStatus)) {
      if (currentStatus == "issued") {
        // 2. Update status to "ready_for_collection"
        if (updateStatusInFirebase(safeKey, "ready_for_collection")) {
          Serial.println("Firebase status updated to 'ready_for_collection'.");

          // 3. Send "Ready for Collection" SMS
          String message = "Dear Applicant, your Resident Permit is ready for collection";
          if (sendTwilioSms(mobileNumber, message)) {
            Serial.println("SMS notification sent successfully to " + mobileNumber);
          } else {
            Serial.println("Failed to send SMS notification.");
          }
        } else {
          Serial.println("Failed to update Firebase status.");
        }
      } else {
        Serial.println("Permit status is '" + currentStatus + "'. No action taken.");
      }
    } else {
      Serial.println("Could not find a matching record in Firebase for this tag.");
    }

    Serial.println("\nContinuing to scan in READ MODE...");
    delay(3000);  // Pause to avoid spamming reads of the same tag
  }
}
//HELPER FUNCTIONS

void printMenu() {
  Serial.println("\n--- MENU ---");
  Serial.println("Type 'WRITE' to enter Issuing Office mode.");
  Serial.println("Type 'READ' to enter Collection Office mode.");
  Serial.println("------------");
}

/**
 * Encrypts a plaintext string into a 16-byte buffer using the Crypto library.
 */
bool encryptAppNumber(String plainText, byte* outputBuffer) {
  byte plain[16];
  memset(plain, 0, 16);  // Pad with zeros
  plainText.getBytes(plain, 16);

  // Set the encryption key. This is a required step for the Crypto library.
  if (!aesCipher.setKey(aesKey, sizeof(aesKey))) {
    Serial.println("Failed to set AES key!");
    return false;
  }

  // Encrypt the block. The Crypto library encrypts in-place or to a separate buffer.
  aesCipher.encryptBlock(outputBuffer, plain);

  return true;
}

/**
 * Converts a byte array to a Base64 encoded string.
 */
String toBase64(byte* data, int len) {
  // Calculate the required buffer size for Base64 encoding
  int encodedLength = Base64.encodedLength(len);
  char b64_out[encodedLength + 1];  // +1 for null terminator

  // Encode the data
  Base64.encode(b64_out, (char*)data, len);

  // Ensure null termination
  b64_out[encodedLength] = '\0';

  return String(b64_out);
}

/**
 * Writes 16 bytes of data to a specific block on an RFID tag.
 */
bool writeDataToRFID(byte* dataToWrite) {
  Serial.println("Waiting for RFID tag to write...");
  while (!(mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial())) {
    delay(50);
  }

  MFRC522::MIFARE_Key key;
  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;  // Default key
  byte blockAddr = 4;                                  // Use a standard data block

  MFRC522::StatusCode status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, blockAddr, &key, &(mfrc522.uid));
  if (status != MFRC522::STATUS_OK) {
    Serial.println("Authentication failed: " + String(mfrc522.GetStatusCodeName(status)));
    return false;
  }

  status = mfrc522.MIFARE_Write(blockAddr, dataToWrite, 16);
  if (status != MFRC522::STATUS_OK) {
    Serial.println("Write failed: " + String(mfrc522.GetStatusCodeName(status)));
    return false;
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  return true;
}

/**
 * Reads 16 bytes of data from a specific block on an RFID tag.
 */
bool readDataFromRFID(byte* outputBuffer) {
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
    return false;
  }

  byte blockAddr = 4;
  byte bufferSize = 18;  // MFRC522 library requires 18-byte buffer
  byte readBlockBuffer[18];
  MFRC522::MIFARE_Key key;
  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

  MFRC522::StatusCode status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, blockAddr, &key, &(mfrc522.uid));
  if (status != MFRC522::STATUS_OK) {
    return false;
  }

  status = mfrc522.MIFARE_Read(blockAddr, readBlockBuffer, &bufferSize);
  if (status == MFRC522::STATUS_OK) {
    memcpy(outputBuffer, readBlockBuffer, 16);  // Copies the 16 data bytes
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return true;
  }
  return false;
}

// --- Firebase Functions ---

/**
 * Fetches the mobile number and status from Firebase using the encrypted Base64 key.
 */
bool getPermitInfo(String encryptedBase64Key, String& mobileNumber, String& status) {
  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();
  HTTPClient https;

  String url = String(FIREBASE_HOST) + "/permits_data/" + encryptedBase64Key + ".json";
  if (https.begin(*client, url)) {
    int httpCode = https.GET();
    if (httpCode == HTTP_CODE_OK) {
      DynamicJsonDocument doc(256);
      deserializeJson(doc, https.getString());
      if (doc.isNull()) {
        https.end();
        return false;  // Record not found or empty
      }
      mobileNumber = doc["mobile_number"].as<String>();
      status = doc["status"].as<String>();
      https.end();
      return true;
    }
    https.end();
  }
  return false;
}

/**
 * Updates the status field for a given record in Firebase.
 */
bool updateStatusInFirebase(String encryptedBase64Key, String newStatus) {
  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();
  HTTPClient https;

  String url = String(FIREBASE_HOST) + "/permits_data/" + encryptedBase64Key + "/status.json";
  if (https.begin(*client, url)) {
    // Firebase REST API requires putting the string in quotes for a PUT request
    int httpCode = https.PUT("\"" + newStatus + "\"");
    https.end();
    return httpCode == HTTP_CODE_OK;
  }
  return false;
}

// --- Twilio Function ---

/**
 * Sends an SMS using the Twilio API.
 */
bool sendTwilioSms(String to, String message) {
  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();
  HTTPClient https;

  String url = "https://api.twilio.com/2010-04-01/Accounts/" + String(TWILIO_ACCOUNT_SID) + "/Messages.json";
  if (https.begin(*client, url)) {
    // Basic Authentication header
    String auth = String(TWILIO_ACCOUNT_SID) + ":" + String(TWILIO_AUTH_TOKEN);
    String auth_b64 = toBase64((byte*)auth.c_str(), auth.length());
    https.addHeader("Authorization", "Basic " + auth_b64);
    https.addHeader("Content-Type", "application/x-www-form-urlencoded");

    // Build POST body
    String postData = "To=" + to + "&From=" + String(TWILIO_FROM_NUMBER) + "&Body=" + message;

    int httpCode = https.POST(postData);
    if (httpCode > 0 && (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED)) {
      Serial.println("Twilio Response: " + https.getString());
      https.end();
      return true;
    } else {
      Serial.println("Twilio POST failed, error code: " + String(httpCode));
      Serial.println("Twilio Response: " + https.getString());
      https.end();
      return false;
    }
  }
  return false;
}

/**
 * Makes a Base64 string safe for use as a Firebase key.
 * Replaces '/' with '_' to prevent Firebase from creating nested paths.
 */
String makeKeySafe(String key) {
  key.replace('/', '_');
  key.replace('+', '-');
  return key;
}