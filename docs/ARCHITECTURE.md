# ferret architecture

## Pipeline

```text
URL -> fetch -> HTML source -> DOM tree -> computed styles -> character grid -> ANSI text
              (fetch.c)       (html.c)    (css.c)            (render.c)
```

Each stage has a narrow contract and is independently testable: the parser
never sees the network, the CSS engine never sees the terminal, the renderer
never sees a socket.

## html.c - the parser

A single-pass, stack-driven parser. `ps.cur` is the top of the open-element
stack; each start tag pushes, each end tag walks up to its match and pops,
implicitly closing anything above it - which is what makes `<p>one<p>two`
and unclosed `<div>`s parse like a browser would handle them. Void elements
(`br`, `img`, `hr`, ...) never push. Raw-text elements (`script`, `style`,
`pre`, `title`) consume bytes until their own close tag, preserving
whitespace verbatim. Text runs are entity-decoded and whitespace-collapsed,
and adjacent text merges into a single node. Malformed input degrades into
text instead of failing.

## css.c - the cascade

`css_parse` produces rules: a selector (up to 8 compound parts joined by
descendant combinators) plus declarations, in source order. `css_apply`
collects the DOM's elements in pre-order, then for each element keeps the
winning declaration per property: higher specificity wins
(`#id` 100, `.class` 10, `tag` 1, summed across parts), ties broken by
later source order, inline `style` attributes applied last so they always
win. The output is a flat per-element property list - the renderer asks
`style_get(style, "color")` and nothing more.

## render.c - layout on a character grid

The renderer is a tree walk with a cursor (`x`, `y`) writing into a sparse
character grid that pairs every cell with one attribute (bold, underline,
or a palette index). Blocks (`p`, `div`, headings, `blockquote`) break
lines and control indent; inline elements flow through `emit`, which
word-wraps at the configured width. Headings get blank lines and bold;
lists indent and bullet; tables collect their rows (from `thead`/`tbody`
or direct `tr` children), size columns to the widest cell, and rule the
header; `pre` writes raw characters, honoring newlines. Links append a
numbered marker and their targets are listed after the document. The final
pass walks the grid and emits ANSI escapes only when the attribute changes.

## fetch.c - the network

Minimal HTTP/1.0: `Connection: close`, read to EOF, split headers at the
first blank line, follow up to five redirects, report non-200 statuses.
`file://` and bare paths bypass the socket entirely. TLS is an optional
layer (`make TLS=1`, auto-detected when OpenSSL headers are present): the
socket is wrapped in an OpenSSL client with SNI and full certificate
verification against the system trust store - a bad certificate fails the
fetch with the OpenSSL error, it never downgrades. Every read and write
goes through `conn_read`/`conn_write_all`, so the HTTP code above the
connection is identical with and without TLS. A `TLS=0` build is fully
dependency-free and says so when asked for https.

## Testing

- **Unit** - the parser suite covers nesting, void elements, attributes in
  all quoting styles, comments and doctypes, raw-text elements, entity
  decoding, and garbage input. The CSS suite covers specificity ordering
  (`p.big` > `.big` > `p`), descendant selectors, inline-style precedence,
  and `@media` skipping.
- **Golden render** - `tests/page.html` exercises every renderer feature;
  the output is diffed byte-for-byte against `tests/page.expected`, so any
  layout regression fails loudly.
