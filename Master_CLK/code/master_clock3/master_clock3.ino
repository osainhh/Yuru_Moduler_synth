
// =====================================================
// Modular Synth Master Clock
// Arduino Nano
// - BPM設定: ロータリーエンコーダ
// - パルス幅設定: SW2で設定画面へ，エンコーダで変更
// - ON/OFF: SW1
// - クロック出力: D6
// - OLED表示: SSD1306 I2C (SDA=A4, SCL=A5)
// =====================================================

#include <MsTimer2.h>
#include <Rotary.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ピン設定
#define CLOCK_OUT_PIN   6   // クロック出力ピン
#define ENC_A_PIN       2   // ロータリーエンコーダ A相 (割り込み対応ピン)
#define ENC_B_PIN       3   // ロータリーエンコーダ B相 (割り込み対応ピン)
#define SW1_PIN         5   // クロック ON/OFF スイッチ
#define SW2_PIN         4   // 設定画面切り替えスイッチ

Rotary rotary = Rotary(ENC_A_PIN, ENC_B_PIN);

// =====================================================
// OLED設定
// =====================================================
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// =====================================================
// BPM / パルス幅 設定
// =====================================================
#define BPM_MIN           20
#define BPM_MAX          300
#define BPM_INIT         120

#define PULSE_WIDTH_MIN   10   // % (最小パルス幅)
#define PULSE_WIDTH_MAX   90   // % (最大パルス幅)
#define PULSE_WIDTH_STEP   1   // % (変更ステップ)
#define PULSE_WIDTH_INIT  50   // % (初期パルス幅)

// 動作モード
enum Mode {
  MODE_MAIN,    // 通常画面 (BPM表示 / エンコーダでBPM変更)
  MODE_SETTING  // 設定画面 (パルス幅表示 / エンコーダでパルス幅変更)
};

volatile int   bpm           = BPM_INIT;
volatile int   pulseWidthPct = PULSE_WIDTH_INIT;  // パルス幅
volatile bool  clockRunning  = false;
volatile bool  clockState    = false;  // 現在の出力状態
volatile bool  needTimerUpdate = false;

Mode currentMode = MODE_MAIN;

// スイッチ用 (チャタリング防止)
bool          sw1LastState = HIGH;
bool          sw2LastState = HIGH;
unsigned long sw1LastTime  = 0;
unsigned long sw2LastTime  = 0;
#define SW_DEBOUNCE_MS  50

// OLED更新フラグ
volatile bool needDisplayUpdate = true;

// 1周期 [ms]    = 60000 / BPM
// HIGH時間 [ms] = 1周期 × pulseWidthPct / 100
// LOW時間  [ms] = 1周期 × (100 - pulseWidthPct) / 100
volatile unsigned long highTimeMs = 0;
volatile unsigned long lowTimeMs  = 0;

void calcPulseTimes() {
  float periodMs = 60000.0 / (float)bpm;
  highTimeMs = (unsigned long)(periodMs * pulseWidthPct / 100.0);
  lowTimeMs  = (unsigned long)(periodMs * (100 - pulseWidthPct) / 100.0);
  if (highTimeMs < 1) highTimeMs = 1;
  if (lowTimeMs  < 1) lowTimeMs  = 1;
}

// MsTimer2 割り込み
void clockISR() {
  if (!clockRunning) {
    clockState = false;
    digitalWrite(CLOCK_OUT_PIN, LOW);
    return;
  }

  clockState = !clockState;
  digitalWrite(CLOCK_OUT_PIN, clockState ? HIGH : LOW);

  MsTimer2::set(clockState ? highTimeMs : lowTimeMs, clockISR);
  MsTimer2::start();
}

// タイマー再起動 (BPM/パルス幅変更時・loop()内から呼ぶ)
void restartTimer() {
  calcPulseTimes();
  clockState = false;
  digitalWrite(CLOCK_OUT_PIN, LOW);
  MsTimer2::set(lowTimeMs, clockISR);
  MsTimer2::start();
}

