#define BLYNK_PRINT Serial

#define BLYNK_TEMPLATE_ID "TMPL6G4brLHow"
#define BLYNK_TEMPLATE_NAME "IoT"
#define BLYNK_AUTH_TOKEN "qyPfbFH_VJr4rqlL8HvEZ0snlUNTi0zT"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <math.h>

// =================================================
// WIFI
// =================================================

char ssid[] = "Dru";
char pass[] = "12345678";

BlynkTimer timer;

// =================================================
// PIN SENSOR
// =================================================

// Sensor pH
#define PH_PIN 39

// Sensor TDS
#define TDS_PIN 34

// =================================================
// PIN RELAY
// =================================================

#define RELAY_1 25
#define RELAY_2 33
#define RELAY_3 32
#define RELAY_4 26

// Modul relay umumnya Active LOW
#define RELAY_ON LOW
#define RELAY_OFF HIGH

// =================================================
// KALIBRASI SENSOR
// =================================================

// -------------------------------------------------
// KALIBRASI pH
// -------------------------------------------------

float calibration_value = 21.69;

// -------------------------------------------------
// SUHU AIR
// -------------------------------------------------

float temperature = 25.0;

// -------------------------------------------------
// FAKTOR KALIBRASI TDS
// -------------------------------------------------

float tdsCalibrationFactor = 1.0;

// =================================================
// KALIBRASI TURBIDITY DARI TDS
// =================================================
//
// Karena sensor turbidity mengalami kebocoran,
// sensor turbidity FISIK tidak digunakan.
//
// Turbidity sekarang diestimasi dari TDS:
//
// TDS = 0    -> Turbidity = 1000
// TDS = 70   -> Turbidity = 625
// TDS = 140  -> Turbidity = 250
//
// Semakin kecil TDS:
//     semakin besar nilai turbidity.
//
// Semakin besar TDS:
//     semakin kecil nilai turbidity.
//
// Nilai turbidity ini adalah INDEX,
// bukan NTU absolut.
// =================================================

const float TDS_TURBIDITY_MIN = 0.0;
const float TDS_TURBIDITY_MAX = 140.0;

const float TURBIDITY_MAX = 1000.0;

// Pada TDS sekitar 140,
// target turbidity sekitar 200-300.
// Kita gunakan titik tengah = 250.
const float TURBIDITY_CLEAN = 250.0;

// =================================================
// KONTROL PEMBACAAN pH
// =================================================
//
// Saat salah satu relay ON:
//     pembacaan pH dihentikan.
//
// Setelah SEMUA relay OFF:
//     tunggu 2 detik.
//
// Setelah 2 detik:
//     pembacaan pH dilanjutkan.
// =================================================

const unsigned long PH_DELAY_AFTER_RELAY = 2000UL;

bool phReadingPaused = false;

unsigned long phResumeTime = 0;

// Menyimpan nilai pH terakhir
float lastPHValue = 0.0;

// =================================================
// BUFFER SENSOR pH
// =================================================

int buffer_arr[10];

// =================================================
// STATUS RELAY
// =================================================

bool relay1State = false;
bool relay2State = false;
bool relay3State = false;
bool relay4State = false;

// =================================================
// TIMER POMPA
// =================================================

unsigned long pumpEndTime = 0;

bool pumpTimerActive = false;

// =================================================
// SETUP
// =================================================

