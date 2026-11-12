#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <time.h>
#include "MonsterSprites.h"
#include "RetroSounds.h"

/**
 * 像素怪獸養成 (Monster Pet) —— 逐行移植自 simulator/index.html 的「5. 像素怪獸養成」
 *
 * 規則層 (tickMinute)：每「遊戲分鐘」結算一次數值，與畫面完全分離
 * 表現層 (render)   ：只畫圖，不改任何狀態 (走動、置中、音效都在 update 處理)
 * 不懲罰設計：沒有死亡 / 逃家 / 退化；數值最低到 0 只會鬧脾氣與呼叫；生病一針見效；長時間離線最多只補算 24 小時
 *
 * 世界時間：以 NTP 校時後的真實時間 (time()) 為準，夜間 23:00 ~ 07:00 睡覺 (時區請在主程式以 configTzTime 設定)。
 *   - 尚未校時：以 millis() 推進；校時後自動接回真實時間
 *   - 關機 / 離線回來：補算經過的分鐘數 (靜音、不發起挑戰)，上限 24 小時
 * 圖鑑 (最多 12 隻) 與數值存在 NVS (Preferences 命名空間 "monster")：事件發生時、以及每 10 遊戲分鐘存一次。
 *
 * 用法 (主迴圈)：
 *   Tamagotchi::pet.begin();                 // setup()
 *   Tamagotchi::pet.quiet = alarm.isActive;  // 鬧鐘響時不讓寵物自動發聲 (呼叫 / 挑戰 / 進化)
 *   Tamagotchi::pet.update();                // 每圈呼叫 (不論目前顯示哪個模式，寵物世界都在走)
 *   frame.clear(); Tamagotchi::pet.render(frame, millis()); frame.show();   // 寵物模式時
 *   按鍵：Tamagotchi::pet.key(1..4)          // 暫定：K1 餵正餐、K2 訓練 / 出拳 / 接受挑戰、K3 打掃或打針、K4 狀態頁 (最後一頁圖鑑書架)
 *   序列埠測試：Tamagotchi::pet.debug("hunger" | "mood" | "poop" | "sick" | "power" | "snack" | "battle" | "challenge" | "evolve" | "fill" | "reset" | "skip 60")
 *
 * FrameType 需提供 drawPixel(x, y, bool) (相容 IkeaMatrix::Frame)；false = 熄滅 (怪獸以不透明方式畫在便便前面)。
 */
namespace Tamagotchi {

using namespace MonsterSprites;

// ---------------------------------------------------------------------------
// 常數 (與模擬器相同)
// ---------------------------------------------------------------------------
static const int COLS = 42, ROWS = 9;
static const int AREA_X0 = 6, AREA_X1 = 41;          // 怪獸活動區 (x = 0..5 為抽象刻度)
static const uint16_t HUNGER_MIN = 45;                // 每 45 遊戲分鐘飽食 -1 (睡覺時減半)
static const uint16_t MOOD_MIN = 60;                  // 每 60 遊戲分鐘心情 -1 (睡覺時減半)
static const uint16_t POOP_MOOD_MIN = 60;             // 便便放著不清：每 60 分鐘額外心情 -1
static const uint8_t DIGEST_MEALS = 3;                // 每吃 3 次 (正餐或零食)，30 分鐘後拉一坨便便
static const uint32_t EVOLVE_AT[4] = { 2 * 60, 2 * 1440, 6 * 1440, 13 * 1440 }; // 蛋 2 小時孵化；滿 2 / 6 / 13 天進化
static const uint16_t CATCHUP_MAX_MIN = 24 * 60;      // 離線補算上限
static const uint8_t DEX_MAX = 12;                    // 圖鑑上限 = 物種數
static const uint8_t POOP_W = 6;                      // 每坨便便至少相隔 6 格
static const int8_t STATUS_PAGES = 6;                 // 飽食 → 心情 → 力量 → 天數 → 戰績 → 圖鑑書架 → 返回
static const int8_t SHELF_PAGE = 5;
static const uint32_t STATUS_IDLE_MS = 4000, SHELF_IDLE_MS = 8000;
static const uint32_t BATTLE_INTRO = 900, TURN_MS = 900, BATTLE_END = 2200, BATTLE_TRAVEL = 450;
static const uint8_t MAX_TURNS = 14;
static const uint8_t ENEMY_POWER[5][2] = { { 0, 0 }, { 0, 10 }, { 0, 20 }, { 5, 45 }, { 15, 75 } }; // 各階段野生對手力量範圍
static const uint8_t STAGE_BONUS[5] = { 0, 0, 10, 25, 40 };                                          // 各階段攻擊力加成
static const uint32_t VALID_EPOCH = 1700000000UL;     // 小於此值 = 尚未 NTP 校時

// 音效 (與模擬器 playMelody 相同：{頻率, 長度, 停頓})
const RetroSounds::SoundNote MEL_EAT[] PROGMEM = { { 800, 40, 120 }, { 800, 40, 120 }, { 1000, 60, 0 } };
const RetroSounds::SoundNote MEL_MEDICINE[] PROGMEM = { { 1200, 60, 40 }, { 1500, 60, 40 }, { 1800, 120, 0 } };
const RetroSounds::SoundNote MEL_EVOLVE[] PROGMEM = { { 523, 100, 20 }, { 659, 100, 20 }, { 784, 100, 20 }, { 1047, 300, 0 } };
const RetroSounds::SoundNote MEL_CHALLENGE[] PROGMEM = { { 880, 60, 40 }, { 880, 60, 40 }, { 1320, 120, 0 } };
const RetroSounds::SoundNote MEL_WIN[] PROGMEM = { { 784, 90, 20 }, { 988, 90, 20 }, { 1319, 200, 0 } };
const RetroSounds::SoundNote MEL_LOSE[] PROGMEM = { { 392, 160, 30 }, { 330, 160, 30 }, { 262, 300, 0 } };
const RetroSounds::SoundNote MEL_NEW_EGG[] PROGMEM = { { 523, 80, 20 }, { 784, 80, 20 }, { 1047, 160, 0 } };
const RetroSounds::SoundNote MEL_SWITCH[] PROGMEM = { { 784, 60, 20 }, { 1047, 120, 0 } };
#define TAMA_PLAY(m) RetroSounds::player.play(m, sizeof(m) / sizeof(m[0]))

// ---------------------------------------------------------------------------
// 存檔資料 (每隻怪獸一筆，圖鑑整包寫進 NVS)
// ---------------------------------------------------------------------------
struct PetData {
  uint8_t form;            // MonsterSprites::FormId / speciesForm()
  int8_t species;          // -1 = 還沒孵化 (孵化瞬間抽物種)
  int8_t pendingForm;      // 等待播放的孵化 / 進化，-1 = 無
  uint8_t hunger, mood;    // 0 ~ 4
  uint8_t power;           // 0 ~ 99
  uint8_t poop;            // 0 ~ 3
  uint8_t sick;
  uint32_t ageMin;
  uint32_t clock;          // 寵物世界時間 (epoch 秒)
  uint16_t hungerAcc, moodAcc, poopAcc;   // 半分鐘為單位 (睡覺時每分鐘 +1，醒著 +2)
  uint16_t hungerZeroMin, poopLongMin;
  uint8_t digest;
  int8_t poopTimer;        // -1 = 沒在消化
  uint8_t poopXs[3];       // 每坨便便的 x
  uint16_t trains, wins, losses, snacks, meals;
};

enum ActionType : uint8_t { ACT_NONE, ACT_EAT, ACT_REFUSE, ACT_CLEAN, ACT_MEDICINE, ACT_HATCH, ACT_EVOLVE, ACT_TRAIN, ACT_BATTLE };

struct Action {
  ActionType type = ACT_NONE;
  uint32_t start = 0;
  uint8_t item = ICON_FOOD;                 // 進食：ICON_FOOD / ICON_CAKE
  uint8_t from = 0;                         // 孵化 / 進化前的造型
  bool recentred = false;                   // 進化後置中 (只做一次)
  bool aiming = false;                      // 訓練：true = 力量條來回中，false = 已出拳
  uint32_t shootAt = 0; bool ok = false; uint8_t lvl = 0;
  uint8_t poopBefore[3]; uint8_t poopBeforeN = 0;   // 打掃前的便便位置
  uint8_t enemy = FORM_SLIME;               // 對戰
  uint8_t nTurns = 0; uint16_t turnMe = 0, turnHit = 0;   // 第 i 回合：bit i = 我方出手 / 命中
  bool win = false, done = false; int8_t shot = -1;
};

class MonsterPet {
public:
  bool soundOn = true;     // 呼叫 / 挑戰 / 進化音效開關
  bool quiet = false;      // 主程式在鬧鐘響時設為 true：寵物不自動發聲

