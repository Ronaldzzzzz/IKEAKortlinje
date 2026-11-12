#!/usr/bin/env node
/**
 * 韌體動畫匯出工具：在電腦上以 1ms 精度無頭執行 simulator/index.html 的場景程式，
 * 把每個主題的每一組劇本錄成「42x9 畫格序列 + 蜂鳴器事件」，輸出成 ESP32 韌體用的 PROGMEM 資料：
 *
 *   firmware/AlarmClips.h        —— 韌體只負責播放 (顯示畫格 + 依時間發聲 + 隨機挑劇本)
 *   simulator/firmware_clips.js  —— 同一份資料給 firmware_preview.html 用，確認韌體播放結果與模擬器一致
 *
 * 錄製規則：
 *   - 每組劇本錄「第 2 輪」(穩定狀態) 當作循環片段，避免第一輪的觸發前奏混進循環
 *   - playTheme() 當下發出的前奏音效另存為主題的 intro (只在鬧鐘剛響時播一次)
 *   - 按鍵解除：於穩定狀態中途呼叫 dismissAlarm()，錄 2500ms 反轉彩蛋 (畫格 + 音效)
 *   - 連續相同畫格合併為 (畫格編號, 持續毫秒)；所有畫格全域去重
 *
 * 同時做完整檢查 (輸出到 stderr)：例外、畫面全黑過久、循環接縫跳動、音效超出片段、解除動畫是否與解除時間點有關…
 *
 * 用法：node tools/export_firmware.js
 */
const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..');
const html = fs.readFileSync(path.join(ROOT, 'index.html'), 'utf8');
const SCRIPT = html.slice(html.indexOf('<script>') + 8, html.lastIndexOf('</script>'));
const COLS = 42, ROWS = 9, DISMISS_MS = 2500, T0 = 1000;

// ---------------------------------------------------------------------------
// 無頭沙盒：假 DOM + 可控時鐘 + 假計時器 + 錄音
// ---------------------------------------------------------------------------
function makeSandbox() {
  const env = { now: 0, timers: [], timerId: 1, sounds: [], tag: 'loop' };
  const el = () => ({
    classList: { add() {}, remove() {}, toggle() {} }, style: {}, textContent: '', value: '',
    addEventListener() {}, getContext: () => new Proxy({}, { get: () => () => {} })
  });
  const g = {
    document: { getElementById: el, addEventListener() {}, querySelectorAll: () => [] },
    window: { addEventListener() {} },
    performance: { now: () => env.now },
    requestAnimationFrame: () => {},
    setTimeout: (fn, ms) => { const id = env.timerId++; env.timers.push({ id, at: env.now + (ms || 0), fn, tag: env.tag }); return id; },
    clearTimeout: id => { env.timers = env.timers.filter(t => t.id !== id); },
    console,
  };
  const code = `${SCRIPT}
;initAudio = () => {}; renderMatrix = () => {};
playTone = (f, d) => __env.sounds.push({ t: __env.now, f1: Math.round(f), f2: 0, dur: Math.round(d), tag: __env.tag });
playPortamento = (a, b, d) => __env.sounds.push({ t: __env.now, f1: Math.round(a), f2: Math.max(1, Math.round(b)), dur: Math.round(d), tag: __env.tag });
let __forcedPac = null;
const __origRoll = rollPacmanVariant;
rollPacmanVariant = () => (__forcedPac !== null ? __forcedPac : __origRoll());
return {
  playTheme, dismissAlarm, gameLoop, switchMode,
  get buffer() { return buffer; }, get mode() { return currentMode; },
  set snakeForcedPlan(v) { snakeForcedPlan = v; }, set racerForcedPlan(v) { racerForcedPlan = v; },
  set pongForcedPlan(v) { pongForcedPlan = v; }, set flappyForcedPlan(v) { flappyForcedPlan = v; },
  set tetrisForcedPlan(v) { tetrisForcedPlan = v; }, set pokeForcedStyle(v) { pokeForcedStyle = v; },
  set forcedPac(v) { __forcedPac = v; },
  SNAKE_SIMS, FLAPPY_SIMS, RACER_SIMS, PONG_SIMS, TETRIS_PLANS, DINO_LOOP_MS, RHYTHM_CYCLE,
};`;
  const names = Object.keys(g);
  const api = new Function(...names, '__env', code)(...names.map(n => g[n]), env);
  const advance = t => {
    env.now = t;
    for (;;) {
      env.timers.sort((a, b) => a.at - b.at);
      const due = env.timers[0];
      if (!due || due.at > t) break;
      env.timers.shift();
      const keep = env.tag;
      env.tag = due.tag;
      due.fn();
      env.tag = keep;
    }
  };
  return { api, env, advance };
}

