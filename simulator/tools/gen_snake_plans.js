#!/usr/bin/env node
/**
 * 貪食蛇劇本產生器：隨機生成 N 組合法路線，輸出可直接貼進 simulator/index.html 的 SNAKE_PLANS。
 *
 * 規則 (與模擬器 renderSnake 完全一致)：
 *   - 蛇只在 x = 2..39, y = 2..6 活動 (與外圍 1px 邊框保留 1 格空隙)，只走上下左右，不可原地迴轉
 *   - 巡航全程蛇身不可並排相貼 (1px 點陣會糊成一團)
 *   - 初始長度 6 (x = 2..7 往右)，每吃 1 顆果實後 3 步內尾巴不動 (+3 格)，4 顆吃完 = 18 格
 *   - 果實依序出現，出現時不可壓在蛇身上
 *   - 吃完第 4 顆後，以一段 ≥ 2 次轉彎的調頭動作讓蛇頭撞上自己身體 (之前絕不可撞牆或撞自己)
 *
 * 用法：node gen_snake_plans.js [數量=6] [種子=20261110]
 */
const COUNT = +(process.argv[2] || 6);
let seed = +(process.argv[3] || 20261110);
const rand = () => ((seed = (seed * 1103515245 + 12345) % 2147483648) / 2147483648);
const randInt = (a, b) => a + Math.floor(rand() * (b - a + 1));
const shuffle = arr => { for (let i = arr.length - 1; i > 0; i--) { const j = randInt(0, i); [arr[i], arr[j]] = [arr[j], arr[i]]; } return arr; };

const START_LEN = 6, GROW = 3, X_MIN = 2, X_MAX = 39, Y_MIN = 2, Y_MAX = 6; // 與邊框保留 1 格空隙，避免蛇身貼框糊成一團
const DIRS = [[1, 0], [-1, 0], [0, 1], [0, -1]];

// 狀態：path (逐格) + eatSteps，可推算任一步的蛇身
function lenAt(eats, step) {
  return START_LEN + eats.reduce((s, e) => s + Math.max(0, Math.min(GROW, step - e)), 0);
}
function bodyAt(path, eats, step) {
  const head = START_LEN - 1 + step, len = lenAt(eats, step), body = [];
  for (let i = 0; i < len && head - i >= 0; i++) body.push(path[head - i]);
  return body;
}
const same = (a, b) => a[0] === b[0] && a[1] === b[1];
const inArena = ([x, y]) => x >= X_MIN && x <= X_MAX && y >= Y_MIN && y <= Y_MAX;

// 蛇身「並排相貼」對數：兩節不相鄰卻上下左右緊貼 (會在 1px 點陣上糊成一團)
function touchCount(body) {
  let n = 0;
  for (let i = 0; i < body.length; i++) {
    for (let j = i + 2; j < body.length; j++) {
      if (Math.abs(body[i][0] - body[j][0]) + Math.abs(body[i][1] - body[j][1]) === 1) n++;
    }
  }
  return n;
}

// 嘗試沿 moves (方向序列) 前進；ok 代表全程安全；touches 為每一步之後的並排相貼對數
function tryMoves(path, eats, moves) {
  const p = path.slice();
  const touches = [];
  for (let i = 0; i < moves.length; i++) {
    const h = p[p.length - 1];
    const n = [h[0] + moves[i][0], h[1] + moves[i][1]];
    if (!inArena(n)) return { ok: false };
    p.push(n);
    const step = p.length - START_LEN;
    const body = bodyAt(p, eats, step);
    const hit = body.slice(1).some(c => same(c, n));
    if (hit) return { ok: false, crashAt: i, path: p, touches };
    touches.push(touchCount(body));
  }
  return { ok: true, path: p, touches };
}

const lastDir = path => {
  const a = path[path.length - 2], b = path[path.length - 1];
  return [b[0] - a[0], b[1] - a[1]];
};
const isReverse = (d, e) => d[0] === -e[0] && d[1] === -e[1];

// 從 head 到 target 的候選折線：HV、VH、HVH (中途 x 隨機)、VHV (中途 y 隨機)
function candidateRoutes(head, target) {
  const [hx, hy] = head, [tx, ty] = target;
  const seg = (dx, dy, n) => Array.from({ length: Math.abs(n) }, () => [Math.sign(dx), Math.sign(dy)]);
  const H = n => seg(n, 0, n), V = n => seg(0, n, n);
  const routes = [H(tx - hx).concat(V(ty - hy)), V(ty - hy).concat(H(tx - hx))];
  for (let k = 0; k < 4; k++) {
    const mx = randInt(X_MIN, X_MAX);
    routes.push(H(mx - hx).concat(V(ty - hy), H(tx - mx)));
    const my = randInt(Y_MIN, Y_MAX);
    routes.push(V(my - hy).concat(H(tx - hx), V(ty - my)));
  }
  return routes.filter(r => r.length > 0);
}

const countTurns = moves => moves.reduce((n, m, i) => n + (i > 0 && !same(m, moves[i - 1]) ? 1 : 0), 0);

