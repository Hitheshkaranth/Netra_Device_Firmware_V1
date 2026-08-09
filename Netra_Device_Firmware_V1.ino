#include <Arduino.h>
#include <Wire.h>

// PCBCupid Glyph C6 wiring.
constexpr uint8_t ECHO_PIN = 15;  // Board pin D15, through divider.
constexpr uint8_t TRIG_PIN = 20;  // Board pin D20.
constexpr uint8_t SDA_PIN = 4;    // Board pin SDA.
constexpr uint8_t SCL_PIN = 5;    // Board pin SCL.

constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t ECHO_TIMEOUT_US = 30000;  // About 5 m maximum round trip.
constexpr uint32_t SAMPLE_PERIOD_MS = 250;

// The HC-SR04 is only specified from 2 cm to 400 cm. The echo timeout allows
// more than that, so readings beyond these bounds are reported as out of spec
// rather than passed off as measurements.
constexpr float RANGE_MIN_CM = 2.0f;
constexpr float RANGE_MAX_CM = 400.0f;

// Approximated range: the median of the last few valid readings. A median
// rejects the isolated wild values this sensor produces on a glancing or soft
// target, which a mean would instead smear across several samples.
constexpr uint8_t RANGE_WINDOW = 5;

// Proximity bands for the approximated range, nearest first.
constexpr float ZONE_IMMEDIATE_CM = 25.0f;
constexpr float ZONE_NEAR_CM = 60.0f;
constexpr float ZONE_MID_CM = 120.0f;
constexpr float ZONE_FAR_CM = 250.0f;

enum RangeStatus : uint8_t {
  RANGE_STATUS_OK,
  RANGE_STATUS_TIMEOUT,
  RANGE_STATUS_OUT_OF_SPEC,
};

uint8_t mpuAddress = 0;
uint8_t mpuWhoAmI = 0;

float rangeWindow[RANGE_WINDOW];
uint8_t rangeWriteIndex = 0;
uint8_t rangeFilled = 0;

bool writeMpuRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(mpuAddress);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readMpuRegisters(uint8_t firstRegister, uint8_t *data, size_t length) {
  Wire.beginTransmission(mpuAddress);
  Wire.write(firstRegister);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  const size_t received = Wire.requestFrom(mpuAddress, static_cast<uint8_t>(length),
                                           static_cast<uint8_t>(true));
  if (received != length) {
    return false;
  }

  for (size_t i = 0; i < length; ++i) {
    data[i] = Wire.read();
  }
  return true;
}

// A genuine MPU6050 reports WHO_AM_I 0x68, but boards sold as one are often
// MPU6500/MPU9250 parts or clones reporting something else entirely. Their
// accelerometer, gyroscope and configuration registers are compatible, so an
// unexpected value is reported and accepted rather than failing the probe.
uint8_t findMpu6050(bool verbose = false) {
  const uint8_t candidates[] = {0x68, 0x69};
  for (uint8_t address : candidates) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() != 0) {
      continue;
    }

    mpuAddress = address;
    uint8_t whoAmI = 0;
    if (!readMpuRegisters(0x75, &whoAmI, 1)) {
      if (verbose) {
        Serial.printf("0x%02X acknowledged its address but WHO_AM_I would not read\n",
                      address);
      }
      mpuAddress = 0;
      continue;
    }

    if (verbose && whoAmI != 0x68 && whoAmI != 0x69) {
      Serial.printf("0x%02X reports WHO_AM_I 0x%02X, not a genuine MPU6050; using it anyway\n",
                    address, whoAmI);
    }
    mpuWhoAmI = whoAmI;
    return address;
  }
  mpuAddress = 0;
  return 0;
}

bool initialiseMpu6050(bool verbose = false) {
  if (findMpu6050(verbose) == 0) {
    return false;
  }

  // Wake up, use the X gyroscope PLL, select +/-2 g and +/-250 deg/s.
  return writeMpuRegister(0x6B, 0x01) &&
         writeMpuRegister(0x1C, 0x00) &&
         writeMpuRegister(0x1B, 0x00) &&
         writeMpuRegister(0x1A, 0x03);
}