  // 時間來源 (預設 time())；測試時可換掉
  uint32_t (*timeSource)() = nullptr;

  void begin() {
    prefs.begin("monster", false);
    load();
    resetView();
    lastWorldMs = millis();
  }

  // ------------------------------------------------------------------
  // 每圈呼叫
  // ------------------------------------------------------------------
  void update() {
    const uint32_t now = millis();
    advanceWorld(now);

    Action &a = act;
    if (a.type != ACT_NONE) {
      const uint32_t t = now - a.start;
      uint32_t dur = 0;
      switch (a.type) {
        case ACT_EAT: dur = 1600; break;
        case ACT_REFUSE: dur = 900; break;
        case ACT_CLEAN: dur = 1300; break;
        case ACT_MEDICINE: dur = 1500; break;
        case ACT_HATCH: dur = 2600; break;
        case ACT_EVOLVE: dur = 3200; break;
        default: break;
      }
      // 原本在模擬器畫面裡做的狀態變更，移到這裡
      if (a.type == ACT_HATCH && t >= 1800) view.x = 18;
      if (a.type == ACT_EVOLVE && t >= 2400 && !a.recentred) {
        a.recentred = true;
        const int w = formW(M->form);
        view.x = clampi(view.x + floorDiv(formW(a.from) - w, 2), AREA_X0, AREA_X1 - w + 1);
      }
      if (dur && t >= dur) a.type = ACT_NONE;
      if (a.type == ACT_TRAIN && ((a.aiming && t > 5000) || (!a.aiming && now - a.shootAt > 1100))) {
        if (a.aiming) M->trains++;   // 逾時 = 沒打中
        a.type = ACT_NONE;
      }
      if (a.type == ACT_BATTLE) {
        const uint32_t endT = BATTLE_INTRO + a.nTurns * TURN_MS;
        if (t >= BATTLE_INTRO && t < endT) {   // 發射音效 (每回合一次)
          const int8_t ti = (t - BATTLE_INTRO) / TURN_MS;
          if ((t - BATTLE_INTRO) % TURN_MS < BATTLE_TRAVEL && a.shot != ti) {
            a.shot = ti;
            RetroSounds::player.tone((a.turnMe >> ti) & 1 ? 900 : 700, 30);
          }
        }
        if (!a.done && t >= endT) {
          a.done = true;
          if (a.win) { M->wins++; M->mood = min<uint8_t>(4, M->mood + 1); M->power = min<uint8_t>(99, M->power + 1); }
          else M->losses++;
          if (a.win) TAMA_PLAY(MEL_WIN); else TAMA_PLAY(MEL_LOSE);
          save();
        }
        if (t >= endT + BATTLE_END) a.type = ACT_NONE;
      }
    }
    if (view.challenge && (int32_t)(now - view.challengeUntil) > 0) view.challenge = false;
    if (view.statusPage >= 0 && (int32_t)(now - view.statusUntil) > 0) view.statusPage = -1;

    // 孵化 / 進化：醒著且空閒時播放
    if (M->pendingForm >= 0 && act.type == ACT_NONE && (M->form == FORM_EGG || !sleeping())) {
      const uint8_t from = M->form;
      M->form = M->pendingForm;
      M->pendingForm = -1;
      startAction(from == FORM_EGG ? ACT_HATCH : ACT_EVOLVE);
      act.from = from;
      if (autoSfx()) TAMA_PLAY(MEL_EVOLVE);
      save();
    }

    // 呼叫：白天出現新需求時響一次「嗶-波」×3 (至少間隔 10 秒)
    const bool need = hasNeed() && stage() > 0;
    if (need && !sleeping() && !view.called && now - view.lastCall > 10000) {
      view.called = true;
      view.lastCall = now;
      if (autoSfx()) TAMA_PLAY(RetroSounds::SFX_TAMA_CALL);
    }
    if (!need) view.called = false;

    // 閒晃 (模擬器在一般畫面才走動：非狀態頁、非蛋、沒有動作或在吃 / 拒絕 / 打針)
    if (view.statusPage < 0 && M->form != FORM_EGG &&
        (act.type == ACT_NONE || act.type == ACT_EAT || act.type == ACT_REFUSE || act.type == ACT_MEDICINE))
      idleStep(now);
  }