const frameKey = buf => {
  const bytes = new Uint8Array(ROWS * 6);
  for (let y = 0; y < ROWS; y++) for (let x = 0; x < COLS; x++) if (buf[y * COLS + x]) bytes[y * 6 + (x >> 3)] |= 0x80 >> (x & 7);
  return Buffer.from(bytes).toString('hex');
};

// ---------------------------------------------------------------------------
// 主題與劇本清單：start(api) 設定指定劇本，loop = 單輪長度 (ms)
// ---------------------------------------------------------------------------
function themeList() {
  const { api } = makeSandbox();
  const L = [];
  const add = (key, name, clips, pick) => L.push({ key, name, clips, pick });
  add('pacman', '小精靈', [1, 2, 3, 4].map(v => ({
    label: `場景 ${v}`, weight: [40, 40, 10, 10][v - 1], loop: 80 * (v === 4 ? 90 : 120),
    boostNext: v === 3 ? 3 : -1, // 播完場景 3 後，下一輪以 90% 機率播場景 4
    start: a => { a.forcedPac = v; a.playTheme('pacman', v); }
  })), 'weighted');
  add('dino', '恐龍', [{ label: '奔跑', loop: api.DINO_LOOP_MS, start: a => a.playTheme('dino') }], 'uniform');
  add('mario', '瑪利歐', [{ label: '1-1', loop: 140 * 70, start: a => a.playTheme('mario') }], 'uniform');
  add('invaders', '太空侵略者', [{ label: '戰鬥', loop: 90 * 60, start: a => a.playTheme('invaders') }], 'uniform');
  add('pokemon', '寶可夢', [0, 1, 2, 3].map(s => ({
    label: ['百葉窗', '雷達掃描', '方框漩渦', '幾何三角'][s], loop: 94 * 60,
    start: a => { a.pokeForcedStyle = s; a.playTheme('pokemon'); }
  })), 'uniform');
  add('continue', '接關倒數', [{ label: '倒數', loop: 14600, start: a => a.playTheme('continue') }], 'uniform');
  add('bsod', '藍白當機', [{ label: '當機', loop: 6400, start: a => a.playTheme('bsod') }], 'uniform');
  add('tetris', '俄羅斯方塊', api.TETRIS_PLANS.map((p, i) => ({
    label: `劇本 ${i + 1}`, loop: p.total, start: a => { a.tetrisForcedPlan = i; a.playTheme('tetris'); }
  })), 'norepeat');
  add('snake', '貪食蛇', api.SNAKE_SIMS.map((s, i) => ({
    label: `劇本 ${i + 1}`, loop: s.cycle, start: a => { a.snakeForcedPlan = i; a.playTheme('snake'); }
  })), 'norepeat');
  add('flappy', '笨鳥先飛', api.FLAPPY_SIMS.map((s, i) => ({
    label: `劇本 ${i + 1}`, loop: s.cycle, start: a => { a.flappyForcedPlan = i; a.playTheme('flappy'); }
  })), 'norepeat');
  add('rhythm', '節奏音軌', [{ label: '歡樂頌', loop: api.RHYTHM_CYCLE, start: a => a.playTheme('rhythm') }], 'uniform');
  add('racer', '公路賽車', api.RACER_SIMS.map((s, i) => ({
    label: `劇本 ${i + 1} ${s.death}`, loop: s.cycle, start: a => { a.racerForcedPlan = i; a.playTheme('racer'); }
  })), 'norepeat');
  add('pong', '乒乓球', api.PONG_SIMS.map((s, i) => ({
    label: `劇本 ${i + 1} (${s.label})`, loop: s.cycle, start: a => { a.pongForcedPlan = i; a.playTheme('pong'); }
  })), 'norepeat');
  return L;
}

// ---------------------------------------------------------------------------
// 錄製
// ---------------------------------------------------------------------------
const framePool = new Map(); // hex -> index
const framePoolList = [];
const frameIndex = hex => {
  if (!framePool.has(hex)) { framePool.set(hex, framePoolList.length); framePoolList.push(hex); }
  return framePool.get(hex);
};
const issues = [];
const warn = (where, msg) => issues.push(`${where}：${msg}`);

// 把逐毫秒畫格壓成 [畫格編號, 持續 ms]
function compress(keys) {
  const seq = [];
  for (const k of keys) {
    const idx = frameIndex(k);
    const last = seq[seq.length - 1];
    if (last && last[0] === idx && last[1] < 65000) last[1]++;
    else seq.push([idx, 1]);
  }
  return seq;
}

