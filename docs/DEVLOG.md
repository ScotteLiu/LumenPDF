# 開發日誌 / Development Log

倒序排列，最新的在最上面。每則記錄：**做了什麼**、**為什麼**、**驗證狀態**。
「已驗證」= 實際跑過並看到結果；「未驗證」= 寫完但還沒編譯／執行過。

---

## 2026-07-30 — 全文搜尋與大綱側欄

### 實測數字

| 項目 | 結果 |
|---|---|
| 搜尋 245 頁、1028 個命中 | **130 ms** |
| 開啟 245 頁文件 + 建立 214 筆大綱 | 瞬間（無感） |
| 6 頁論文搜尋 197 個命中 | 無感 |

### 架構

**搜尋**：`SearchController` 同時是結果模型也是控制器。QML 需要的正是這個形狀——
一個可綁定的 list，加上驅動它的屬性和動詞。

用 `QtConcurrent::mapped` + `QFutureWatcher::resultReadyAt` 而非
`QtConcurrent::run`：**同時拿到平行度與順序**。`mapped` 會保持索引順序並在每個結果
到位時通知，所以第 2 頁的命中不可能排在第 900 頁後面，但頁面本身是併發掃描的。
結果邊掃邊進場，掃描還沒結束就已經能用。

輸入有 180 ms debounce——打「annotation」應該跑一次搜尋，不是十次。

**誠實的限制**：`PdfDocument` 對 PDFium 呼叫加了 mutex（FPDF_DOCUMENT 本身非
執行緒安全），所以實際上 PDFium 的部分是序列化的，平行度目前沒有真的發揮。
130 ms 的數字下不值得處理；文件再大一個量級時要改成每執行緒各開一份
document handle。

**大綱**：扁平陣列 + depth 欄位，不是真的樹。QML 端本來就只吃線性模型，
展開／收合就是同一個陣列上的可見性過濾。可見性計算是單次前向掃描
（`ancestorOpen[d]` 記住深度 d 最近一個項目是否展開），O(items × depth)
而非反向找祖先的 O(items²)——大綱動輒數千列。

書籤走訪用顯式堆疊而非遞迴，並用 `QSet` 斷開環：這是在處理不受信任的輸入，
惡意檔案可以做出無限深或循環的書籤樹。

### 踩到的坑

**1. `Qt6Concurrent.dll` 沒被部署 —— 0xC0000135，零輸出**

App 啟動即死，exit code `-1073741515`，log 一個字都沒有。
原因：`QtConcurrent::run` 是 header-only，不產生 DLL 依賴；改用
`QtConcurrent::mapped` 之後才第一次真的依賴 `Qt6Concurrent.dll`，
而 windeployqt 是上一版跑的。

**修法不只是補跑一次**——把 windeployqt 併進 `scripts/build.ps1`，每次建置都跑。
這類 bug 沒有任何診斷訊息，不該讓它有第二次機會。

**2. 關閉時 access violation（0xC0000005）**

`PdfEngine::shutdown()`（`FPDF_DestroyLibrary`）在 `QQmlApplicationEngine` 解構
**之前**執行 → 文件的解構子在函式庫已銷毀後才呼叫 `FPDF_CloseDocument`。
修法：把 engine 包進一個 scope，離開 scope 後才 shutdown。

**3. QML 屬性遮蔽**

- delegate 裡宣告 `readonly property real scale` → 遮蔽 `Item.scale`，
  整個 component 載入失敗（`"scale" is a read-only property`）。改名 `pointScale`。
- `LumenIconButton` 宣告 `property string icon` → `Cannot override FINAL property`，
  `AbstractButton` 已有 FINAL 的 `icon` group。改名 `iconPath`。

**4. PowerShell `Set-Content -Encoding utf8` 會毀掉非 ASCII 原始碼**

用它做正規表示式取代時，把連字字元 `ﬀﬁﬂ` 全部寫成 mojibake，而且壞掉的位元組
之後連編輯工具都無法比對。**教訓：原始碼一律用編輯工具改，不要用 shell 做取代。**
連字表現在寫成明確碼位（`0xFB00`）而非字元字面值——原始碼不該依賴自己的編碼
能存活過每一個經手的工具。

### 品質細節

- **字型解析**：`Inter` 沒安裝時不該掉到襯線預設字型。啟動時從
  per-platform 偏好清單挑一個實際存在的（Inter → Segoe UI Variable → Segoe UI），
  解析一次，所有元件透過 `Tokens.fontFamily` 取用。
