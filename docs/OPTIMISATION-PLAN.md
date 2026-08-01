# Optimisation and hardening plan

> **Status, 2026-08-02:** all ten items below are implemented. 107 → 141
> assertions. One finding did not reproduce and was not shipped as a fix — see
> the note under item 2.
>
> A separate regression was found while measuring and is **still open**; it is
> written up at the end of this document under "Open: startup regression in
> db222a8". It was not part of the audit.

The codebase is in good shape. Ten findings survived adversarial verification and none of them is an architectural mistake — the invariants this project paid for are documented in the source and are honoured almost everywhere. `invalidatePage()` tears down both per-page caches correctly; `setTextObjectString` respects the held-form-page rule with its `held` check; `renderPage` enforces the pixel budget; PKCE is implemented properly; `download()` refuses a release with no published checksum. What the audit actually found is a set of omissions at the edges of those rules: six functions that were written before the form-page cache existed and never learned about it, a save path that does not blur the field first, a redaction loop that reports pages it did not touch, and a hover handler that reaches into PDFium from the GUI thread. Two coverage gaps and three update/OAuth defects round it out. Verification downgraded four of the ten — the claimed use-after-free, the arbitrary-file-delete, the Drive account takeover and the null-`deleteLater` crash do not occur — so this is a hardening pass, not an incident.

