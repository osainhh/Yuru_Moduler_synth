// ピン定義（XIAO ESP32C3のピン番号）
#define audio_in      D0  // 音声入力 (アナログピン)
#define time_selector D3  // 時間切り替え用ボタン (プルアップ)
#define LED_INDICATOR D1  // インジケータLED (例: D1)
#define LED_REV       D2  // Reverse表示用LED (例: D2)
#define audio_out     D5  // PWMオーディオ出力ピン

const unsigned int d_size = 1900;  // Delay memory buffer size
unsigned int val, d_val, d_time;
int i, j;
byte count = 2;
bool rev = 0;
char delay_data[d_size + 1] = { 0 };    // Delay memory buffer
char delay_data_1[d_size + 1] = { 0 };  // Delay memory buffer

// Arduino公式のESP32用タイマーオブジェクトを作成
hw_timer_t *timer = NULL;

void sampling();
void delay_sound();
void up_time();

// 16384Hzで呼ばれるタイマー割り込みハンドラ
void IRAM_ATTR onTimer() {
  sampling();
}

void setup() {
  Serial.begin(115200);

  // ピンモード設定
  pinMode(time_selector, INPUT_PULLUP);
  pinMode(LED_INDICATOR, OUTPUT);
  pinMode(LED_REV, OUTPUT);

  // 高速PWM DACの設定 (62.5kHz, 8bit分解能)
  ledcAttach(audio_out, 62500, 8);

  // 最新のESP32 Arduinoタイマー設定（周波数を直接16384Hzに指定）
  // 16384Hz周期のタイマーオブジェクトを生成
  timer = timerBegin(16384); 
  
  // 割り込みハンドラ関数をバインド
  timerAttachInterrupt(timer, &onTimer);
  
  // アラーム値を1に設定することで、毎回（16384Hzの周期ごと）割り込みを発生させる
  // 第3引数の true は「オートリロード有効（CTCモードと同じ挙動）」の意味
  timerAlarm(timer, 1, true, 0);

  up_time();
}

void loop() {
  // スイッチが押された時の処理
  if (digitalRead(time_selector) == LOW) {
    up_time();
    digitalWrite(LED_INDICATOR, HIGH); // ボタン押下中にLED点灯
    while (digitalRead(time_selector) == LOW) {
      delay(10); // チャタリング防止とウォッチドッグタイマ対策
    }
  } else {
    digitalWrite(LED_INDICATOR, LOW);
  }

  // Reverseモードのインジケータ表示
  if (rev) {
    digitalWrite(LED_REV, HIGH);
  } else {
    digitalWrite(LED_REV, LOW);
  }
}

void sampling() {
  // アナログ入力からサンプリングして8bitに変換
  val = map(analogRead(audio_in), 0, 900, 0, 255);
  
  // ディレイエフェクトの計算
  delay_sound();
  
  // 高速PWMへのオーディオ出力
  ledcWrite(audio_out, d_val);
}

void delay_sound() {
  i = i + 1; 
  if (i > d_time) i = 0;
  delay_data[i] = val;
  
  if (i == d_time) j = 0;
  delay_data_1[i] = delay_data[i];
  
  j = j + 1; 
  if (j > d_time) j = 0;
  
  if (!rev) d_val = delay_data_1[j];
  if (rev) d_val = delay_data_1[d_time - j];
}

void up_time() {
  // タイム設定変更時は安全のためにタイマーを一時停止
  if (timer) timerStop(timer);
  
  count++;
  if (count > 7) count = 1;
  delay(20);

  switch (count) {
    case 1: d_time = 400;  rev = 0; break; // 63ms
    case 2: d_time = 700;  rev = 0; break; // 110ms
    case 3: d_time = 1000; rev = 0; break; // 158ms
    case 4: d_time = 1300; rev = 0; break; // 205ms
    case 5: d_time = 1600; rev = 0; break; // 253ms
    case 6: d_time = 1900; rev = 0; break; // 300ms
    case 7: d_time = 1500; rev = 1; break; // Reverse speech
  }
  
  // 設定変更後にタイマーを再開
  if (timer) timerStart(timer);
}
