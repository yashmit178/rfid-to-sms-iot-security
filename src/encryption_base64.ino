#include <Crypto.h>
#include <AES.h>
#include <Base64.h>  // Correct library for Base64 encoding
#include <SPI.h>
#include <MFRC522.h>

// ****** 4. AES ENCRYPTION KEY (MUST be 16 bytes for AES-128) ******
uint8_t aesKey[] = { 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
                     0x39, 0x30, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46 };

// --- RFID PINS (for Wemos D1 R2 / D1 Mini) ---
#define RST_PIN D3  // RST
#define SS_PIN D2   // SDA (CS)

// --- GLOBAL OBJECTS & VARIABLES ---
MFRC522 mfrc522(SS_PIN, RST_PIN);
// Use the specific AES128 class from the Crypto library
AES128 aesCipher;
void setup() {
  Serial.begin(115200);
  delay(500);  // Give serial a moment to initialize
  Serial.println("\n\n--- Automated Permit Notification System ---");
  Serial.println("--- Key Generation Utility (CryptoLib Stable Version) ---");

  SPI.begin();
  mfrc522.PCD_Init();

  String appNumberToTest = "12345678";

  byte encryptedData[16];

  if (encryptAppNumber(appNumberToTest, encryptedData)) {
    String encryptedBase64 = toBase64(encryptedData, 16);

    Serial.println("\nFor Application Number: " + appNumberToTest);
    Serial.println("Use this as the KEY in Firebase:");
    Serial.println("------------------------------------");
    Serial.println(encryptedBase64);
    Serial.println("------------------------------------");
  } else {
    Serial.println("Encryption failed!");
  }
}

void loop() {
  // Intentionally empty for this test.
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
  int encodedLength = Base64.encodedLength(len);
  char b64_out[encodedLength + 1];  //testing with +1
  Base64.encode(b64_out, (char*)data, len);
  return String(b64_out);
}
