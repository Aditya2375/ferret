# ferret

A terminal web browser written in C with no dependencies beyond libc. It
fetches a page over HTTP or from disk, parses the HTML into a DOM, applies
its CSS with a real cascade (specificity, source order, inline styles), and
renders the result as styled ANSI text - headings, lists, tables, preformatted
blocks, and a numbered link list.

```text
$ ferret http://example.com/page.html
$ ferret ./notes.html -w 100
```

## Quick start

```bash
make
./build/ferret tests/page.html            # local file
./build/ferret http://127.0.0.1:8000/     # over HTTP
```

Options: `-w cols` sets the render width (default 80), `--no-color` turns
off ANSI styling for piping or golden-file tests.

## What it implements

- **HTML parser** - tag soup in, DOM out: implicit closes, void elements,
  comments, doctypes, raw-text elements (`script`, `style`, `pre`, `title`),
  attributes in any quoting style, entity decoding (`&amp;`, `&#65;`, ...).
  Malformed input still yields a usable tree.
- **CSS engine** - element, `.class`, `#id`, compound and descendant
  selectors; cascade by specificity then source order; inline `style`
  attributes win; `@media` blocks are skipped safely; shorthand-free
  property model.
- **Layout and rendering** - block and inline flow onto a character grid
  with word wrapping, indentation for lists and blockquotes, ruled tables,
  preserved `pre` whitespace, horizontal rules, and heading hierarchy.
- **Styling** - `font-weight`, `text-decoration`, and named `color` values
  map to bold, underline, and the ANSI palette.
- **HTTP client** - HTTP/1.0 over raw sockets: redirects (301/302), error
  reporting, connection-close bodies. `file://` URLs and bare paths read
  from disk. No TLS - ferret is for the open web and local files.

## Architecture

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Tests

```bash
./tests/run-tests.sh
```

Runs the HTML-parser and CSS-cascade unit suites (24 checks) and a golden
end-to-end render: the sample page is rendered and diffed byte-for-byte
against `tests/page.expected`. CI runs the same script on every push.

## Layout

```text
src/     html.c (parser), css.c (engine), render.c (layout), fetch.c (HTTP), main.c
tests/   unit tests, golden render fixture
docs/    architecture notes
```
