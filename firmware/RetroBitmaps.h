#pragma once
#include <Arduino.h>

/**
 * IKEA KORTLINJE 42×9 點陣 LED 專用 1-bit 圖元庫
 * 時鐘數字字型 (鬧鐘動畫改由 AlarmClips.h 預錄畫格播放；怪獸圖元在 MonsterSprites.h，由模擬器匯出)
 * 儲存於 ESP32 Flash (PROGMEM) 中，零 RAM 佔用
 */
namespace RetroBitmaps {

// 鬧鐘 13 大主題的動畫已在電腦上預先錄成畫格 (AlarmClips.h)，此處僅保留時鐘字型

// =========================================================================
// 8. 數字字體庫 (極簡 3x5、原廠 7x9 雙線粗體、折衷版 6x9 雙線粗胖字體)
// =========================================================================
const uint8_t DIGITS_3x5[10][5] PROGMEM = {
  { 0x7, 0x5, 0x5, 0x5, 0x7 }, // 0
  { 0x2, 0x6, 0x2, 0x2, 0x7 }, // 1
  { 0x7, 0x1, 0x7, 0x4, 0x7 }, // 2
  { 0x7, 0x1, 0x7, 0x1, 0x7 }, // 3
  { 0x5, 0x5, 0x7, 0x1, 0x1 }, // 4
  { 0x7, 0x4, 0x7, 0x1, 0x7 }, // 5
  { 0x7, 0x4, 0x7, 0x5, 0x7 }, // 6
  { 0x7, 0x1, 0x1, 0x1, 0x1 }, // 7
  { 0x7, 0x5, 0x7, 0x5, 0x7 }, // 8
  { 0x7, 0x5, 0x7, 0x1, 0x7 }  // 9
};

// 原廠 / VIPnytt 7x9 雙線加粗字體 (數字 1 寬 6 格)
const uint8_t DIGITS_KORTLINJE_7x9[10][9] PROGMEM = {
  { 0b0111110, 0b1100011, 0b1100011, 0b1100011, 0b1100011, 0b1100011, 0b1100011, 0b1100011, 0b0111110 }, // 0
  { 0b001100,  0b011100,  0b111100,  0b001100,  0b001100,  0b001100,  0b001100,  0b001100,  0b111111  }, // 1 (width 6)
  { 0b0111110, 0b1100011, 0b1100011, 0b0000110, 0b0001100, 0b0001100, 0b0011000, 0b0110000, 0b1111111 }, // 2
  { 0b0111110, 0b1100011, 0b1100011, 0b0000011, 0b0001110, 0b0000011, 0b1100011, 0b1100011, 0b0111110 }, // 3
  { 0b0000110, 0b0001110, 0b0011110, 0b0110110, 0b1100110, 0b1100110, 0b1111111, 0b0000110, 0b0000110 }, // 4
  { 0b1111111, 0b1100000, 0b1100000, 0b1100000, 0b1111110, 0b0000011, 0b0000011, 0b0000011, 0b1111110 }, // 5
  { 0b0111110, 0b1100011, 0b1100011, 0b1100000, 0b1111110, 0b1100011, 0b1100011, 0b1100011, 0b0111110 }, // 6
  { 0b1111111, 0b0000011, 0b0000011, 0b0000011, 0b0000110, 0b0000110, 0b0001100, 0b0001100, 0b0001100 }, // 7
  { 0b0111110, 0b1100011, 0b1100011, 0b1100011, 0b0111110, 0b1100011, 0b1100011, 0b1100011, 0b0111110 }, // 8
  { 0b0111110, 0b1100011, 0b1100011, 0b1100011, 0b0111111, 0b0000011, 0b1100011, 0b1100011, 0b0111110 }  // 9
};

// 折衷版 6x9 原廠雙線粗胖字體 (垂直筆畫 2 格粗)
const uint8_t DIGITS_BOLD_6x9[10][9] PROGMEM = {
  { 0x1E, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x1E }, // 0
  { 0x0C, 0x1C, 0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F }, // 1
  { 0x1E, 0x33, 0x03, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x3F }, // 2
  { 0x1E, 0x33, 0x03, 0x03, 0x0E, 0x03, 0x03, 0x33, 0x1E }, // 3
  { 0x06, 0x0E, 0x1E, 0x36, 0x36, 0x3F, 0x06, 0x06, 0x06 }, // 4
  { 0x3F, 0x30, 0x30, 0x30, 0x3E, 0x03, 0x03, 0x33, 0x1E }, // 5
  { 0x1E, 0x33, 0x30, 0x30, 0x3E, 0x33, 0x33, 0x33, 0x1E }, // 6
  { 0x3F, 0x03, 0x03, 0x06, 0x06, 0x0C, 0x0C, 0x0C, 0x0C }, // 7
  { 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x33, 0x1E }, // 8
  { 0x1E, 0x33, 0x33, 0x33, 0x1F, 0x03, 0x33, 0x33, 0x1E }  // 9
};

/**
 * 通用 Sprite 繪製公用函式 (支援 8-bit 與 16-bit 寬度點陣)
 * @param frame 42x9 畫面緩衝結構 (相容 IkeaMatrix::Frame)
 */
template <typename FrameType>
void draw(FrameType &frame, const uint8_t *bitmap, int w, int h, int x, int y, bool flipX = false) {
  for (int r = 0; r < h; r++) {
    int py = y + r;
    if (py < 0 || py >= 9) continue; // 超出垂直邊界安全裁剪 (支援大幅度跳躍超出螢幕)
    uint8_t rowByte = pgm_read_byte(&bitmap[r]);
    for (int c = 0; c < w; c++) {
      int px = x + c;
      if (px < 0 || px >= 42) continue; // 超出水平邊界安全裁剪
      int bitIdx = flipX ? c : (w - 1 - c);
      if (rowByte & (1 << bitIdx)) {
        frame.drawPixel(px, py, true);
      }
    }
  }
}

template <typename FrameType>
void draw16(FrameType &frame, const uint16_t *bitmap, int w, int h, int x, int y, bool flipX = false) {
  for (int r = 0; r < h; r++) {
    int py = y + r;
    if (py < 0 || py >= 9) continue;
    uint16_t rowWord = pgm_read_word(&bitmap[r]);
    for (int c = 0; c < w; c++) {
      int px = x + c;
      if (px < 0 || px >= 42) continue;
      int bitIdx = flipX ? c : (w - 1 - c);
      if (rowWord & (1 << bitIdx)) {
        frame.drawPixel(px, py, true);
      }
    }
  }
}

template <typename FrameType>
void drawDigit(FrameType &frame, int num, int x, int y) {
  if (num >= 0 && num <= 9) {
    draw(frame, DIGITS_3x5[num], 3, 5, x, y);
  }
}

template <typename FrameType>
void drawDigitBold6x9(FrameType &frame, int num, int x, int y) {
  if (num >= 0 && num <= 9) {
    draw(frame, DIGITS_BOLD_6x9[num], 6, 9, x, y);
  }
}

template <typename FrameType>
void drawDigitKortlinje7x9(FrameType &frame, int num, int x, int y) {
  if (num >= 0 && num <= 9) {
    draw(frame, DIGITS_KORTLINJE_7x9[num], num == 1 ? 6 : 7, 9, x, y);
  }
}

} // namespace RetroBitmaps
