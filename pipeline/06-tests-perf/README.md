# 06 — 測試與效能

**狀態**：✅ 完成
**最近更新**：2026-10-26

## 目的
每個功能有不靠截圖的測試；速度的說法都量過、有基準擋回歸。

## 輸入
- 程式產生的 fixture（拉丁、CJK、十種文字、表單、連結、加密）、十個惡意檔

## 輸出
- `scripts/run-tests.ps1`（141 個 assertion，CI 每次 push 都跑）、`docs/perf-baseline.json`

## 做法
- 測試用 `LUMEN_*` hook 驅動 app，透過 `src/app/StateReport` 寫 JSON 狀態報告再斷言。
- fixture：`src/app/TestFixtures.cpp`、`scripts/make-*-fixture.ps1`；惡意檔要被拒絕而不是當掉或吃光記憶體。
- 效能：`scripts/benchmark.ps1`（5 次取中位數）；2026-08-02 稽核（`docs/OPTIMISATION-PLAN.md`）十項全做完，找到並修好 db222a8 的啟動回歸，加上效能基準閘（`1a340e7`）。

程式：`scripts/run-tests.ps1`、`scripts/benchmark.ps1`、`src/app/StateReport.cpp`、`.github/workflows/build.yml`

## 相關決策
- D-001（改用 pipeline 格式整理；這一步的設計理由見 README 與 docs/DEVLOG.md）

## 待辦
（目前沒有；有新的工作就寫在這裡）

## 紀錄
| 日期 | 內容 |
|---|---|
| 2026-10-26 | 從 README 與 docs/DEVLOG.md 整理成步驟檔 |
