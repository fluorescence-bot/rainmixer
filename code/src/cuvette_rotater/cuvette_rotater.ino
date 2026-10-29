// Cuvette rotator firmware: Arduino Nano (ATmega328P, old bootloader) + Pololu A4988
// + pancake NEMA 17 + limit switch. Turns the cuvette 0-60 degrees.
// Serial protocol (115200 baud, one command per line, replies are <...> lines):
// see README.md next to this file.

#define dirPin 5
#define stepPin 6
#define limitSwitchPin 12  // Switch between D12 and GND, normally open

// --- Motor Configuration ---
const long MICROSTEPS = 16;                    // MS1, MS2, MS3 all tied to VDD
const long STEPS_PER_REV = 200 * MICROSTEPS;   // 3200 steps, 0.1125 deg/step
const unsigned int STEP_US = 500;              // Half-period for moves (~112 deg/s)
const unsigned int HOME_STEP_US = 1000;        // Half-period while homing (~56 deg/s)
const bool TOWARD_SWITCH = HIGH;               // DIR level that turns clockwise toward the switch

// --- Travel Limits ---
const float MIN_DEG = 0.0;
const float MAX_DEG = 60.0;
const float HOME_MAX_DEG = 370.0;              // Just over one full turn: always reaches the switch
const long BACKOFF_STEPS = 123;               // 13.84 deg off the switch; this spot is 0 deg (found with calibrate.py)
const long MAX_STEPS = 541;                    // Steps from 0 deg to 60 deg (found with calibrate.py)
const long RELEASE_MAX_STEPS = 20 * MICROSTEPS; // Max steps to get off a pressed switch
const unsigned long AUTO_HOME_MAX_MS = 10UL * 60 * 1000; // Auto-homing gives up after 10 minutes

// State
long currentStep = 0;
bool isHomed = false;
char lineBuf[32];
byte lineLen = 0;

// Auto-homing: the Nano runs off USB, so it is usually on before the motor's 12 V, and it
// can't see the 12 V. So after start-up (and after a HOME that failed) it keeps looking for
// the switch on its own, one step per loop(): steps without 12 V do nothing, and once the
// 12 V is on the motor turns onto the switch. It only ever steps toward the switch (the arm
// can't get past it), except to get off it when it starts out pressed.
enum AutoHome { AUTO_OFF, AUTO_TOWARD, AUTO_RELEASE };
AutoHome autoHome = AUTO_OFF;
long autoSteps = 0;          // Steps taken trying to get off the switch
bool autoSawOpen = false;    // Switch seen open since the last release (a press only counts after that)
unsigned long autoStartMs = 0;

long degToSteps(float deg) {
  return lround(deg / 360.0 * STEPS_PER_REV);
}

float stepsToDeg(long steps) {
  return steps * 360.0 / STEPS_PER_REV;
}

// Working angles (0-60) are scaled so that MAX_DEG lands exactly on MAX_STEPS
long angleToSteps(float deg) {
  return lround(deg / MAX_DEG * MAX_STEPS);
}

float stepsToAngle(long steps) {
  return steps * MAX_DEG / MAX_STEPS;
}

bool switchPressed() {
  // Pulled up, so pressed = LOW. Require 3 matching reads to ignore motor noise.
  for (byte i = 0; i < 3; i++) {
    if (digitalRead(limitSwitchPin) == HIGH) return false;
    delayMicroseconds(10);
  }
  return true;
}

void pulse(unsigned int halfPeriodUs) {
  digitalWrite(stepPin, HIGH);
  delayMicroseconds(halfPeriodUs);
  digitalWrite(stepPin, LOW);
  delayMicroseconds(halfPeriodUs);
}

void setDirection(bool towardSwitch) {
  digitalWrite(dirPin, towardSwitch ? TOWARD_SWITCH : !TOWARD_SWITCH);
  delayMicroseconds(5);  // Let DIR settle before stepping
}

