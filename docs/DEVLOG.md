# 開發日誌 / Development Log

倒序排列，最新的在最上面。每則記錄：**做了什麼**、**為什麼**、**驗證狀態**。
「已驗證」= 實際跑過並看到結果；「未驗證」= 寫完但還沒編譯／執行過。

---

## 2026-07-30 — 壓縮，以及表單的前置作業

### 壓縮

實測：**657 KB → 363 KB（-45%）**，1 張圖從 840 萬像素降到 210 萬。

**只降取樣「相對於實際繪製尺寸」過大的圖片。** 一張 4000px 的圖填滿整頁是合理的，
同一張圖當成 20pt 的 logo 就是浪費。所以判斷依據是頁面物件的矩陣
（`FPDFPageObj_GetMatrix` 的 `a`／`d` 就是繪製寬高），而不是圖片本身的像素數。

已在預算內的圖片**保持位元組完全不變**——重新編碼只會損失畫質，換不到任何東西。

**存檔改用 compact（非 incremental）** 才真的回收空間：incremental 會把新資料附加在
檔尾並保留原本的位元組，所以換掉的圖片其實兩份都還在。

**一律寫到新檔案。** 降取樣是不可逆的有損操作，默默劣化某人唯一一份文件不可接受。
UI 也誠實回報「沒有可壓縮的東西」而不是編一個假的百分比——多數純文字 PDF 本來就壓不動。

### 表單：先解決「無法驗證」的問題

整台機器上**沒有任何一份含表單欄位的 PDF**（掃過所有 PDF 找 `/AcroForm` + `/Widget`，
命中 0）。在沒有測試對象的情況下實作表單填寫，等於交出未經驗證的程式碼，
而這個專案到目前為止每一個功能都是對真實檔案驗證過的。

所以先寫了 `scripts/make-form-fixture.ps1`——**自己產生測試檔**，而不是下載樣本：
產生器的原始碼可讀、輸出是確定性的、內容剛好是測試需要的（兩個文字欄位、一個核取方塊），
而且進版控的是產生器而不是一團看不懂的二進位。

手寫 PDF 的關鍵細節：PDF 是**以位元組位移建索引**的格式，xref 表必須記錄每個物件
從第幾個位元組開始。所以產生器是逐物件組裝並同時追蹤位移，不是把字串接起來。
另外用 Latin-1 編碼寫出，讓「字元數 = 位元組數」成立。

結果：1662 位元組的檔案，PDFium 正確開啟並渲染。`formFieldCount()` 可以數出
`/Widget` 標註。**表單填寫的實作現在不再被「沒東西可測」擋住。**

---

## 2026-07-30 — v1：匯出、真塗黑、簽名

### 塗黑：一個必須推翻自己第一版設計的功能

**第一版做的是物件級移除**：走訪頁面物件，移除與塗黑區域相交的文字／影像物件，
再畫黑框。邏輯上「過度移除是安全的，移除不足才是漏洞」——聽起來對。

**實測結果是整頁被清空。** 這份 LaTeX 產生的 PDF，一個文字物件就是一整段或一整欄，
所以**任何**重疊都會判死整段。第 1 頁只剩一個黑框，標題、作者、右欄全部消失。

PDFium 沒有提供切割文字物件或刪除單一字符的 API，所以透過它**做不到精確的
物件級塗黑**。剩下的選擇只有兩個：摧毀遠多於使用者要求的內容（把頁面弄爛），
或者把頁面點陣化後替換。

**選點陣化**：它同時做到「看起來對」和「證明不漏」——黑框底下沒有文字，
因為整頁都沒有文字了。

實作：300 dpi 渲染 → 在 raster 上塗黑 → 清空頁面所有物件 → 插入該 raster
作為整頁影像物件 → `FPDFPage_GenerateContent`。

**代價是真實的**：該頁失去所有可選取／可搜尋的文字。所以 UI **明說**這件事——
確認對話框寫「每一個受影響的頁面會被平面化成影像，那些頁面將不再可搜尋或可選取。
這正是黑框底下無法還原的保證」。

**一個安全工具如果會讓你意外，它就不值得信任。**

### 驗證方式：不看畫面，看文字

塗黑的正確性不能用肉眼看截圖判斷——黑框畫上去了不代表文字不見了。
所以驗證是**把存檔後的檔案重新開啟並匯出全文**，然後比對：

| | 原始 | 塗黑後 |
|---|---|---|
| 全文位元組 | 35,523 | 30,792 |
| **第 1 頁文字區塊** | **4,731** | **0** |
| `"CAD community"` 出現次數 | 4 | 3 |
| `"Howard"` 出現次數 | 19 | 18 |