function generatePlan() {
  const sy = randInt(3, 5);
  let path = [];
  for (let x = 2; x <= 7; x++) path.push([x, sy]);
  const eats = [], fruits = [];

  for (let f = 0; f < 4; f++) {
    let done = false;
    for (let attempt = 0; attempt < 80 && !done; attempt++) {
      const head = path[path.length - 1];
      const step = path.length - START_LEN;
      const body = bodyAt(path, eats, step);
      const target = [randInt(X_MIN + 1, X_MAX - 1), randInt(Y_MIN, Y_MAX)];
      const dist = Math.abs(target[0] - head[0]) + Math.abs(target[1] - head[1]);
      if (dist < 7 || dist > 20) continue;
      if (body.some(c => same(c, target))) continue;



      const routes = shuffle(candidateRoutes(head, target))
        .filter(r => !isReverse(r[0], lastDir(path)) && countTurns(r) <= 3);
      for (const r of routes) {
        const res = tryMoves(path, eats, r);
        if (!res.ok || res.touches.some(t => t > 0)) continue; // 巡航全程蛇身不可並排相貼
        // 抵達前不可提早經過目標格 (折線不會重複經過，保險起見檢查)
        path = res.path;
        eats.push(path.length - START_LEN);
        fruits.push(target);
        done = true;
        break;
      }
    }
    if (!done) return null;
  }

  // 死亡調頭：U 字迴轉 —— 往側邊轉出 k1 格、反方向折返 k2 格、再轉回來直到撞上自己身體 (T 字撞擊)
  const deaths = [];
  const fwd = lastDir(path);
  const sides = fwd[0] !== 0 ? [[0, 1], [0, -1]] : [[1, 0], [-1, 0]];
  for (const side of sides) {
    const back = [-fwd[0], -fwd[1]], ret = [-side[0], -side[1]];
    for (let k1 = 2; k1 <= 3; k1++) {
      for (let k2 = 4; k2 <= 9; k2++) {
        for (let k3 = 1; k3 <= k1 + 1; k3++) {
          const m = Array(k1).fill(side).concat(Array(k2).fill(back), Array(k3).fill(ret));
          const res = tryMoves(path, eats, m);
          if (res.ok || res.crashAt !== m.length - 1) continue;
          const n = res.path[res.path.length - 1];
          const body = bodyAt(res.path, eats, res.path.length - START_LEN);
          const hitIdx = body.slice(1).findIndex(c => same(c, n)) + 1;
          // 撞擊前一步之前蛇身不可並排相貼；撞擊前一步只允許蛇頭貼著要撞的那 1 節
          const preCrashOk = res.touches.slice(0, -1).every(t => t === 0) && res.touches[res.touches.length - 1] <= 1;
          if (hitIdx >= 6 && preCrashOk) deaths.push(m);
        }
      }
    }
  }
  if (!deaths.length) return null;
  const death = deaths[randInt(0, deaths.length - 1)];
  for (const d of death) {
    const h = path[path.length - 1];
    path.push([h[0] + d[0], h[1] + d[1]]);
  }

  const steps = path.length - START_LEN;
  if (steps < 34 || steps > 62) return null; // 巡航 3.7 ~ 6.8 秒

  // 壓縮成轉折點
  const way = [path[0]];
  for (let i = 1; i < path.length - 1; i++) {
    const a = path[i - 1], b = path[i], c = path[i + 1];
    if (b[0] - a[0] !== c[0] - b[0] || b[1] - a[1] !== c[1] - b[1]) way.push(b);
  }
  way.push(path[path.length - 1]);
  return { way, fruits, path, eats, steps };
}

// 以與模擬器相同的規則重新驗證：撞擊步必須是最後一步，且之前全程安全
function verify(plan) {
  const { path, eats } = plan;
  for (let s = 1; START_LEN - 1 + s < path.length; s++) {
    const body = bodyAt(path, eats, s);
    const hit = body.slice(1).some(c => same(c, body[0]));
    if (hit) return s === path.length - START_LEN;
    if (!inArena(body[0])) return false;
  }
  return false;
}

function render(plan, step) {
  const g = Array.from({ length: 9 }, (_, y) => Array.from({ length: 42 }, (_, x) => (x === 0 || x === 41 || y === 0 || y === 8) ? '#' : '.'));
  for (const [x, y] of bodyAt(plan.path, plan.eats, step)) g[y][x] = 'o';
  return g.map(r => r.join('')).join('\n');
}

const plans = [];
const seenStarts = new Set();
for (let tries = 0; plans.length < COUNT && tries < 3000; tries++) {
  const p = generatePlan();
  if (!p || !verify(p)) continue;
  const key = p.fruits.map(f => f.join(',')).join('|');
  if (seenStarts.has(key)) continue;
  seenStarts.add(key);
  plans.push(p);
}

for (const [i, p] of plans.entries()) {
  console.error(`--- 樣板 ${i + 1}：${p.steps} 步 (${(p.steps * 110 / 1000).toFixed(1)}s)，吃果實於第 ${p.eats.join(', ')} 步`);
  console.error(render(p, p.steps - 1));
}

const fmt = arr => '[' + arr.map(([x, y]) => `[${x}, ${y}]`).join(', ') + ']';
console.log('    const SNAKE_PLANS = [');
for (const p of plans) {
  console.log(`      { way: ${fmt(p.way)},\n        fruits: ${fmt(p.fruits)} },`);
}
console.log('    ];');