  // ------------------------------------------------------------------
  // 按鍵 (1..4)
  // ------------------------------------------------------------------
  void key(uint8_t k) {
    if (view.statusPage == SHELF_PAGE) { shelfKey(k); return; }
    if (view.statusPage >= 0 && k != 4) view.statusPage = -1;
    if (k == 1) feed(false);
    if (k == 2) train();
    if (k == 3) clean();
    if (k == 4) status();
  }

  // 餵食 (snack = 蛋糕，心情 +1；目前沒有對應按鍵，可由 debug("snack") 觸發)
  void feed(bool snack) {
    if (busy() || stage() == 0 || sleeping()) return;
    if (!snack && M->hunger >= 4) { startAction(ACT_REFUSE); RetroSounds::player.tone(300, 120); return; }
    if (snack) { M->mood = min<uint8_t>(4, M->mood + 1); M->snacks++; }
    else { M->hunger = min<uint8_t>(4, M->hunger + 1); M->meals++; }
    M->digest++;
    startAction(ACT_EAT);
    act.item = snack ? ICON_CAKE : ICON_FOOD;
    TAMA_PLAY(MEL_EAT);
    save();
  }

  // 以下為唯讀資訊 (序列埠除錯 / Web 介面用)
  const PetData &data() const { return *M; }
  uint8_t dexCount() const { return dexN; }
  uint8_t activeIndex() const { return active; }
  uint8_t stage() const { return formStage(M->form); }
  bool sleeping() const { return stage() > 0 && isNight(); }

  // ------------------------------------------------------------------
  // 序列埠測試指令 (同模擬器測試面板)
  // ------------------------------------------------------------------
  void debug(const char *cmd) {
    if (!strcmp(cmd, "hunger") && M->hunger > 0) M->hunger--;
    if (!strcmp(cmd, "mood") && M->mood > 0) M->mood--;
    if (!strcmp(cmd, "poop")) addPoop();
    if (!strcmp(cmd, "sick")) M->sick = 1;
    if (!strcmp(cmd, "power")) M->power = min(99, M->power + 5);
    if (!strcmp(cmd, "snack")) feed(true);
    if (!strcmp(cmd, "battle")) startBattle(rnd(2) ? FORM_BAT : FORM_SLIME);
    if (!strcmp(cmd, "challenge") && stage() >= 1) { view.challenge = true; view.challengeEnemy = rnd(2) ? FORM_BAT : FORM_SLIME; view.challengeUntil = millis() + 20000; }
    if (!strcmp(cmd, "evolve") && stage() <= 3 && M->pendingForm < 0) {
      if (M->ageMin < EVOLVE_AT[stage()] - 1) M->ageMin = EVOLVE_AT[stage()] - 1;
      skipMinutes(1);
    }
    if (!strncmp(cmd, "skip ", 5)) skipMinutes(atoi(cmd + 5));
    if (!strcmp(cmd, "reset")) { const uint32_t c = M->clock; dexN = 1; active = 0; M = &dex[0]; newEgg(*M, c); resetView(); }
    if (!strcmp(cmd, "fill") && dexN < DEX_MAX) {   // 圖鑑直接加入一隻已長成完全體的新物種 (不切換)
      PetData m; newEgg(m, M->clock);
      m.species = pickSpecies();
      m.form = speciesForm(m.species, 4);
      m.ageMin = EVOLVE_AT[3] + 1440UL * rnd(7);
      m.power = rnd(60); m.wins = rnd(15); m.losses = rnd(10);
      // 插在目前這隻之前 (若目前這隻在最後)，與模擬器相同
      const uint8_t pos = dexN - (active == dexN - 1 ? 1 : 0);
      for (int i = dexN; i > pos; i--) dex[i] = dex[i - 1];
      dex[pos] = m;
      dexN++;
      if (active >= pos) active++;
      M = &dex[active];
    }
    if (!strcmp(cmd, "sound")) soundOn = !soundOn;
    save();
  }

  // 快轉 n 遊戲分鐘 (世界時間領先真實時間 n 分鐘，只保留在 RAM)
  void skipMinutes(uint32_t n) {
    for (uint32_t i = 0; i < n; i++) tickMinute(false);
    debugOffset += n * 60;
    save();
  }