Start with items 1 and 2. They are both one-line-scale changes in `PdfDocument`, they are the only two findings that lose user data silently, and they share a cause: the form-fill environment holds a page for the session and five renumbering operations plus the save path forgot about it. Item 2 in particular means a plain Ctrl+S after typing into a field writes the pre-edit value while the screen still shows the typed text — invisible until reopen. After those, do the LUMEN_PAGEOP step-splitting harness change (item 7's plumbing), because items 1 and 3 both need it to be testable, then work down.

## Work items

### 1. Structural page edits leave the held form page behind
**File** `src/core/PdfDocument.cpp:1823,1854,1902,1938,1983` (plus `:2680`) · **Severity** High · **Effort** Trivial

`PdfDocument` holds two live `FPDF_PAGE` handles: the text cache and the form-fill session page (`m_formPage`/`m_formPageIndex`). `invalidatePage()` (line 941) drops both. Every operation that renumbers or destroys pages drops only the text cache: `movePage`, `deletePage`, `insertPageFrom`, `insertPagesFrom`, `deletePageRange`. `rebuildPageInfo()` (1713) refills `m_pages` and never touches the form members, and nothing upstream compensates — `PageOperations::apply/revert` only emit `structureChanged()`, and the handler at `DocumentController.cpp:63-75` clears the provider cache, selection, search and models but says nothing about forms. Note `acquireFormPage` is reached from `formFieldTypeAt`, which `PageView.qml:420` calls on plain hover, so the form page is held whenever the pointer has crossed a fillable page — not only while a field is focused.

**How it fails:** Hover or click a field on page 3, then delete page 1 from the thumbnail rail. `m_formPageIndex` is still 3, but page 3 is now the old page 4. `acquireFormPage(3)` short-circuits at line 1247 and returns the stale object, so `FPDFPage_HasFormFieldAtPoint`, `formMousePress` and every keystroke are dispatched against the wrong page's widgets, and the `FORM_OnBeforeClosePage` value flush at 1275 lands on the wrong page. `renderPage`'s `isEditedPage` test (line 459) goes stale the same way and skips the `FORM_OnAfterLoadPage`/`OnBeforeClosePage` pair for the wrong index. This is misrouting, not a crash: `m_formPage` is a reference this class obtained from its own `FPDF_LoadPage`, and `FPDFPage_Delete` does not free a page the embedder still holds.

**Fix:** Add a private `void releaseAllPageCaches() const` next to `invalidatePage()` that unconditionally calls `releaseTextCache()` then `releaseFormPage()` (caller holds `m_mutex`), and replace the bare `releaseTextCache();` at 1823, 1854, 1902, 1938 and 1983 with it. Unconditional, not index-guarded — indices shift, so the held index cannot be trusted after the operation. It must stay at the top of each function, before the mutation, so `FORM_OnBeforeClosePage` flushes into the still-correct page. `downsampleImages` (2680) belongs in the same change for a different reason: it renumbers nothing, but its loop does `FPDF_LoadPage`/`FPDF_ClosePage` over every page (2686/2806) including the one the form environment holds, which is exactly the "any `FPDF_ClosePage` affects all holders" hazard the code documents at 1226 — so it needs `releaseFormPage()` before the loop too. Finally, have the `structureChanged` handler in `DocumentController` clear `FormController::m_editingPage`.

**Test:** New case: open the form fixture, merge the latin fixture onto it so page 0 has fields, touch the field to populate the form-page cache, delete page 0, then probe the field's coordinates at index 0 again — a plain latin page. Assert the probe returns `FormFieldKind.None`; with the bug it reports a text field. Needs two hook additions: a `formtouch,<page>,<x>,<y>` step verb in `LUMEN_PAGEOP` (which requires the `;`-separated step splitting from item 7, so land that first) calling `FormController::fieldKindAt`, and a `LUMEN_FORM_PROBE="<page>,<x>,<y>" → report.formProbe` in `StateReport.cpp`, mirroring the existing `LUMEN_LINK_PROBE` at line 81. Vary the middle step to `move,0,2` and `delete,1` to cover the other renumbering operations off the same fixture.

### 2. `saveAs()` writes the file before the edited field value is flushed
**File** `src/core/PdfDocument.cpp:2063` (and `extractPagesTo`, `:2083`) · **Severity** High · **Effort** Trivial

PDFium keeps a text field's edited value in the live widget until focus is killed or the page view is torn down; `releaseFormPage()` says so in its own comment at 1273-1276. `saveAs()` takes `m_mutex` and goes straight to `FPDF_SaveAsCopy` at 2063 with `m_formPage` still open. Nothing on the save path blurs first: `DocumentController::saveAs` writes to the scratch file at line 349 and only reaches `m_document->close()` — the only caller of `releaseFormPage` on that path — at line 362, after the bytes are on disk, and only in the overwrite branch. `FormController::clearFocus` has exactly one caller in the tree: the `LUMEN_FORM_FILL` hook at `main.cpp:431`, which is precisely why the 107 assertions do not catch this.

**How it fails:** Open a fillable PDF, click a field, type "Jane Doe", press Ctrl+S without clicking elsewhere. The incremental `FPDF_SaveAsCopy` serialises the pre-edit field dictionary, so the saved file has an empty field. The screen still shows "Jane Doe" because `renderPage` draws the live widget through `FPDF_FFLDraw` (456-470). In the overwrite branch the document is then closed — flushing into a document that is immediately discarded — and reloaded from the file just written, so the text vanishes from the UI too.

**Fix:** Extract the body of `formClearFocus()` (1405-1417) into a private `void flushFormPageLocked() const` that assumes the caller holds `m_mutex`: `FORM_ForceToKillFocus(m_formHandle)` then `releaseFormPage()`, in that order — `FORM_ForceToKillFocus` is the documented save-the-value call, `FORM_OnBeforeClosePage` is the teardown. `formClearFocus()` becomes lock-plus-call. Then call `flushFormPageLocked()` immediately after the `QMutexLocker` in `saveAs` (2035) and `extractPagesTo` (2089). Do **not** call `formClearFocus()` from inside those — `m_mutex` is a plain non-recursive `QMutex` (`PdfDocument.h:321`) and it would deadlock. Mark the helper `const` and rely on `m_formPage` already being mutable, so `extractPagesTo`'s constness is preserved. Doing this inside `PdfDocument` rather than `DocumentController` also covers `ExportController` and the Drive upload path. Re-acquisition is already lazy via `acquireFormPage`.

**Test:** Delete the `forms->clearFocus();` call at `main.cpp:431`. That single deletion converts the existing `form-fill` case (`run-tests.ps1:406-419`) into the regression test: its three assertions — "Test Name" present, "test@example.com" present, `/AS /Yes` — then pass only if the production save path flushes. No new hook needed.

### 3. Multi-page redaction reports pages it did not flatten
**File** `src/bridge/RedactionController.cpp:79` · **Severity** High · **Effort** Small

`redactSelection()` loops `first..last`, `continue`s past any page whose range is empty or whose `redactRegions()` returns `ok == false`, sets `any = true` on the first success, and then unconditionally emits `flattenedPages(last - first + 1)`. `Main.qml:805-811` turns that into "Redacted — N page(s) flattened to an image", after the `LumenConfirm` at `Main.qml:757-767` has already promised nothing is recoverable. The trigger is not hypothetical: `RedactionResult::ok` defaults false (`PdfTypes.h:60`) and `redactRegions` returns early when `renderPage` yields a null image (`PdfDocument.cpp:2559-2561`); redaction rasterises at 300 dpi (2552) and `renderPage` refuses anything over 40 megapixels (406-411), so any page above roughly 21 × 21 inches — A1, A0, plotter output, posters — fails outright today. The coverage half is the reason nobody noticed: `LUMEN_SELECT` gates on `parts.size() == 5` (`main.cpp:298`) and then passes the *same* page to `begin()` and `extend()`, so no test can produce `firstPage != lastPage`. `SelectionController::ordered()` (line 22), the intermediate-page branch of `rangeForPage()` (72-75) and `selectAll()` (217) exist for cross-page selection and none is reachable from any case. The current redaction assertion, `pageTextLengths[1] -gt 100`, pins the *absence* of multi-page behaviour.

**How it fails:** A user drags from mid-page 1 through mid-page 2 where page 2 is an A1 drawing, and confirms Redact. Page 1 becomes a raster, page 2's `redactRegions` returns `ok == false` and is skipped, `any` is still true, and the toast says two pages flattened. The sensitive text on page 2 is intact, selectable and searchable. No `failed()` signal. All 107 assertions still pass.

**Fix:** Count successes. Replace `bool any` with `int succeeded`, emit `flattenedPages(succeeded)`, and emit `failed(tr("%1 of %2 pages could not be redacted..."))` when `succeeded < (last - first + 1)` — including when `succeeded > 0`, so a partial run is a visible error rather than a success toast. Keep the existing all-fail path. Then extend `LUMEN_SELECT` in `main.cpp:296-333` to accept a 6-part `p1,x1,y1,p2,x2,y2` form (begin on `p1`, extend on `p2` — the controller already handles it) and a literal `LUMEN_SELECT=all` calling `selection->selectAll()`.

**Test:** Three cases in `run-tests.ps1`, all using the extended `LUMEN_SELECT`:
- cross-page redact: `LUMEN_SELECT="0,72,140,1,400,400"` + `LUMEN_ANNOTATE=redact` + save, reopen, assert `pageTextLengths[0] -eq 0` **and** `pageTextLengths[1] -eq 0` **and** `pageTextLengths[2] -gt 100`. The third clause is what stops an over-broad regression from passing.
- redact-all: `LUMEN_SELECT=all` + redact + save, assert every entry of `pageTextLengths` is 0.
- oversize page: add an A1 fixture — a `writeOversizeSample()` beside `writeLatinSample()` in `TestFixtures.cpp:57`, same text, `QPageSize::A1` — merge it as page 1 of the latin fixture, select across pages 0-1, redact, and assert the run reports a failure and that `pageTextLengths[1]` is still non-zero. That is the assertion that pins the honest count.

### 4. Hover handler calls into PDFium on the GUI thread
**File** `qml/App/PageView.qml:371,415-425` · **Severity** High · **Effort** Medium

The per-page `MouseArea` sets `hoverEnabled: true` unconditionally, and `onPositionChanged` calls `Document.forms.fieldKindAt()` (419-420) and `Document.linkAt()` (421-425) on every hover move. Both are plain synchronous `Q_INVOKABLE`s with no cache and no fast path; `PdfDocument::linkAt` (1550-1583) opens with `QMutexLocker locker(&m_mutex)` and does `FPDF_LoadPage` / `FPDFLink_GetLinkAtPoint` / `resolveLink` / `FPDF_ClosePage`. `renderPage` (394-476) holds that same non-recursive mutex across `FPDF_RenderPageBitmap` and the `FPDF_FFLDraw` pass, and `PageImageProvider.cpp:97` calls it from a `QRunnable` on the pool. This is the GUI thread touching PDFium, which the project explicitly forbids. The `pageHasLinks` binding at line 388 is the same defect once per delegate: `DocumentController::linkCount` calls `m_document->links()`, which also locks.

**How it fails:** Open a link-heavy paper at 400% zoom, scroll so the pool is rasterising, and sweep the pointer across the references. Each delivered move blocks the GUI thread until the in-flight raster finishes — 100-500 ms at that zoom. Qt compresses queued move events, so it is not 125 stacked acquisitions per second and the app will not usually cross the 5 s threshold that paints "Not Responding"; the realistic symptom is repeated multi-hundred-millisecond stalls and dropped frames. A flick with the pointer resting on a link is the same thing, because Qt Quick re-delivers hover frame-synchronously when items move under a stationary cursor.

**Fix:** Remove the PDFium call from the move path entirely. `PdfDocument::links()` already returns `QVector<LinkTarget>` with `rect` in top-left points (`PdfTypes.h:76-83`). Prefetch that vector once per page off-thread — reuse `PageImageProvider`'s pool, or a small `QRunnable` kicked from the delegate's `Component.onCompleted` — cache it in `DocumentController` keyed by page index, invalidate it from the existing `structureChanged` handler at `DocumentController.cpp:63-75`, and make `linkAt()` and `linkCount()` answer from the cache with zero mutex acquisition; keep the PDFium path only as the cold-miss filler, called from the worker. Do the same for form field geometry, or at minimum gate the `fieldKindAt` call so it is not on the per-move path. Then set `hoverEnabled: pageHasLinks || Document.forms.hasForms` so pages with neither deliver no move events at all. If a synchronous probe must remain anywhere, coalesce it behind a short `Timer` the way `currentPageTick` already coalesces `contentY`.

**Test:** Add a `pdfiumProbesFromGui` counter to `DocumentController`, incremented only when `linkAt`/`linkCount` miss the cache and fall through to `PdfDocument`, reported by `StateReport`. Add a `LUMEN_HOVER="<page>,<x>,<y>,<repeats>"` hook driving the two calls the QML hover path makes. Assert two things on the links fixture: after 50 hovers the counter is 0 (nothing reached PDFium from the GUI thread), and the existing `LUMEN_LINK_PROBE` still resolves to the correct target — the second assertion is what stops "cache is always empty" from passing the first.

### 5. Update asset name reaches a path builder unsanitised
**File** `src/app/UpdateChecker.cpp:253` · **Severity** Medium · **Effort** Small

`m_assetName` comes verbatim from the release JSON's `name` field (152) and the only filter is `endsWith("setup.exe", CaseInsensitive)` (157). Line 253 hands it to `QDir::filePath()`, which returns its argument unchanged when it is absolute and otherwise just concatenates — `..` segments included. The checksum step does not filter it either: the regex at 217 captures `(.+)` and compares to `m_assetName`, so a separator-laden name matches fine. `m_assetUrl` and `m_checksumUrl` come from the same JSON and are not anchored to any host.

**How it fails:** Today, narrowly: whoever controls the release feed names the asset `C:/Users/Public/x-setup.exe` or `..\..\..\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup\lumen-setup.exe`, the user clicks Download, and LumenPDF creates and then deletes an attacker-named `.part` file at an attacker-chosen writable location — `WriteOnly|Truncate`, so it also truncates a pre-existing file whose name happens to end in `.part`. The rename-into-place at 342-343 is currently dead code because of item 8, which is the only reason this is not an arbitrary-location write plus arbitrary-file delete. **The moment item 8's open-mode fix lands, it becomes exactly that.** Fix this first, in the same commit.

**Fix:** In `handleReleaseReply`, reduce before storing: `const QString base = QFileInfo(name).fileName();` then accept only if `base` matches `^LumenPDF-[0-9A-Za-z._-]+-setup\.exe$` and contains no `/`, `\`, `..`, `:` or control characters; otherwise drop the asset and leave `m_assetUrl` clear. In `startAssetDownload`, re-check the built target with `QDir::cleanPath` and confirm it is still inside the Downloads directory before opening the `QFile`. Require `https` and a host of `github.com` or `objects.githubusercontent.com` on both `m_assetUrl` and `m_checksumUrl`. Replace the unconditional `QFile::remove(finalPath)` at 342 with a non-colliding name (`-1`, `-2`, …) when the target exists.

**Test:** No update-path hook exists — `main.cpp:525` covers only `compareVersions`. Add `LUMEN_UPDATE_ASSET="<name>"`, which runs the new sanitiser and reports `acceptedAssetName` (empty when rejected). Assert `LumenPDF-0.3.2-setup.exe` is accepted verbatim, and that `..\..\Startup\lumen-setup.exe`, `C:/Users/Public/x-setup.exe`, `sub/../evil-setup.exe` and `Lumen\0-setup.exe` all come back empty.

### 6. OAuth loopback has no `state` and the listener never times out
**File** `src/cloud/GoogleAuth.cpp:241,272-283` · **Severity** Medium · **Effort** Small

The authorization URL adds `client_id`, `redirect_uri`, `response_type`, `scope`, `code_challenge`, `code_challenge_method`, `access_type` and `prompt` — no `state`. The `newConnection` handler reads `code`/`error` out of the first request line with no check of path, method, `Host`, `Origin`, or any correlation value it issued. `m_loopback->close()` appears exactly once, inside that handler (259), and `setBusy(false)` only on the callback paths — there is no cancel method and no timeout, and `signOut()` (418-427) does not reset `m_busy` either.

**How it fails:** The liveness bug is what a real user hits: click Sign in, close the consent tab, and the port stays bound and `m_busy` stays true for the rest of the process, so `signIn()` early-returns at 218 forever — Drive sign-in is permanently dead until restart. The injection side is bounded by PKCE, which is correctly implemented (`m_codeVerifier` regenerated per `signIn`, S256 challenge sent, verifier posted at exchange), so no token or account binding is achievable. What any local process or any web page reaching `127.0.0.1` *can* do is send `?error=` or a bogus `?code=` to abort an in-progress sign-in — and because the handler closes the listener on the way out, the genuine redirect then gets connection-refused. Add `state` as the standards-required control (RFC 8252 §8.9, OAuth 2.0 Security BCP §4.7), not as an incident response.

**Fix:** Generate `m_state = randomUrlSafe(24)` in `signIn()` alongside `m_codeVerifier`, add it as a `state` query item, and in the `newConnection` handler reject the request — without closing the listener — unless the parsed `state` equals `m_state`, the path is `/`, and the method is `GET`. Clear `m_state` after use. Add a `QTimer` (3 minutes) started in `startLoopbackServer` that closes and deletes `m_loopback`, clears `m_state`, calls `setBusy(false)` and emits `failed(tr("Sign-in timed out."))`; add a public `cancel()` doing the same, and make `signOut()` reset `m_busy`. Separately, accumulate socket data until CRLF before parsing rather than assuming the request line arrives in one packet at 248-250 — the current split-read behaviour degrades to the no-code error path rather than misbehaving, so this is tidiness, not a fix.

**Test:** Add `LUMEN_OAUTH_CALLBACK="<query string>"` that feeds a synthetic request through the handler after `signIn()`, and `LUMEN_OAUTH_TIMEOUT_MS` to shorten the timer. Assert: a callback with a missing or wrong `state` leaves `busy` true and the listener open (report `oauthListening`); a callback with the matching `state` proceeds; and with `LUMEN_OAUTH_TIMEOUT_MS=200`, `busy` is false and `oauthListening` is false after the wait — the assertion that pins the wedge.

### 7. Page-operation undo is asserted vacuously and redo is never executed
**File** `scripts/run-tests.ps1:369` · **Severity** Medium · **Effort** Small

The whole undo coverage is `Assert-Equal 3 $r.report.pageCount` over `rotate,1,1`, `delete,1` and `move,0,2`. Rotate and move do not change the page count, so those two assertions hold identically if `undo()` returned false immediately. Only the delete case observes anything (3 vs 2), and even it cannot distinguish a correctly restored page from a duplicate of the wrong one. `PageOperations::redo()` (`PageOperations.cpp:347`) has zero coverage — `LUMEN_PAGEOP` (`main.cpp:363`) only understands `undo` — so the `m_commands[m_position] = command` write-back at 356, which rewrites Delete's stash index on re-apply, and the redo-branch truncation in `push()` at line 65 have never run, despite Ctrl+Y and a toolbar button being wired to them at `Main.qml:149` and `:350`. I read `revert()` and `redo()` and both are currently correct, so this is regression risk, not a shipped bug, and the operations it guards are in-memory until an explicit save.

**How it fails:** A later change transposes `movePage(command.b, command.a)` or loses the sign in `rotatePage(index, -quarterTurns)`. The suite stays at 107/107 because the page count is 3 either way. The user presses Ctrl+Z after reordering and the document lands in a third, wrong order. On the redo path, losing the stash-index write-back makes a following undo reinsert the wrong page — a silent content swap, green suite.

**Fix:** Three steps.
1. Rotate needs no new plumbing: `rotatePage` calls `rebuildPageInfo()` (`PdfDocument.cpp:1801`), which repopulates from `FPDF_GetPageSizeByIndexF`, so `pageWidthsPoints` flips 612↔792. Add a case running `LUMEN_PAGEOP="rotate,1,1"` asserting `pageWidthsPoints[1]` rounds to 792, and change the existing undo case to assert 612 instead of the page count.
2. Add a `pageTextHeads` array to `StateReport.cpp` beside the `pageTextLengths` loop at line 69 — the text is already being extracted by `pageTextLength`, so the first 40 characters cost nothing. `TestFixtures.cpp:83` writes "Latin fixture page N" on each page, so `move,0,2` becomes exactly assertable: heads are `[2,3,1]` after the move and `[1,2,3]` after undo. This also upgrades the delete/undo case from "count is 3" to "page 2 is back, and it is page 2".
3. Extend `LUMEN_PAGEOP` (`main.cpp:338-367`) to split on `;` and accept `undo` and `redo` as standalone steps, so one run drives `delete,1;undo;redo;undo`. This is the same step-splitting items 1 and 3 need, so do it once, early.

**Test:** The three above, plus the sequenced one: `LUMEN_PAGEOP="delete,1;undo;redo;undo"` asserting `pageTextHeads` is `[1,2,3]` at the end. That is the assertion that pins the stash-index write-back.

### 8. Installer checksum verification can never succeed
**File** `src/app/UpdateChecker.cpp:325` · **Severity** Medium · **Effort** Medium

`m_sink` is opened `WriteOnly | Truncate` (255); `finishDownload` then seeks to 0 and calls `hash.addData(m_sink)` (327). Qt 6's `addData(QIODevice*)` starts with `if (!device->isReadable()) return false;`, and `WriteOnly` is not readable, so it returns false unconditionally. Every download ends at 328 with "The download could not be read back for verification." The comparison at 333 has never executed, and neither has the rename-into-place at 341-343. Nothing in the tree tests the download path at all.

**How it fails:** The update download button is live in `UpdateBanner.qml:114`, so in shipped v0.3.1 the feature is simply inoperative — every user who clicks Download gets an error. It fails closed, which is why this is a functional defect rather than a vulnerability: no unverified installer is ever handed to the user, and `fail()` removes the `.part`. The security weight is that the branch which decides whether to hand the user a runnable `.exe` is entirely unexercised code, so whoever fixes the open mode lands untested logic directly on that decision.

**Fix:** Open the sink `QIODevice::ReadWrite | QIODevice::Truncate` — or close and reopen `ReadOnly` before hashing, which is tidier since nothing needs read access during the transfer. Then extract the verify-and-promote step out of `finishDownload` into a testable `bool verifyAndPromote(const QString &partPath, const QString &expectedHex, QString *outPath)` so it can be driven without a network. **This change must ship in the same commit as item 5's sanitiser** — it is what makes the attacker-controlled path reachable. Land the sanitiser first in the diff.

**Test:** Add `LUMEN_UPDATE_VERIFY="<file>,<expected-hex>"` driving `verifyAndPromote` over a local file, reporting the resulting `downloadedFile`. Two cases: matching hash → `downloadedFile` is the `.exe` path, the file exists, and no `.part` remains; mismatched hash → `downloadedFile` is empty, the file is gone, and `failed()` fired. The mismatch case is the one that matters.

### 9. `cancelDownload()` calls `deleteLater()` through a nulled member
**File** `src/app/UpdateChecker.cpp:365` · **Severity** Low · **Effort** Small

`m_reply` is a raw pointer, the QNAM lives on the GUI thread, and the `finished` connection at 288 is therefore direct. `QNetworkReplyHttpImpl::abort()` emits `finished()` synchronously, so `finishDownload()` runs to completion inside `abort()`: it passes its guard, calls `m_reply->deleteLater()`, sets `m_reply = nullptr`, and — `error == OperationCanceledError` — runs `fail()`, which deletes `m_sink`. Control returns to `cancelDownload()`, whose `if (m_reply)` was evaluated before the re-entry, and it calls `deleteLater()` on a null `this`. This does not crash: `deleteLater()` is non-virtual, does not dereference `this`, and `QCoreApplication::postEvent` null-guards its receiver — the observable result is one "Unexpected null receiver" warning. The `readyRead` size-cap path (269-279) is unaffected; it does `abort(); return;` and touches nothing afterwards.

**How it fails:** The user-visible symptom is not the null call, it is the toast: cancelling a download, or quitting mid-download via `~UpdateChecker`, drives `fail()` and emits `failed("The download failed: Operation canceled")`, so a deliberate cancel surfaces in `UpdateBanner` as a download error.

**Fix:** Detach before aborting, the way `SearchController` and `OcrController` already do: `QNetworkReply *reply = m_reply; m_reply = nullptr; reply->disconnect(this); reply->abort(); reply->deleteLater();`, and the same detach-then-delete for `m_sink`. Because `disconnect(this)` runs first, `finishDownload` is never re-entered and the bogus error toast disappears. Re-check members rather than stale locals after any call that can re-enter.

**Test:** Verified by inspection today; there is no way to drive an in-flight cancel from the harness. Fold coverage into item 8's refactor: once `UpdateChecker`'s state machine is drivable, add a `LUMEN_UPDATE_URL` override pointing at a local file and a `LUMEN_UPDATE_CANCEL=1` hook, and assert that after cancel no `failed()` was emitted, `downloading` is false, and no `.part` remains. If that plumbing is judged not worth it, say so in the commit message rather than leaving a silent gap.

## Not doing, and why

- **Raising or bypassing the 40-megapixel render budget so large-page redaction succeeds.** The budget exists because a crafted page forced a 268 MB raster; the A1 case in item 3 must surface as a visible failure, not as a bigger allocation. Tiled rasterisation for oversize redaction is a real future feature — it is not this pass.
- **Authenticode or pinned-key verification of the installer (finding 8(b)).** The observation is accurate: `m_checksumUrl` and `m_assetUrl` come from the same JSON, so the checksum proves only that nothing corrupted in transit. But that is a description of the trust model, not a defect — no unsigned update channel does better, and `download()` already refuses a release with no checksum at all. It needs signing infrastructure and belongs in its own task.
- **Chasing the crash claims.** Verification refuted all four: the `FPDFPage_Delete` use-after-free (the deleted page's dictionary stays retained by the held `CPDF_Page`), the arbitrary-file-delete and Startup persistence primitive (dead code behind item 8), the OAuth account takeover (PKCE holds), and the null-`deleteLater` crash (Qt null-guards `postEvent`). Fix the mechanisms; do not ship an emergency release or write tests for failures that cannot occur.
- **Making `linkAt`/`fieldKindAt` asynchronous.** The cache in item 4 removes the PDFium call from the hover path entirely, which is strictly better; an async API would add a round-trip to a cursor-shape decision that has to be instant.

## Sequencing

1. **Items 1 and 2, today.** Both are small edits in `PdfDocument`, both are silent data loss, neither depends on anything. Item 2 also deletes one line from `main.cpp:431`, which converts an existing test case into its regression guard for free.
2. **Item 7's harness plumbing next, in parallel with the above** — `;`-separated steps in `LUMEN_PAGEOP` plus `pageTextHeads` in `StateReport`. Items 1 and 3 both need it; do it once.
3. **Item 1's test, then item 3.** Item 1's test needs the step splitting from (2) and the new `formtouch` step and `LUMEN_FORM_PROBE`. Item 3 needs the extended `LUMEN_SELECT` and the A1 fixture; its controller change is independent of everything else.
4. **Item 4 can run in parallel throughout.** It touches `PageView.qml`, `DocumentController` and the render pool and shares no files with items 1-3 except the `structureChanged` handler — coordinate that one hunk with item 1.
5. **Items 5 and 8 are a single commit, sanitiser written first.** Never merge the `ReadWrite` open-mode change without the asset-name filter in the same diff; that ordering is the whole point.
6. **Item 6 is independent** of all of the above and can land whenever.
7. **Item 9 last**, folded into item 8's refactor so it can be covered rather than merely reasoned about.
8. **Finish the rest of item 7** — the rotate-width and `delete;undo;redo;undo` assertions — once the plumbing from (2) is in. It is the only item with no production code change, so it is the safe one to slip if the week runs short.
---

## Open: startup regression in db222a8

Not from the audit. Found while measuring, and **not yet fixed** — I ruled out
four hypotheses and ran out of road. Written down so the next attempt starts
from evidence instead of repeating mine.

### What is established

Same machine, back-to-back, `scripts/benchmark.ps1` (5 runs, median), a git
worktree at each commit:

| | `eb40424` (before) | `db222a8` (after) |
|---|---|---|
| First page, 3 pages | **315 ms** | 542 ms |
| First page, 1000 pages | **547 ms** | 784 ms |
| Memory, 3 pages | **135 MB** | 204 MB |
| Memory, 1000 pages | **148 MB** | 215 MB |

`db222a8` is "Add printing, links, encrypted documents, settings, translations
and CI". Its parent is clean, so the cause is inside that one commit. Whole
process lifetime is *unchanged* (527 ms then, 528 ms now) — only time-to-first-
page and resident memory moved.

The startup marks in `main.cpp` localise it precisely. On current HEAD:

```
  font-resolved           16 ms   +16
  engine-created          22 ms   +6
  settings-read           22 ms   +0
  translations-loaded     22 ms   +0
  updates-created         22 ms   +0
  before-qml-load         22 ms   +0
  qml-loaded             389 ms   +367     <- all of it is here
  first-page-visible     584 ms   +195
```

**The entire cost is inside `engine.loadFromModule("App", "Main")`.**

### What has been ruled out

Each of these was tested by building and measuring, not by reasoning:

- **`Qt6PrintSupport` pulling in `Qt6Widgets`.** Real — linking PrintSupport
  does map 6.6 MB of DLLs, which is why the print sheet was built in QML in the
  first place, and linking it gave the dependency back anyway. Both are now
  delay-loaded and verified absent from the process at idle
  (`startup-does-not-load-printing` asserts it). It changed the number by
  ~20 ms and nothing at all in memory. **Not the cause.**
- **`QSettings`, the translators, and `UpdateChecker`.** 0 ms combined, per the
  marks above. **Not the cause.**
- **The new sheets being instantiated at startup.** Removing `PrintSheet`,
  `PasswordSheet`, `SettingsSheet` and `UpdateBanner` from `Main.qml` entirely
  recovers 23 ms of the 367. `PrintSheet` and `SettingsSheet` are Loader-gated
  now regardless, which is right on its own merits. **Not the cause.**
- **`Document.linkCount()` on the page delegate.** Replacing it with a constant
  changed 526 ms to 521 ms. **Not the cause.**

### Where to look next

Untested, in the order I would try them:

1. **`width: Prefs.windowSize.width` / `visibility: Prefs.windowMaximized ? …`
   in `Main.qml`.** The window used to be a fixed 1280×840; it is now sized from
   a property read at creation, and `visibility` is bound too. If that triggers
   a window-state change or a resize after the surface exists, the swapchain is
   recreated — which would explain the memory as well as the time, and nothing
   else on this list explains the memory at all.
2. `PageView.qml`'s changes in the same commit, beyond `linkCount`.
3. Whether `qt_add_translations`' resource affects QML module load.

### How to measure it

Do not benchmark while anything else is running on the machine. The first
measurement in this investigation was taken while a 13-agent workflow was
compiling, and it produced a 1528 ms outlier for a run that measured 538 ms
minutes later — which sent me after the wrong cause twice.

```powershell
git worktree add ..\lumen-before <commit>
Copy-Item third_party\pdfium ..\lumen-before\third_party\pdfium -Recurse
cd ..\lumen-before ; ./scripts/build.ps1 ; ./scripts/benchmark.ps1
```
