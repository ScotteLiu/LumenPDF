# LumenPDF

A PDF viewer and editor built for three things, in this order: **speed**,
**restraint**, and **craft**.

Most PDF software picks one. SumatraPDF is instant but cannot edit and looks
like 2009. Acrobat can do everything and takes eight seconds to show you a
page. Stirling-PDF has fifty tools and no coherent interface. LumenPDF is an
attempt at all three at once.

Status: **working editor.** Opens, renders, searches, selects, annotates, and
saves. Text editing and OCR are not implemented yet.

## What works today

**Reading** — open, render, scroll, zoom, fit-width, page thumbnails, document
outline with collapsible sections, full-text search (245 pages and 1028 matches
in 130 ms), match highlighting with a strong marker on the current hit.

**Selecting** — click-drag and double-click selection that follows the real
text flow, including across columns and across pages. Copy to clipboard.

**Every script, not just Latin** — text extraction is where PDF tools quietly go
wrong, and each writing system fails differently: right-to-left order in Arabic
and Hebrew, stacked tone marks in Vietnamese, clusters in Devanagari and Thai,
scripts written without spaces. Ten writing systems are regression-tested on
every change.

Chinese alone had four real bugs, all now fixed and held by tests: extraction
returned Kangxi-radical lookalikes so copied text matched nothing when pasted;
searching for a single ideograph was blocked by a minimum query length; a
one-character selection read as empty; and Latin word rules swallowed whole
runs of Han on a double-click.

**OCR** — scanned pages get an invisible text layer written underneath the
image, so the scan looks identical and becomes searchable, selectable and
copyable. Backed by Windows' own recogniser: offline, no extra download, and it
uses whichever languages your system already has.

**Google Drive** — open from Drive, edit, and save back to the same file,
keeping its ID and sharing. Scoped to `drive.file`, so it can only touch files
you explicitly open or that LumenPDF created. Requires your own OAuth client —
see [docs/GOOGLE-DRIVE-SETUP.md](docs/GOOGLE-DRIVE-SETUP.md) for why that cannot
be shipped for you, and how to get one in a few minutes.

**Editing** — highlight, underline and strike-through in four colours, applied
from a floating bar that appears over the selection. Rotate, reorder and delete
pages. Append another PDF, or export any page as its own file. Sign by drawing,
stamped as vector paths rather than a bitmap. Everything that modifies the
document is undoable. Save, with the annotations written as
standards-conformant PDF markup that other PDF software reads.

**Redaction that actually redacts** — the selected text is destroyed, not
covered. The affected page is rasterised and its objects replaced, so there is
nothing left underneath to recover. That costs the page its selectable text, and
the confirmation dialog says so rather than hiding it. Verified by extracting
text from the saved file, not by looking at the black box.

**Text correction** — press Ctrl+E, click a run of text, and edit it in place.
Be aware of what this is and is not.

PDFium can replace a text object's string but cannot split one, and most PDFs
embed fonts as *subsets* containing only the glyphs already used. Typing a
character the font does not have produces silent garbage — the first version of
this turned "Latin fixture page 1" into "Latin fixite  Luen ure page 1" while
reporting success. So every edit is read back and verified, and one that does
not survive re-encoding is rolled back with an explanation. Corrupting a
document and calling it success is worse than refusing.

What that leaves is a reliable tool for **correcting** text — typos, numbers,
labels — and an honest refusal everywhere else. It is not a word processor, and
the editor shows you the exact run it is about to replace rather than letting
you assume you are changing one word.

**Forms** — click a field and type. AcroForm text fields, checkboxes and radio
buttons, filled through PDFium's form-fill environment and written back into the
document. The cursor changes over a field, which is how anyone discovers a PDF
is fillable at all.

**Export and compression** — pages to PNG or JPEG at a chosen DPI, plain text,
and a reduced copy that downsamples only images oversized relative to how large
they are actually drawn.

---

## Measured

`./scripts/benchmark.ps1` — 5 runs, median. On a 143 Hz display:

| | 3-page document | 1000-page document |
|---|---|---|
| First page on screen | **287 ms** | **451 ms** |
| Whole process lifetime | 536 ms | **527 ms** |
| Memory | 132 MB | 145 MB |
| Scrolling, 1000 pages | — | **144 fps** (vsync-locked; no dropped frames) |

Opening a 1000-page document costs the same as opening a 3-page one. That is
the architectural claim, and it is what the numbers say: page geometry is
cached once at load and rendering is fully virtualised, so page count does not
enter the opening cost.

For scale, SumatraPDF reaches input-idle on the same file in 585 ms — but that
is a different measurement, not a like-for-like comparison, and the benchmark
script says so rather than implying a win.

## Architecture