void setup() {

  Serial.begin(115200);

  delay(500);

  // =================================================
  // ADC ESP32
  // =================================================

  analogReadResolution(12);

  // ADC attenuation
  analogSetPinAttenuation(PH_PIN, ADC_11db);
  analogSetPinAttenuation(TDS_PIN, ADC_11db);

  // =================================================
  // PIN RELAY
  // =================================================

  pinMode(RELAY_1, OUTPUT);
  pinMode(RELAY_2, OUTPUT);
  pinMode(RELAY_3, OUTPUT);
  pinMode(RELAY_4, OUTPUT);

  // Semua relay OFF
  semuaRelayOff();

  // =================================================
  // HEADER
  // =================================================

  Serial.println();
  Serial.println("================================");
  Serial.println("       WATER FILTER ESP32");
  Serial.println("================================");

  Serial.println("Mode turbidity:");
  Serial.println("Turbidity dihitung dari TDS");

  Serial.println("TDS 0   -> Turbidity 1000");
  Serial.println("TDS 70  -> Turbidity 625");
  Serial.println("TDS 140 -> Turbidity 250");

  Serial.println();

  Serial.println("Menghubungkan ke Blynk...");

  // =================================================
  // BLYNK
  // =================================================

  Blynk.begin(
    BLYNK_AUTH_TOKEN,
    ssid,
    pass
  );

  // =================================================
  // TIMER SENSOR
  // =================================================

  // Baca sensor setiap 2 detik
  timer.setInterval(
    2000L,
    kirimSensorBlynk
  );

  // =================================================
  // TIMER POMPA
  // =================================================

  // Cek timer pompa setiap 100 ms
  timer.setInterval(
    100L,
    cekTimerPompa
  );

  // =================================================
  // MENU
  // =================================================

  tampilkanMenu();
}

// =================================================
// LOOP
// =================================================

void loop() {

  Blynk.run();

  timer.run();

  kontrolSerial();

  // Kontrol pause/resume pH
  kontrolPembacaanPH();
}

// =================================================
// CEK APAKAH ADA RELAY YANG ON
// =================================================

bool adaRelayON() {

  if (relay1State) {
    return true;
  }

  if (relay2State) {
    return true;
  }

  if (relay3State) {
    return true;
  }

  if (relay4State) {
    return true;
  }

  return false;
}

// =================================================
// KONTROL PEMBACAAN pH
// =================================================

void kontrolPembacaanPH() {

  bool relayAktif = adaRelayON();

  // =================================================
  // JIKA ADA RELAY ON
  // =================================================

  if (relayAktif) {

    // Selama relay ON, pH harus pause
    if (!phReadingPaused) {

      phReadingPaused = true;

      Serial.println();
      Serial.println(
        "[pH] Pembacaan dihentikan karena relay ON"
      );
    }

    // Reset timer resume
    phResumeTime = 0;

    return;
  }

  // =================================================
  // SEMUA RELAY OFF
  // =================================================

  if (phReadingPaused) {

    // Belum mulai timer resume
    if (phResumeTime == 0) {

      phResumeTime =
        millis() + PH_DELAY_AFTER_RELAY;

      Serial.println();
      Serial.println(
        "[pH] Semua relay OFF"
      );

      Serial.println(
        "[pH] Menunggu 2 detik sebelum membaca kembali..."
      );
    }

    // =================================================
    // CEK APAKAH 2 DETIK SUDAH LEWAT
    // =================================================

    if (
      (long)(millis() - phResumeTime) >= 0
    ) {

      phReadingPaused = false;

      phResumeTime = 0;

      Serial.println(
        "[pH] Pembacaan pH dilanjutkan"
      );
    }
  }
}

// =================================================
// KONTROL BLYNK
// =================================================

// =================================================
// V3 = RELAY 1 / POMPA INPUT
// =================================================

BLYNK_WRITE(V3) {

  bool state = param.asInt();

  hentikanTimerPompa();

  relay1State = state;

  digitalWrite(
    RELAY_1,
    state ? RELAY_ON : RELAY_OFF
  );

  Serial.println(
    state
      ? "Blynk: Pompa Input ON"
      : "Blynk: Pompa Input OFF"
  );
}

// =================================================
// V4 = RELAY 2 / POMPA FILTER
// =================================================

BLYNK_WRITE(V4) {

  bool state = param.asInt();

  hentikanTimerPompa();

  relay2State = state;

  digitalWrite(
    RELAY_2,
    state ? RELAY_ON : RELAY_OFF
  );

  Serial.println(
    state
      ? "Blynk: Pompa Filter ON"
      : "Blynk: Pompa Filter OFF"
  );
}