  // ------------------------------------------------------------------
  // 繪製 (只讀狀態)
  // ------------------------------------------------------------------
  template <typename F>
  void render(F &f, uint32_t now) {
    const Action &a = act;
    // 全螢幕：狀態頁 / 對戰
    if (view.statusPage == SHELF_PAGE) { renderShelf(f, now); return; }
    if (view.statusPage >= 0) {
      const int p = view.statusPage;
      if (p == 0) { drawIcon(f, ICON_MEAT, 1, 1); hearts(f, M->hunger); }
      if (p == 1) { drawIcon(f, M->mood >= 3 ? ICON_HAPPY : M->mood >= 1 ? ICON_NEUTRAL : ICON_SAD, 1, 1); hearts(f, M->mood); }
      if (p == 2) { drawIcon(f, ICON_BOLT, 2, 1); char b[8]; snprintf(b, sizeof b, "%u", (unsigned)M->power); drawText(f, b, 11, 2); }
      if (p == 3) { char b[16]; snprintf(b, sizeof b, "DAY %lu", (unsigned long)(M->ageMin / 1440 + 1)); drawTextCentered(f, b, 2); }
      if (p == 4) { char b[16]; snprintf(b, sizeof b, "W%u L%u", (unsigned)M->wins, (unsigned)M->losses); drawTextCentered(f, b, 2); }
      for (int i = 0; i < STATUS_PAGES; i++)   // 右下角頁碼 (目前頁恆亮、其他閃)
        if (i == p || (now / 400) % 2) px(f, COLS - 1 - (STATUS_PAGES - 1 - i) * 2, 8);
      return;
    }
    if (a.type == ACT_BATTLE) { renderBattle(f, now); return; }

    drawGauges(f, now);
    const int w = formW(M->form), h = formH(M->form);
    const uint32_t t = a.type != ACT_NONE ? now - a.start : 0;
    const bool sleep = sleeping();
    int frame = M->sick ? (now / 1000) % 2 : sleep ? 0 : (now / 500) % 2;   // 同模擬器：生病優先 (睡著也會慢慢動)

    // 便便 (地上隨機位置，最多 3 坨)
    if (a.type == ACT_CLEAN) {
      const int waveX = AREA_X0 + (int)(t * (AREA_X1 - AREA_X0 + 3) / 1300);
      for (uint8_t i = 0; i < a.poopBeforeN; i++) if (a.poopBefore[i] > waveX) drawIcon(f, ICON_POOP, a.poopBefore[i], 5);
      for (int y = 0; y < ROWS; y++) if ((y + t / 60) % 2 == 0) px(f, waveX, y);   // 沖水波
      px(f, waveX - 1, 8); px(f, waveX - 2, 7);
    } else {
      for (uint8_t i = 0; i < M->poop; i++) drawIcon(f, ICON_POOP, M->poopXs[i], 5);
    }

    if (M->form == FORM_EGG) {
      const int wob = (now / 400) % 4;
      drawForm(f, FORM_EGG, 18 + (wob == 1 ? 1 : wob == 3 ? -1 : 0), 0);
      return;
    }
    if (a.type == ACT_HATCH) {
      if (t < 1600) {   // 蛋越晃越快 -> 裂開
        const uint32_t sp = t < 800 ? 160 : 80;
        const int ex = 18 + ((t / sp) % 2 ? 1 : -1);
        drawForm(f, FORM_EGG, ex, 0);
        if (t > 1000) { px(f, ex + 2, 3, false); px(f, ex + 3, 4, false); px(f, ex + 2, 5, false); }
      } else if (t < 1800) {
        fillArea(f);
      } else {
        drawForm(f, M->form, 18, 0);
        sparkle(f, now, 18, w);
      }
      return;
    }
    if (a.type == ACT_EVOLVE) {
      const int fromW = formW(a.from);
      if (t < 2200) {   // 新舊型態交替閃爍，越來越快
        const uint32_t sp = t < 1000 ? 250 : t < 1700 ? 120 : 60;
        const uint8_t form = (t / sp) % 2 ? M->form : a.from;
        const int cx = view.x + fromW / 2;
        drawForm(f, form, cx - formW(form) / 2, 0);
      } else if (t < 2400) {
        fillArea(f);
      } else {
        drawForm(f, M->form, view.x, 0);
        sparkle(f, now, view.x, w);
      }
      return;
    }

    // 訓練：左側怪獸、右側沙包、最右側力量條
    if (a.type == ACT_TRAIN) {
      const int mx = AREA_X0 + 1;
      const int lvl = a.aiming ? trainLevel(t) : a.lvl;
      for (int i = 0; i < lvl; i++) px(f, AREA_X1, 8 - i);
      px(f, AREA_X1 - 1, 2);   // 成功門檻刻度 (>= 7)
      const int bagX = 34;
      if (a.aiming) {
        drawForm(f, M->form, mx, (now / 200) % 2);
        drawIcon(f, ICON_BAG, bagX, 4);
      } else {
        const uint32_t st = now - a.shootAt;
        drawForm(f, M->form, mx, 0, a.ok && st > 500 ? -(int)((st / 120) % 2) : 0);
        const int bx = mx + w + st / 25;
        const int reach = bagX - 2;
        if (bx < reach) { px(f, bx, 5); px(f, bx + 1, 5); px(f, bx, 6); px(f, bx + 1, 6); }
        else if (!a.ok) { const int drop = floorDiv((int)st - (reach - mx - w) * 25, 60); px(f, reach, min(8, 6 + drop)); }
        if (!a.ok || bx < reach) drawIcon(f, ICON_BAG, bagX, 4);
        else if ((st / 100) % 2 == 0) { px(f, bagX - 1, 3); px(f, bagX + 3, 3); px(f, bagX - 1, 8); px(f, bagX + 3, 8); px(f, bagX + 1, 2); }
      }
      return;
    }

    // 一般 / 進食 / 拒絕 / 打針
    int x = view.x;
    const int dy = ((int32_t)(view.hopUntil - now) > 0 && a.type == ACT_NONE) ? -1 : 0;
    if (a.type == ACT_EAT) {
      frame = (t / 150) % 2;
      Icon ic; loadIcon(a.item, ic);
      const int fx = view.dir > 0 ? min(AREA_X1 - 4, x + w + 1) : max(AREA_X0, x - 6);
      const int bites = min<int>(ic.h, (t / 450) * 2);                // 分 3 口，每口吃掉上方 2 排
      const int fy = min<int>(ROWS - ic.h, (int)(t / 40) - 5);         // 從上方掉下
      drawMask(f, ic.rows + bites, nullptr, ic.w, ic.h - bites, fx, fy + bites, false, false);
    }
    if (a.type == ACT_REFUSE) { x += (t / 110) % 2 ? 1 : -1; frame = 0; }
    if (a.type == ACT_MEDICINE) {
      const int sx = max(x + w + 1, AREA_X1 - 4 - (int)(t / 40));
      if (t < 900) drawIcon(f, ICON_SYRINGE, sx, 4);
      if (t >= 900 && (t / 100) % 2 == 0) sparkle(f, now, x, w);
    }
    const bool sulk = M->hunger == 0 || M->mood == 0;
    if (sulk) frame = 1;   // 鬧脾氣：趴著不動
    drawForm(f, M->form, x, frame, dy, sleep, true);

    if (sleep) {   // 兩個 3x5 的 Z 輪流往右上冒出
      const int zx = x + w + 8 <= AREA_X1 ? x + w + 1 : x - 8;
      if ((now / 700) % 2 == 0) drawIcon(f, ICON_ZEE, zx, 4);
      else drawIcon(f, ICON_ZEE, zx + 4, 1);
    } else {
      if (sulk && (now / 500) % 2 == 0) px(f, x - 1, ROWS - h + 4);   // 淚滴
      // 需要照顧：頭旁閃 2x5 粗驚嘆號 (生病時與 3x3 + 交替)
      if (hasNeed() && a.type == ACT_NONE && !view.challenge) {
        const int ph = (now / 300) % 4;   // 0,1 = 驚嘆號、2 = 生病時顯示 +、3 = 暗
        const int hx = x + w + 4 <= AREA_X1 + 1 ? x + w + 1 : x - 4;
        const int hy = max(0, ROWS - h);
        if (ph < 2 || (ph == 2 && !M->sick)) drawIcon(f, ICON_BANG, hx, hy);
        else if (ph == 2) drawIcon(f, ICON_PLUS, hx, hy + 1);
      }
    }
    if (view.challenge && a.type == ACT_NONE) {
      const int ew = formW(view.challengeEnemy);
      if ((now / 250) % 2 == 0) drawForm(f, view.challengeEnemy, AREA_X1 - ew / 2, 0);
      else { for (int y = 0; y <= 2; y++) px(f, AREA_X1 - 1, y); px(f, AREA_X1 - 1, 4); }   // !
    }
  }

private:
  Preferences prefs;
  PetData dex[DEX_MAX];
  uint8_t dexN = 1, active = 0;
  PetData *M = &dex[0];
  Action act;
  uint32_t lastWorldMs = 0, worldMsAcc = 0;
  uint32_t debugOffset = 0;
  uint8_t ticksSinceSave = 0;

