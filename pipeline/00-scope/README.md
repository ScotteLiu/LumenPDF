# 00 — 需求與範圍

**狀態**：✅ 完成
**最近更新**：2026-10-26

## 目的
一個依序追求**速度、克制、工藝**的 PDF 檢視與編輯器：比 Acrobat 快、比 SumatraPDF 能編輯、比 Stirling-PDF 一致。

## 輸入
- 現有 PDF 軟體的缺點（README 開頭）

## 輸出
- Windows 優先的桌面程式（Qt 6 Quick＋PDFium），Apache 2.0 開源

## 做法
- GUI 執行緒絕不碰 PDFium；頁面在背景 pool 繪製，畫面虛擬化（README「Architecture」）。
- 不用 MuPDF（AGPL）；Qt 動態連結（LGPLv3）。
- 授權：先 BSL 1.1，2026-08-01 改 Apache 2.0（README「Licensing」、DEVLOG）。
- 誠實原則：做不到的就說做不到（文字修正驗證失敗就還原、塗黑會失去可選文字、簽名只是墨跡章）。

程式：`README.md`、`docs/DEVLOG.md`

## 相關決策
- D-001（改用 pipeline 格式整理；這一步的設計理由見 README 與 docs/DEVLOG.md）

## 待辦
（目前沒有；有新的工作就寫在這裡）

## 紀錄
| 日期 | 內容 |
|---|---|
| 2026-10-26 | 從 README 與 docs/DEVLOG.md 整理成步驟檔 |