// =================================================
// V5 = RELAY 2 + RELAY 3 / LOOP FILTER
// =================================================

BLYNK_WRITE(V5) {

  bool state = param.asInt();

  hentikanTimerPompa();

  relay2State = state;
  relay3State = state;

  digitalWrite(
    RELAY_2,
    state ? RELAY_ON : RELAY_OFF
  );

  digitalWrite(
    RELAY_3,
    state ? RELAY_ON : RELAY_OFF
  );

  Serial.println(
    state
      ? "Blynk: Loop Filter ON"
      : "Blynk: Loop Filter OFF"
  );

  // V4 mengikuti kondisi Relay 2
  Blynk.virtualWrite(
    V4,
    state ? 1 : 0
  );
}

// =================================================
// V6 = RELAY 4 / POMPA OUTLET
// =================================================

BLYNK_WRITE(V6) {

  bool state = param.asInt();

  hentikanTimerPompa();

  relay4State = state;

  digitalWrite(
    RELAY_4,
    state ? RELAY_ON : RELAY_OFF
  );

  Serial.println(
    state
      ? "Blynk: Pompa Outlet ON"
      : "Blynk: Pompa Outlet OFF"
  );
}

// =================================================
// SENSOR pH
// =================================================

float bacaPH() {

  // =================================================
  // AMBIL 10 SAMPLE
  // =================================================

  for (int i = 0; i < 10; i++) {

    buffer_arr[i] =
      analogRead(PH_PIN);

    delay(5);
  }

  // =================================================
  // SORTING ADC
  // =================================================

  for (int i = 0; i < 9; i++) {

    for (int j = i + 1; j < 10; j++) {

      if (
        buffer_arr[i] >
        buffer_arr[j]
      ) {

        int temp =
          buffer_arr[i];

        buffer_arr[i] =
          buffer_arr[j];

        buffer_arr[j] =
          temp;
      }
    }
  }

  // =================================================
  // AMBIL 6 NILAI TENGAH
  // =================================================

  unsigned long avgValue = 0;

  for (int i = 2; i < 8; i++) {

    avgValue +=
      buffer_arr[i];
  }

  // =================================================
  // KONVERSI KE VOLTAGE
  // =================================================

  float voltage =
    (float)avgValue
    * 3.3
    / 4095.0
    / 6.0;

  // =================================================
  // HITUNG pH
  // =================================================

  float pHValue =
    -5.70 * voltage
    + calibration_value;

  // =================================================
  // LIMIT pH
  // =================================================

  if (pHValue < 0.0) {

    pHValue = 0.0;
  }

  if (pHValue > 14.0) {

    pHValue = 14.0;
  }

  return pHValue;
}

// =================================================
// SENSOR TDS
// =================================================

float bacaTDS() {

  // =================================================
  // BACA ADC
  // =================================================

  int adc =
    analogRead(TDS_PIN);

  // =================================================
  // KONVERSI VOLTAGE
  // =================================================

  float voltage =
    adc
    * 3.3
    / 4095.0;

  // =================================================
  // KOMPENSASI SUHU
  // =================================================

  float compensationCoefficient =
    1.0
    + 0.02
    * (temperature - 25.0);

  float compensationVoltage =
    voltage
    / compensationCoefficient;

  // =================================================
  // RUMUS TDS
  // =================================================

  float tdsValue =
    (
      133.42
      * pow(
        compensationVoltage,
        3
      )

      - 255.86
      * pow(
        compensationVoltage,
        2
      )

      + 857.39
      * compensationVoltage

    )
    * 0.5;

  // =================================================
  // KALIBRASI TDS
  // =================================================

  tdsValue *=
    tdsCalibrationFactor;

  // =================================================
  // BATAS MINIMUM
  // =================================================

  if (tdsValue < 0.0) {

    tdsValue = 0.0;
  }

  return tdsValue;
}

