# md2rtf

A command-line tool written in C that converts Markdown documents to RTF (Rich Text Format), preserving all typographic styles present in the source document. Designed and tested on macOS.

## Features

### Block-level elements

| Element | Syntax |
|---------|--------|
| Headings H1–H6 | `#` … `######` (ATX) |
| Headings H1–H2 | `===` / `---` underline (setext) |
| Paragraphs | Blank-line separated; soft line breaks merged as spaces |
| Fenced code blocks | ` ``` ` or `~~~` (with optional language label) |
| Indented code blocks | 4 spaces or tab |
| Blockquotes | `>` |
| Unordered lists | `-`, `*`, `+` (nested via indentation) |
| Ordered lists | `1.` `2.` … |
| GFM tables | `\| col \| col \|` with separator row |
| Horizontal rule | `---`, `***`, `___` (3+ chars) |
| Hard line break | Two trailing spaces |

### Inline elements

| Element | Syntax |
|---------|--------|
| Bold | `**text**` or `__text__` |
| Italic | `*text*` or `_text_` |
| Bold + Italic | `***text***` |
| Strikethrough | `~~text~~` |
| Inline code | `` `code` `` or ` ``code`` ` |
| Link | `[label](url)` |
| Image | `![alt](url)` — renders alt text in italics |
| Backslash escape | `\*`, `\[`, etc. |

### RTF output characteristics

- **Page size:** A4 portrait, 2.5 cm margins
- **Body font:** Times New Roman 12 pt
- **Heading font:** Helvetica Bold (24 pt → 11 pt, H1 → H6)
- **Code font:** Courier New 10 pt
- **Links:** blue, underlined
- **Blockquotes:** grey left border
- **Tables:** bordered cells, evenly distributed columns
- **Character encoding:** full UTF-8 → `\uNNNN?` Unicode RTF escaping

The RTF output is compatible with **TextEdit**, **Microsoft Word**, **Pages**, and any standard RTF reader on macOS.

## Requirements

- macOS (any recent version)
- Xcode Command Line Tools (`xcode-select --install`)

No external libraries or dependencies.

## Build

```bash
make
```

This compiles `md2rtf.c` with `clang -std=c11 -O2 -Wall`.

### Additional targets

```bash
make install      # copies md2rtf to /usr/local/bin
make uninstall    # removes it from /usr/local/bin
make test         # builds, converts a sample document, opens it in TextEdit
make clean        # removes compiled files
```

## Usage

```bash
md2rtf input.md
```

The output file is written to the same directory as the input, with the same base name and the `.rtf` extension:

```
report.md  →  report.rtf
notes.MD   →  notes.rtf
```

The tool prints the output path to `stderr` on completion:

```
Scritto: report.rtf
```

## Examples

```bash
# Convert a single file
md2rtf README.md

# Convert all Markdown files in a directory
for f in docs/*.md; do md2rtf "$f"; done
```

## Supported Markdown flavour

The converter follows standard CommonMark conventions for the implemented subset:

- A setext heading cannot interrupt a paragraph (blank line required before it).
- A single newline inside a paragraph is treated as a space.
- Italic `_…_` is only recognised when not surrounded by alphanumeric characters (avoids false positives inside `snake_case` identifiers).
- Inline code spans with double backticks (` ``…`` `) are supported.

## Limitations

- No support for nested blockquotes or list continuation paragraphs.
- Images are rendered as italic placeholder text `[Immagine: alt]`; binary image embedding is not implemented.
- Link URLs are not written into the RTF (the visible label is styled as a link, but the destination is discarded).
- Footnotes, definition lists, and task lists (GFM extensions) are not supported.

## Project structure

```
md2rtf.c    — single-file C source (~600 lines)
Makefile    — build, install, and test targets
```

## License

MIT
