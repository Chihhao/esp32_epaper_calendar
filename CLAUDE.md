# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 專案概述

LilyGo T5 2.13" e-paper (ESP32) 桌上月曆。單一 Arduino sketch `esp32_epaper_calendar/esp32_epaper_calendar.ino`，平常在 deep sleep，每天醒來兩次：

- **00:00:30**：不連任何網路，直接用 RTC 時間換日重畫月曆。
- **09:00:30**：開 BLE 廣播 30 秒讓 PC 校時 (`tools/ble_time_sync.py`)；日期有變才重畫。

時間來源優先序：BLE → WiFi (僅當 CONST_SSID 不是 `*****`) → RTC 現有時間。冷開機時三者皆無，螢幕顯示等待畫面並睡 SYNC_RETRY_SEC (30 分鐘) 再試。

## 建置與燒錄

- 沒有 arduino-cli / PlatformIO 設定，用 Arduino IDE 開 .ino；ESP32 core 3.0.4，板子選 ESP32 Dev Module。
- **分割方式必須選 Huge APP (3MB No OTA/1MB SPIFFS)**，BLE 程式庫讓韌體約 1.8MB，預設的 1.3MB 分割放不下。
- GxEPD 3.1.3 (非 GxEPD2) 隨 repo 放在 `library/GxEPD/`，Arduino IDE 不會自動找到，須複製或 symlink 到 `~/Documents/Arduino/libraries/`；相依的 Adafruit GFX 已裝在該目錄。BLE 用 core 內建程式庫，不用另外裝。
- 沒有自動化測試，驗證方式是燒錄後看 Serial (115200) 印出的電壓、haveTime/wakeForSync、BLE 狀態與下次醒來秒數。
- WiFi 帳密 CONST_SSID / CONST_PSWD 在 sketch 頂端寫死為 `*****`，要用 WiFi 備援才填，commit 前改回去。

## 程式結構 (單檔)

- 腳位：SPI MOSI 23 / CLK 18 / CS 5，e-paper BUSY 4 / RST 16 / DC 17，電池 ADC 35，按鈕 39 (目前沒用到)。面板驅動 `GxGDEH0213B73`，setRotation(1) 後畫布 250x122。
- setup() 只重置 GPIO、初始化螢幕 (不刷新)、設時區 (TZ 寫死 CST-8)。所有工作在 loop() 且只跑一次，結尾一定進 deep sleep。
- 跨睡眠狀態靠 `RTC_DATA_ATTR`：`rtcWakeForSync` 標記這次醒來要不要開 BLE，`rtcLastDrawnDay` 記上次畫的日期避免重複刷新。
- BLE 校時：PC 端往特徵值寫 UTC epoch 秒數 (ASCII 十進位或 4 bytes little-endian)，小於 1600000000 一律當錯的丟掉；收到就 settimeofday() 並提早結束廣播。UUID 與裝置名稱在 sketch 頂端的 BLE_ 開頭常數，改了要同步改 `tools/ble_time_sync.py`。
- **不要呼叫 `esp_sleep_pd_config()`**：ESP32 core 3.x (IDF 5) 改成引用計數，沒 ON 過就 OFF 會 assert 當機重開；deep sleep 預設就會關掉那些電源域。
- UpdateScreen() 的座標全是手算絕對值 (格子 27x17、右側欄寬 66)，改版面要一起調。
- 電量：ADC 讀值 ×2.2 (分壓電阻誤差補償) 換算 3.3V，再把 3.2V~3.7V 線性映射到 0~99%。
- 閏年只看 %4；daysOfMonth 的 month 是 0-based，與 tm_mon 一致。

## 其他目錄

- `tools/ble_time_sync.py`：PC 端常駐校時程式 (Python 3.8+ 與 bleak)，`--once` 只跑一次供測試。
- `3Dstl/`：外殼 STL (Thingiverse thing:4055993)。
- `image/`：README 用的實機照片。
