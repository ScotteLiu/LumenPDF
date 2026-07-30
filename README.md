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

**Editing** — highlight, underline and strike-through in four colours, applied
from a floating bar that appears over the selection. Rotate, reorder and delete
pages. Append another PDF, or export any page as its own file. Everything that
modifies the document is undoable. Save, with the annotations written as
standards-conformant PDF markup that other PDF software reads.

---

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

## Roadmap

**MVP** — ~~open, render, scroll, zoom, search, thumbnails, outline, annotate,
reorder pages, merge, split, save~~ ✅ **complete**
**v1** — text editing, forms, signatures, true redaction, compression, export,
installer.
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