第 1 頁的文字**完全歸零**，其他頁一字未動。這才是塗黑的證明。

### 匯出

- **頁面轉圖片**：`QtConcurrent::mapped` 併發渲染，回報進度。這是 app 裡唯一
  真正慢的操作（300 dpi × 245 頁是實際工作量），凍住視窗會無法接受。
  進度是工具列下方一條細線，不是模態對話框——文件在匯出期間保持可讀。
- 檔名補零到最大頁數的位數，讓所有檔案管理器都能正確排序。
- **文字匯出**用 stream 寫出，峰值記憶體與文件大小無關。

### 簽名

用**向量路徑**而不是點陣圖：`FPDFPageObj_CreateNewPath` + `FPDFPath_LineTo`，
只描邊不填色（填色會讓筆畫自交處糊成一團）。任何縮放都銳利，
而且只增加幾 KB 而不是一張圖。

QML 端筆畫正規化到單位正方形再傳給 C++，座標轉換只在一個地方發生。
簽名尺寸相對頁面計算，所以在 A4 和 Letter 上看起來一致。

### 踩到的坑

**`Repeater` 的 delegate 必須是 `Item`，`ShapePath` 不是。**
畫多筆獨立筆畫要用 `PathMultiline`——它就是為此設計的（一條 path 裡若干條
不相連的折線，正是「一組筆跡」）。

### 新增 UI 元件

功能開始變多，工具列不能再塞，所以做了 `LumenMenu`（彈出選單，從錨點邊緣長出來
而不是居中淡入，讓眼睛記得它從哪來）和 `LumenConfirm`（不可復原動作的確認框，
按鈕標示動詞而不是「確定」——寫「你確定嗎？」加一個 OK 按鈕，只會訓練使用者
不讀就點）。

---

## 2026-07-30 — 打包與淺色主題

### 打包

`scripts/package.ps1` 產出可攜 zip 與 Inno Setup 安裝檔。

| | 檔案數 | 大小 |
|---|---|---|
| 第一版（windeployqt 原樣） | 1587 | 189 MB |
| 修剪後 | **220** | **81.6 MB** |
| 可攜 zip | | 47.7 MB |
| 安裝檔 | | 40.5 MB |

**策略是先全複製再修剪，不是白名單。** windeployqt 決定 Qt 需要什麼；
用手工白名單去猜它，正是「在自己機器上跑得好、到別人機器上就死」的來源。

修剪掉的東西與理由：

| 移除 | 大小 | 為什麼確定不需要 |
|---|---|---|
| `qml/Lumen`、`qml/App` | ~60 MB | 我們的 QML 模組是 **STATIC**，已編進 exe。build 目錄下那份是 CMake 的原始碼副本，帶著它不只肥，還會讓磁碟上過期的 `.qml` 遮蔽編譯進去的版本 |
| `opengl32sw.dll` | 20 MB | `main.cpp` 把 scene graph 釘在 Direct3D 11，軟體 OpenGL 永遠不會載入 |
| `dxcompiler.dll` + `dxil.dll` | 14 MB | DXIL shader 編譯器，只有 D3D12 後端會用 |
| 5 種未使用的 Quick Controls 樣式 | ~8 MB | 我們明確設定 Basic 樣式且所有控件自繪 |
| build 中間產物（`.pdb`/`.lib`/`CMakeFiles`/`_autogen`） | ~90 MB | — |

**修剪之後會啟動修剪過的那份副本，要求它成功渲染出一頁才寫出成品。**
修剪是最容易做出「編得過、到使用者手上才死」的封裝步驟，不能靠信任。
（第一次跑就抓到：遞迴複製把 `qml/App/CMakeFiles` 一起帶進去了。）

`vc_redist.x64.exe` 保留並由安裝程式靜默執行。Qt 官方二進位連結動態 MSVC
runtime，**這是硬需求，不是我們能靠靜態連結繞掉的**。

### LGPL 合規

- Qt 一律以獨立 DLL 動態連結，從未靜態連結。
- `LICENSES\THIRD-PARTY-NOTICES.txt` 說明 Qt 為 LGPLv3、指向授權全文與原始碼位置，
  並明說隨附的 Qt DLL 可被相容版本替換。
- PDFium 的 BSD 授權全文一併隨附。

### 安裝程式的兩個決定

- **不搶 `.pdf` 預設關聯**。建立自己的 ProgID 並註冊到 `OpenWithProgids`，
  讓 LumenPDF 出現在「開啟檔案」清單裡由使用者選。默默搶走預設關聯是敵意行為。
- **預設 per-user 安裝**（`PrivilegesRequired=lowest`）。一個文件檢視器不該為了安裝
  彈 UAC。

