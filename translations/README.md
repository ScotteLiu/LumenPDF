# Translations

LumenPDF ships in English, 繁體中文, 简体中文 and 日本語.

Only **complete** translations are built into the binary. A `.qm` with missing
entries falls back to English string by string, which produces a window in two
languages and no error anywhere — worse than the language not being offered at
all.

## Files

| | |
|---|---|
| `lumenpdf_<locale>.ts` | Qt's translation format. **Generated** — do not hand-edit. |
| `<locale>.json` | The actual translations, as `"source": "translation"`. **Edit this.** |

The `.ts` files are regenerated from the source by `lupdate` whenever a string
changes, and hand edits are exactly what gets lost when it does. The JSON is
the file a translator works in.

## Adding a language

1. Create the stub and let `lupdate` fill in the source strings:

   ```powershell
   $locale = "de"   # or fr, es, ko, pt_BR, ...
   @"
   <?xml version="1.0" encoding="utf-8"?>
   <!DOCTYPE TS>
   <TS version="2.1" language="$locale">
   </TS>
   "@ | Out-File "translations/lumenpdf_$locale.ts" -Encoding utf8
   ./scripts/update-translations.ps1
   ```

2. Copy `zh_TW.json` to `<locale>.json` and replace the values. Keep the keys
   exactly as they are — they are matched literally, and
   `scripts/apply-translations.py` fails rather than silently skipping a key
   that matches nothing.

   Watch for:
   - `%1`, `%2` — argument placeholders, keep them.
   - `%n` — a count. These messages need a plural form; a language with several
     takes a JSON **array** of forms in specification order.
   - `…` `—` `·` `›` `“ ”` — real Unicode characters, not ASCII lookalikes.

3. Apply and check:

   ```powershell
   python scripts/apply-translations.py "translations/lumenpdf_$locale.ts" "translations/$locale.json"
   ```

   It prints `226/226 translated (complete)` when nothing is left.

4. Add the `.ts` to `LUMEN_TS_FILES` in `CMakeLists.txt` and the locale to
   `languages` in `qml/App/SettingsSheet.qml`, naming it as its own speakers
   write it (`Deutsch`, not `German`).

## After changing a UI string

```powershell
./scripts/update-translations.ps1
```

Then re-apply every language. The summary at the end shows which are no longer
complete.
