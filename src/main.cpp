#include <Arduino.h>

const long pushTime = 3000L;
const long fillTime = 5000L;
const long capTime = 2000L;

const int conveyorPin = 14;
const int capLoaderPin = 27;
const int fillPin = 25;
const int capPin = 33;
const int pushRegisterPin = 32;

// Blue = Trigger, White = Echo
// Used to check if a bottle is loaded
const int triggerPinBottle = 4;
const int echoPinBottle = 2;

// Used to check if the cap loader is full
const int triggerPinCapFull = 23;
const int echoPinCapFull = 22;

// Used to check if the cap loader has a cap available to bottle
const int triggerPinCapLoaded = 15;
const int echoPinCapLoaded = 5;

// 🧮 MATHEMATICAL WARFARE: Calculate mean of readings array
float _calculateMean(float *readings, int count)
{
  float sum = 0;
  for (int i = 0; i < count; i++)
  {
    sum += readings[i];
  }
  return sum / count;
}

void setup()
{
  // Initialize serial communication for debugging
  Serial.begin(115200);

  pinMode(conveyorPin, OUTPUT);
  pinMode(capLoaderPin, OUTPUT);
  pinMode(fillPin, OUTPUT);
  pinMode(capPin, OUTPUT);
  pinMode(pushRegisterPin, OUTPUT);

  pinMode(triggerPinBottle, OUTPUT);
  pinMode(echoPinBottle, INPUT);
  pinMode(triggerPinCapFull, OUTPUT);
  pinMode(echoPinCapFull, INPUT);
  pinMode(triggerPinCapLoaded, OUTPUT);
  pinMode(echoPinCapLoaded, INPUT);

  Serial.println("Pin setup complete");
}

