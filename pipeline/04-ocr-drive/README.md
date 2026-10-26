# 04 — OCR 與 Google Drive

**狀態**：✅ 完成
**最近更新**：2026-10-26

## 目的
掃描頁變得可搜尋、可選取；Drive 上的檔能開、改、存回同一個檔。

## 輸入
- 掃描的頁面、Google Drive 上的 PDF

## 輸出
- 圖片底下的隱形文字層；存回 Drive（保留檔案 ID 與分享）

## 做法
- `src/ocr/OcrEngine`：用 Windows 內建辨識器，離線、用系統已有的語言。
- `src/cloud`：`GoogleAuth`（PKCE；2026-08-02 加 `state`、listener 會逾時，`62bd36f`）、`GoogleDrive`（`drive.file` 權限）。
- 使用者要自己的 OAuth client：`docs/GOOGLE-DRIVE-SETUP.md`。

程式：`src/ocr/`、`src/cloud/`

## 相關決策
- D-001（改用 pipeline 格式整理；這一步的設計理由見 README 與 docs/DEVLOG.md）

## 待辦
（目前沒有；有新的工作就寫在這裡）

## 紀錄
| 日期 | 內容 |
|---|---|
| 2026-10-26 | 從 README 與 docs/DEVLOG.md 整理成步驟檔 |