// =================================================
// TURBIDITY DARI TDS
// =================================================
//
// Tidak lagi membaca GPIO36.
//
// Mapping:
//
// TDS = 0
//     -> Turbidity = 1000
//
// TDS = 70
//     -> Turbidity = 625
//
// TDS = 140
//     -> Turbidity = 250
//
// =================================================

float hitungTurbidityDariTDS(
  float tds
) {

  // =================================================
  // BATASI NILAI TDS
  // =================================================

  float tdsClamped =
    constrain(
      tds,
      TDS_TURBIDITY_MIN,
      TDS_TURBIDITY_MAX
    );

  // =================================================
  // MAPPING TERBALIK
  // =================================================
  //
  // TDS 0:
  //
  // 1000 - 0 = 1000
  //
  // TDS 140:
  //
  // 1000 - 750 = 250
  //
  // =================================================

  float turbidity =
    TURBIDITY_MAX
    -
    (
      (
        tdsClamped
        - TDS_TURBIDITY_MIN
      )
      /
      (
        TDS_TURBIDITY_MAX
        - TDS_TURBIDITY_MIN
      )
    )
    *
    (
      TURBIDITY_MAX
      - TURBIDITY_CLEAN
    );

  // =================================================
  // BATASI HASIL
  // =================================================

  turbidity =
    constrain(
      turbidity,
      TURBIDITY_CLEAN,
      TURBIDITY_MAX
    );

  return turbidity;
}

// =================================================
// KIRIM DATA SENSOR KE BLYNK
// =================================================

void kirimSensorBlynk() {

  // =================================================
  // BACA TDS
  // =================================================

  float tds =
    bacaTDS();

  // =================================================
  // HITUNG TURBIDITY DARI TDS
  // =================================================

  float turbidity =
    hitungTurbidityDariTDS(tds);

  // =================================================
  // BACA pH
  // =================================================

  if (!phReadingPaused) {

    lastPHValue =
      bacaPH();
  }

  // =================================================
  // KIRIM TDS
  // =================================================

  Blynk.virtualWrite(
    V1,
    tds
  );

  // =================================================
  // KIRIM TURBIDITY
  // =================================================

  Blynk.virtualWrite(
    V2,
    turbidity
  );

  // =================================================
  // KIRIM pH
  // =================================================

  // Saat pause, jangan kirim nilai baru.
  // Nilai terakhir tetap dipertahankan di Blynk.

  if (!phReadingPaused) {

    Blynk.virtualWrite(
      V0,
      lastPHValue
    );
  }

  // =================================================
  // SERIAL MONITOR
  // =================================================

  Serial.println();
  Serial.println(
    "========== DATA SENSOR =========="
  );

  // -------------------------------------------------
  // pH
  // -------------------------------------------------

  if (phReadingPaused) {

    Serial.println(
      "pH       : PAUSED"
    );

    Serial.print(
      "pH terakhir: "
    );

    Serial.println(
      lastPHValue,
      2
    );
  }

  else {

    Serial.print(
      "pH       : "
    );

    Serial.println(
      lastPHValue,
      2
    );
  }

  // -------------------------------------------------
  // TDS
  // -------------------------------------------------

  Serial.print(
    "TDS      : "
  );

  Serial.print(
    tds,
    2
  );

  Serial.println(
    " ppm"
  );

  // -------------------------------------------------
  // TURBIDITY
  // -------------------------------------------------

  Serial.print(
    "Turbidity: "
  );

  Serial.println(
    turbidity,
    0
  );

  // -------------------------------------------------
  // STATUS RELAY
  // -------------------------------------------------

  Serial.print(
    "Relay    : "
  );

  Serial.println(
    adaRelayON()
      ? "ON"
      : "OFF"
  );

  Serial.println(
    "================================="
  );
}

// =================================================
// KONTROL SERIAL MONITOR
// =================================================

