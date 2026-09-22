# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 專案概述

LilyGo T5 2.13" e-paper (ESP32) 桌上月曆。單一 Arduino sketch `esp32_epaper_calendar/esp32_epaper_calendar.ino`，每天午夜醒來一次：連 WiFi → 向 time.stdtime.gov.tw 校時 → 重畫整月月曆與電量 → 算出到隔天 00:00:30 的秒數後 deep sleep。

## 建置與燒錄

- 沒有 arduino-cli / PlatformIO 設定，用 Arduino IDE 開 .ino；ESP32 core 3.0.4，板子選 ESP32 Dev Module。
- GxEPD 3.1.3 (非 GxEPD2) 隨 repo 放在 `library/GxEPD/`，Arduino IDE 不會自動找到，須複製或 symlink 到 `~/Documents/Arduino/libraries/`；相依的 Adafruit GFX 已裝在該目錄。
- 沒有自動化測試，驗證方式是燒錄後看 Serial (115200) 印出的電壓、時間與 TIME_TO_MIDNIGHT。
- WiFi 帳密 CONST_SSID / CONST_PSWD 在 sketch 頂端寫死為 `*****`，燒錄前填自己的，commit 前改回去。

## 程式結構 (單檔)

- 腳位：SPI MOSI 23 / CLK 18 / CS 5，e-paper BUSY 4 / RST 16 / DC 17，電池 ADC 35，按鈕 39 (目前沒用到)。面板驅動 `GxGDEH0213B73`，setRotation(1) 後畫布 250x122。
- setup() 只重置 GPIO、初始化螢幕、設時區 (TZ 寫死 CST-8)。所有工作在 loop() 且只跑一次，結尾就 deep sleep；WiFi 連不上時休眠 WIFI_RETRY_SEC (30 分鐘) 再重試。校時成功後先關 WiFi 再畫圖。
- isFirstBootUp() 用 getLocalTime() 是否成功判斷「還沒校過時」，決定要不要在螢幕顯示 WIFI Connect... / IP 的過場畫面。
- UpdateScreen() 的座標全是手算絕對值 (格子 27x17、右側欄寬 66)，改版面要一起調。
- 電量：ADC 讀值 ×2.2 (分壓電阻誤差補償) 換算 3.3V，再把 3.2V~3.7V 線性映射到 0~99%。
- 閏年只看 %4；daysOfMonth 的 month 是 0-based，與 tm_mon 一致。

## 其他目錄

- `3Dstl/`：外殼 STL (Thingiverse thing:4055993)。
- `image/`：README 用的實機照片。