  struct View {
    int x = 24; int8_t dir = 1;
    uint32_t nextStep = 0, pauseUntil = 0, hopUntil = 0;
    int target = -1;
    int8_t statusPage = -1; uint32_t statusUntil = 0;
    uint8_t shelfSel = 0; bool shelfDetail = false;
    bool challenge = false; uint8_t challengeEnemy = FORM_SLIME; uint32_t challengeUntil = 0;
    uint32_t lastCall = 0; bool called = false;
  } view;

  // ---------------- 小工具 ----------------
  static uint32_t rnd(uint32_t n) { return n ? esp_random() % n : 0; }
  static float rnd01() { return (esp_random() >> 8) / 16777216.0f; }
  static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
  static int floorDiv(int a, int b) { int q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }
  static int jsRound(float v) { return (int)floorf(v + 0.5f); }   // 同 JS Math.round
  bool autoSfx() const { return soundOn && !quiet; }
  bool busy() const { return act.type != ACT_NONE; }
  bool hasNeed() const { return M->hunger == 0 || M->mood == 0 || M->poop > 0 || M->sick; }

  static uint8_t formStage(uint8_t id) { return pgm_read_byte(&FORMS[id].stage); }
  static int formW(uint8_t id) { return pgm_read_byte(&FORMS[id].w); }
  static int formH(uint8_t id) { return pgm_read_byte(&FORMS[id].h[0]); }

  uint32_t epochNow() const {
    const uint32_t e = timeSource ? timeSource() : (uint32_t)time(nullptr);
    return e + debugOffset;
  }
  int hourOf(uint32_t epoch) const {
    time_t tt = epoch; struct tm tmv;
    localtime_r(&tt, &tmv);
    return tmv.tm_hour;
  }
  bool isNight() const { const int h = hourOf(M->clock); return h >= 23 || h < 7; }

  // ---------------- 存檔 ----------------
  static void newEgg(PetData &m, uint32_t clock) {
    memset(&m, 0, sizeof m);
    m.form = FORM_EGG; m.species = -1; m.pendingForm = -1;
    m.hunger = 4; m.mood = 4; m.poopTimer = -1;
    m.clock = clock;
  }
  void save() {
    ticksSinceSave = 0;
    prefs.putUChar("ver", 2);
    prefs.putUChar("n", dexN);
    prefs.putUChar("active", active);
    prefs.putBytes("dex", dex, sizeof(PetData) * dexN);
  }
  void load() {
    dexN = 1; active = 0; M = &dex[0];
    newEgg(dex[0], 12 * 3600UL);   // 尚未校時前的假時間 (白天)；第一次校時後接上真實時間
    if (prefs.getUChar("ver", 0) != 2) return;
    const uint8_t n = prefs.getUChar("n", 0);
    if (n < 1 || n > DEX_MAX) return;
    PetData tmp[DEX_MAX];
    if (prefs.getBytes("dex", tmp, sizeof(PetData) * n) != sizeof(PetData) * n) return;
    for (uint8_t i = 0; i < n; i++)
      if (tmp[i].form >= FORM_COUNT || tmp[i].poop > 3 || tmp[i].species >= SPECIES_COUNT) return;
    memcpy(dex, tmp, sizeof(PetData) * n);
    dexN = n;
    active = min<uint8_t>(n - 1, prefs.getUChar("active", 0));
    M = &dex[active];
    // 離線補算由 advanceWorld 處理 (需要已校時的真實時間)
  }

  // ---------------- 世界時間 ----------------
  void advanceWorld(uint32_t nowMs) {
    const uint32_t dms = nowMs - lastWorldMs;
    lastWorldMs = nowMs;
    const uint32_t nowE = epochNow();
    if (nowE >= VALID_EPOCH + debugOffset) {
      if (M->clock < VALID_EPOCH) { M->clock = nowE; save(); return; }        // 第一次校時：接上真實時間
      if (M->clock > nowE + 86400UL) { M->clock = nowE; return; }              // 時鐘往回跳太多：直接對齊
      if (nowE < M->clock + 60) return;
      uint32_t behind = (nowE - M->clock) / 60;
      if (behind > CATCHUP_MAX_MIN) {                                          // 離線太久：最多補 24 小時
        for (uint16_t i = 0; i < CATCHUP_MAX_MIN; i++) tickMinute(true);
        M->clock = nowE;
        save();
        return;
      }
      const bool silent = behind > 2;
      while (behind--) tickMinute(silent);
      if (silent || ticksSinceSave >= 10) save();
    } else {                                                                   // 尚未校時：以 millis 推進
      worldMsAcc += min<uint32_t>(dms, 60000);
      while (worldMsAcc >= 60000) { worldMsAcc -= 60000; tickMinute(false); }
      if (ticksSinceSave >= 10) save();
    }
  }