bool readMpu6050(float &ax, float &ay, float &az,
                 float &temperatureC, float &gx, float &gy, float &gz) {
  uint8_t raw[14];
  if (!readMpuRegisters(0x3B, raw, sizeof(raw))) {
    return false;
  }

  const int16_t rawAx = static_cast<int16_t>((raw[0] << 8) | raw[1]);
  const int16_t rawAy = static_cast<int16_t>((raw[2] << 8) | raw[3]);
  const int16_t rawAz = static_cast<int16_t>((raw[4] << 8) | raw[5]);
  const int16_t rawTemp = static_cast<int16_t>((raw[6] << 8) | raw[7]);
  const int16_t rawGx = static_cast<int16_t>((raw[8] << 8) | raw[9]);
  const int16_t rawGy = static_cast<int16_t>((raw[10] << 8) | raw[11]);
  const int16_t rawGz = static_cast<int16_t>((raw[12] << 8) | raw[13]);

  ax = rawAx / 16384.0f;
  ay = rawAy / 16384.0f;
  az = rawAz / 16384.0f;
  // The MPU6050 and the MPU6500/MPU9250 parts sold under its name scale
  // temperature differently. Accelerometer and gyroscope sensitivities match,
  // so only this conversion has to branch.
  if (mpuWhoAmI == 0x68 || mpuWhoAmI == 0x69) {
    temperatureC = rawTemp / 340.0f + 36.53f;
  } else {
    temperatureC = rawTemp / 333.87f + 21.0f;
  }
  gx = rawGx / 131.0f;
  gy = rawGy / 131.0f;
  gz = rawGz / 131.0f;
  return true;
}

// Report every I2C address that answers, so a missing MPU6050 can be told
// apart from one strapped to an unexpected address or a dead bus.
void scanI2cBus() {
  uint8_t found = 0;
  Serial.print("I2C scan:");
  for (uint8_t address = 0x08; address < 0x78; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.printf(" 0x%02X", address);
      ++found;
    }
  }
  if (found == 0) {
    Serial.print(" no devices responded");
  }
  Serial.println();
}

bool readDistanceCm(float &distanceCm, uint32_t &echoUs) {
  // A burst still echoing from the previous cycle would make pulseIn measure
  // the tail of that one instead of ours.
  const uint32_t idleStart = micros();
  while (digitalRead(ECHO_PIN) == HIGH) {
    if (micros() - idleStart > ECHO_TIMEOUT_US) {
      echoUs = 0;
      return false;
    }
  }

  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  echoUs = pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT_US);
  if (echoUs == 0) {
    return false;
  }

  // At approximately 20 C, sound travels 0.0343 cm/us. Divide by two
  // because the measured time includes the trip to the target and back.
  distanceCm = echoUs * 0.0343f / 2.0f;
  return true;
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1000);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  Wire.begin(SDA_PIN, SCL_PIN);
  // 100 kHz. Clone breakout boards on jumper wires often ACK an address at
  // 400 kHz but fail longer register reads.
  Wire.setClock(100000);

  Serial.println();
  Serial.println("========================================");
  Serial.println("  Project NETRA");
  Serial.println("  ESP32-C6 sensor node");
  Serial.println("  HC-SR04 ultrasonic + MPU6050 IMU");
  Serial.println("========================================");
  if (initialiseMpu6050(true)) {
    Serial.printf("MPU6050 found at I2C address 0x%02X\n", mpuAddress);
  } else {
    Serial.println("ERROR: MPU6050 not found at 0x68 or 0x69");
    scanI2cBus();
  }
  Serial.println("distance_cm,echo_us,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,temp_c,status");
}

void loop() {
  float distanceCm = NAN;
  uint32_t echoUs = 0;
  float ax = NAN, ay = NAN, az = NAN;
  float gx = NAN, gy = NAN, gz = NAN, temperatureC = NAN;

  const bool rangeOk = readDistanceCm(distanceCm, echoUs);
  bool imuOk = false;
  if (mpuAddress != 0) {
    imuOk = readMpu6050(ax, ay, az, temperatureC, gx, gy, gz);
  } else {
    initialiseMpu6050();  // Allow a sensor connected after boot to recover.
  }

  // One buffered write per sample. The USB CDC endpoint drops whole writes
  // when its FIFO is full, which shredded fields when this was 22 separate
  // print calls. Unset values are already NAN and format as "nan".
  char line[192];
  snprintf(line, sizeof(line),
           "%.2f,%lu,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%.2f,%s|%s\r\n",
           distanceCm, static_cast<unsigned long>(echoUs), ax, ay, az,
           gx, gy, gz, temperatureC,
           rangeOk ? "RANGE_OK" : "RANGE_TIMEOUT",
           imuOk ? "IMU_OK" : "IMU_ERROR");
  Serial.print(line);

  delay(SAMPLE_PERIOD_MS);
}
