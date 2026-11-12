#pragma once
#include <Arduino.h>
#include "AlarmClips.h"
#include "RetroSounds.h"

/**
 * 13 大經典復古主題「隨機盲盒」鬧鐘播放引擎
 *
 * 所有動畫邏輯 (物理、碰撞、劇本、隨機堆疊…) 都已在電腦上的 Web 模擬器跑完，
 * 由 simulator/tools/export_firmware.js 錄成固定的畫格序列與蜂鳴器事件表 (AlarmClips.h)。
 * 韌體只做三件事：
 *   1. 隨機挑主題 / 挑劇本 (均等、加權、不連續重複三種規則，全部寫在資料表裡)
 *   2. 依毫秒時間軸切換畫格，原樣畫到 42x9 點陣
 *   3. 依時間軸觸發單音 / 滑音
 *
 * 用法 (主迴圈)：
 *   alarm.update();                  // 每圈呼叫：推進時間軸、觸發音效
 *   RetroSounds::player.update();
 *   frame.clear(); alarm.render(frame, millis()); frame.show();
 *   按下任一鍵：alarm.dismiss();     // 播放 2.5 秒反轉彩蛋後自動結束 (isActive 變回 false)
 */
namespace RetroAlarms {

using AlarmTheme = AlarmClips::ThemeId;

class AlarmEngine {
public:
  bool isActive = false;
  bool isDismissed = false;
  uint8_t currentTheme = 0;
  uint8_t currentClip = 0;

  // 隨機觸發一個主題
  void triggerRandom() {
    trigger(esp_random() % AlarmClips::THEME_COUNT);
  }

  // 觸發指定主題 (AlarmClips::THEME_xxx)
  void trigger(uint8_t theme) {
    if (theme >= AlarmClips::THEME_COUNT) theme = 0;
    memcpy_P(&th, &AlarmClips::THEMES[theme], sizeof(th));
    currentTheme = theme;
    isActive = true;
    isDismissed = false;
    lastClip = 0xFF;
    boostClip = -1;

    RetroSounds::player.stop();
    const uint32_t now = millis();
    startIntro(now);
    startClip(pickClip(), now);
  }

  // 鬧鐘響起時按下任一鍵：解除鬧鈴，播放此劇本的反轉彩蛋
  void dismiss() {
    if (!isActive || isDismissed) return;
    isDismissed = true;
    introActive = false;
    RetroSounds::player.stop();
    AlarmClips::Dismiss d;
    memcpy_P(&d, &th.dismiss[clip.dismiss], sizeof(d));
    startTimeline(d.steps, d.stepCount, d.sounds, d.soundCount, millis());
  }

  // 主迴圈每圈呼叫
  void update() {
    if (!isActive) return;
    const uint32_t now = millis();

    // 1. 推進畫格 (以累加方式計時，循環銜接不漂移)
    while (stepIdx < stepCount && (uint32_t)(now - stepStart) >= step.ms) {
      stepStart += step.ms;
      if (++stepIdx < stepCount) loadStep();
    }
    if (stepIdx >= stepCount) {
      if (isDismissed) {   // 反轉彩蛋播完 -> 回到時鐘
        isActive = false;
        isDismissed = false;
        RetroSounds::player.stop();
        return;
      }
      const uint32_t loopStart = stepStart;   // 下一輪從上一輪結束的那一毫秒接續
      startClip(pickClip(), loopStart);
      if (th.introEachLoop) startIntro(loopStart);
    }

    // 2. 觸發音效 (前奏 + 本輪 / 彩蛋時間軸)
    fireSounds(sounds, soundCount, soundIdx, now - timelineStart);
    if (introActive) introActive = fireSounds(th.intro, th.introCount, introIdx, now - introStart);
  }