- **片段清理**：排版 PDF 充滿連字碼位、軟連字號、零寬字元——在頁面上是對的，
  在結果清單裡是一堆豆腐方塊。顯示前展開連字、移除不可見控制字元。
  偏移量因此會變，所以是把「match 前／match／match 後」三段分別處理再重算偏移。
- **片段切齊單字邊界**：結果不再從半個音節開始（`mbedded real-time systems`）。
- **搜尋高亮**：全部命中畫淡色，當前命中畫濃色並做一次彈跳脈衝——
  跨頁跳轉後眼睛能立刻找到。
- **`LumenSegmented`**：選取指示器是**滑動**過去而不是在新分頁下方重新出現。
  眼睛追蹤的是一個移動的物體，不是兩個閃爍的物體。

### 測試鉤子

`LUMEN_SIDEBAR_TAB` 與 `LUMEN_SEARCH` 環境變數讓截圖與煙霧測試能到達任一面板、
觸發任一狀態，完全不需要合成輸入事件。

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

### 第一次建置與執行 — 已驗證

**C++ 端第一次就編過，零錯誤。** 92 個 target 全綠。
PDFium 正常運作：`lumen.document: opened ... pages: 6`。

真正的 bug 全在 QML 那一側，而且都是「編得過、跑起來才錯」的類型：

**Bug 1：QML singleton 沒註冊（嚴重）**

症狀是 app 開得起來，但畫面一片黑，log 噴出上百行
`Unable to assign [undefined] to QColor` —— **每一個 `Tokens.*` 和 `Motion.*` 都是 undefined。**

原因：`set_source_files_properties(... QT_QML_SINGLETON_TYPE TRUE)` 寫在
`qt_add_qml_module()` **後面**。`qmldir` 是在 `qt_add_qml_module()` 當下就產生的，
之後才設的屬性根本不會被讀到。修正後 `qmldir` 才出現：

```
singleton Tokens 1.0 Tokens.qml
singleton Motion 1.0 Motion.qml
```

這個坑很陰：CMake 不報錯、編譯不報錯、連 qmlcachegen 都正常跑完。
**教訓：所有 `set_source_files_properties` 一律寫在 `qt_add_qml_module` 之前。**

**Bug 2：`icon` 是 FINAL 屬性**

`LumenIconButton` 宣告 `property string icon` → 執行期
`Cannot override FINAL property`。`AbstractButton` 已經有一個 FINAL 的 `icon`
group property。改名為 `iconPath`。

**Bug 3：`cacheBuffer` 為負**

第一次 layout 時 `height` 還是 -1，`height * 1.5` 變負數。加 `Math.max(0, ...)`。

### 圖示：從 Unicode 字元換成自繪向量

第一版工具列用 `☰ ＋ − ⤢` 這類 Unicode 字元。截圖一看就發現兩個問題：
「開檔」和「放大」都用了 `＋`，而且這些字元在 macOS／Linux 上根本不保證存在。

改成 `Icons.qml`（24×24 SVG path 資料）+ `LumenIcon.qml`（用 `QtQuick.Shapes` 描邊繪製）。
全平台一致、任何 DPI 都銳利、換色不用換資產。house style：24×24 網格、
只描邊不填色、線寬 1.75、圓端點圓接合。

### UI 截圖機制

外部截圖（PowerShell + `CopyFromScreen`）在自動化環境下必然失敗——
`The handle is invalid`，因為沒有互動式桌面工作階段。

改成 **app 自己截自己**：設 `LUMEN_CAPTURE=<path.png>` 就會在
`LUMEN_CAPTURE_DELAY` 毫秒後呼叫 `QQuickWindow::grabWindow()` 存檔並退出。
這也是之後做視覺回歸測試的基礎。

```powershell
$env:LUMEN_CAPTURE="work\ui-check\shot.png"; $env:LUMEN_CAPTURE_DELAY="4500"
.\build\windows-release\lumenpdf.exe some.pdf
```

另外：因為 exe 是 WIN32 子系統（沒有 console），Qt 的 log 預設無處可去。
要設 `QT_FORCE_STDERR_LOGGING=1` 才看得到。

### 目前狀態

- 開檔、渲染、捲動、縮放、深色主題、拖放開檔：**已驗證可用**
- Qt/QML 警告：**已全部清空**
- 記憶體：開一份 6 頁論文約 160 MB（含 Qt runtime）

### 下一步

搜尋、大綱側欄、註解層、頁面操作、存檔。

Git：`main` 分支，尚未推上 GitHub。
