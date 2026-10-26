# Pipeline 總表

**最近更新**：2026-10-26 — 改用 pipeline 格式整理（D-001）：照 README 的功能與架構切成 00～08；v0.3.1＋2026-08-02 稽核修正都完成，README「Next」的功能還沒開始。

狀態：⬜ 未開始 · 🟡 進行中 · ✅ 完成 · ⛔ 卡住 · ➖ 不需要

| # | 步驟 | 狀態 | 輸入 | 輸出 | 細節 |
|---|---|---|---|---|---|
| 00 | 需求與範圍 | ✅ | 現有 PDF 軟體的缺點 | 速度、克制、工藝三者兼顧的 PDF 檢視／編輯器 | [00-scope](00-scope/README.md) |
| 01 | PDF 引擎與繪製 | ✅ | PDF 檔、PDFium | 背景執行緒繪好的頁面（虛擬化、LRU 快取） | [01-engine-render](01-engine-render/README.md) |
| 02 | 閱讀、搜尋與列印 | ✅ | 已開啟的文件 | 搜尋、大綱、縮圖、選取、連結、加密檔、列印 | [02-reading](02-reading/README.md) |
| 03 | 編輯與存檔 | ✅ | 文件＋使用者的操作 | 標註、頁面操作、真塗黑、簽名、表單、文字修正、匯出 | [03-editing](03-editing/README.md) |
| 04 | OCR 與 Google Drive | ✅ | 掃描頁、Drive 上的檔 | 隱形文字層、存回 Drive | [04-ocr-drive](04-ocr-drive/README.md) |
| 05 | 介面與多語言 | ✅ | Lumen 設計系統、翻譯 JSON | QML 介面、四種語言、設定與最近檔案 | [05-ui-i18n](05-ui-i18n/README.md) |
| 06 | 測試與效能 | ✅ | 產生的 fixture、惡意檔 | 141 個 assertion、效能基準 | [06-tests-perf](06-tests-perf/README.md) |
| 07 | 打包與發佈 | ✅ | 建置結果 | 免安裝 zip、安裝檔、CI release、winget、自動更新 | [07-packaging-release](07-packaging-release/README.md) |
| 08 | 下一階段功能 | ⬜ | README「Next」清單 | 標註檢視、分頁、數位簽章、頁面組合、轉檔… | [08-roadmap](08-roadmap/README.md) |

## 流向

```
PDF 檔 ─ 01 PDFium 引擎（背景執行緒）─ 繪製快取 ─ QML 介面（05）
                ├─ 02 閱讀：搜尋／大綱／選取／連結／列印
                ├─ 03 編輯：標註／頁面／塗黑／表單／存檔
                └─ 04 OCR、Google Drive
06 測試（LUMEN_* hook＋JSON 報告）、效能基準 ─ 07 打包與發佈 ─ 08 下一階段
```

## 目前的阻塞

（無）

## 歷史

| 日期 | 變更 |
|---|---|
| 2026-10-26 | 改用 pipeline 格式整理（D-001）；之前的開發紀錄在 docs/DEVLOG.md |