// ロータリーエンコーダ 割り込み
void encoderISR() {
  unsigned char result = rotary.process();
  if (result == DIR_NONE) return;

  if (currentMode == MODE_MAIN) {
    // --- BPM変更 ---
    if (result == DIR_CW) {
      if (bpm < BPM_MAX) { bpm++; needTimerUpdate = true; needDisplayUpdate = true; }
    } else {
      if (bpm > BPM_MIN) { bpm--; needTimerUpdate = true; needDisplayUpdate = true; }
    }
  } else {
    // --- パルス幅変更 ---
    if (result == DIR_CW) {
      if (pulseWidthPct + PULSE_WIDTH_STEP <= PULSE_WIDTH_MAX) {
        pulseWidthPct += PULSE_WIDTH_STEP;
        needTimerUpdate   = true;
        needDisplayUpdate = true;
      }
    } else {
      if (pulseWidthPct - PULSE_WIDTH_STEP >= PULSE_WIDTH_MIN) {
        pulseWidthPct -= PULSE_WIDTH_STEP;
        needTimerUpdate   = true;
        needDisplayUpdate = true;
      }
    }
  }
}

// OLED表示: メイン画面
void drawMainDisplay() {
  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(28, 0);
  display.print(F("MASTER CLOCK"));
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  // BPM
  display.setTextSize(1);
  display.setCursor(0, 15);
  display.print(F("BPM"));

  display.setTextSize(3);
  int x = 20;
  if (bpm < 100) x = 38;
  if (bpm < 10)  x = 56;
  display.setCursor(x, 24);
  display.print(bpm);

  // パルス幅
  display.setTextSize(1);
  display.setCursor(78, 15);
  display.print(F("PW:"));
  display.print(pulseWidthPct);
  display.print(F("%"));

  // ステータス
  display.drawLine(0, 52, 127, 52, SSD1306_WHITE);
  display.setTextSize(1);
  if (clockRunning) {
    display.setCursor(30, 56);
    display.print(F("[  RUNNING  ]"));
  } else {
    display.setCursor(30, 56);
    display.print(F("[  STOPPED  ]"));
  }

  display.display();
}

// OLED表示: パルス幅設定画面
void drawSettingDisplay() {
  display.clearDisplay();

  // タイトル
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 0);
  display.print(F("- PULSE WIDTH SET -"));
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  // パルス幅値
  display.setTextSize(1);
  display.setCursor(0, 14);
  display.print(F("WIDTH"));

  display.setTextSize(3);
  display.setCursor(20, 22);
  display.print(pulseWidthPct);
  display.setTextSize(2);
  display.print(F(" %"));

  // バーグラフ
  display.drawLine(0, 50, 127, 50, SSD1306_WHITE);
  int barWidth = map(pulseWidthPct, PULSE_WIDTH_MIN, PULSE_WIDTH_MAX, 0, 124);
  display.fillRect(2, 54, barWidth, 8, SSD1306_WHITE);
  display.drawRect(2, 54, 124, 8, SSD1306_WHITE);

  display.display();
}

void updateDisplay() {
  if (currentMode == MODE_MAIN) {
    drawMainDisplay();
  } else {
    drawSettingDisplay();
  }
}

void setup() {
  pinMode(CLOCK_OUT_PIN, OUTPUT);
  digitalWrite(CLOCK_OUT_PIN, LOW);

  pinMode(SW1_PIN, INPUT_PULLUP);
  pinMode(SW2_PIN, INPUT_PULLUP);

  rotary.begin();

  attachInterrupt(digitalPinToInterrupt(ENC_A_PIN), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B_PIN), encoderISR, CHANGE);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    for (;;);
  }
  display.clearDisplay();
  display.display();

  calcPulseTimes();
  MsTimer2::set(lowTimeMs, clockISR);
  MsTimer2::start();

  updateDisplay();
}

void loop() {
  unsigned long now = millis();

  // クロック ON/OFF
  bool sw1Current = digitalRead(SW1_PIN);
  if (sw1Current == LOW && sw1LastState == HIGH) {
    if (now - sw1LastTime > SW_DEBOUNCE_MS) {
      sw1LastTime  = now;
      clockRunning = !clockRunning;
      if (!clockRunning) {
        clockState = false;
        digitalWrite(CLOCK_OUT_PIN, LOW);
      }
      needDisplayUpdate = true;
    }
  }
  sw1LastState = sw1Current;

  // 設定画面トグル
  bool sw2Current = digitalRead(SW2_PIN);
  if (sw2Current == LOW && sw2LastState == HIGH) {
    if (now - sw2LastTime > SW_DEBOUNCE_MS) {
      sw2LastTime  = now;
      currentMode  = (currentMode == MODE_MAIN) ? MODE_SETTING : MODE_MAIN;
      needDisplayUpdate = true;
    }
  }
  sw2LastState = sw2Current;

  // タイマー再設定
  if (needTimerUpdate) {
    needTimerUpdate = false;
    restartTimer();
  }

  // OLED更新
  if (needDisplayUpdate) {
    needDisplayUpdate = false;
    updateDisplay();
  }
}