function recordLoop(theme, clip) {
  const { api, env, advance } = makeSandbox();
  env.now = T0; env.tag = 'intro';
  clip.start(api);
  env.tag = 'loop';
  const A = clip.loop, B = 2 * clip.loop; // 錄第 2 輪
  const keys = [];
  let firstKeys = null;
  for (let t = 0; t < B; t++) {
    advance(T0 + t);
    api.gameLoop();
    if (api.mode !== 'ALARM') throw new Error('意外離開鬧鐘模式');
    if (t >= A) keys.push(frameKey(api.buffer));
  }
  advance(T0 + B); api.gameLoop(); firstKeys = frameKey(api.buffer); // 第 3 輪第一格 (檢查接縫)
  const intro = env.sounds.filter(s => s.tag === 'intro').map(s => ({ ...s, t: s.t - T0 }));
  const loopSnd = env.sounds.filter(s => s.tag === 'loop' && s.t >= T0 + A && s.t < T0 + B).map(s => ({ ...s, t: s.t - T0 - A }));
  const loopSnd1 = env.sounds.filter(s => s.tag === 'loop' && s.t < T0 + A).map(s => s.t - T0);

  // ---- 檢查 ----
  const where = `${theme.name}/${clip.label}`;
  const blank = '0'.repeat(ROWS * 12);
  let run = 0, maxBlank = 0;
  for (const k of keys) { run = k === blank ? run + 1 : 0; maxBlank = Math.max(maxBlank, run); }
  if (maxBlank > 1200) warn(where, `連續全黑 ${maxBlank}ms`);
  const uniq = new Set(keys).size;
  if (uniq < 3) warn(where, `整輪只有 ${uniq} 種畫面`);
  if (firstKeys !== keys[0]) {
    const a = Buffer.from(keys[keys.length - 1], 'hex'), b = Buffer.from(keys[0], 'hex');
    let diff = 0; for (let i = 0; i < a.length; i++) { let x = a[i] ^ b[i]; while (x) { diff += x & 1; x >>= 1; } }
    const a2 = Buffer.from(firstKeys, 'hex'); let seam = 0; for (let i = 0; i < a2.length; i++) { let x = a2[i] ^ b[i]; while (x) { seam += x & 1; x >>= 1; } }
    if (seam > 12) warn(where, `循環接縫：下一輪開頭與本輪開頭差 ${seam} 點 (尾→頭差 ${diff} 點)`);
  }
  const s1 = loopSnd1.length, s2 = loopSnd.length;
  if (s1 !== s2) warn(where, `第 1 輪 ${s1} 個音效、第 2 輪 ${s2} 個 (循環音效不穩定)`);
  for (const s of loopSnd) if (s.t + s.dur > clip.loop + 50) { warn(where, `音效 @${s.t}ms 長 ${s.dur}ms 超出片段尾端`); break; }
  return { seq: compress(keys), sounds: loopSnd, intro, uniq };
}

function recordDismiss(theme, clip, atFrac) {
  const { api, env, advance } = makeSandbox();
  env.now = T0;
  clip.start(api);
  const at = clip.loop + Math.floor(clip.loop * atFrac);
  for (let t = 0; t <= at; t++) { advance(T0 + t); api.gameLoop(); }
  env.sounds.length = 0;
  env.timers = []; // 韌體按下解除鍵時會停掉所有進行中的音效
  env.tag = 'dismiss';
  api.dismissAlarm();
  const keys = [];
  for (let t = 0; t <= DISMISS_MS + 50; t++) {
    advance(T0 + at + t);
    api.gameLoop();
    if (t < DISMISS_MS && api.mode === 'ALARM') keys.push(frameKey(api.buffer));
  }
  if (api.mode !== 'CLOCK') warn(`${theme.name}/${clip.label}`, '解除後 2.5 秒沒有回到時鐘');
  const sounds = env.sounds.map(s => ({ ...s, t: s.t - T0 - at }));
  return { keys, sounds };
}