  // ---------------- 規則層：每遊戲分鐘結算 ----------------
  void tickMinute(bool silent) {
    PetData &m = *M;
    m.clock += 60;
    m.ageMin++;
    ticksSinceSave++;
    if (m.form == FORM_EGG) {
      if (m.ageMin >= EVOLVE_AT[0] && m.pendingForm < 0) { m.pendingForm = FORM_BABY; m.species = pickSpecies(); }   // 盲盒：孵化時決定物種
      return;
    }
    const bool sleep = sleeping();
    const uint16_t inc = sleep ? 1 : 2;   // 半分鐘單位：睡覺時減半

    m.hungerAcc += inc;
    if (m.hungerAcc >= HUNGER_MIN * 2) { m.hungerAcc -= HUNGER_MIN * 2; if (m.hunger > 0) m.hunger--; }
    m.moodAcc += inc;
    if (m.moodAcc >= MOOD_MIN * 2) { m.moodAcc -= MOOD_MIN * 2; if (m.mood > 0) m.mood--; }

    // 消化 -> 便便 (吃滿 3 次的那一分鐘開始算，30 分鐘後拉出來)
    if (m.poopTimer < 0 && m.digest >= DIGEST_MEALS) { m.digest -= DIGEST_MEALS; m.poopTimer = 30; }
    if (m.poopTimer > 0 && --m.poopTimer == 0) { addPoop(); m.poopTimer = -1; }

    // 便便放著：心情額外下降；放太久或餓太久 -> 生病。睡覺時生病計時暫停
    if (m.poop > 0) {
      m.poopAcc += inc;
      if (m.poopAcc >= POOP_MOOD_MIN * 2) { m.poopAcc -= POOP_MOOD_MIN * 2; if (m.mood > 0) m.mood--; }
      m.poopLongMin = m.poop >= 2 ? m.poopLongMin + (sleep ? 0 : 1) : 0;
    } else { m.poopAcc = 0; m.poopLongMin = 0; }
    m.hungerZeroMin = m.hunger == 0 ? m.hungerZeroMin + (sleep ? 0 : 1) : 0;
    if (!m.sick && (m.poopLongMin >= 120 || m.hungerZeroMin >= 180)) m.sick = 1;

    // 進化 (外型由物種決定；等醒著且沒在做動作時才播動畫)
    const uint8_t st = stage();
    if (m.pendingForm < 0 && st >= 1 && st <= 3 && m.ageMin >= EVOLVE_AT[st] && m.species >= 0) m.pendingForm = speciesForm(m.species, st + 1);

    // 野生挑戰：白天、成長期以上、健康時，平均約每 4 小時一次
    if (!silent && stage() >= 2 && !sleep && !m.sick && !view.challenge && !busy() && rnd(240) == 0) {
      view.challenge = true;
      view.challengeEnemy = rnd(2) ? FORM_BAT : FORM_SLIME;
      view.challengeUntil = millis() + 20000;
      if (autoSfx()) TAMA_PLAY(MEL_CHALLENGE);
    }
  }

  void addPoop() {
    PetData &m = *M;
    if (m.poop >= 3) return;
    int x = AREA_X0;
    for (int tries = 0; tries < 30; tries++) {
      x = AREA_X0 + 2 + rnd(AREA_X1 - 4 - AREA_X0 - 1);   // 與左側刻度保持距離
      bool ok = true;
      for (uint8_t i = 0; i < m.poop; i++) if (abs((int)m.poopXs[i] - x) < POOP_W) ok = false;
      if (ok) break;
    }
    m.poopXs[m.poop++] = x;
  }

  int8_t pickSpecies() {
    uint8_t pool[SPECIES_COUNT], n = 0;
    for (uint8_t s = 0; s < SPECIES_COUNT; s++) {
      bool owned = false;
      for (uint8_t i = 0; i < dexN; i++) if (dex[i].species == s) owned = true;
      if (!owned) pool[n++] = s;
    }
    return n ? pool[rnd(n)] : rnd(SPECIES_COUNT);
  }

  // ---------------- 圖鑑 ----------------
  bool canNewEgg() const {
    if (dexN >= DEX_MAX) return false;
    for (uint8_t i = 0; i < dexN; i++) if (formStage(dex[i].form) != 4) return false;
    return true;
  }
  uint8_t shelfItems() const { return dexN + (canNewEgg() ? 1 : 0); }
  void resetView() {
    view.x = 24; view.dir = 1; view.nextStep = 0; view.pauseUntil = 0; view.target = -1;
    act.type = ACT_NONE; view.statusPage = -1; view.challenge = false; view.called = false;
  }
  void switchTo(uint8_t i) {
    if (i == active || i >= dexN) return;
    dex[i].clock = M->clock;   // 世界時間延續；被收起來的那隻時間暫停
    active = i; M = &dex[i];
    resetView();
    save();
  }
  void startNewEgg() {
    if (!canNewEgg()) return;
    newEgg(dex[dexN], M->clock);
    active = dexN++; M = &dex[active];
    resetView();
    TAMA_PLAY(MEL_NEW_EGG);
    save();
  }

  // ---------------- 互動 ----------------
  void startAction(ActionType type) {
    act = Action();
    act.type = type;
    act.start = millis();
  }
  static uint8_t trainLevel(uint32_t ms) {
    const float p = (ms % 1000) / 1000.0f;
    return jsRound((p < 0.5f ? p * 2 : 2 - p * 2) * 9);
  }
  void train() {
    if (view.challenge) {   // 接受挑戰 (動作播放中按下不算)
      if (busy()) return;
      startBattle(view.challengeEnemy);
      view.challenge = false;
      return;
    }
    if (act.type == ACT_TRAIN && act.aiming) {   // 第二次按：出拳
      const uint32_t now = millis();
      act.lvl = trainLevel(now - act.start);
      act.aiming = false; act.shootAt = now; act.ok = act.lvl >= 7;
      M->trains++;
      if (act.ok) { M->power = min<uint8_t>(99, M->power + 1); M->mood = min<uint8_t>(4, M->mood + 1); }
      RetroSounds::player.sweep(500, act.ok ? 1500 : 300, 200);
      save();
      return;
    }
    if (busy() || stage() == 0 || sleeping()) return;
    if (M->sick) { RetroSounds::player.tone(300, 120); startAction(ACT_REFUSE); return; }
    startAction(ACT_TRAIN);
    act.aiming = true;
  }
  void clean() {
    if (busy() || stage() == 0) return;
    if (M->poop > 0) {
      startAction(ACT_CLEAN);
      act.poopBeforeN = M->poop;
      memcpy(act.poopBefore, M->poopXs, 3);
      M->poop = 0; M->poopAcc = 0; M->poopLongMin = 0;
      RetroSounds::player.sweep(1800, 400, 400);
    } else if (M->sick) {
      startAction(ACT_MEDICINE);
      M->sick = 0; M->hungerZeroMin = 0;
      TAMA_PLAY(MEL_MEDICINE);
    } else { RetroSounds::player.tone(600, 40); return; }
    save();
  }
  void status() {
    if (busy()) return;
    view.statusPage++;
    if (view.statusPage >= STATUS_PAGES) view.statusPage = -1;
    if (view.statusPage == SHELF_PAGE) { view.shelfSel = active; view.shelfDetail = false; }
    view.statusUntil = millis() + (view.statusPage == SHELF_PAGE ? SHELF_IDLE_MS : STATUS_IDLE_MS);
  }
  // 書架：K1 左、K3 右、K2 看資料 → 再按 K2 換牠出來養 (或孵新蛋)、K4 離開
  void shelfKey(uint8_t k) {
    const uint8_t n = shelfItems();
    if (view.shelfSel >= n) view.shelfSel = n - 1;
    view.statusUntil = millis() + SHELF_IDLE_MS;
    if (k == 4) { view.statusPage = -1; return; }
    if (k == 1 || k == 3) {
      if (view.shelfDetail) { view.shelfDetail = false; return; }
      const int ns = view.shelfSel + (k == 1 ? -1 : 1);
      if (ns >= 0 && ns < n) { view.shelfSel = ns; RetroSounds::player.tone(1000, 20); }
      return;
    }
    if (k == 2) {
      if (!view.shelfDetail) { view.shelfDetail = true; RetroSounds::player.tone(1200, 30); return; }
      view.statusPage = -1;
      if (view.shelfSel >= dexN) startNewEgg();
      else if (view.shelfSel != active) { switchTo(view.shelfSel); TAMA_PLAY(MEL_SWITCH); }
    }
  }