```
src/
  core/       PDF backend -- document loading, page geometry, rasterisation
  render/     LRU raster cache + async QML image provider (worker thread pool)
  bridge/     QObject/QAbstractListModel layer exposed to QML
  platform/   every OS-specific call in the app lives here, and nowhere else
  app/        entry point
qml/
  Lumen/      the design system: tokens, motion, and hand-built components
  App/        screens assembled from Lumen components
```

**The GUI thread never touches PDFium.** Pages are rasterised on a worker pool
and handed to QML as textures; the view virtualises so a 5000-page document
costs the same as a 5-page one. That is the whole performance story.

## The design system

`qml/Lumen` is a component set written from scratch. Qt Quick Controls' built-in
styles are not used anywhere — a Qt app that looks like a Qt app was not the
goal.

- **`Squircle`** — rounded rectangles with continuous corner curvature rather
  than circular arcs. This one primitive does more for the look of the app than
  anything else in the repo.
- **`Tokens`** — every colour, radius, spacing step and type size. No component
  hard-codes a visual value, so theming is one property assignment.
- **`Motion`** — durations and curves. Direct manipulation springs; state
  changes ease out. Nothing is linear.

## Building

Requires **Qt 6.7+**, **CMake 3.24+**, **Ninja**, and **MSVC 2022**.

```powershell
# 1. Fetch the render backend (BSD-licensed, ~10 MB, not committed)
./scripts/fetch-pdfium.ps1

# 2. Configure and build
cmake --preset windows-release
cmake --build --preset windows-release
```

The app also builds without PDFium present — it falls back to a stub renderer
that draws placeholder pages, which is enough to work on the UI.

### Tests

```powershell
./scripts/run-tests.ps1
```

54 assertions, none of them a screenshot comparison. Fixtures are generated
rather than committed — Latin and CJK samples by `src/app/TestFixtures.cpp`
(QPdfWriter), the AcroForm sample by `scripts/make-form-fixture.ps1`, and ten
deliberately broken files by `scripts/make-hostile-fixtures.ps1`.

The hostile set is empty files, truncated downloads, corrupted bodies, an xref
pointing past the end, a page tree containing itself, a self-referential
outline, and a page declaring itself 200000 inches square. The bar is not that
they open — five of them cannot — but that they are refused without crashing or
hanging, and that no single crafted file dictates how much memory the process
uses. That last one is a real fix: the huge-page case forced a 268 MB raster
until the render path grew a pixel budget.

The suite runs the app with `LUMEN_*` hooks, has it write a JSON state report
through the same controllers the UI uses, and asserts on that. Redaction is the
case that makes the approach necessary: a black box proves nothing, so the
assertion is that the page's extracted text length went to **zero** while every
other page stayed intact.

### Packaging

```powershell
./scripts/package.ps1
```

Produces `build/dist/LumenPDF-<version>-win64-portable.zip` and
`build/dist/LumenPDF-<version>-win64-setup.exe` (the installer step needs
`winget install JRSoftware.InnoSetup`).

Packaging stages a self-contained directory, prunes what this build cannot use
— the software OpenGL fallback, the DXIL compiler, and five unused Qt Quick
Controls styles, ~110 MB in total — and then **launches the pruned copy and
requires it to render a page** before writing either artefact. Pruning is the
step most likely to produce a package that builds cleanly and dies on someone
else's machine, so it is not taken on trust.

## Roadmap

**MVP** — ~~open, render, scroll, zoom, search, thumbnails, outline, annotate,
reorder pages, merge, split, save~~ ✅ **complete**

**v1** — complete: installer, export, true redaction, signatures, compression,
form filling, and text correction.

**v2** — OCR, batch processing, split-view comparison, AI summarisation, and
real text editing (see the caveat below).
**v2** — OCR, batch processing, split-view comparison, AI summarisation.

## Licensing

LumenPDF itself is under the **Business Source License 1.1** — the source is
public and readable, personal, academic and non-profit use is free, commercial
use requires a licence, and on **2030-07-30** it converts automatically to
Apache 2.0.

That combination is deliberate. A tool that claims to redact documents is
asking to be trusted with something irreversible, and "read the code yourself"
is the only honest answer to that. Keeping it source-available rather than
proprietary makes that answer possible without giving away the ability to sell
it. If you want to use LumenPDF commercially, get in touch.

| Component | Licence | Note |
|---|---|---|
| LumenPDF | BSL 1.1 → Apache 2.0 on 2030-07-30 | Free for personal, academic and non-profit use. |
| Qt 6 | LGPLv3 | **Dynamically linked.** Static linking would require a commercial Qt licence. |
| PDFium | BSD-3-Clause | Safe for commercial distribution. |
| Inter (planned) | SIL OFL 1.1 | Stands in for SF Pro, which cannot be redistributed. |

MuPDF is deliberately not used: it is AGPL, which would force this project open
or require a commercial licence.

## Platforms

Windows first. macOS and Linux are planned — all platform-specific code is
already isolated in `src/platform/`, and the graphics backend is selected in
one place in `main.cpp`.
