const int ADC_SIGNAL = 4;
const int ADC_ZERO   = 5;
const int ADC_FULL   = 6;

void setup() {
  Serial.begin(115200);
  delay(1000);

  analogReadResolution(12);

  analogSetPinAttenuation(ADC_SIGNAL, ADC_11db);
  analogSetPinAttenuation(ADC_ZERO,   ADC_11db);
  analogSetPinAttenuation(ADC_FULL,   ADC_11db);
}

void loop() {
  int signal = analogRead(ADC_SIGNAL);
  int zero   = analogRead(ADC_ZERO);
  int full   = analogRead(ADC_FULL);

  int signal_percent = signal*100.0/full;

  Serial.print("signal V:");
  Serial.print(signal*3.3/4095);

  Serial.print(",signal %:");
  Serial.print(signal_percent);

  Serial.print(",zero:");
  Serial.print(zero);

  Serial.print(",full:");
  Serial.println(full);

  delay(10);
}