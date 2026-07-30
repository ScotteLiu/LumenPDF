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

**CJK correctness** — Chinese, Japanese and Korean text is a first-class case,
not an afterthought. Extraction normalises the Kangxi-radical duplicates that
CJK fonts map glyphs through, so copied text is the characters you actually see
rather than lookalikes that match nothing when pasted. Searching works for a
single ideograph, and double-clicking selects one character instead of the whole
line. All four of those were real bugs, and each now has a regression test.

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

33 assertions, none of them a screenshot comparison. Fixtures are generated
rather than committed — Latin and CJK samples by `src/app/TestFixtures.cpp`
(QPdfWriter), the AcroForm sample by `scripts/make-form-fixture.ps1`.

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

**v1** — ~~installer~~ ✅ · ~~export (images, text)~~ ✅ · ~~true redaction~~ ✅ ·
~~signatures~~ ✅ · ~~compression~~ ✅ · ~~form filling~~ ✅ · **text editing**
remains.

Text editing is the one genuinely open item. PDFium can replace a text object's
string, but not split one, and in most real PDFs a text object is a whole line
or paragraph — the same limitation that decided the redaction design. Editing a
word therefore means re-laying out its whole run, which is a typesetting
problem, not an API call.
**v2** — OCR, batch processing, split-view comparison, AI summarisation.

## Licensing

| Component | Licence | Note |
|---|---|---|
| LumenPDF | TBD | |
| Qt 6 | LGPLv3 | **Dynamically linked.** Static linking would require a commercial Qt licence. |
| PDFium | BSD-3-Clause | Safe for commercial distribution. |
| Inter (planned) | SIL OFL 1.1 | Stands in for SF Pro, which cannot be redistributed. |

MuPDF is deliberately not used: it is AGPL, which would force this project open
or require a commercial licence.

## Platforms

Windows first. macOS and Linux are planned — all platform-specific code is
already isolated in `src/platform/`, and the graphics backend is selected in
one place in `main.cpp`.
