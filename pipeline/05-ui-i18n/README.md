# 05 — 介面與多語言

**狀態**：✅ 完成
**最近更新**：2026-10-26

## 目的
不像 Qt 的 Qt 程式：自己寫的設計系統；介面跟著系統語言。

## 輸入
- Lumen 設計系統、翻譯 JSON

## 輸出
- QML 介面；English、繁體中文、简体中文、日本語；設定與最近 12 個檔案

## 做法
- `qml/Lumen`：`Tokens`、`Motion`、`Squircle` 等元件，不用 Qt Quick Controls 內建樣式。
- `qml/App`：`Main.qml`、`PageView.qml`、各面板與 sheet。
- 翻譯：`translations/*.json`（`scripts/apply-translations.py`、`update-translations.ps1`），只提供翻完的語言。
- `src/app/Settings`；2026-08-02 修正英文介面被簡中取代（`7bfe373`）。

程式：`qml/Lumen/`、`qml/App/`、`translations/`、`src/app/Settings.cpp`

## 相關決策
- D-001（改用 pipeline 格式整理；這一步的設計理由見 README 與 docs/DEVLOG.md）

## 待辦
（目前沒有；有新的工作就寫在這裡）

## 紀錄
| 日期 | 內容 |
|---|---|
| 2026-10-26 | 從 README 與 docs/DEVLOG.md 整理成步驟檔 |
