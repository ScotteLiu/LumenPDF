# 07 — 打包與發佈

**狀態**：✅ 完成
**最近更新**：2026-10-26

## 目的
做出在別人機器上一定開得起來的安裝檔和免安裝版，並能自動更新。

## 輸入
- 建置結果

## 輸出
- `LumenPDF-<version>-win64-portable.zip`、`-setup.exe`（Inno Setup）、GitHub release（目前 tag `v0.3.1`）、winget manifest、產品網站

## 做法
- `scripts/package.ps1`：先修剪用不到的 Qt 部分（約 110 MB），再**啟動修剪後的版本、要求繪出一頁**才寫成品。
- `packaging/lumenpdf.iss`、`.github/workflows/release.yml`、`scripts/make-winget-manifest.ps1`。
- 自動更新：`src/app/UpdateChecker`（2026-08-02 修好校驗碼永遠驗不過、asset 名稱消毒，`6cd91fb`）。
- 程式碼簽章：`scripts/package.ps1` 已實作，等憑證（`docs/CODE-SIGNING.md`）。

程式：`scripts/package.ps1`、`packaging/lumenpdf.iss`、`.github/workflows/release.yml`、`src/app/UpdateChecker.cpp`

## 相關決策
- D-001（改用 pipeline 格式整理；這一步的設計理由見 README 與 docs/DEVLOG.md）

## 待辦
- [ ] 決定要不要買程式碼簽章憑證（OV／EV／Azure Trusted Signing；`docs/CODE-SIGNING.md`，目前安裝檔沒簽、SmartScreen 會警告）
- [ ] 發下一版：`v0.3.1` 之後的 2026-08-02 稽核修正與啟動回歸修正還沒有 tag（推測，請確認）

## 紀錄
| 日期 | 內容 |
|---|---|
| 2026-10-26 | 從 README 與 docs/DEVLOG.md 整理成步驟檔 |