// Replies with <STATUS:HOMING>, then <STATUS:READY> or <ERROR:...>
void homeMotor() {
  isHomed = false;
  Serial.println(F("<STATUS:HOMING>"));

  // If already sitting on the switch, move off it first
  if (switchPressed()) {
    setDirection(false);
    long steps = 0;
    while (switchPressed()) {
      if (steps >= RELEASE_MAX_STEPS) {
        Serial.println(F("<ERROR:SWITCH_STUCK>"));
        return;
      }
      pulse(HOME_STEP_US);
      steps++;
    }
    delay(200);
  }

  // Approach the switch slowly, with a travel limit
  setDirection(true);
  long maxSteps = degToSteps(HOME_MAX_DEG);
  long steps = 0;
  while (!switchPressed()) {
    if (steps >= maxSteps) {
      Serial.println(F("<ERROR:SWITCH_NOT_FOUND>"));
      return;
    }
    pulse(HOME_STEP_US);
    steps++;
  }

  backOffAndSetZero();
  Serial.println(F("<STATUS:READY>"));
}

// Back off the switch; this becomes 0 deg
void backOffAndSetZero() {
  delay(200);
  setDirection(false);
  for (long i = 0; i < BACKOFF_STEPS; i++) {
    pulse(HOME_STEP_US);
  }
  currentStep = 0;
  isHomed = true;
}

void startAutoHome() {
  autoHome = AUTO_TOWARD;
  autoSteps = 0;
  autoSawOpen = false;
  autoStartMs = millis();
}

// One step of auto-homing. Replies <STATUS:AUTO_HOMED> when it finds the switch, or
// <STATUS:AUTO_HOME_GAVE_UP> after AUTO_HOME_MAX_MS.
void autoHomeStep() {
  if (autoHome == AUTO_OFF) return;
  if (millis() - autoStartMs > AUTO_HOME_MAX_MS) {
    autoHome = AUTO_OFF;
    Serial.println(F("<STATUS:AUTO_HOME_GAVE_UP>"));
    return;
  }
  bool pressed = switchPressed();
  if (autoHome == AUTO_RELEASE) {
    if (!pressed) {
      autoHome = AUTO_TOWARD;
      autoSawOpen = true;
      delay(200);
      return;
    }
    // Without 12 V these steps do nothing; start over so it never gets far from the switch
    if (autoSteps >= RELEASE_MAX_STEPS) autoSteps = 0;
  } else if (pressed) {
    if (autoSawOpen) {
      autoHome = AUTO_OFF;
      backOffAndSetZero();
      Serial.println(F("<STATUS:AUTO_HOMED>"));
    } else {
      autoHome = AUTO_RELEASE;  // Resting on the switch: get off it, then come back
      autoSteps = 0;
    }
    return;
  } else {
    autoSawOpen = true;
  }
  setDirection(autoHome == AUTO_TOWARD);
  pulse(HOME_STEP_US);
  autoSteps++;
}

// Replies with <STATUS:MOVING_TO_<deg>>, then <STATUS:MOVE_COMPLETE> or <ERROR:...>.
// The angle is printed with one decimal (30 -> "30.0"), as the GUI expects.
void moveToDegree(float targetDeg) {
  if (targetDeg < MIN_DEG || targetDeg > MAX_DEG) {
    Serial.println(F("<ERROR:OUT_OF_BOUNDS>"));
    return;
  }

  Serial.print(F("<STATUS:MOVING_TO_"));
  Serial.print(targetDeg, 1);
  Serial.println(F(">"));

  if (!isHomed) {
    Serial.println(F("<ERROR:NOT_HOMED>"));
    return;
  }

  long targetStep = angleToSteps(targetDeg);
  long stepsToMove = targetStep - currentStep;
  bool towardSwitch = stepsToMove < 0;
  setDirection(towardSwitch);
  int direction = towardSwitch ? -1 : 1;
  for (long i = 0; i < labs(stepsToMove); i++) {
    // Safety: the switch should never be hit during a normal move
    if (towardSwitch && switchPressed()) {
      isHomed = false;
      Serial.println(F("<ERROR:LIMIT_HIT>"));
      return;
    }
    pulse(STEP_US);
    currentStep += direction;
  }

  Serial.println(F("<STATUS:MOVE_COMPLETE>"));
}

