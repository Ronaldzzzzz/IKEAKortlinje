# IKEA KORTLINJE 復古互動時鐘

把 IKEA KORTLINJE 42×9 LED 點陣鬧鐘改裝成 **Seeed XIAO ESP32-C6** 智慧時鐘：Wi-Fi / NTP 校時、OTA 無線更新，再加上 **13 款復古遊戲主題的隨機盲盒鬧鐘**與原創的**像素怪獸養成**。

> 目前版本 **v0.1**：軟體邏輯與模擬器階段。按鍵映射、聲音與實機整合待硬體焊接完成後處理。詳細拆機與焊接教學（含照片）整理中。

## 快速體驗（不需要硬體）

用瀏覽器直接開啟 `simulator/index.html`：

* **時鐘**：兩種樣式（原廠 7×9 粗體 / 折衷版含秒數）。
* **鬧鐘**：13 款主題逐一預覽，按任一鍵觸發反轉彩蛋。
* **像素怪獸**：餵食、訓練、打掃、對戰、圖鑑；下方測試面板可以快轉時間。

`simulator/firmware_preview.html` 則以與 ESP32 韌體完全相同的規則播放匯出的鬧鐘資料。

## 專案結構

```
├── ikea_clock_modding_guide.md      硬體改裝指南 (材料、接線、燒錄、OTA)
├── retro_clock_features_spec.md     功能規格書 (13 大主題、像素怪獸、軟體架構、延後事項)
├── docs/images/                     教學圖片與示意圖
├── CHANGELOG.md                     版本變更
├── LICENSE                          MIT License
├── simulator/
│   ├── index.html                   42×9 模擬器 (所有場景與怪獸的唯一原始碼)
│   ├── firmware_preview.html        韌體播放預覽
│   └── tools/                       劇本產生器 + 韌體資料匯出工具 (Node.js)
└── firmware/                        ESP32 標頭檔 (鬧鐘播放引擎、像素怪獸、蜂鳴器、字型)
```

## 開發流程

圖形與動畫只寫在模擬器裡，韌體資料一律由工具匯出：

```bash
node simulator/tools/export_firmware.js   # 鬧鐘主題 → firmware/AlarmClips.h
node simulator/tools/export_monster.js    # 怪獸圖元 → firmware/MonsterSprites.h
```

## 致謝

本專案整合以下兩個開源專案的硬體逆向研究與時鐘基礎，在此致謝：

* [`kobebeefg/ikea-clock-lab`](https://github.com/kobebeefg/ikea-clock-lab)（經原作者同意，其程式碼可在本專案中使用並以 MIT 授權發佈；著作權屬原作者）
* [`VIPnytt/Kortlinje`](https://github.com/VIPnytt/Kortlinje)（MIT License，著作權屬原作者）

v0.1 只參考上述專案的硬體研究成果並附上連結，尚未收錄其原始碼；日後引用其程式碼時，會在對應檔案註明出處與原作者著作權。

## AI 輔助開發

本專案在 AI（Anthropic Claude）輔助下開發：規格討論、模擬器、匯出工具、韌體與文件皆由作者與 AI 協作完成，並由作者審閱與決定。

## 免責聲明

* **非官方專案**：本專案與 IKEA 無關，未經 IKEA 認可或贊助。KORTLINJE 為 IKEA 的產品名稱。
* **遊戲致敬**：鬧鐘主題是向經典遊戲致敬的 42×9 點陣原創演繹，未使用任何原作素材。文中提到的遊戲名稱（如 Pac-Man、Super Mario、Pokémon、Space Invaders、Tetris、Flappy Bird、Chrome 離線恐龍遊戲等）及相關商標，皆屬各自權利人所有，本專案與這些公司無關。像素怪獸角色全部原創。
* **改裝風險自負**：拆機與焊接會使產品失去保固，並可能損壞裝置或造成觸電、短路、燙傷等危險。請在斷電狀態下作業，並自行評估能力與風險。依照本專案操作所造成的任何損失，作者不負任何責任。
* v0.1 韌體尚未經實機驗證。

## 授權

[MIT License](LICENSE)。
