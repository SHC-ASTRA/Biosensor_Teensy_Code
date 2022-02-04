#include "main.h"

#define ONBOARD_ADC
#ifdef ONBOARD_ADC
const int ADC_CS = 10;
#endif

#ifndef ONBOARD_ADC
#define TEENSY_ADC_
const int VIDEO_P = 24;
ADC *adc = new ADC();
#endif

const int PWR_DOWN = 32;
const int DATA_RDY = 30;
const int CLR_DATA = 31;
const int SHUTTER = 29;
const int CLR_PIX = 28;
const int READ = 9;

const int T_WAKEUP = 12;
const int T_FLUSH = 1;
const int T_CDS = 1;
const int T_SHIFT = 1;
const int T_READ = 2;

const int numPixels = 1024;
uint16_t frameBuffer[numPixels];

const int baud = 115200;

unsigned long exposure = 250;

void setup()
{
  Serial.begin(baud);
  while (!Serial)
  {
    delay(10);
  }
  //Serial.println(F("Starting up"));

  if (adcSetup())
  {
    //Serial.println(F("ADC Initialized"));
  }
  else
  {
    //Serial.println(F("ADC Init Failure"));
    exit(0);
  }

  if (epc901Setup())
  {
    //Serial.println(F("EPC_901 Initialized"));
  }
  else
  {
    //Serial.println(F("EPC_901 Init Failure"));
    exit(0);
  }

  epc901Wake();
  takePicture(10);
  epc901Sleep();

  //printFramebufferASCII();

  delay(10);

  //Serial.println(F("Setup Complete!"));
  delay(1000);

  epc901Wake();
}

unsigned long lastReadMillis = 0;
void loop()
{
  if (millis() > lastReadMillis + 100)
  {
    lastReadMillis = millis();
    takePicture(1);
    takePicture(exposure);
    printFramebufferBinary();
  }

  while (Serial.available() > 0)
  {
    if (Serial.read() == 0x0C)
    {
      delay(1);
      if(Serial.read() == 0xAB)
      delay(1);
      exposure = Serial.parseInt();
    }
  }
  delay(1);
}

bool adcSetup()
{
#ifdef ONBOARD_ADC
  pinMode(ADC_CS, OUTPUT);
  digitalWrite(ADC_CS, HIGH);
  SPI.begin();
#endif

#ifdef TEENSY_ADC_
  pinMode(VIDEO_P, INPUT);

  adc->adc0->setAveraging(16);                                          // set number of averages
  adc->adc0->setResolution(16);                                         // set bits of resolution
  adc->adc0->setConversionSpeed(ADC_CONVERSION_SPEED::VERY_HIGH_SPEED); // change the conversion speed
  adc->adc0->setSamplingSpeed(ADC_SAMPLING_SPEED::VERY_HIGH_SPEED);     // change the sampling speed
#endif

  return adcReadTwoBytes() > 0;
}

uint16_t adcReadTwoBytes()
{
#ifdef ONBOARD_ADC
  digitalWrite(ADC_CS, LOW);
  uint8_t byte1 = SPI.transfer(0);
  uint8_t byte2 = SPI.transfer(0);
  digitalWrite(ADC_CS, HIGH);

  return ((uint16_t)byte1) * 256 + (uint16_t)byte2;

#endif

#ifdef TEENSY_ADC_

  return (uint16_t)adc->adc0->analogRead(VIDEO_P);
#endif
}

bool epc901Setup()
{
  pinMode(PWR_DOWN, OUTPUT); // HIGH --> power down (sleep) mode
  pinMode(CLR_DATA, OUTPUT); // active high, rising edge trigger
  pinMode(SHUTTER, OUTPUT);
  pinMode(CLR_PIX, OUTPUT); // rising edge trigger
  pinMode(READ, OUTPUT);    // read clock

  epc901Wake();

  epc901FlushBuffer();

  bool earlyRdy = digitalRead(DATA_RDY);

  epc901Capture(1000);

  bool Rdy = digitalRead(DATA_RDY);
  for (int i = 1; i < 1000 && !Rdy; i++)
  {
    Rdy = digitalRead(DATA_RDY);
    delayMicroseconds(1);
  }

  epc901Sleep();

  return Rdy && !earlyRdy;
}

void epc901Configure()
{
  digitalWrite(PWR_DOWN, HIGH); // chip off

  delayMicroseconds(20);

  pinMode(DATA_RDY, OUTPUT);
  digitalWrite(DATA_RDY, HIGH);
  digitalWrite(CLR_DATA, LOW);
  digitalWrite(CLR_PIX, LOW);
  digitalWrite(READ, LOW);

  digitalWrite(PWR_DOWN, LOW); // chip on

  delayMicroseconds(20);

  digitalWrite(DATA_RDY, LOW);
  pinMode(DATA_RDY, INPUT);
}

void epc901FlushBuffer()
{
  digitalWrite(CLR_DATA, HIGH);
  delayMicroseconds(T_FLUSH);
  digitalWrite(CLR_DATA, LOW);
  delayMicroseconds(T_FLUSH);
}

void epc901Wake()
{
  epc901Configure();
  delayMicroseconds(T_WAKEUP);
}

void epc901Sleep()
{
  digitalWrite(PWR_DOWN, HIGH);
  delayMicroseconds(T_WAKEUP);
}

void epc901Capture(long exposure) // exposure time [us]
{
  digitalWrite(SHUTTER, HIGH);
  delayMicroseconds(T_FLUSH + exposure);
  digitalWrite(SHUTTER, LOW);
  delayMicroseconds(T_SHIFT);
}

void adcReadFrame()
{
  sendReadPulse();
  for (int i = 0; i < 3; i++)
  {
    sendReadClock();
  }

  for (int i = 0; i < numPixels; i++)
  {
    sendReadClock();
    frameBuffer[i] = adcReadTwoBytes();
  }
}

void sendReadPulse()
{
  digitalWrite(READ, HIGH);
  delayMicroseconds(T_READ);
  digitalWrite(READ, LOW);
  delayMicroseconds(T_READ);
}

void sendReadClock()
{
  digitalWrite(READ, HIGH);
  delayMicroseconds(T_READ);
  digitalWrite(READ, LOW);
  delayMicroseconds(T_READ);
}

void takePicture(long exposure)
{
  epc901FlushBuffer();
  epc901Capture(exposure);
  adcReadFrame();
}

void printFramebufferASCII()
{
  for (int i = 0; i < numPixels; i++)
  {
    Serial.println(frameBuffer[i]);
  }
}

void printFramebufferBinary()
{
  uint16_t dataLen = 2 * numPixels + 7;
  uint8_t syncWord = 0xAF;
  uint8_t syncWord2 = 0xA6;
  uint8_t checksum = syncWord + syncWord2 + highByte(dataLen) + lowByte(dataLen) + highByte(exposure) + lowByte(exposure);
  Serial.write(syncWord);
  Serial.write(syncWord2);
  Serial.write(lowByte(dataLen));
  Serial.write(highByte(dataLen));
  Serial.write(lowByte(exposure));
  Serial.write(highByte(exposure));
  for (int i = 0; i < numPixels; i++)
  {
    checksum += highByte(frameBuffer[i]) + lowByte(frameBuffer[i]);
    Serial.write(lowByte(frameBuffer[i]));
    Serial.write(highByte(frameBuffer[i]));
  }
  Serial.write(checksum);
}