void kontrolSerial() {

  if (Serial.available() > 0) {

    char command =
      Serial.read();

    // Abaikan enter dan spasi
    if (
      command == '\n' ||
      command == '\r' ||
      command == ' '
    ) {

      return;
    }

    switch (command) {

      // ===========================================
      // POMPA ON SELAMA 5 DETIK
      // ===========================================

      case '1':

        pompaSementara(
          RELAY_1
        );

        break;

      case '2':

        pompaSementara(
          RELAY_2
        );

        break;

      case '3':

        pompaLoopSementara();

        break;

      case '4':

        pompaSementara(
          RELAY_4
        );

        break;

      // ===========================================
      // MANUAL ON/OFF
      // ===========================================

      case '5':

        setRelay(
          1,
          true
        );

        break;

      case '6':

        setRelay(
          1,
          false
        );

        break;

      case '7':

        setRelay(
          2,
          true
        );

        break;

      case '8':

        setRelay(
          2,
          false
        );

        break;

      case '9':

        setRelay(
          3,
          true
        );

        break;

      case 'a':

        setRelay(
          3,
          false
        );

        break;

      case 'b':

        setRelay(
          4,
          true
        );

        break;

      case 'c':

        setRelay(
          4,
          false
        );

        break;

      // ===========================================
      // MATIKAN SEMUA RELAY
      // ===========================================

      case '0':

        semuaRelayOff();

        hentikanTimerPompa();

        sinkronisasiBlynk();

        Serial.println(
          "Semua relay OFF"
        );

        break;

      // ===========================================
      // UNKNOWN COMMAND
      // ===========================================

      default:

        Serial.println(
          "Perintah tidak dikenal."
        );

        tampilkanMenu();

        break;
    }
  }
}

// =================================================
// FUNGSI RELAY
// =================================================

void setRelay(
  int nomor,
  bool state
) {

  int pin;

  // =================================================
  // TENTUKAN PIN
  // =================================================

  if (nomor == 1) {

    pin = RELAY_1;
  }

  else if (nomor == 2) {

    pin = RELAY_2;
  }

  else if (nomor == 3) {

    pin = RELAY_3;
  }

  else if (nomor == 4) {

    pin = RELAY_4;
  }

  else {

    return;
  }

  // =================================================
  // HENTIKAN TIMER POMPA
  // =================================================

  hentikanTimerPompa();

  // =================================================
  // SET RELAY
  // =================================================

  digitalWrite(
    pin,
    state
      ? RELAY_ON
      : RELAY_OFF
  );

  // =================================================
  // UPDATE STATUS
  // =================================================

  if (nomor == 1) {

    relay1State = state;
  }

  if (nomor == 2) {

    relay2State = state;
  }

  if (nomor == 3) {

    relay3State = state;
  }

  if (nomor == 4) {

    relay4State = state;
  }

  // =================================================
  // SYNC BLYNK
  // =================================================

  sinkronisasiBlynk();

  // =================================================
  // SERIAL
  // =================================================

  Serial.print(
    "Relay "
  );

  Serial.print(
    nomor
  );

  Serial.println(
    state
      ? " ON"
      : " OFF"
  );
}

// =================================================
// MATIKAN SEMUA RELAY
// =================================================

void semuaRelayOff() {

  digitalWrite(
    RELAY_1,
    RELAY_OFF
  );

  digitalWrite(
    RELAY_2,
    RELAY_OFF
  );

  digitalWrite(
    RELAY_3,
    RELAY_OFF
  );

  digitalWrite(
    RELAY_4,
    RELAY_OFF
  );

  // =================================================
  // RESET STATUS
  // =================================================

  relay1State = false;

  relay2State = false;

  relay3State = false;

  relay4State = false;
}

// =================================================
// HENTIKAN TIMER POMPA
// =================================================

void hentikanTimerPompa() {

  pumpTimerActive = false;
}

// =================================================
// SINKRONISASI STATUS RELAY KE BLYNK
// =================================================

