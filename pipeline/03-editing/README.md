# 03 — 編輯與存檔

**狀態**：✅ 完成
**最近更新**：2026-10-26

## 目的
常用的 PDF 編輯都能做、都能復原，存出其他軟體讀得懂的標準 PDF。

## 輸入
- 文件＋使用者的操作

## 輸出
- 標註（螢光、底線、刪除線）、頁面旋轉／排序／刪除、合併與抽頁、墨跡簽名、真塗黑、AcroForm 填寫、文字修正（Ctrl+E）、匯出 PNG／JPEG／文字、壓縮

## 做法
- `src/bridge`：`AnnotationController`、`PageOperations`（undo／redo）、`RedactionController`、`FormController`、`ExportController`。
- 真塗黑：頁面點陣化、物件換掉，測試看存檔後抽出的文字長度變 0。
- 文字修正：每次編輯讀回驗證，字型子集沒有的字就還原並說明。
- 2026-08-02 稽核修正：頁面結構變動時丟掉握住的表單頁（`d65b940`）、塗黑只回報真的處理到的頁（`4d1ed94`）、undo／redo 測試真的跑到 redo（`87881ab`）。

程式：`src/bridge/AnnotationController.cpp`、`src/bridge/PageOperations.cpp`、`src/bridge/RedactionController.cpp`、`src/bridge/FormController.cpp`

## 相關決策
- D-001（改用 pipeline 格式整理；這一步的設計理由見 README 與 docs/DEVLOG.md）

## 待辦
（目前沒有；有新的工作就寫在這裡）

## 紀錄
| 日期 | 內容 |
|---|---|
| 2026-10-26 | 從 README 與 docs/DEVLOG.md 整理成步驟檔 |