  // 對戰：開打時一次決定整場回合。對手強度只跟階段有關，所以訓練越多勝率越高
  void startBattle(uint8_t enemy) {
    if (busy() || stage() < 1) return;
    const int bonus = STAGE_BONUS[stage()];
    const int pAtk = M->power + bonus;
    const int eMin = ENEMY_POWER[stage()][0], eMax = ENEMY_POWER[stage()][1];
    const int eAtk = bonus + eMin + rnd(eMax - eMin + 1);
    const float pHit = max(0.25f, min(0.85f, 0.55f + (pAtk - eAtk) / 60.0f));
    const float eHit = max(0.25f, min(0.85f, 0.55f + (eAtk - pAtk) / 60.0f));
    startAction(ACT_BATTLE);
    act.enemy = enemy;
    int php = 3, ehp = 3; bool me = true;
    while (php > 0 && ehp > 0 && act.nTurns < MAX_TURNS) {
      const bool hit = rnd01() < (me ? pHit : eHit);
      if (me) act.turnMe |= 1 << act.nTurns;
      if (hit) { act.turnHit |= 1 << act.nTurns; if (me) ehp--; else php--; }
      act.nTurns++;
      me = !me;
    }
    act.win = ehp <= 0 || (php > 0 && ehp <= php);
    RetroSounds::player.sweep(200, 900, 300);
  }

  // 閒晃：挑隨機目標點走過去 (每 200ms 一步，生病 700ms)，到點停 0.3 ~ 1.2 秒；心情好時偶爾蹦一下
  void idleStep(uint32_t now) {
    View &v = view;
    const int w = formW(M->form);
    const int maxX = AREA_X1 - w + 1;
    v.x = clampi(v.x, AREA_X0, maxX);
    const bool sulk = M->hunger == 0 || M->mood == 0;
    if (sleeping() || sulk || (int32_t)(now - v.pauseUntil) < 0 || (int32_t)(now - v.nextStep) < 0) return;
    if (v.target < 0 || v.target == v.x || v.target > maxX) {
      if (v.target == v.x) { v.target = -1; v.pauseUntil = now + 300 + (uint32_t)(rnd01() * 900); return; }
      v.target = AREA_X0 + rnd(maxX - AREA_X0 + 1);
    }
    if (v.target != v.x) v.dir = v.target > v.x ? 1 : -1;
    v.x += v.dir;
    v.nextStep = now + (M->sick ? 700 : 200);
    if (!M->sick && M->mood >= 3 && rnd(100) < 4) v.hopUntil = now + 180;
  }

  // ---------------- 繪圖 ----------------
  template <typename F> static void px(F &f, int x, int y, bool on = true) {
    if (x >= 0 && x < COLS && y >= 0 && y < ROWS) f.drawPixel(x, y, on);
  }
  // 遮罩繪圖：body 恆亮、eyes 在 eyesOn 時亮；opaque = 先清掉每一排輪廓範圍 (站在便便前面不會糊成一團)
  template <typename F>
  static void drawMask(F &f, const uint16_t *body, const uint16_t *eyes, int w, int h, int x, int y, bool eyesOn, bool opaque) {
    for (int r = 0; r < h; r++) {
      const uint16_t b = body[r], e = eyes ? eyes[r] : 0, all = b | e;
      if (opaque && all) {
        int l = 0, rr = w - 1;
        while (!(all & (1 << (w - 1 - l)))) l++;
        while (!(all & (1 << (w - 1 - rr)))) rr--;
        for (int c = l; c <= rr; c++) px(f, x + c, y + r, false);
      }
      const uint16_t lit = eyesOn ? all : b;
      for (int c = 0; c < w; c++) if (lit & (1 << (w - 1 - c))) px(f, x + c, y + r);
    }
  }
  template <typename F>
  static void drawForm(F &f, uint8_t id, int x, int frameIdx, int dy = 0, bool eyesOn = false, bool opaque = false) {
    Form fm; memcpy_P(&fm, &FORMS[id], sizeof fm);
    const int fi = frameIdx & 1, h = fm.h[fi];
    drawMask(f, fm.body[fi], fm.eyes[fi], fm.w, h, x, ROWS - h + dy, eyesOn, opaque);
  }
  static void loadIcon(uint8_t id, Icon &ic) { memcpy_P(&ic, &ICONS[id], sizeof ic); }
  template <typename F> static void drawIcon(F &f, uint8_t id, int x, int y) {
    Icon ic; loadIcon(id, ic);
    drawMask(f, ic.rows, nullptr, ic.w, ic.h, x, y, false, false);
  }
  static bool glyph(char ch, Glyph &g) {
    for (uint8_t i = 0; i < GLYPH_COUNT; i++) {
      memcpy_P(&g, &GLYPHS[i], sizeof g);
      if (g.ch == ch) return true;
    }
    g.ch = ' '; g.w = 1; memset(g.rows, 0, 5);
    return false;
  }
  static int textWidth(const char *s) {
    int w = 0, n = 0; Glyph g;
    for (; *s; s++, n++) { glyph(*s, g); w += g.w; }
    return w + (n > 0 ? n - 1 : 0);
  }
  template <typename F> static void drawText(F &f, const char *s, int x, int y) {
    Glyph g;
    for (; *s; s++) {
      glyph(*s, g);
      for (int r = 0; r < 5; r++) for (int c = 0; c < g.w; c++) if (g.rows[r] & (1 << (g.w - 1 - c))) px(f, x + c, y + r);
      x += g.w + 1;
    }
  }
  template <typename F> static void drawTextCentered(F &f, const char *s, int y) { drawText(f, s, floorDiv(COLS - textWidth(s), 2), y); }
  template <typename F> static void fillArea(F &f) { for (int y = 0; y < ROWS; y++) for (int x = AREA_X0; x <= AREA_X1; x++) px(f, x, y); }
  template <typename F> static void hearts(F &f, uint8_t n) { for (int i = 0; i < 4; i++) drawIcon(f, i < n ? ICON_HEARTF : ICON_HEARTE, 11 + i * 7, 2); }
  template <typename F> static void sparkle(F &f, uint32_t now, int x, int w) {
    if ((now / 150) % 2) { px(f, x - 2, 1); px(f, x + w + 1, 2); px(f, x + w / 2, 0); }
    else { px(f, x - 1, 3); px(f, x + w, 0); px(f, x + w + 2, 4); }
  }