void sinkronisasiBlynk() {

  // Relay 1
  Blynk.virtualWrite(
    V3,
    relay1State
      ? 1
      : 0
  );

  // Relay 2
  Blynk.virtualWrite(
    V4,
    relay2State
      ? 1
      : 0
  );

  // Relay 2 + 3
  Blynk.virtualWrite(
    V5,
    (
      relay2State &&
      relay3State
    )
      ? 1
      : 0
  );

  // Relay 4
  Blynk.virtualWrite(
    V6,
    relay4State
      ? 1
      : 0
  );
}

// =================================================
// POMPA SEMENTARA 5 DETIK
// =================================================

void pompaSementara(
  int pin
) {

  // =================================================
  // MATIKAN SEMUA TERLEBIH DAHULU
  // =================================================

  semuaRelayOff();

  // =================================================
  // NYALAKAN RELAY
  // =================================================

  digitalWrite(
    pin,
    RELAY_ON
  );

  // =================================================
  // UPDATE STATUS
  // =================================================

  if (pin == RELAY_1) {

    relay1State = true;
  }

  if (pin == RELAY_2) {

    relay2State = true;
  }

  if (pin == RELAY_3) {

    relay3State = true;
  }

  if (pin == RELAY_4) {

    relay4State = true;
  }

  // =================================================
  // SET TIMER 5 DETIK
  // =================================================

  pumpEndTime =
    millis()
    + 5000UL;

  pumpTimerActive = true;

  Serial.println(
    "Pompa ON selama 5 detik"
  );

  // =================================================
  // SYNC BLYNK
  // =================================================

  sinkronisasiBlynk();
}

// =================================================
// POMPA LOOP SEMENTARA 5 DETIK
// =================================================

void pompaLoopSementara() {

  // =================================================
  // MATIKAN SEMUA
  // =================================================

  semuaRelayOff();

  // =================================================
  // RELAY 2 + RELAY 3 ON
  // =================================================

  digitalWrite(
    RELAY_2,
    RELAY_ON
  );

  digitalWrite(
    RELAY_3,
    RELAY_ON
  );

  relay2State = true;

  relay3State = true;

  // =================================================
  // TIMER 5 DETIK
  // =================================================

  pumpEndTime =
    millis()
    + 5000UL;

  pumpTimerActive = true;

  Serial.println(
    "Relay 2 + 3 ON selama 5 detik"
  );

  // =================================================
  // SYNC BLYNK
  // =================================================

  sinkronisasiBlynk();
}

// =================================================
// CEK TIMER POMPA
// =================================================

void cekTimerPompa() {

  if (
    pumpTimerActive &&
    (
      (long)(
        millis()
        - pumpEndTime
      ) >= 0
    )
  ) {

    // =================================================
    // MATIKAN SEMUA
    // =================================================

    semuaRelayOff();

    pumpTimerActive = false;

    Serial.println(
      "Pompa OFF setelah 5 detik"
    );

    // =================================================
    // SYNC BLYNK
    // =================================================

    sinkronisasiBlynk();
  }
}

// =================================================
// MENU SERIAL MONITOR
// =================================================

void tampilkanMenu() {

  Serial.println();

  Serial.println(
    "========== KONTROL POMPA =========="
  );

  Serial.println(
    "1 = Input ON 5 detik"
  );

  Serial.println(
    "2 = Filter ON 5 detik"
  );

  Serial.println(
    "3 = Loop Filter ON 5 detik"
  );

  Serial.println(
    "4 = Outlet ON 5 detik"
  );

  Serial.println();

  Serial.println(
    "========== MANUAL RELAY =========="
  );

  Serial.println(
    "5 = Relay 1 ON"
  );

  Serial.println(
    "6 = Relay 1 OFF"
  );

  Serial.println(
    "7 = Relay 2 ON"
  );

  Serial.println(
    "8 = Relay 2 OFF"
  );

  Serial.println(
    "9 = Relay 3 ON"
  );

  Serial.println(
    "a = Relay 3 OFF"
  );

  Serial.println(
    "b = Relay 4 ON"
  );

  Serial.println(
    "c = Relay 4 OFF"
  );

  Serial.println(
    "0 = Semua Relay OFF"
  );

  Serial.println(
    "=================================="
  );
}
