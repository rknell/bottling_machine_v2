#include <Arduino.h>

// Operation control flags - set to false to disable specific operations
const bool enableFilling = true;  // Set to false to skip filling operations
const bool enableCapping = false; // Set to false to skip capping operations

const long pushTime = 3000L;
const long fillTime = 32000L;
const long capTime = 2000L;
const long postPushDelay = 3000L;          // Delay after push operation before resuming
const long postFillDelay = 1000L;          // Delay after fill operation before next push
const long bottlePositioningDelay = 1000L; // Delay to keep conveyor running after bottle detection

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
const int triggerPinCapLoaded = 18;
const int echoPinCapLoaded = 5;

const int rollingAverageCount = 5;
const int maxSensorBuffers = 10; // Maximum number of different sensor buffers supported

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

float _getRawUltrasonicSensorReading(int triggerPin, int echoPin)
{
  digitalWrite(triggerPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(triggerPin, LOW);
  return pulseIn(echoPin, HIGH);
}

// 🎯 UNIVERSAL SENSOR BUFFER SYSTEM: Map-like structure for per-pin rolling averages
struct SensorBuffer
{
  float readings[rollingAverageCount];
  int readingIndex;
  int totalReadingCount;
};

// 🏛️ SENSOR BUFFER REGISTRY: Static storage for multiple sensor buffers
static SensorBuffer sensorBuffers[maxSensorBuffers]; // Support up to maxSensorBuffers different trigger pins
static int registeredPins[maxSensorBuffers];         // Track which pins are registered
static int bufferCount = 0;                          // Number of registered buffers

// 🔍 BUFFER RECONNAISSANCE: Find or create buffer for specific trigger pin
SensorBuffer *_getSensorBuffer(int triggerPin)
{
  // 🎯 EXISTING BUFFER SEARCH: Check if pin already registered
  for (int i = 0; i < bufferCount; i++)
  {
    if (registeredPins[i] == triggerPin)
    {
      return &sensorBuffers[i];
    }
  }

  // 🚀 NEW BUFFER CREATION: Register new pin if space available
  if (bufferCount < maxSensorBuffers)
  {
    registeredPins[bufferCount] = triggerPin;
    SensorBuffer *newBuffer = &sensorBuffers[bufferCount];
    // 🛡️ BUFFER INITIALIZATION: Zero out new buffer
    for (int i = 0; i < rollingAverageCount; i++)
    {
      newBuffer->readings[i] = 0;
    }
    newBuffer->readingIndex = 0;
    newBuffer->totalReadingCount = 0;
    bufferCount++;
    return newBuffer;
  }

  // 💀 BUFFER OVERFLOW PROTECTION: Return first buffer as fallback
  return &sensorBuffers[0];
}

float _getUltrasonicSensorDistance(int triggerPin, int echoPin)
{
  // 🎯 BUFFER ACQUISITION: Get dedicated buffer for this trigger pin
  SensorBuffer *buffer = _getSensorBuffer(triggerPin);

  // 📡 SENSOR RECONNAISSANCE: Get raw distance measurement
  float rawDistance = _getRawUltrasonicSensorReading(triggerPin, echoPin);

  // 💾 TACTICAL DATA STORAGE: Store reading in pin-specific circular buffer
  buffer->readings[buffer->readingIndex] = rawDistance;
  buffer->readingIndex = (buffer->readingIndex + 1) % rollingAverageCount;
  buffer->totalReadingCount++;

  // 🎯 INITIALIZATION PROTOCOL: Return default for first rollingAverageCount readings
  if (buffer->totalReadingCount < rollingAverageCount)
  {
    return 1000; // 🛡️ BUFFER WARMING: Return safe default until buffer full
  }

  // ⚡ MEAN CALCULATION: Return average of last rollingAverageCount readings for this specific pin
  float mean = _calculateMean(buffer->readings, rollingAverageCount);
  if (mean < 0.01)
  {
    return 1000;
  }
  return mean;
}

float getBottleDistance()
{
  return _getUltrasonicSensorDistance(triggerPinBottle, echoPinBottle);
}

float getCapLoadedDistance()
{
  // 🔧 OPERATION CHECK: Return safe distance when capping is disabled
  if (!enableCapping)
  {
    return 50; // Return distance indicating cap is loaded
  }
  return _getUltrasonicSensorDistance(triggerPinCapLoaded, echoPinCapLoaded);
}
float getCapFullDistance()
{
  // 🔧 OPERATION CHECK: Return safe distance when capping is disabled
  if (!enableCapping)
  {
    return 50; // Return distance indicating cap loader is full
  }
  return _getUltrasonicSensorDistance(triggerPinCapFull, echoPinCapFull);
}

bool isCapLoaded()
{
  // 🔧 OPERATION CHECK: Assume cap is always loaded when capping is disabled
  if (!enableCapping)
  {
    Serial.println("🚫 CAPPING DISABLED: Assuming cap is loaded");
    digitalWrite(capLoaderPin, LOW); // Stop cap loader when capping disabled
    return true;
  }

  const int maxDistance = 160;
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

void loadBottle()
{
  // ⚔️ CONVEYOR DOMINATION PROTOCOL: Run until bottle is loaded
  Serial.println("🚀 CONVEYOR ACTIVATION: Running until bottle loaded");

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

  Serial.println("🏆 BOTTLE LOADED: Conveyor stopped");
}

void capBottle()
{
  // 🔧 OPERATION CHECK: Skip if capping is disabled
  if (!enableCapping)
  {
    Serial.println("🚫 CAPPING DISABLED: Skipping cap sequence");
    return;
  }

  while (isCapLoaded() == false)
  {
    isBottleLoaded();
    delay(50);
  }

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

void pushBottle()
{

  // ⚔️ BOTTLE PUSH PROTOCOL: Execute push sequence
  Serial.println("🚀 BOTTLE PUSH ACTIVATION: Initiating push sequence");

  while (isBottleLoaded() == false)
  {
    isCapLoaded();
    delay(50);
  }

  // 🎯 BOTTLE POSITIONING: Keep conveyor running to position bottle properly
  digitalWrite(conveyorPin, HIGH);
  Serial.print("🎯 BOTTLE POSITIONING: Conveyor running for ");
  Serial.print(bottlePositioningDelay / 1000.0);
  Serial.println(" seconds to position bottle");
  delay(bottlePositioningDelay);

  // 🛑 CONVEYOR STOP: Ensure conveyor is stopped during push operation
  digitalWrite(conveyorPin, LOW);
  Serial.println("🛑 CONVEYOR STOPPED: For push operation");

  // 🎯 TACTICAL ENGAGEMENT: Activate push mechanism
  digitalWrite(pushRegisterPin, HIGH);
  Serial.print("⚡ PUSH MECHANISM: Activated for ");
  Serial.print(pushTime / 1000.0);
  Serial.println(" seconds");

  // ⏱️ TIMED OPERATION: Maintain push for precise duration
  delay(pushTime);

  // 🛡️ MISSION COMPLETE: Deactivate push mechanism
  digitalWrite(pushRegisterPin, LOW);
  Serial.println("🏆 PUSH SEQUENCE COMPLETE: Bottle pushed successfully");

  // ⏳ POST-PUSH DELAY: Wait before resuming operations
  Serial.print("⏳ POST-PUSH DELAY: Waiting ");
  Serial.print(postPushDelay / 1000.0);
  Serial.println(" seconds before resuming operations");
  delay(postPushDelay);
  Serial.println("✅ POST-PUSH DELAY COMPLETE: Resuming operations");

  capBottle();
}

void fillBottle()
{
  // 🔧 OPERATION CHECK: Skip if filling is disabled
  if (!enableFilling)
  {
    Serial.println("🚫 FILLING DISABLED: Skipping fill sequence");
    return;
  }

  // ⚔️ BOTTLE FILL PROTOCOL: Execute 5-second fill sequence
  Serial.println("🚀 BOTTLE FILL ACTIVATION: Initiating fill sequence");

  while (isBottleLoaded() == false)
  {
    isCapLoaded();
    delay(50);
  }

  // 🎯 TACTICAL ENGAGEMENT: Activate fill mechanism
  digitalWrite(fillPin, HIGH);
  Serial.print("⚡ FILL MECHANISM: Activated for ");
  Serial.print(fillTime / 1000.0);
  Serial.println(" seconds");

  // ⏱️ TIMED OPERATION: Maintain fill for precise duration
  delay(fillTime);

  // 🛡️ MISSION COMPLETE: Deactivate fill mechanism
  digitalWrite(fillPin, LOW);
  Serial.println("🏆 FILL SEQUENCE COMPLETE: Bottle filled successfully");

  // ⏳ POST-FILL DELAY: Wait before next push operation
  Serial.print("⏳ POST-FILL DELAY: Waiting ");
  Serial.print(postFillDelay / 1000.0);
  Serial.println(" seconds before next operation");
  delay(postFillDelay);
  Serial.println("✅ POST-FILL DELAY COMPLETE: Ready for next operation");
}

// State management
// int currentAction = 1;

// void loop()
// {
//   // 🔍 SENSOR RECONNAISSANCE: Gather distance intelligence from both sensors
//   float bottleDistance = getBottleDistance();
//   float capDistance = getCapLoadedDistance();

//   bool isCapLoadedValue = isCapLoaded();
//   bool isBottleLoadedValue = isBottleLoaded();

//   // 📡 TACTICAL COMMUNICATION: Report sensor data to command center
//   Serial.print("Bottle Distance: ");
//   Serial.print(bottleDistance);
//   Serial.print(" | Cap Distance: ");
//   Serial.println(capDistance);

//   if (isCapLoadedValue == false)
//   {
//     Serial.println("⚔️ CAP NOT LOADED: Activating cap loader");
//     digitalWrite(capLoaderPin, HIGH);
//   }
//   else
//   {
//     digitalWrite(capLoaderPin, LOW);
//   }

//   if (isBottleLoadedValue == false)
//   {
//     Serial.println("⚔️ BOTTLE NOT LOADED: Activating conveyor");
//     digitalWrite(conveyorPin, HIGH);
//   }
//   else
//   {
//     digitalWrite(conveyorPin, LOW);
//   }

//   if (currentAction == 1)
//   {
//     for (int i = 0; i < 3; i++)
//     {
//       while (isBottleLoaded() == false)
//       {
//         isCapLoaded(); // Stop the capper if we are waiting on bottle
//         delay(50);
//       }
//       pushBottle();
//     }
//     currentAction = 2;
//   }
//   else if (currentAction == 2)
//   {
//     fillBottle();
//     currentAction = 3;
//   }
//   else if (currentAction == 3)
//   {
//     while (isBottleLoaded() != true)
//     {
//       isCapLoaded(); // Stop the capper if we are waiting on bottle
//       delay(50);
//     }
//     pushBottle();
//     currentAction = 4;
//   }
//   else if (currentAction == 4)
//   {
//     fillBottle();
//     currentAction = 5;
//   }
//   else if (currentAction == 5)
//   {
//     while (isBottleLoaded() != true && isCapLoaded() != true)
//     {
//       delay(50);
//     }
//     pushBottle();
//     pushBottle();
//     currentAction = 6;
//   }
//   else if (currentAction == 6)
//   {
//     while (isCapLoaded() != true)
//     {
//       isBottleLoaded(); // Stop the conveyor if we are waiting on cap
//       delay(50);
//     }
//     fillBottle();
//     currentAction = 1;
//   }
//   delay(50);
// }

// Double filler sequence

// 1. Push 3 bottles
// 2. Fill bottle
// 3. Push bottle
// 4. Fill bottle

void loop()
{
  while (isBottleLoaded() == false || isCapLoaded() == false)
  {
    delay(50);
  }
  pushBottle();
  pushBottle();
  pushBottle();

  // Conditionally fill bottle if filling is enabled
  if (enableFilling)
  {
    fillBottle();
  }

  pushBottle();

  // Conditionally fill bottle again if filling is enabled
  if (enableFilling)
  {
    fillBottle();
  }
}