  // 最左側抽象刻度：x=0..1 飽食、x=3..4 心情，每 1 點 = 2 排 (由下往上)；剩 1 點或 0 時底部一格閃爍
  template <typename F> void drawGauges(F &f, uint32_t now) const {
    const bool blink = (now / 300) % 2 == 0;
    const uint8_t vals[2] = { M->hunger, M->mood };
    for (int g = 0; g < 2; g++) {
      const int x = g * 3, val = vals[g];
      for (int i = 0; i < val; i++) for (int y = 7 - i * 2; y <= 8 - i * 2; y++) { px(f, x, y); px(f, x + 1, y); }
      if (val <= 1) for (int y = 7; y <= 8; y++) { px(f, x, y, blink); px(f, x + 1, y, blink); }
    }
  }

  // 圖鑑書架：一次並排 3 隻 (每格 14 寬)，選中的那隻閃爍；左右邊緣小點 = 那一側還有更多
  template <typename F> void renderShelf(F &f, uint32_t now) const {
    const int n = shelfItems();
    const int sel = min<int>(view.shelfSel, n - 1);
    const bool blink = (now / 300) % 2 == 0;
    if (view.shelfDetail) {
      if (sel >= dexN) {   // 新蛋
        drawForm(f, FORM_EGG, 4 + (blink ? 1 : -1), 0, -1);
        if (blink) drawText(f, "NEW", 16, 2);
        return;
      }
      const PetData &it = dex[sel];
      const bool isActive = sel == active;
      drawForm(f, it.form, 1, isActive ? (now / 500) % 2 : 0, isActive && blink ? -1 : 0);
      char b[12];
      snprintf(b, sizeof b, "D%lu", (unsigned long)(it.ageMin / 1440 + 1)); drawText(f, b, 15, 2);
      snprintf(b, sizeof b, "W%u", (unsigned)it.wins); drawText(f, b, 28, 2);
      return;
    }
    const int start = max(0, min(n - 3, sel - 1));
    for (int k = 0; k < 3; k++) {
      const int idx = start + k;
      if (idx >= n || (idx == sel && !blink)) continue;
      const uint8_t form = idx >= dexN ? (uint8_t)FORM_EGG : dex[idx].form;
      drawForm(f, form, k * 14 + (14 - formW(form)) / 2, 0, form == FORM_EGG ? -1 : 0);
    }
    if (start > 0) px(f, 0, 4);
    if (start + 3 < n) px(f, COLS - 1, 4);
  }

  template <typename F> void renderBattle(F &f, uint32_t now) const {
    const Action &a = act;
    const uint32_t t = now - a.start;
    const uint8_t me = M->form, en = a.enemy;
    const int mw = formW(me), ew = formW(en);
    // 入場：兩邊滑進來
    const float slide = t < BATTLE_INTRO ? 1.0f - (float)t / BATTLE_INTRO : 0.0f;
    const int mx = jsRound(-mw * slide), ex = COLS - ew + jsRound(ew * slide);
    // 已結算的回合 -> 血量
    const int turnIdx = t >= BATTLE_INTRO ? (int)((t - BATTLE_INTRO) / TURN_MS) : -1;
    int php = 3, ehp = 3;
    for (int i = 0; i < min<int>(turnIdx, a.nTurns); i++) if ((a.turnHit >> i) & 1) { if ((a.turnMe >> i) & 1) ehp--; else php--; }
    const uint32_t endT = BATTLE_INTRO + a.nTurns * TURN_MS;
    if (t >= endT + 600) {   // 結果畫面
      drawTextCentered(f, a.win ? "WIN!" : "LOSE", 2);
      if (a.win && (now / 150) % 2) { px(f, 5, 2); px(f, 36, 2); px(f, 4, 6); px(f, 37, 6); }
      return;
    }
    int mdy = 0, edy = 0; bool mBlink = false, eBlink = false;
    if (t >= BATTLE_INTRO && turnIdx < a.nTurns) {
      const uint32_t tt = (t - BATTLE_INTRO) % TURN_MS;
      const bool tMe = (a.turnMe >> turnIdx) & 1, tHit = (a.turnHit >> turnIdx) & 1;
      const int fromX = tMe ? mx + mw : ex - 2;
      const int toX = tMe ? ex : mx + mw - 2;
      if (tt < BATTLE_TRAVEL) {
        const int bx = jsRound(fromX + (toX - fromX) * (float)tt / BATTLE_TRAVEL);
        px(f, bx, 4); px(f, bx + 1, 4); px(f, bx, 5); px(f, bx + 1, 5);
      } else if (tHit) {
        if (tMe) eBlink = (tt / 80) % 2 == 0; else mBlink = (tt / 80) % 2 == 0;
      } else {   // 閃避：往上跳，子彈穿過
        if (tMe) edy = -3; else mdy = -3;
        const int bx = jsRound(toX + (tMe ? 1 : -1) * (float)(tt - BATTLE_TRAVEL) / 12);
        px(f, bx, 4); px(f, bx + 1, 4); px(f, bx, 5); px(f, bx + 1, 5);
      }
    }
    if (t >= endT) {   // 收尾：勝者跳、敗者閃
      const int hop = ((t - endT) / 120) % 2 ? -2 : 0;
      if (a.win) { mdy = hop; eBlink = (t / 100) % 2 == 0; } else { edy = hop; mBlink = (t / 100) % 2 == 0; }
    }
    if (!mBlink) drawForm(f, me, mx, (now / 300) % 2, mdy);
    if (!eBlink) drawForm(f, en, ex, (now / 300) % 2, edy);
    // 中間上方血條：我方 x=13..20、對手 x=23..30，各 3 格 (2x2、間隔 1)，從中間往外扣
    for (int i = 0; i < 3; i++) {
      if (i < php) { const int x = 13 + i * 3; px(f, x, 0); px(f, x + 1, 0); px(f, x, 1); px(f, x + 1, 1); }
      if (i < ehp) { const int x = 29 - i * 3; px(f, x, 0); px(f, x + 1, 0); px(f, x, 1); px(f, x + 1, 1); }
    }
  }
};

extern MonsterPet pet;

} // namespace Tamagotchi
