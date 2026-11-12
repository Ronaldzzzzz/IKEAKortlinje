#pragma once
#include <Arduino.h>

/**
 * 8-bit 復古音效庫與非阻塞式聲音引擎
 * 專為 XIAO ESP32-C6 (GPIO 21, D3) 無源蜂鳴器打造
 */
namespace RetroSounds {

// =========================================================================
// 標準音樂頻率定義 (Hz)
// =========================================================================
#define NOTE_B0  31
#define NOTE_C1  33
#define NOTE_CS1 35
#define NOTE_D1  37
#define NOTE_DS1 39
#define NOTE_E1  41
#define NOTE_F1  44
#define NOTE_FS1 46
#define NOTE_G1  49
#define NOTE_GS1 52
#define NOTE_A1  55
#define NOTE_AS1 58
#define NOTE_B1  62
#define NOTE_C2  65
#define NOTE_CS2 69
#define NOTE_D2  73
#define NOTE_DS2 78
#define NOTE_E2  82
#define NOTE_F2  87
#define NOTE_FS2 93
#define NOTE_G2  98
#define NOTE_GS2 104
#define NOTE_A2  110
#define NOTE_AS2 117
#define NOTE_B2  123
#define NOTE_C3  131
#define NOTE_CS3 139
#define NOTE_D3  147
#define NOTE_DS3 156
#define NOTE_E3  165
#define NOTE_F3  175
#define NOTE_FS3 185
#define NOTE_G3  196
#define NOTE_GS3 208
#define NOTE_A3  220
#define NOTE_AS3 233
#define NOTE_B3  247
#define NOTE_C4  262
#define NOTE_CS4 277
#define NOTE_D4  294
#define NOTE_DS4 311
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_FS4 370
#define NOTE_G4  392
#define NOTE_GS4 415
#define NOTE_A4  440
#define NOTE_AS4 466
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_CS5 554
#define NOTE_D5  587
#define NOTE_DS5 622
#define NOTE_E5  659
#define NOTE_F5  698
#define NOTE_FS5 740
#define NOTE_G5  784
#define NOTE_GS5 831
#define NOTE_A5  880
#define NOTE_AS5 932
#define NOTE_B5  988
#define NOTE_C6  1047
#define NOTE_CS6 1109
#define NOTE_D6  1175
#define NOTE_DS6 1245
#define NOTE_E6  1319
#define NOTE_F6  1397
#define NOTE_FS6 1480
#define NOTE_G6  1568
#define NOTE_GS6 1661
#define NOTE_A6  1760
#define NOTE_AS6 1865
#define NOTE_B6  1976
#define NOTE_C7  2093
#define NOTE_CS7 2217
#define NOTE_D7  2349
#define NOTE_DS7 2489
#define NOTE_E7  2637
#define NOTE_F7  2794
#define NOTE_FS7 2960
#define NOTE_G7  3136
#define NOTE_GS7 3322
#define NOTE_A7  3520
#define NOTE_AS7 3729
#define NOTE_B7  3951
#define NOTE_C8  4186
#define NOTE_DS8 4978

struct SoundNote {
  uint16_t freq;     // 頻率 (Hz)
  uint16_t duration; // 持續時間 (ms)
  uint16_t pause;    // 停頓間隔 (ms)
};

// =========================================================================
// 經典復古旋律簡譜表 (PROGMEM)
// =========================================================================

// 鬧鐘主題音效已預先錄成事件表 (AlarmClips.h)，怪獸音效在 Tamagotchi.h；此處僅保留共用的呼叫音

// 寵物呼叫 (1200Hz -> 2400Hz 雙音 x3)
const SoundNote SFX_TAMA_CALL[] PROGMEM = {
  { 1200, 100, 40 }, { 2400, 140, 100 },
  { 1200, 100, 40 }, { 2400, 140, 100 },
  { 1200, 100, 40 }, { 2400, 140, 0 }
};


// =========================================================================
// 非阻塞式非同期播放引擎 (Non-blocking Sound Player)
// =========================================================================
class SoundPlayer {
public:
  uint8_t buzzerPin = 21; // XIAO ESP32-C6 預設 D3 (GPIO 21)
  bool isAttached = false;

