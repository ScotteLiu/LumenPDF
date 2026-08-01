# LumenPDF

A PDF viewer and editor built for three things, in this order: **speed**,
**restraint**, and **craft**.

Most PDF software picks one. SumatraPDF is instant but cannot edit and looks
like 2009. Acrobat can do everything and takes eight seconds to show you a
page. Stirling-PDF has fifty tools and no coherent interface. LumenPDF is an
attempt at all three at once.

Status: **working editor.** Opens, renders, prints, searches, selects,
annotates, redacts, fills forms, recognises scanned text, and saves.

## What works today

**Reading** — open, render, scroll, zoom, fit-width, page thumbnails, document
outline with collapsible sections, full-text search (245 pages and 1028 matches
in 130 ms), match highlighting with a strong marker on the current hit. It
reopens each document where you stopped reading, and remembers the last dozen
files you opened.

**Printing** — page ranges, copies, greyscale, fit-to-page or actual size, and
printing to a PDF file that keeps the source page size. Pages are rasterised
through the same engine that draws them on screen, so annotations, filled form
fields and OCR layers all come out — rather than depending on the printer's own
PDF interpreter.

**Links** — internal jumps follow on click. External links never open without
showing you the actual target first, because what a PDF prints and where its
link points are unrelated strings. Only `http`, `https` and `mailto` are ever
handed to the shell; a `Launch` action naming a program is not offered as a
link at all.

**Encrypted documents** — a password-protected PDF asks for its password
instead of refusing to open. The password is used once and never written
anywhere: this application has no keystore of its own, and storing someone
else's document password without one is not a trade worth making.

**Your language** — the interface is available in English, 繁體中文, 简体中文
and 日本語, following the system locale by default. Only fully translated
languages are offered; a partial translation produces a window in two languages
and no error anywhere. Adding one is a single JSON file — see
[translations/README.md](translations/README.md).

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

The signature is an **ink stamp**, not a cryptographic digital signature. It
does not identify who signed and does not break if the document is altered
afterwards. If you need a legally recognised signature, this is not it.

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

107 assertions, none of them a screenshot comparison, run on every push by
[GitHub Actions](.github/workflows/build.yml).

Fixtures are generated rather than committed — Latin, CJK and ten-script
samples by `src/app/TestFixtures.cpp` (QPdfWriter), an AcroForm sample, a
link-annotation sample, and ten deliberately broken files, each by its own
script in `scripts/`.

The encrypted fixture is the odd one: `scripts/make-encrypted-fixture.ps1`
implements PDF 1.4's standard security handler — RC4, the key derivation, the
`/O` and `/U` entries — in about a hundred lines of PowerShell, because no
library on the build machine produces one and a downloaded encrypted PDF would
be an opaque binary in the repository. It exists so the unlock path can be
tested against a real encrypted file. LumenPDF reads these; it never writes
them.

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

**v2** — ~~OCR~~, ~~printing~~, ~~links~~, ~~encrypted documents~~,
~~persisted settings and recent files~~, ~~UI translations~~, ~~CI~~ ✅

**Next, in rough order of how much their absence hurts:**

| | |
|---|---|
| Annotation review | Sticky notes, shapes, and a sidebar listing every annotation. Today you can create markup but not browse or delete it from one place. |
| Tabs | One document at a time. |
| Cryptographic signatures | Both signing and verification. The current signature is an ink stamp — see above. |
| Page composition | Insert a blank page, crop, watermark, page numbers, split into several files. |
| Conversion | PDF → Word, and images or Office documents → PDF. |
| More languages | The infrastructure ships; each new one is a single JSON file. |
| macOS and Linux | All platform-specific code is already isolated in `src/platform/`. |
| Code signing | Implemented in `scripts/package.ps1`, waiting on a certificate — see [docs/CODE-SIGNING.md](docs/CODE-SIGNING.md). |

## Licensing

LumenPDF is under the **Apache License 2.0**. Free for anything, including
commercial use, with no obligation to ask.

It started under the Business Source License, which reserved commercial rights
until 2030. That was the wrong trade. A tool that claims to *destroy* text on a
page is asking to be trusted with something irreversible, and "read the code
yourself" is the only honest answer to that — which argues for the most
permissive licence, not a restricted one. The reserved commercial rights were
protecting revenue that was never going to arrive, while shutting the project
out of Linux distributions, free code signing for open source, and anyone who
might have wanted to help.

If it is useful to you, [contributions and donations](#contributing) are the
support model. Neither is required.

**The name is not part of the licence.** Section 6 of Apache 2.0 grants no
trademark rights: fork it, change it, sell it — but an unofficial build should
not call itself LumenPDF. See [NOTICE](NOTICE).

| Component | Licence | Note |
|---|---|---|
| LumenPDF | Apache 2.0 | Free for any use. |
| Qt 6 | LGPLv3 | **Dynamically linked.** The `Qt6*.dll` files sit next to the executable and can be replaced; static linking would require a commercial Qt licence. |
| PDFium | BSD-3-Clause | |
| Inter (planned) | SIL OFL 1.1 | Stands in for SF Pro, which cannot be redistributed. |

MuPDF is deliberately not used: it is AGPL, which would make the licence above
impossible.

## Contributing

The two things that would help most are the two this project cannot do alone:

**Translations.** The interface ships in English, 繁體中文, 简体中文 and 日本語.
Adding a language is one JSON file — see
[translations/README.md](translations/README.md). Only complete translations are
built in, so a half-finished one is never shown to anyone.

**macOS and Linux.** Every platform-specific call is already isolated in
`src/platform/`, and the graphics backend is chosen in one place in `main.cpp`.
The port is real work, but it is not archaeology.

Bug reports are welcome, especially with the PDF that caused them — this project
has been wrong about text extraction in four different writing systems so far,
and every one of those was found by someone looking at a real document.

## Platforms

Windows first. macOS and Linux are planned — all platform-specific code is
already isolated in `src/platform/`, and the graphics backend is selected in
one place in `main.cpp`.
