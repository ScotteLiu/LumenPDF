#!/usr/bin/env python3
"""Fills a .ts file from a JSON dictionary of source -> translation.

Translations are kept in translations/*.json rather than edited into the XML by
hand. The .ts files are generated artefacts: lupdate rewrites them whenever a
string changes, and hand edits are exactly what gets lost when it does.

Two things are checked rather than assumed:

  * every key in the dictionary must match a real source string. A typo in a
    key would otherwise silently translate nothing, and an untranslated string
    shows as English with no error anywhere.

  * a plural message ("%n page(s)") needs a <numerusform>, not a <translation>.
    Writing the wrong element makes Qt fall back to the source at runtime.

Usage:
    apply-translations.py translations/lumenpdf_zh_TW.ts translations/zh_TW.json
"""

import json
import sys
import xml.etree.ElementTree as ET


def main(ts_path: str, json_path: str) -> int:
    with open(json_path, encoding="utf-8") as handle:
        table = json.load(handle)

    # ElementTree drops the DOCTYPE, so it is restored on write.
    tree = ET.parse(ts_path)
    root = tree.getroot()

    seen = set()
    translated = 0
    total = 0

    for context in root.findall("context"):
        for message in context.findall("message"):
            total += 1
            source_node = message.find("source")
            source = source_node.text or ""
            seen.add(source)

            value = table.get(source)
            if value is None:
                continue

            node = message.find("translation")
            if node is None:
                node = ET.SubElement(message, "translation")

            # "unfinished" is what makes Qt ignore a translation entirely.
            node.attrib.pop("type", None)

            if message.get("numerus") == "yes":
                for child in list(node):
                    node.remove(child)
                node.text = None
                # Chinese and Japanese have a single plural form; a language
                # with more would list several here.
                forms = value if isinstance(value, list) else [value]
                for form in forms:
                    ET.SubElement(node, "numerusform").text = form
            else:
                node.text = value

            translated += 1

    unknown = sorted(set(table) - seen)
    if unknown:
        print(f"error: {len(unknown)} key(s) match no source string:", file=sys.stderr)
        for key in unknown[:10]:
            print(f"    {key!r}", file=sys.stderr)
        return 1

    with open(ts_path, "w", encoding="utf-8") as handle:
        handle.write('<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE TS>\n')
        handle.write(ET.tostring(root, encoding="unicode"))
        handle.write("\n")

    missing = total - translated
    status = "complete" if missing == 0 else f"{missing} missing"
    print(f"{ts_path}: {translated}/{total} translated ({status})")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2]))
