#!/usr/bin/env node
/**
 * 公路賽車劇本產生器：隨機生成 N 組合法劇本，輸出可直接貼進 simulator/index.html 的 RACER_PLANS。
 *
 * 規則 (與模擬器 renderRacer 完全一致)：
 *   - 2 條車道 y = 1..3 / 5..7，玩家車 6x3 固定 x = 3..8，對向來車 6x3 自 x = 42 每 22ms 逼近 1px
 *   - 換道 160ms (y 線性移動四捨五入)
 *   - 閃避 3~5 次，與來車抵達的餘裕一次比一次小；閃避之間穿插對向車道「擦身而過」的來車
 *   - 最後一次以 3 種死法之一撞車：
 *       pincer = 前後錯開的兩台車夾擊，閃開第一台直接撞上第二台
 *       late   = 反應太慢，換道換到一半被車頭撞上
 *       swerve = 慌張提早換道，正面撞上對向車道來車
 *   - 以逐毫秒碰撞模擬驗證：最後一台車之前完全不可碰撞，且一定撞在劇本的最後一擊
 *
 * 用法：node gen_racer_plans.js [數量=6] [種子=20261111]
 */
const COUNT = +(process.argv[2] || 6);
let seed = +(process.argv[3] || 20261111);
const rand = () => ((seed = (seed * 1103515245 + 12345) % 2147483648) / 2147483648);
const randInt = (a, b) => a + Math.floor(rand() * (b - a + 1));
const pick = arr => arr[randInt(0, arr.length - 1)];

const COLS = 42, PLAYER_X = 3, ENEMY_PX_MS = 22, SWITCH_MS = 160, LANE_Y = [1, 5];
const ARRIVE = (COLS - (PLAYER_X + 5)) * ENEMY_PX_MS; // 生成到車頭碰到玩家車頭 (x=8) 的時間 = 748ms

function playerY(plan, t) {
  let y = LANE_Y[plan.start];
  for (const s of plan.switches) {
    if (t < s.t) break;
    const from = y, to = LANE_Y[s.lane];
    y = Math.round(from + (to - from) * Math.min(1, (t - s.t) / SWITCH_MS));
  }
  return y;
}
// 回傳第一次碰撞 { t, enemy } 或 null
function firstCrash(plan, until = 9000) {
  for (let t = 0; t < until; t++) {
    const py = playerY(plan, t);
    for (let i = 0; i < plan.enemies.length; i++) {
      const e = plan.enemies[i];
      if (t < e.spawn) continue;
      const ex = COLS - Math.floor((t - e.spawn) / ENEMY_PX_MS);
      const ey = LANE_Y[e.lane];
      if (ex <= PLAYER_X + 5 && ex + 5 >= PLAYER_X && ey <= py + 2 && ey + 2 >= py) return { t, enemy: i };
    }
  }
  return null;
}

function generatePlan() {
  const start = randInt(0, 1);
  const enemies = [], switches = [];
  let lane = start, t = randInt(150, 350);
  const dodges = randInt(3, 5);
  // 餘裕由寬到窄：第一次 ~260ms、最後一次 ~125ms (低於約 110ms 換道來不及會被擦撞)
  const margins = Array.from({ length: dodges }, (_, i) => Math.round(260 - (135 * i) / Math.max(1, dodges - 1) + randInt(-10, 10)));
  for (let k = 0; k < dodges; k++) {
    enemies.push({ spawn: t, lane });
    const sw = t + ARRIVE - margins[k];
    lane = 1 - lane;
    switches.push({ t: sw, lane });
    // 換道後偶爾從原車道後方再來一台擦身而過 (不需閃避)
    if (rand() < 0.45) enemies.push({ spawn: t + randInt(260, 420), lane: 1 - lane, pass: true });
    t += randInt(950, 1200);
  }
  const death = pick(['pincer', 'late', 'swerve']);
  let deathSwitch;
  if (death === 'pincer') {
    enemies.push({ spawn: t, lane });
    enemies.push({ spawn: t + randInt(130, 190), lane: 1 - lane, killer: true });
    deathSwitch = { t: t + ARRIVE - randInt(110, 170), lane: 1 - lane };
  } else if (death === 'late') {
    enemies.push({ spawn: t, lane, killer: true });
    deathSwitch = { t: t + ARRIVE - randInt(20, 60), lane: 1 - lane };
  } else {
    // 對向車道來車先到；自己車道的車還很遠就慌張換道，正面撞上
    enemies.push({ spawn: t, lane });
    enemies.push({ spawn: t - randInt(250, 380), lane: 1 - lane, killer: true });
    deathSwitch = { t: t - randInt(250, 380) + ARRIVE - randInt(300, 420), lane: 1 - lane };
  }
  switches.push(deathSwitch);
  enemies.sort((a, b) => a.spawn - b.spawn);
  switches.sort((a, b) => a.t - b.t);
  return { start, enemies, switches, death, dodges };
}

function verify(plan) {
  const c = firstCrash(plan);
  if (!c) return null;
  if (!plan.enemies[c.enemy].killer) return null;
  if (c.t < 3800 || c.t > 6600) return null;            // 撞車時間落在 3.8 ~ 6.6 秒
  if (plan.switches[plan.switches.length - 1].t > c.t) return null;
  // 撞擊後不應還有已生成卻沒用到的車 (撞車瞬間畫面凍結，晚生成的車永遠不會出現)
  plan.enemies = plan.enemies.filter(e => e.spawn <= c.t);
  // 換道之間至少間隔 300ms，避免抽搐
  for (let i = 1; i < plan.switches.length; i++) if (plan.switches[i].t - plan.switches[i - 1].t < 300) return null;
  return c;
}

function render(plan, t) {
  const g = Array.from({ length: 9 }, () => Array(COLS).fill('.'));
  const py = playerY(plan, t);
  for (const e of plan.enemies) {
    if (t < e.spawn) continue;
    const ex = COLS - Math.floor((t - e.spawn) / ENEMY_PX_MS);
    for (let dx = 0; dx < 6; dx++) for (let dy = 0; dy < 3; dy++) if (ex + dx >= 0 && ex + dx < COLS) g[LANE_Y[e.lane] + dy][ex + dx] = 'e';
  }
  for (let dx = 0; dx < 6; dx++) for (let dy = 0; dy < 3; dy++) g[py + dy][PLAYER_X + dx] = g[py + dy][PLAYER_X + dx] === 'e' ? 'X' : 'P';
  return g.map(r => r.join('')).join('\n');
}

const plans = [];
const quota = { pincer: 0, late: 0, swerve: 0 };
for (let tries = 0; plans.length < COUNT && tries < 20000; tries++) {
  const p = generatePlan();
  if (quota[p.death] >= Math.ceil(COUNT / 3)) continue; // 三種死法平均分配
  const c = verify(p);
  if (!c) continue;
  p.crash = c;
  quota[p.death]++;
  plans.push(p);
}

for (const [i, p] of plans.entries()) {
  console.error(`--- 劇本 ${i + 1}：${p.death}，閃避 ${p.dodges} 次，${p.enemies.length} 台車，撞車於 ${p.crash.t}ms`);
  console.error(render(p, p.crash.t));
}

console.log('    const RACER_PLANS = [');
for (const p of plans) {
  const en = p.enemies.map(e => `[${e.spawn}, ${e.lane}]`).join(', ');
  const sw = p.switches.map(s => `[${s.t}, ${s.lane}]`).join(', ');
  console.log(`      { death: '${p.death}', start: ${p.start},\n        enemies: [${en}],\n        switches: [${sw}] },`);
}
console.log('    ];');
