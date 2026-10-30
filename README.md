# ferret

A terminal web browser written in C. It fetches a page over HTTP or HTTPS
(OpenSSL, auto-detected at build time) or from disk, parses the HTML into a DOM, applies
its CSS with a real cascade (specificity, source order, inline styles), and
renders the result as styled ANSI text - headings, lists, tables, preformatted
blocks, and a numbered link list.

```text
$ ferret http://example.com/page.html
$ ferret ./notes.html -w 100
```

## Quick start

```bash
make                    # auto-detects OpenSSL; TLS=0 opts out
./build/ferret tests/page.html            # local file
./build/ferret https://example.com/       # over HTTPS
./build/ferret http://127.0.0.1:8000/     # over plain HTTP
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
- **HTTP/HTTPS client** - HTTP/1.0 over raw sockets, with TLS via OpenSSL:
  SNI, certificate verification against the system trust store, redirects
  (301/302/307/308), error reporting, connection-close bodies. `file://`
  URLs and bare paths read from disk. `make TLS=0` builds a
  dependency-free binary without https support.

## Architecture

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Tests

```bash
./tests/run-tests.sh
```

Runs the HTML-parser and CSS-cascade unit suites (36 checks) and a golden
end-to-end render: the sample page is rendered and diffed byte-for-byte
against `tests/page.expected`. CI runs the same script on every push.

## Layout

```text
src/     html.c (parser), css.c (engine), render.c (layout), fetch.c (HTTP), main.c
tests/   unit tests, golden render fixture
docs/    architecture notes
```

## Tested / not tested

Tested here: 36 parser/cascade unit checks (including entities without
semicolons, lone ampersands, compound .a.b selectors, and a 100k-deep
document), a byte-exact golden render (word wrap, long-word hard wrap,
ordered and unordered lists, tables with inline markup in cells), a
300-link page, fetching and rendering a live page over both http:// and
https:// (real TLS with SNI and certificate verification), acceptance of
a locally trusted certificate and rejection of an untrusted self-signed
one (both against a local TLS server), a three-hop redirect chain
covering relative, absolute-path, scheme-relative, and absolute-URL
Location values, a bounded redirect loop, and a 50k-deep page rendering
without a crash. `tests/check-docs.sh` keeps the counts above honest:
CI fails if the stated check count drifts from what the suites run.

Known limits: tables render up to 64 rows x 16 columns, cell text is
truncated at 127 bytes, and column widths cap at 24 characters.
Rendering below 256 nesting levels is styled; deeper text renders flat.

Not tested: JavaScript-heavy sites (ferret renders documents, there is no
JS engine), chunked transfer encoding or keep-alive (HTTP/1.0
connection-close only), and very large pages.