### 淺色主題

我從頭到尾只看過深色，所以加了 `LUMEN_THEME=light|dark` 來檢查——**果然抓到問題**：
淺色的畫布 `#E8E8ED` 太接近紙白，頁面浮不起來。頁緣是眼睛判斷「文件在哪裡」的依據，
所以畫布必須明顯比紙深：改成 `#DCDCE1`，陰影不透明度 0.18 → 0.26。

另外，原生標題列和視窗背景不是 QML，切換主題時必須主動通知它們——
否則會出現深色標題列壓在淺色視窗上。

---

## 2026-07-30 — 合併與分割

**MVP 的功能清單到此全部完成。**

### 實測

| 操作 | 結果 |
|---|---|
| 合併：6 頁 + 10 頁 | 16 頁 |
| 合併後復原 | 回到 6 頁 |
| 匯出第 2–3 頁 | 產生獨立檔案，重開確認**恰好 2 頁** |
| 四種操作各自復原 | 全部回到 6 頁（rotate / move / delete / insert） |

### 合併的復原不需要暫存區

刪除需要暫存區是因為頁面資料會消失。**插入不需要**——復原就是把插入的那一段刪掉，
重做則從來源文件重新匯入。

關鍵在於 `Command` **持有來源文件的 `QSharedPointer`**，
所以重做匯入的一定是當初匯入的那份內容，即使磁碟上的檔案之後被改了或刪了。

### 分割是複製出去，不是就地過濾

`extractPagesTo()` 先 `FPDF_CreateNewDocument()`，把要的頁 `FPDF_ImportPages` 進去，
再寫出那份新文件。**只能往外複製**——就地過濾等於從使用者還開著的文件裡刪頁。

因為不修改當前文件，所以不進復原堆疊。

### 細節

- `deletePageRange()` 從後往前刪，這樣每次刪除都不會位移後面還沒處理的索引。
- 拒絕把文件匯入它自己。技術上可行，但幾乎沒人是故意的，結果是一份靜靜變成兩倍的檔案。
- 匯出頁面的目標頁是在**對話框開啟時**決定的，不是關閉時——對話框開著的時候使用者可以繼續捲動。
- `undo` / `redo` 現在會記錄操作後的頁數，之後驗證不必靠肉眼數縮圖。

---

## 2026-07-30 — 頁面操作與復原／重做

旋轉、重新排序、刪除頁面，全部可復原可重做。已驗證：

| 操作 | 結果 |
|---|---|
| `rotate,1,1` | 第 2 頁縮圖變成橫向，長寬比正確跟著換 |
| `delete,1` | 6 頁 → 5 頁 |
| `delete,1,undo` | 頁面完整回來，位置正確 |
| `move,0,3` | 頁序重排成功 |

### PDFium 沒有復原，所以復原是我們自己的

- **旋轉**和**移動**在算術上可逆：反向轉、移回去。
- **刪除**不可逆。所以刪除前先用 `FPDF_ImportPages` 把該頁**複製到一份只為
  存放它而存在的暫存文件**，復原時再匯入回來。**那份暫存文件就是復原緩衝區**，
  而且它會連同該頁的標註一起保留。

暫存文件是延遲建立的——多數使用情境從不刪頁，不該為此多付一份文件的成本。

### 結構性變更的連鎖反應

頁面重新編號會讓很多東西同時失效。`structureChanged` 一發出就：
丟掉整份 raster 快取、重設頁面模型與大綱模型、遞增 `renderGeneration`。

**選取和搜尋結果是清空，不是遷移。** 它們持有的頁碼可能已經指向不同的頁——
**默默指向錯的頁比失去它們更糟。**

### 細節

- 旋轉會交換頁面的有效寬高，所以它是**結構性**變更而不只是重繪——
  版面必須重算。`rebuildPageInfo()` 重新讀取所有頁面尺寸。
- 拒絕刪到零頁：零頁 PDF 是無效檔案，而使用者幾乎肯定是想關閉檔案。
- 頁面操作按鈕**只在滑鼠移到該縮圖時浮現**。200 張縮圖每張都掛三顆按鈕是噪音；
  這些動作屬於你正指著的那一頁。

### 踩到的坑

`Row` 裡面的元素不能用 `anchors.fill`——整個 `Row` 會拒絕排版並印警告。
按鈕的背板必須是 `Row` 的**兄弟節點**，不是子節點。

---

## 2026-07-30 — 文字選取、標註、存檔

**LumenPDF 從這一版開始是編輯器，不只是閱讀器。**
端到端已驗證：選取文字 → 套用螢光標記 → 存檔 → **另一個行程重新開啟該檔案，
標註仍在**（頁面與縮圖都有）。寫出來的是符合規範的 PDF 標註，不是自訂格式，
其他 PDF 軟體讀得到。

