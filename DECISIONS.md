# 決策紀錄

格式：每條一個編號，不刪除；被推翻的標「已被 D-00N 取代」。

**之前的設計決定**寫在 README（「Architecture」「The design system」「Licensing」）、`docs/DEVLOG.md`（每天的開發紀錄與理由）和 `docs/OPTIMISATION-PLAN.md`（2026-08-02 稽核）；這個專案沒有 CHANGELOG.md。從 2026-10-26 起新的決定記在這裡。

---

## D-001 — 改用 pipeline 格式整理專案文件
- **日期**：2026-10-26
- **誰決定**：使用者（「好的，你想幫我去轉，然後順序由你來定，開始吧」——所有專案轉成 ai-standard 的 pipeline 格式）
- **決定**：加 `pipeline/PIPELINE.md`＋每步一份 README（照 README 的功能與架構切成 00 範圍、01 PDF 引擎與繪製、02 閱讀搜尋列印、03 編輯與存檔、04 OCR 與 Google Drive、05 介面與多語言、06 測試與效能、07 打包與發佈、08 下一階段功能）、`DECISIONS.md`、`logs/conversations/`。**原本的 README、docs、程式都不動**；pipeline 文件是索引，細節還是以 README／DEVLOG 為準。
- **理由**：讓專案儀表板（project-dashboard）看得到進度和下一步；所有專案同一套格式。
- **待確認**：步驟的切法、各步的狀態與待辦是 Claude 從 README／DEVLOG／git log 整理的；08 的內容就是 README「Next」清單。
