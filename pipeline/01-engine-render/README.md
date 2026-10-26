# 01 — PDF 引擎與繪製

**狀態**：✅ 完成
**最近更新**：2026-10-26

## 目的
載入 PDF、算頁面幾何、背景繪製；1000 頁和 3 頁一樣輕。

## 輸入
- PDF 檔、PDFium（`scripts/fetch-pdfium.ps1` 下載，不進 repo）

## 輸出
- 繪好的頁面貼圖（QML image provider）

## 做法
- `src/core`：`PdfEngine`、`PdfDocument`（載入、幾何快取、`renderPage` 有 40 megapixel 像素預算）。
- `src/render`：`PageRenderCache`（LRU）、`PageImageProvider`（worker thread pool）。
- 沒有 PDFium 時退回 stub renderer，可以只做 UI。
- 2026-08-02：hover 路徑不再呼叫 PDFium（`1d3443e`，`docs/OPTIMISATION-PLAN.md` item 4）。

程式：`src/core/`、`src/render/`

## 相關決策
- D-001（改用 pipeline 格式整理；這一步的設計理由見 README 與 docs/DEVLOG.md）

## 待辦
（目前沒有；有新的工作就寫在這裡）

## 紀錄
| 日期 | 內容 |
|---|---|
| 2026-10-26 | 從 README 與 docs/DEVLOG.md 整理成步驟檔 |