const themes = themeList();
const out = [];
for (const theme of themes) {
  const T = { key: theme.key, name: theme.name, pick: theme.pick, clips: [], dismiss: [], intro: null };
  const dismissMap = new Map();
  for (const clip of theme.clips) {
    let rec;
    try { rec = recordLoop(theme, clip); } catch (e) { warn(`${theme.name}/${clip.label}`, `例外 ${e.message}`); continue; }
    if (!T.intro) T.intro = rec.intro;
    // 解除動畫：在單輪 30% 與 70% 兩個時間點各錄一次，比對是否一致
    const d1 = recordDismiss(theme, clip, 0.3), d2 = recordDismiss(theme, clip, 0.7);
    const setEq = (p, q) => { const A = new Set(p), B = new Set(q); return A.size === B.size && [...A].every(k => B.has(k)); };
    if (!setEq(d1.keys, d2.keys)) warn(`${theme.name}/${clip.label}`, '解除動畫會隨按鍵時間點改變 (韌體只會播 30% 時間點錄下的版本)');
    const dk = d1.keys.join('|') + JSON.stringify(d1.sounds.map(s => [s.t, s.f1, s.f2, s.dur]));
    if (!dismissMap.has(dk)) { dismissMap.set(dk, T.dismiss.length); T.dismiss.push({ seq: compress(d1.keys), sounds: d1.sounds }); }
    T.clips.push({ label: clip.label, weight: clip.weight || 1, boostNext: clip.boostNext === undefined ? -1 : clip.boostNext, loop: clip.loop, seq: rec.seq, sounds: rec.sounds, dismiss: dismissMap.get(dk), uniq: rec.uniq });
  }
  // 單輪內沒有任何音效的主題 (1~8 號舊主題只有觸發前奏)：韌體每輪開頭重播前奏，鬧鐘才不會只響一聲就安靜
  T.introEachLoop = T.clips.every(cl => cl.sounds.length === 0);
  out.push(T);
}

// ---------------------------------------------------------------------------
// 報告
// ---------------------------------------------------------------------------
let totalSeq = 0, totalSnd = 0;
for (const T of out) {
  const lines = T.clips.map(c => `${c.label} ${(c.loop / 1000).toFixed(2)}s/${c.seq.length}段/${c.uniq}格/${c.sounds.length}音`);
  console.error(`■ ${T.name} [${T.pick}] intro ${T.intro ? T.intro.length : 0} 音、解除 ${T.dismiss.length} 種：${lines.join('；')}`);
  T.clips.forEach(c => { totalSeq += c.seq.length; totalSnd += c.sounds.length; });
  T.dismiss.forEach(d => { totalSeq += d.seq.length; totalSnd += d.sounds.length; });
}
const bytes = framePoolList.length * 54 + totalSeq * 4 + totalSnd * 8;
console.error(`\n畫格池 ${framePoolList.length} 格 (${(framePoolList.length * 54 / 1024).toFixed(1)} KB)、序列 ${totalSeq} 段、音效 ${totalSnd} 個，合計約 ${(bytes / 1024).toFixed(1)} KB Flash`);
console.error(issues.length ? `\n⚠️ 檢查發現 ${issues.length} 項：\n  - ${issues.join('\n  - ')}` : '\n✅ 檢查全數通過');

