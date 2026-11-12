#!/usr/bin/env node
/**
 * 像素怪獸圖元匯出工具：從 simulator/index.html 讀出怪獸造型 (MON_FORMS，含 12 物種展開後的 36 張)、
 * 圖示 (MON_ICONS) 與 3x5 文字字型，輸出成 ESP32 韌體用的 PROGMEM 資料 firmware/MonsterSprites.h。
 *
 * 圖元的唯一來源是模擬器；改造型 → 模擬器確認 → 重新執行本工具 → 重新燒錄。
 *
 * 輸出格式：
 *   - 每列一個 uint16_t，第 c 欄 = bit (w - 1 - c) (與 RetroBitmaps::draw 相同)，寬度上限 16
 *   - 怪獸每幀兩張遮罩：body ('#' 恆亮) 與 eyes ('o'，睡覺時亮 = 閉眼)
 *   - 造型編號：0 蛋、1 幼年期、2 史萊姆、3 蝙蝠、4 + 物種 * 3 + (階段 - 2) = 各物種成長期 / 成熟期 / 完全體
 *
 * 用法：node tools/export_monster.js
 */
const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..');
const html = fs.readFileSync(path.join(ROOT, 'index.html'), 'utf8');
const SCRIPT = html.slice(html.indexOf('<script>') + 8, html.lastIndexOf('</script>'));

const el = () => ({ classList: { add() {}, remove() {}, toggle() {} }, style: {}, textContent: '', value: '', addEventListener() {},
                    getContext: () => new Proxy({}, { get: () => () => {} }) });
const g = { document: { getElementById: el, addEventListener() {}, querySelectorAll: () => [] }, window: { addEventListener() {} },
            performance: { now: () => 0 }, requestAnimationFrame: () => {}, setTimeout: () => 0, clearTimeout() {}, console };
const names = Object.keys(g);
const S = new Function(...names, `${SCRIPT}
;return { MON_FORMS, MON_SPECIES, MON_ICONS, TEXT_FONT_3x5, NUM_FONT_3x5, monFormKey };`)(...names.map(n => g[n]));

const errors = [];
const hex = (v, w) => '0x' + v.toString(16).toUpperCase().padStart(Math.ceil(w / 4), '0');
function mask(rows, chars) {
  const w = rows[0].length;
  return rows.map((r, y) => {
    if (r.length !== w) errors.push(`列寬不一致：${JSON.stringify(rows)} 第 ${y} 列`);
    let v = 0;
    for (let c = 0; c < w; c++) if (chars.includes(r[c])) v |= 1 << (w - 1 - c);
    return v;
  });
}

// ---------- 造型 ----------
const order = ['egg', 'baby', 'slime', 'bat'];
S.MON_SPECIES.forEach((_, sp) => { for (let st = 2; st <= 4; st++) order.push(S.monFormKey(sp, st)); });
const missing = Object.keys(S.MON_FORMS).filter(k => !order.includes(k));
if (missing.length) errors.push('MON_FORMS 有未匯出的造型：' + missing.join(', '));

let maxH = 0;
const forms = order.map(key => {
  const f = S.MON_FORMS[key];
  if (!f) { errors.push('找不到造型 ' + key); return null; }
  const frames = f.frames.length === 1 ? [f.frames[0], f.frames[0]] : f.frames;
  if (frames.length !== 2) errors.push(`${key} 幀數 ${frames.length} (只支援 1 或 2)`);
  const w = frames[0][0].length;
  if (w > 16) errors.push(`${key} 寬 ${w} > 16`);
  frames.forEach(fr => { if (fr[0].length !== w) errors.push(`${key} 兩幀寬度不同`); if (fr.length > 9) errors.push(`${key} 高 ${fr.length} > 9`); maxH = Math.max(maxH, fr.length); });
  return { key, name: f.name, stage: f.stage, species: f.species === undefined ? -1 : f.species, w,
           h: frames.map(fr => fr.length), body: frames.map(fr => mask(fr, '#')), eyes: frames.map(fr => mask(fr, 'o')) };
});

// ---------- 圖示 ----------
const icons = Object.entries(S.MON_ICONS).map(([k, rows]) => ({ k, w: rows[0].length, h: rows.length, rows: mask(rows, '#') }));
icons.forEach(i => { if (i.w > 16 || i.h > 9) errors.push(`圖示 ${i.k} 太大`); });