### 文字選取

一份選取就是兩個 (page, character) 錨點。方向、跨哪些頁、要畫哪些矩形，
全部是推導出來的——**只有一份狀態需要維持一致**。

跨頁選取能成立是因為文字索引是每頁獨立的：第一頁從錨點到頁尾，
最後一頁從頁首到焦點，中間整頁全選。

**命中測試的細節**：拖曳時指標有很多時間待在字距、行距、行尾之外。
先試 `FPDFText_GetCharIndexAtPos` 精確命中；沒中就找最近字元，
但**垂直距離權重乘 1000**——指標所在那一行的字元，永遠比上一行更近的字元更正確。

**文字層快取**：拖曳時每次滑鼠移動都要命中測試，每次都
`FPDF_LoadPage` + `FPDFText_LoadPage` 會讓密集頁面的選取明顯延遲。
快取最近使用的那一頁的 handle，編輯該頁時失效。

**`preventStealing: true`**：`ListView` 是 `Flickable`，預設會把拖曳搶去捲動，
那樣滑鼠就永遠選不了字。選取優先；滾輪照樣捲動。

雙擊選字、`Ctrl+A` 全選、`Ctrl+C` 複製、`Esc` 取消。
word/line 粒度會**黏著**：雙擊後繼續拖曳是逐字擴展，不是逐字元——
所有像樣的文字視圖都是這個行為。

### 標註

四種文字標記（highlight / underline / strikeout / squiggly）共用一條程式路徑，
差別只有 subtype 和預設顏色。

**從選取建立，而不是自由畫框**：這才能讓螢光標記精準落在字上、跨行正確斷開、
而且被其他 PDF 軟體正確解讀。座標透過 quadpoints 附著在文字上。

`FPDFPage_GenerateContent()` 是必要的——少了它，標註存在於記憶體中，
但寫出文件時不會出現在頁面物件清單裡。

### 存檔

`FPDF_SaveAsCopy` 需要 `FPDF_FILEWRITE` 回呼結構，不吃路徑。

**不能覆寫自己正在讀的檔案**：PDFium 是邊寫邊從原檔按需拉取物件資料。
所以流程是：寫到**同目錄**的暫存檔 → 關閉文件 → 原檔改名成備份 →
暫存檔改名成目標 → 刪備份 → 重新開啟。

暫存檔放同目錄而非系統 temp，是因為**跨磁碟區的 rename 是複製**，
那樣幾百 MB 的檔案就失去了原子交換的意義。每一步失敗都會把原檔換回來。

### 踩到的坑

**1. 測試鉤子造成無限迴圈（CPU 100%、記憶體 773 MB）**

`LUMEN_SELECT` 鉤子連到 `documentChanged`。存檔會重開文件 → 再次發出
`documentChanged` → 再選取、再標註、再存檔 → 永不停止。
修法：`Qt::SingleShotConnection`。

這不只是測試碼的問題，它暴露了一件真事：**存檔會重新發出 `documentChanged`**，
任何監聽者都必須能承受這件事。

**2. `windows.h` 的 `max` 巨集**

`std::numeric_limits<double>::max()` 編不過（`illegal token on right side of '::'`），
因為 PDFium 的標頭會拉進 `windows.h`。
修法是專案層級加 `NOMINMAX` 和 `WIN32_LEAN_AND_MEAN`，不是在呼叫處加括號——
這類 bug 應該一次根除。

**3. 編輯後畫面不更新**

`Image` 的 `source` 字串沒變就永遠不會重新抓取，不管背後的像素多舊。
加一個 `renderGeneration` 計數器接在 URL 後面（`&g=3`），編輯時遞增。
同時整份丟掉 raster 快取——標註會同時改變所有縮放層級的外觀，
而編輯夠罕見，精準逐鍵驅逐不值得。

### 品質細節

- **浮動選取操作列**：拖曳結束後才浮現（拖曳中出現會跟指標搶同一塊螢幕），
  位置跟著選取範圍並鉗制在頁面內，以 spring 曲線升起而非閃現。
  四個顏色色票**點一下就同時設定顏色並套用**——一個手勢，不是兩個。
- **螢光筆顏色刻意調得溫和**：標記必須能墊在黑字底下而不淹沒它。
  底線和刪除線則強制不透明——半透明的線看起來像沒畫好。
- **`LumenToast`**：從底部升起、停留、自行離開。不阻擋、不需關閉、不問問題——
  有問題要問就該用對話框，不是它。
- 標題列的未存檔指示是一個「•」，不是「*」。

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
