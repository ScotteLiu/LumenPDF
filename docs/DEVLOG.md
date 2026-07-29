# 開發日誌 / Development Log

倒序排列，最新的在最上面。每則記錄：**做了什麼**、**為什麼**、**驗證狀態**。
「已驗證」= 實際跑過並看到結果；「未驗證」= 寫完但還沒編譯／執行過。

---

## 2026-07-30 — 建立專案骨架

### 環境建置

| 工具 | 版本 | 來源 | 狀態 |
|---|---|---|---|
| MSVC | VS Build Tools 2022 | 機器上原有 | 已驗證 |
| CMake | 4.4.0 | `winget install Kitware.CMake` | 已驗證 |
| Ninja | 1.13.2 | `winget install Ninja-build.Ninja` | 已驗證 |
| Python | 3.12.10 | `winget install Python.Python.3.12` | 已驗證 |
| aqtinstall | 3.3.0 | `pip install aqtinstall` | 已驗證 |
| Qt | 6.8.3 msvc2022_64 | `aqt install-qt`（+ qtshadertools, qtimageformats） | 安裝中 |
| PDFium | chromium/7961 | `scripts/fetch-pdfium.ps1` | 已驗證 |

**踩到的坑：**

1. **Windows 的 `python.exe` 是 Microsoft Store 的假殼。** `Get-Command python` 會回傳
   `...\WindowsApps\python.exe`，但執行它只會叫你去商店安裝。真正的直譯器在
   `C:\Users\Scotte\AppData\Local\Programs\Python\Python312\python.exe`——腳本裡一律用絕對路徑。

2. **不需要 Qt 帳號。** 官方線上安裝器強制登入，但 `aqtinstall` 直接從 Qt 的官方鏡像
   抓同樣的封包，全程免註冊、可自動化、可寫進 CI。

3. **PDFium 用 `bblanchon/pdfium-binaries` 的預編譯版**，不自己 build。
   從原始碼建置需要 depot_tools 加約一小時；預編譯版用的是官方工具鏈，
   BSD 授權，商用安全。解出來的目錄結構
   （`include/` + `lib/pdfium.dll.lib` + `bin/pdfium.dll`）與 `CMakeLists.txt`
   的預期完全一致，不需調整。

### 已寫出的程式碼

**渲染管線**（速度的全部賣點都在這裡）
- `core/PdfEngine` — PDFium 行程層級的初始化／關閉，唯一的全域狀態
- `core/PdfDocument` — 開檔、頁面幾何、光柵化。頁面尺寸在開檔時一次快取，
  之後排版完全不需要拿鎖
- `render/PageRenderCache` — 以**位元組**（非張數）為預算的 LRU，192 MB
- `render/PageImageProvider` — `QQuickAsyncImageProvider` + 專屬 thread pool
- `bridge/DocumentController`、`bridge/PageListModel` — 給 QML 的介面

**三個關鍵效能決策**
1. `QImage::Format_ARGB32_Premultiplied` 在 little-endian 的記憶體佈局就是 BGRA，
   剛好等於 PDFium 的 `FPDFBitmap_BGRA`。PDFium 直接寫進 QImage 的緩衝區——
   **零複製、零通道交換**。因為頁面先填不透明白色，premultiplied 與直通 alpha 等價。
2. 渲染寬度**量化成 128px 級距**。不量化的話拖曳縮放會每一幀丟一次渲染請求，
   worker pool 立刻塞爆；量化後平滑縮放只觸發數次渲染，且小幅改變視窗大小仍能命中快取。
3. 開檔在 `QtConcurrent` 背景執行緒解析。2000 頁的檔案不會凍住第一幀。

**Lumen 設計系統**（`qml/Lumen/`，10 個檔案）
- `Squircle.qml` — 連續曲率圓角。一般 `Rectangle` 是直線接圓弧，曲率在接點不連續，
  眼睛會讀到一道細微的折痕；這裡每個角是超橢圓四分之一弧，曲率平滑漸入。
  用 `QtQuick.Shapes` + `Shape.CurveRenderer`（解析式 AA），
  不需要編譯 shader，也不需要任何 C++。每個角 10 段就已經看不出多邊形。
- `Tokens.qml` / `Motion.qml` — singleton。所有視覺常數與動畫曲線的唯一來源。
  沒有任何元件可以寫死顏色或間距。
- 8 個元件：Button / IconButton / Toolbar / Sidebar / ScrollBar / SearchField / Separator

**平台隔離**
- `platform/PlatformWindow` — 全app唯一有 `#ifdef Q_OS_WIN` 的地方（Mica 背景 + 深色標題列）
- 圖形後端（D3D11）只在 `main.cpp` 選一次
- 之後加 macOS／Linux 只需動這兩處，UI 完全不用改

### 決策記錄

| 決策 | 理由 | 不可逆程度 |
|---|---|---|
| Qt Quick，不用 Qt Widgets | Widgets 是 CPU 繪製，做不出 GPU 加速的彈簧動畫 | 高——換回去等於重寫 UI |
| PDFium，不用 MuPDF | MuPDF 是 AGPL，會逼專案開源或付商業授權費 | 中——`PdfDocument` 已封裝 |
| Qt LGPLv3 **只動態連結** | 靜態連結需商業授權（約 €4–5k／人／年） | 低——但打包時絕不能改成靜態 |
| 自建 UI 庫，不用 Quick Controls 樣式 | 「一看就是 Qt app」不是目標 | 高 |
| 字型用 Inter，不用 SF Pro | SF Pro 不可隨產品散布 | 低 |
| 先做 Windows | 先驗證市場 | 低——平台碼已隔離 |

### 尚未驗證

**整份骨架還沒編譯過。** Qt 當時仍在下載。第一次 configure 預期會有數個小錯
（QML 模組連結、PDFium API 細節簽章）。下一則日誌會記錄實際踩到什麼。

Git：`main` 分支，1 個 commit，尚未推上 GitHub。
