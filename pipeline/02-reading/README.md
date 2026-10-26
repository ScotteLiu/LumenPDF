# 02 — 閱讀、搜尋與列印

**狀態**：✅ 完成
**最近更新**：2026-10-26

## 目的
開檔、捲動、縮放、找字、選字、跟連結、印出來；各種文字系統都正確。

## 輸入
- 已開啟的文件

## 輸出
- 縮圖、大綱、全文搜尋（245 頁 1028 筆 130 ms）、跨欄跨頁選取、連結（先顯示目標）、密碼檔、列印（含標註與 OCR 層）

## 做法
- `src/bridge`：`SearchController`、`OutlineModel`、`PageListModel`、`SelectionController`、`PrintController`、`DocumentController`。
- 十種文字系統的抽字回歸測試；中文四個 bug 已修並有測試（DEVLOG 2026-07-30）。
- 連結只交 `http`／`https`／`mailto` 給 shell；`Launch` 動作不當連結。
- 密碼只用一次、不存（README「Encrypted documents」）。

程式：`src/bridge/SearchController.cpp`、`src/bridge/SelectionController.cpp`、`src/bridge/PrintController.cpp`

## 相關決策
- D-001（改用 pipeline 格式整理；這一步的設計理由見 README 與 docs/DEVLOG.md）

## 待辦
（目前沒有；有新的工作就寫在這裡）

## 紀錄
| 日期 | 內容 |
|---|---|
| 2026-10-26 | 從 README 與 docs/DEVLOG.md 整理成步驟檔 |