// Calibration only: move by a relative angle, ignoring the 0-60 range (but never onto
// the switch). Replies <STATUS:AT_<deg>> with the new angle from the current 0.
void jog(float deltaDeg) {
  if (!isHomed) {
    Serial.println(F("<ERROR:NOT_HOMED>"));
    return;
  }
  long steps = degToSteps(deltaDeg);
  bool towardSwitch = steps < 0;
  setDirection(towardSwitch);
  for (long i = 0; i < labs(steps); i++) {
    if (towardSwitch && switchPressed()) {
      Serial.println(F("<ERROR:LIMIT_HIT>"));
      break;
    }
    pulse(HOME_STEP_US);
    currentStep += towardSwitch ? -1 : 1;
  }
  Serial.print(F("<STATUS:AT_"));
  Serial.print(stepsToDeg(currentStep), 2);
  Serial.print(F("_STEP_"));
  Serial.print(currentStep);
  Serial.println(F(">"));
}

void printStatus() {
  if (isHomed) {
    Serial.print(F("<STATUS:AT_"));
    Serial.print(stepsToAngle(currentStep), 1);
    Serial.println(F(">"));
  } else {
    Serial.println(F("<STATUS:NOT_HOMED>"));
  }
  Serial.println(switchPressed() ? F("<STATUS:SWITCH_PRESSED>") : F("<STATUS:SWITCH_OPEN>"));
}

// Accepts things like "30", "15.5", "-5"; rejects "abc" (atof() would give 0)
bool isNumber(const char *s) {
  bool digit = false, dot = false;
  if (*s == '-' || *s == '+') s++;
  for (; *s; s++) {
    if (isdigit(*s)) digit = true;
    else if (*s == '.' && !dot) dot = true;
    else return false;
  }
  return digit;
}

void handleCommand(char *cmd) {
  for (char *p = cmd; *p; p++) *p = toupper(*p);

  if (strcmp(cmd, "HOME") == 0) {
    autoHome = AUTO_OFF;
    homeMotor();
    if (!isHomed) startAutoHome();  // Probably no 12 V yet: keep looking
  } else if (strcmp(cmd, "STATUS") == 0) {
    printStatus();
  } else if (strncmp(cmd, "JOG ", 4) == 0 && isNumber(cmd + 4)) {
    jog(atof(cmd + 4));
  } else if (strcmp(cmd, "PING") == 0) {
    Serial.println(F("<STATUS:PONG>"));  // Lets the GUI tell this board from other serial ports
  } else if (isNumber(cmd)) {
    moveToDegree(atof(cmd));
  } else {
    Serial.println(F("<ERROR:UNKNOWN_COMMAND>"));
  }
}

void setup() {
  pinMode(stepPin, OUTPUT);
  pinMode(dirPin, OUTPUT);
  digitalWrite(stepPin, LOW);
  pinMode(limitSwitchPin, INPUT_PULLUP);

  // The Nano restarts whenever the serial port is opened, so this line is the
  // first thing the GUI sees after connecting. The GUI sends HOME; until then
  // (and when there is no GUI) it homes on its own once the 12 V is on.
  Serial.begin(115200);
  Serial.println(F("<STATUS:BOOTED>"));
  startAutoHome();
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (lineLen > 0) {
        lineBuf[lineLen] = '\0';
        // Trim spaces
        char *cmd = lineBuf;
        while (*cmd == ' ') cmd++;
        char *end = lineBuf + lineLen - 1;
        while (end >= cmd && *end == ' ') *end-- = '\0';
        if (*cmd) handleCommand(cmd);
        lineLen = 0;
      }
    } else if (lineLen < sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = c;
    }
  }
  autoHomeStep();
}
