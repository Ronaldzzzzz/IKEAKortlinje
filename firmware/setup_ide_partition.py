#!/usr/bin/env python3
"""
IKEA KORTLINJE 時鐘專用：自動安裝 2 x 1920 KB OTA 分區表至 Arduino IDE ESP32 開發板設定中。
支援 macOS, Windows 與 Linux 之 Arduino 15 路徑自動偵測。
"""
import os
import sys
from pathlib import Path

CANDIDATES = [
    # macOS
    Path.home() / 'Library/Arduino15/packages/esp32/hardware/esp32',
    # Windows
    Path(os.environ.get('LOCALAPPDATA', 'C:/Users/Default/AppData/Local')) / 'Arduino15/packages/esp32/hardware/esp32',
    # Linux
    Path.home() / '.arduino15/packages/esp32/hardware/esp32'
]

def find_latest_core():
    for base in CANDIDATES:
        if base.is_dir():
            versions = sorted([p for p in base.iterdir() if p.is_dir() and (p / 'boards.txt').is_file()], reverse=True)
            if versions:
                return versions[0]
    return None

def install_partition(core_dir: Path):
    boards_txt = core_dir / 'boards.txt'
    if not boards_txt.is_file():
        print(f"錯誤：在 {core_dir} 未找到 boards.txt")
        return False

    boards_local = core_dir / 'boards.local.txt'
    content = boards_local.read_text(encoding='utf-8') if boards_local.exists() else ''

    snippet = '''
# BEGIN IKEA PIXEL CLOCK (2 x 1920 KB OTA)
XIAO_ESP32C6.menu.PartitionScheme.ikea_clock=IKEA Clock (2 x 1920 KB OTA)
XIAO_ESP32C6.menu.PartitionScheme.ikea_clock.build.partitions=min_spiffs
XIAO_ESP32C6.menu.PartitionScheme.ikea_clock.upload.maximum_size=1966080

esp32c3.menu.PartitionScheme.ikea_clock=IKEA Clock (2 x 1920 KB OTA)
esp32c3.menu.PartitionScheme.ikea_clock.build.partitions=min_spiffs
esp32c3.menu.PartitionScheme.ikea_clock.upload.maximum_size=1966080
# END IKEA PIXEL CLOCK
'''
    if '# BEGIN IKEA PIXEL CLOCK' in content:
        print(f"已安裝過分區表設定：{boards_local}")
        return True

    content += snippet
    boards_local.write_text(content, encoding='utf-8')
    print(f"成功安裝自訂分區表至：{boards_local}")
    print("請重新啟動 Arduino IDE，並在「工具」->「Partition Scheme」中選取「IKEA Clock (2 x 1920 KB OTA)」。")
    return True

if __name__ == '__main__':
    if len(sys.argv) > 1:
        target = Path(sys.argv[1]).resolve()
    else:
        target = find_latest_core()

    if not target:
        print("未自動偵測到 Arduino ESP32 開發板安裝路徑。")
        print("用法：python3 setup_ide_partition.py <ESP32開發板路徑>")
        sys.exit(1)

    print(f"偵測到 ESP32 Core 版本目錄：{target}")
    install_partition(target)