float _getDistance(int triggerPin, int echoPin)
{
  digitalWrite(triggerPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(triggerPin, LOW);
  return pulseIn(echoPin, HIGH);
}

float getBottleDistanceReadings[10] = {0};
float getBottleDistance()
{
  // 🎯 ROLLING AVERAGE BUFFER: Store last 10 readings for noise elimination
  static int readingIndex = 0;
  static int totalReadingCount = 0;

  // 📡 SENSOR RECONNAISSANCE: Get raw distance measurement
  float rawDistance = _getDistance(triggerPinBottle, echoPinBottle);

  // 💾 TACTICAL DATA STORAGE: Always store reading in circular buffer (including zeros)
  getBottleDistanceReadings[readingIndex] = rawDistance;
  readingIndex = (readingIndex + 1) % 10;
  totalReadingCount++;

  // 🎯 INITIALIZATION PROTOCOL: Return default for first 10 readings
  if (totalReadingCount < 10)
  {
    return 1000; // 🛡️ BUFFER WARMING: Return safe default until buffer full
  }

  // ⚡ MEAN CALCULATION: Return average of last 10 readings
  return _calculateMean(getBottleDistanceReadings, 10);
}

float getCapDistanceReadings[10] = {0};
float _getCapDistance(int triggerPin, int echoPin)
{
  // 🎯 ROLLING AVERAGE BUFFER: Store last 10 readings for noise elimination
  static int readingIndex = 0;
  static int totalReadingCount = 0;

  // 📡 SENSOR RECONNAISSANCE: Get raw distance measurement
  float rawDistance = _getDistance(triggerPin, echoPin);

  // 💾 TACTICAL DATA STORAGE: Always store reading in circular buffer (including zeros)
  getCapDistanceReadings[readingIndex] = rawDistance;
  readingIndex = (readingIndex + 1) % 10;
  totalReadingCount++;

  // 🎯 INITIALIZATION PROTOCOL: Return default for first 10 readings
  if (totalReadingCount < 10)
  {
    return 1000; // 🛡️ BUFFER WARMING: Return safe default until buffer full
  }

  // ⚡ MEAN CALCULATION: Return average of last 10 readings
  return _calculateMean(getCapDistanceReadings, 10);
}

float getCapLoadedDistance()
{
  return _getCapDistance(triggerPinCapLoaded, echoPinCapLoaded);
}
float getCapFullDistance()
{
  return _getCapDistance(triggerPinCapFull, echoPinCapFull);
}

bool isCapLoaded()
{

  const int maxDistance = 250;
  float capLoadedDistance = getCapLoadedDistance();
  float capFullDistance = getCapFullDistance();

  bool isCapLoaded = capLoadedDistance < maxDistance;
  bool isCapFull = capFullDistance < maxDistance;

  if (!isCapFull)
  {
    digitalWrite(capLoaderPin, HIGH);
    Serial.println("🏆 CAPPER NOT FULL: Cap loader running");
  }
  else
  {
    digitalWrite(capLoaderPin, LOW);
    Serial.println("🏆 CAPPER FULL: Cap loader stopped");
  }

  if (isCapLoaded)
  {
    Serial.println("🏆 CAP LOADED: Distance = ");
    Serial.println(capLoadedDistance);
    return true;
  }
  else
  {
    Serial.print("🏆 CAP NOT LOADED: Distance = ");
    Serial.println(capLoadedDistance);
    return false;
  }
}

bool isBottleLoaded()
{
  const int maxDistance = 200;
  float distance = getBottleDistance();

  if (distance < maxDistance)
  {
    digitalWrite(conveyorPin, LOW);
    Serial.print("🏆 BOTTLE LOADED: Conveyor stopped, Distance = ");
    Serial.println(distance);
    return true;
  }
  else
  {
    digitalWrite(conveyorPin, HIGH);
    Serial.print("🏆 BOTTLE NOT LOADED: Conveyor running, Distance = ");
    Serial.println(distance);
    return false;
  }
}

void runConveyor()
{
  // ⚔️ CONVEYOR DOMINATION PROTOCOL: Run until bottle is loaded
  Serial.println("🚀 CONVEYOR ACTIVATION: Running until bottle loaded");

  digitalWrite(conveyorPin, HIGH);

  // 🎯 TACTICAL LOOP: Monitor bottle loading status
  while (!isBottleLoaded())
  {
    // 📡 CONTINUOUS RECONNAISSANCE: Check bottle position
    float currentBottleDistance = getBottleDistance();
    Serial.print("🔍 BOTTLE TRACKING: Distance = ");
    Serial.println(currentBottleDistance);

    // ⚡ BRIEF TACTICAL PAUSE: Allow sensor readings to stabilize
    delay(50);
  }

  // 🛡️ MISSION ACCOMPLISHED: Stop conveyor when bottle detected
  digitalWrite(conveyorPin, LOW);
  Serial.println("🏆 BOTTLE LOADED: Conveyor stopped");
}

void pushBottle()
{

  // ⚔️ BOTTLE PUSH PROTOCOL: Execute 3-second push sequence
  Serial.println("🚀 BOTTLE PUSH ACTIVATION: Initiating push sequence");

  // 🎯 TACTICAL ENGAGEMENT: Activate push mechanism
  digitalWrite(pushRegisterPin, HIGH);
  Serial.println("⚡ PUSH MECHANISM: Activated for 3 seconds");

  // ⏱️ TIMED OPERATION: Maintain push for precise duration
  delay(pushTime);

  // 🛡️ MISSION COMPLETE: Deactivate push mechanism
  digitalWrite(pushRegisterPin, LOW);
  Serial.println("🏆 PUSH SEQUENCE COMPLETE: Bottle pushed successfully");
}

void fillBottle()
{
  // ⚔️ BOTTLE FILL PROTOCOL: Execute 5-second fill sequence
  Serial.println("🚀 BOTTLE FILL ACTIVATION: Initiating fill sequence");

  // 🎯 TACTICAL ENGAGEMENT: Activate fill mechanism
  digitalWrite(fillPin, HIGH);
  Serial.println("⚡ FILL MECHANISM: Activated for 5 seconds");

  // ⏱️ TIMED OPERATION: Maintain fill for precise duration
  delay(fillTime);

  // 🛡️ MISSION COMPLETE: Deactivate fill mechanism
  digitalWrite(fillPin, LOW);
  Serial.println("🏆 FILL SEQUENCE COMPLETE: Bottle filled successfully");
}

void fillAndCapBottle()
{

  Serial.println("🚀 FILL AND CAP ACTIVATION: Initiating fill and cap sequence");
  digitalWrite(fillPin, HIGH);
  digitalWrite(capPin, HIGH);

  delay(max(fillTime, capTime));

  digitalWrite(fillPin, LOW);
  digitalWrite(capPin, LOW);

  Serial.println("🏆 FILL AND CAP SEQUENCE COMPLETE: Bottle filled and capped successfully");
}

void capBottle()
{
  // ⚔️ BOTTLE CAP PROTOCOL: Execute 2-second cap sequence
  Serial.println("🚀 BOTTLE CAP ACTIVATION: Initiating cap sequence");

  // 🎯 TACTICAL ENGAGEMENT: Activate cap mechanism
  digitalWrite(capPin, HIGH);
  Serial.println("⚡ CAP MECHANISM: Activated for 2 seconds");

  // ⏱️ TIMED OPERATION: Maintain cap for precise duration
  delay(capTime);

  // 🛡️ MISSION COMPLETE: Deactivate cap mechanism
  digitalWrite(capPin, LOW);
  Serial.println("🏆 CAP SEQUENCE COMPLETE: Bottle capped successfully");
}

// State management
int currentAction = 1;

void loop()
{
  // 🔍 SENSOR RECONNAISSANCE: Gather distance intelligence from both sensors
  float bottleDistance = getBottleDistance();
  float capDistance = getCapLoadedDistance();

  bool isCapLoadedValue = isCapLoaded();
  bool isBottleLoadedValue = isBottleLoaded();

  // 📡 TACTICAL COMMUNICATION: Report sensor data to command center
  Serial.print("Bottle Distance: ");
  Serial.print(bottleDistance);
  Serial.print(" | Cap Distance: ");
  Serial.println(capDistance);

  if (isCapLoadedValue == false)
  {
    Serial.println("⚔️ CAP NOT LOADED: Activating cap loader");
    digitalWrite(capLoaderPin, HIGH);
  }
  else
  {
    digitalWrite(capLoaderPin, LOW);
  }

  if (isBottleLoadedValue == false)
  {
    Serial.println("⚔️ BOTTLE NOT LOADED: Activating conveyor");
    digitalWrite(conveyorPin, HIGH);
  }
  else
  {
    digitalWrite(conveyorPin, LOW);
  }

  if (currentAction == 1)
  {
    for (int i = 0; i < 3; i++)
    {
      while (isBottleLoaded() == false)
      {
        isCapLoaded(); // Stop the capper if we are waiting on bottle
        delay(100);
      }
      pushBottle();
      while (isCapLoaded() == false)
      {
        isBottleLoaded(); // Stop the conveyor if we are waiting on cap
        delay(100);
      }
      capBottle();
    }
    currentAction = 2;
  }
  else if (currentAction == 2)
  {
    fillBottle();
    currentAction = 3;
  }
  else if (currentAction == 3)
  {
    while (isBottleLoaded() != true)
    {
      isCapLoaded(); // Stop the capper if we are waiting on bottle
      delay(100);
    }
    pushBottle();
    currentAction = 4;
  }
  else if (currentAction == 4)
  {
    fillBottle();
    currentAction = 5;
  }
  else if (currentAction == 5)
  {
    while (isBottleLoaded() != true)
    {
      isCapLoaded(); // Stop the capper if we are waiting on bottle
      delay(100);
    }
    pushBottle();
    currentAction = 6;
  }
  else if (currentAction == 6)
  {
    while (isCapLoaded() != true)
    {
      isBottleLoaded(); // Stop the conveyor if we are waiting on cap
      delay(100);
    }
    fillAndCapBottle();
    currentAction = 1;
  }

  delay(100);
}