// ---------------------------------------------------------------------------
// 輸出 C 標頭
// ---------------------------------------------------------------------------
const PICK = { uniform: 'PICK_UNIFORM', weighted: 'PICK_WEIGHTED', norepeat: 'PICK_NO_REPEAT' };
const c = [];
const sndArr = (name, list) => {
  const l = list.slice().sort((a, b) => a.t - b.t);
  c.push(`static const SoundEvent ${name}[] PROGMEM = {${l.length ? '' : ' {0, 0, 0, 0} '}`);
  for (const s of l) c.push(`  {${s.t}, ${s.f1}, ${s.f2}, ${s.dur}},`);
  c.push('};');
  return l.length;
};
const seqArr = (name, seq) => {
  c.push(`static const ClipStep ${name}[] PROGMEM = {`);
  for (let i = 0; i < seq.length; i += 8) c.push('  ' + seq.slice(i, i + 8).map(([f, d]) => `{${f}, ${d}}`).join(', ') + ',');
  c.push('};');
};
c.push('#pragma once');
c.push('// ⚠️ 自動產生的檔案，請勿手動修改 —— 來源：simulator/index.html，產生器：simulator/tools/export_firmware.js');
c.push(`// 產生時間：${new Date().toISOString()}`);
c.push('#include <Arduino.h>');
c.push('');
c.push('namespace AlarmClips {');
c.push('');
c.push('// 42x9 畫格：9 列 x 6 bytes，每列由左到右 bit7 -> bit0 (x = byte*8 + (7 - bit))');
c.push('struct ClipStep { uint16_t frame; uint16_t ms; };            // 顯示畫格 frame 持續 ms 毫秒');
c.push('struct SoundEvent { uint16_t t; uint16_t f1; uint16_t f2; uint16_t dur; }; // t 毫秒時發聲：f2 = 0 單音，否則 f1 -> f2 指數滑音');
c.push('// weight = PICK_WEIGHTED 權重；boostNext >= 0 時，播完本片段後下一輪以 90% 機率改播該片段 (小精靈場景 3 -> 4)');
c.push('struct Clip { const ClipStep *steps; uint16_t stepCount; const SoundEvent *sounds; uint16_t soundCount; uint32_t lengthMs; uint8_t weight; uint8_t dismiss; int8_t boostNext; };');
c.push('struct Dismiss { const ClipStep *steps; uint16_t stepCount; const SoundEvent *sounds; uint16_t soundCount; };');
c.push('enum PickMode : uint8_t { PICK_UNIFORM, PICK_WEIGHTED, PICK_NO_REPEAT };');
c.push('// introEachLoop = 每輪開頭重播前奏 (單輪內本身沒有音效的主題)');
c.push('struct Theme { const char *name; const Clip *clips; uint8_t clipCount; PickMode pick; const SoundEvent *intro; uint16_t introCount; const Dismiss *dismiss; bool introEachLoop; };');
c.push('');
c.push('enum ThemeId : uint8_t {');
out.forEach((T, i) => c.push(`  THEME_${T.key.toUpperCase()} = ${i}, // ${T.name}`));
c.push('};');
c.push('');
c.push(`static const uint16_t FRAME_COUNT = ${framePoolList.length};`);
c.push(`static const uint8_t FRAMES[${framePoolList.length}][54] PROGMEM = {`);
for (const hex of framePoolList) c.push('  {' + hex.match(/../g).map(b => '0x' + b).join(',') + '},');
c.push('};');
c.push('');
const themeRows = [];
out.forEach((T, ti) => {
  const p = `T${ti}`;
  c.push(`// ===== ${ti + 1}. ${T.name} (${T.key}) =====`);
  const introN = sndArr(`${p}_INTRO`, T.intro || []);
  const dRows = T.dismiss.map((d, di) => {
    seqArr(`${p}_D${di}_STEPS`, d.seq);
    const n = sndArr(`${p}_D${di}_SND`, d.sounds);
    return `  {${p}_D${di}_STEPS, ${d.seq.length}, ${p}_D${di}_SND, ${n}},`;
  });
  c.push(`static const Dismiss ${p}_DISMISS[] PROGMEM = {`, ...dRows, '};');
  const cRows = T.clips.map((cl, ci) => {
    seqArr(`${p}_C${ci}_STEPS`, cl.seq);
    const n = sndArr(`${p}_C${ci}_SND`, cl.sounds);
    return `  {${p}_C${ci}_STEPS, ${cl.seq.length}, ${p}_C${ci}_SND, ${n}, ${cl.loop}, ${cl.weight}, ${cl.dismiss}, ${cl.boostNext}}, // ${cl.label}`;
  });
  c.push(`static const Clip ${p}_CLIPS[] PROGMEM = {`, ...cRows, '};');
  c.push('');
  themeRows.push(`  {"${T.key}", ${p}_CLIPS, ${T.clips.length}, ${PICK[T.pick]}, ${p}_INTRO, ${introN}, ${p}_DISMISS, ${T.introEachLoop ? 'true' : 'false'}},`);
});
c.push(`static const uint8_t THEME_COUNT = ${out.length};`);
c.push('static const Theme THEMES[THEME_COUNT] PROGMEM = {', ...themeRows, '};');
c.push('');
c.push('} // namespace AlarmClips');
fs.writeFileSync(path.join(ROOT, '..', 'firmware', 'AlarmClips.h'), c.join('\n') + '\n');

// 給 firmware_preview.html 的同一份資料
const js = {
  frames: framePoolList,
  themes: out.map(T => ({
    key: T.key, name: T.name, pick: T.pick, introEachLoop: T.introEachLoop,
    intro: (T.intro || []).map(s => [s.t, s.f1, s.f2, s.dur]),
    dismiss: T.dismiss.map(d => ({ seq: d.seq, sounds: d.sounds.map(s => [s.t, s.f1, s.f2, s.dur]) })),
    clips: T.clips.map(cl => ({ label: cl.label, weight: cl.weight, boostNext: cl.boostNext, loop: cl.loop, dismiss: cl.dismiss, seq: cl.seq, sounds: cl.sounds.map(s => [s.t, s.f1, s.f2, s.dur]) })),
  })),
};
fs.writeFileSync(path.join(ROOT, 'firmware_clips.js'), '// 自動產生 (tools/export_firmware.js)，請勿手動修改\nconst FIRMWARE_CLIPS = ' + JSON.stringify(js) + ';\n');
console.error('\n已輸出 firmware/AlarmClips.h 與 simulator/firmware_clips.js');