  void begin(uint8_t pin = 21) {
    buzzerPin = pin;
    pinMode(buzzerPin, OUTPUT);
    digitalWrite(buzzerPin, LOW);
    // ESP32 3.x 支援 ledcAttach
    isAttached = ledcAttach(buzzerPin, 1000, 10);
  }

  // 播放一段預定義樂譜
  void play(const SoundNote *melody, size_t count, bool loop = false) {
    currentMelody = melody;
    noteCount = count;
    currentIndex = 0;
    isLooping = loop;
    isPlaying = true;
    playNextNote();
  }

  // 即時播放單音 (非阻塞)；會中斷進行中的滑音
  void tone(uint16_t freq, uint16_t durationMs) {
    sweepDur = 0;
    singleToneFreq = freq;
    singleToneEnd = millis() + durationMs;
    ledcWriteTone(buzzerPin, freq);
  }

  // 指數滑音 (非阻塞)：freq 由 fromHz 以等比例變化到 toHz，與 Web Audio exponentialRamp 一致
  void sweep(uint16_t fromHz, uint16_t toHz, uint16_t durationMs) {
    if (durationMs == 0 || fromHz == 0 || toHz == 0) return;
    sweepFrom = fromHz;
    sweepRatio = (float)toHz / (float)fromHz;
    sweepStart = millis();
    sweepDur = durationMs;
    singleToneFreq = fromHz;
    singleToneEnd = sweepStart + durationMs;
    lastSweepFreq = fromHz;
    if (isAttached) ledcWriteTone(buzzerPin, fromHz);
  }

  // 靜音
  void stop() {
    sweepDur = 0;
    isPlaying = false;
    currentMelody = nullptr;
    singleToneFreq = 0;
    if (isAttached) {
      ledcWriteTone(buzzerPin, 0);
    }
  }

  // 主迴圈定期呼叫 (通常每 10~20ms 呼叫一次)
  void update() {
    if (!isAttached) return;

    uint32_t now = millis();

    // 處理單音 / 滑音過期
    if (singleToneFreq > 0 && now >= singleToneEnd) {
      singleToneFreq = 0;
      sweepDur = 0;
      ledcWriteTone(buzzerPin, 0);
    } else if (sweepDur > 0) {
      float p = (float)(now - sweepStart) / (float)sweepDur;
      uint16_t f = (uint16_t)(sweepFrom * powf(sweepRatio, p));
      if (f != lastSweepFreq && f > 0) {
        lastSweepFreq = f;
        ledcWriteTone(buzzerPin, f);
      }
    }

    // 處理樂譜播放
    if (isPlaying) {
      if (isSounding && now >= noteEndTime) {
        // 音符結束，進入 pause 間隔
        ledcWriteTone(buzzerPin, 0);
        isSounding = false;
      } else if (!isSounding && now >= pauseEndTime) {
        // 間隔結束，播放下一個音符
        currentIndex++;
        if (currentIndex >= noteCount) {
          if (isLooping) {
            currentIndex = 0;
            playNextNote();
          } else {
            stop();
          }
        } else {
          playNextNote();
        }
      }
    }
  }

  bool active() const { return isPlaying || (singleToneFreq > 0); }

private:
  const SoundNote *currentMelody = nullptr;
  size_t noteCount = 0;
  size_t currentIndex = 0;
  bool isLooping = false;
  bool isPlaying = false;
  bool isSounding = false;
  uint32_t noteEndTime = 0;
  uint32_t pauseEndTime = 0;

  uint16_t singleToneFreq = 0;
  uint32_t singleToneEnd = 0;

  uint16_t sweepFrom = 0, sweepDur = 0, lastSweepFreq = 0;
  float sweepRatio = 1.0f;
  uint32_t sweepStart = 0;

  void playNextNote() {
    if (!currentMelody || currentIndex >= noteCount) return;

    SoundNote note;
    memcpy_P(&note, &currentMelody[currentIndex], sizeof(SoundNote));

    if (note.freq > 0) {
      ledcWriteTone(buzzerPin, note.freq);
    } else {
      ledcWriteTone(buzzerPin, 0);
    }

    uint32_t now = millis();
    isSounding = true;
    noteEndTime = now + note.duration;
    pauseEndTime = noteEndTime + note.pause;
  }
};

extern SoundPlayer player;

} // namespace RetroSounds