// ---------- 字型 (數字 + 文字) ----------
const glyphs = [];
for (let d = 0; d <= 9; d++) glyphs.push({ ch: String(d), w: 3, rows: S.NUM_FONT_3x5[d] });
for (const [ch, gl] of Object.entries(S.TEXT_FONT_3x5)) glyphs.push({ ch, w: gl.w, rows: gl.r });

if (errors.length) { console.error('❌ 匯出失敗：\n  ' + errors.join('\n  ')); process.exit(1); }

// ---------- 輸出 ----------
const pad = (arr, n, fill = 0) => [...arr, ...Array(n - arr.length).fill(fill)];
const L = [];
L.push('#pragma once');
L.push('// ⚠️ 自動產生的檔案，請勿手動修改 —— 來源：simulator/index.html (MON_FORMS / MON_SPECIES / MON_ICONS / TEXT_FONT_3x5)');
L.push('// 產生器：simulator/tools/export_monster.js');
L.push('#include <Arduino.h>');
L.push('');
L.push('namespace MonsterSprites {');
L.push('');
L.push('// 每列 uint16_t：第 c 欄 = bit (w - 1 - c)。body = 恆亮，eyes = 睡覺時才亮 (閉眼)');
L.push('struct Form { uint8_t w; uint8_t h[2]; uint8_t stage; int8_t species; uint16_t body[2][9]; uint16_t eyes[2][9]; };');
L.push('struct Icon { uint8_t w, h; uint16_t rows[9]; };');
L.push('struct Glyph { char ch; uint8_t w; uint8_t rows[5]; };');
L.push('');
L.push(`static const uint8_t SPECIES_COUNT = ${S.MON_SPECIES.length};`);
L.push(`static const uint8_t FORM_COUNT = ${forms.length};`);
L.push('enum FormId : uint8_t { FORM_EGG = 0, FORM_BABY = 1, FORM_SLIME = 2, FORM_BAT = 3, FORM_SPECIES_BASE = 4 };');
L.push('// 物種 sp 的階段 stage (2 成長期、3 成熟期、4 完全體) 造型編號');
L.push('constexpr uint8_t speciesForm(uint8_t sp, uint8_t stage) { return FORM_SPECIES_BASE + sp * 3 + (stage - 2); }');
L.push('');
L.push('static const Form FORMS[FORM_COUNT] PROGMEM = {');
forms.forEach((f, i) => {
  const arr = m => '{ ' + m.map(fr => '{ ' + pad(fr, 9).map(v => hex(v, 16)).join(', ') + ' }').join(', ') + ' }';
  L.push(`  { ${f.w}, { ${f.h[0]}, ${f.h[1]} }, ${f.stage}, ${f.species}, ${arr(f.body)}, ${arr(f.eyes)} }, // ${i}: ${f.key} ${f.name}`);
});
L.push('};');
L.push('');
L.push('// 造型名稱 (除錯 / 序列埠輸出用，UTF-8)');
L.push('static const char *const FORM_NAMES[FORM_COUNT] = { ' + forms.map(f => JSON.stringify(f.name)).join(', ') + ' };');
L.push('static const char *const SPECIES_NAMES[SPECIES_COUNT] = { ' + S.MON_SPECIES.map(s => JSON.stringify(s.name)).join(', ') + ' };');
L.push('');
L.push('enum IconId : uint8_t { ' + icons.map((ic, i) => `ICON_${ic.k.toUpperCase()} = ${i}`).join(', ') + `, ICON_COUNT = ${icons.length} };`);
L.push('static const Icon ICONS[ICON_COUNT] PROGMEM = {');
icons.forEach(ic => L.push(`  { ${ic.w}, ${ic.h}, { ${pad(ic.rows, 9).map(v => hex(v, 8)).join(', ')} } }, // ${ic.k}`));
L.push('};');
L.push('');
L.push(`static const uint8_t GLYPH_COUNT = ${glyphs.length};`);
L.push('static const Glyph GLYPHS[GLYPH_COUNT] PROGMEM = {');
L.push('  ' + glyphs.map(gl => `{ '${gl.ch === "'" ? "\\'" : gl.ch}', ${gl.w}, { ${gl.rows.join(', ')} } }`).join(',\n  '));
L.push('};');
L.push('');
L.push('} // namespace MonsterSprites');
L.push('');

const out = path.join(ROOT, '..', 'firmware', 'MonsterSprites.h');
fs.writeFileSync(out, L.join('\n'));
console.error(`✅ 已輸出 firmware/MonsterSprites.h：${forms.length} 種造型 (最高 ${maxH})、${icons.length} 個圖示、${glyphs.length} 個字元`);
