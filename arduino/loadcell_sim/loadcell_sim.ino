// Тензодатчик HX711 + протокол пульта (115200 8N1, строки с \n):
//   Arduino → пульт: V <int>
//   Пульт → Arduino: T <int>
//   Arduino → пульт: OK T <int>
// Pin 5 → HIGH, если значение > порога (из HMI).

#include <GyverHX711.h>

GyverHX711 sensor(8, 9, HX_GAIN64_A);
// HX_GAIN128_A - канал А усиление 128
// HX_GAIN32_B - канал B усиление 32
// HX_GAIN64_A - канал А усиление 64

constexpr int kOutPin = 5;
constexpr long kMinValue = -10000L;
constexpr long kMaxValue = 3000000L;
constexpr long kDefaultThreshold = 1100000L;

long g_value = 0;
long g_threshold = kDefaultThreshold;
String g_line;

long clampValue(long v)
{
  if (v < kMinValue)
    return kMinValue;
  if (v > kMaxValue)
    return kMaxValue;
  return v;
}

void applyOutput()
{
  digitalWrite(kOutPin, (g_value > g_threshold) ? HIGH : LOW);
}

void processLine(const String &line)
{
  String s = line;
  s.trim();
  if (s.length() == 0)
    return;

  if (s.startsWith("T ") || s.startsWith("t ")) {
    g_threshold = clampValue(s.substring(2).toInt());
    applyOutput();
    Serial.print(F("OK T "));
    Serial.println(g_threshold);
  }
}

void setup()
{
  Serial.begin(115200);
  pinMode(kOutPin, OUTPUT);
  digitalWrite(kOutPin, LOW);

  // Готовность датчика перед тарированием.
  delay(500);
  sensor.tare();
  delay(500);
}

void loop()
{
  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (g_line.length() > 0) {
        processLine(g_line);
        g_line = "";
      }
    } else if (g_line.length() < 64) {
      g_line += c;
    }
  }

  if (!sensor.available())
    return;

  g_value = sensor.read();
  applyOutput();

  Serial.print(F("V "));
  Serial.println(g_value);
}