  // 繪製目前畫格 (呼叫前請先清空 frame)
  template <typename FrameType>
  void render(FrameType &f, uint32_t /*t*/) {
    if (!isActive) return;
    uint8_t buf[54];
    memcpy_P(buf, AlarmClips::FRAMES[step.frame], sizeof(buf));
    for (uint8_t y = 0; y < 9; y++) {
      const uint8_t *row = &buf[y * 6];
      for (uint8_t x = 0; x < 42; x++) {
        if (row[x >> 3] & (0x80 >> (x & 7))) f.drawPixel(x, y, true);
      }
    }
  }

private:
  AlarmClips::Theme th;
  AlarmClips::Clip clip;
  uint8_t lastClip = 0xFF;
  int8_t boostClip = -1;

  const AlarmClips::ClipStep *steps = nullptr;
  uint16_t stepCount = 0, stepIdx = 0;
  AlarmClips::ClipStep step = {0, 0};
  uint32_t stepStart = 0;

  const AlarmClips::SoundEvent *sounds = nullptr;
  uint16_t soundCount = 0, soundIdx = 0;
  uint32_t timelineStart = 0;

  bool introActive = false;
  uint16_t introIdx = 0;
  uint32_t introStart = 0;

  // 依主題規則挑下一個劇本
  uint8_t pickClip() {
    const uint8_t n = th.clipCount;
    uint8_t pick = 0;
    if (n > 1) {
      if (boostClip >= 0 && esp_random() % 100 < 90) {
        pick = boostClip;
      } else if (th.pick == AlarmClips::PICK_WEIGHTED) {
        uint16_t total = 0;
        for (uint8_t i = 0; i < n; i++) total += clipAt(i).weight;
        uint16_t r = esp_random() % total;
        for (pick = 0; pick < n - 1; pick++) {
          const uint8_t w = clipAt(pick).weight;
          if (r < w) break;
          r -= w;
        }
      } else if (th.pick == AlarmClips::PICK_NO_REPEAT && lastClip < n) {
        pick = esp_random() % (n - 1);
        if (pick >= lastClip) pick++;
      } else {
        pick = esp_random() % n;
      }
    }
    return pick;
  }

  AlarmClips::Clip clipAt(uint8_t i) {
    AlarmClips::Clip c;
    memcpy_P(&c, &th.clips[i], sizeof(c));
    return c;
  }

  void startClip(uint8_t idx, uint32_t at) {
    clip = clipAt(idx);
    currentClip = idx;
    lastClip = idx;
    boostClip = clip.boostNext;
    startTimeline(clip.steps, clip.stepCount, clip.sounds, clip.soundCount, at);
  }

  void startTimeline(const AlarmClips::ClipStep *st, uint16_t stCount, const AlarmClips::SoundEvent *snd, uint16_t sndCount, uint32_t at) {
    steps = st;
    stepCount = stCount;
    stepIdx = 0;
    stepStart = at;
    loadStep();
    sounds = snd;
    soundCount = sndCount;
    soundIdx = 0;
    timelineStart = at;
  }

  void startIntro(uint32_t at) {
    introActive = th.introCount > 0;
    introIdx = 0;
    introStart = at;
  }

  void loadStep() {
    memcpy_P(&step, &steps[stepIdx], sizeof(step));
  }

  // 觸發所有已到時間的音效；回傳 false 代表這張表已播完。落後超過 250ms 的事件直接略過 (與模擬器一致)
  bool fireSounds(const AlarmClips::SoundEvent *list, uint16_t count, uint16_t &idx, uint32_t elapsed) {
    while (idx < count) {
      AlarmClips::SoundEvent e;
      memcpy_P(&e, &list[idx], sizeof(e));
      if (e.t > elapsed) return true;
      idx++;
      if (elapsed - e.t > 250) continue;
      if (e.f2 == 0) RetroSounds::player.tone(e.f1, e.dur);
      else RetroSounds::player.sweep(e.f1, e.f2, e.dur);
    }
    return false;
  }
};

} // namespace RetroAlarms
