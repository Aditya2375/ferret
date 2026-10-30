#include "html.h"

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* Void elements never have children or a closing tag. */
static const char *const void_tags[] = {
    "area", "base", "br", "col", "embed", "hr", "img", "input",
    "link", "meta", "param", "source", "track", "wbr", NULL
};

/* Tags whose raw text is not markup. */
static const char *const raw_tags[] = { "script", "style", "title", "pre", NULL };

static int tag_in(const char *tag, const char *const *list) {
    for (int i = 0; list[i]; i++)
        if (strcmp(tag, list[i]) == 0) return 1;
    return 0;
}

static char *xstrndup(const char *s, size_t n) {
    char *p = malloc(n + 1);
    if (!p) { perror("malloc"); exit(1); }
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static struct node *node_new(enum node_type type) {
    struct node *n = calloc(1, sizeof *n);
    if (!n) { perror("calloc"); exit(1); }
    n->type = type;
    n->style_index = -1;
    return n;
}

static void node_append(struct node *parent, struct node *child) {
    child->parent = parent;
    if (parent->last_child) {
        parent->last_child->next_sibling = child;
    } else {
        parent->first_child = child;
    }
    parent->last_child = child;
}

static void attr_add(struct node *n, char *name, char *value) {
    struct attr *a = malloc(sizeof *a);
    if (!a) { perror("malloc"); exit(1); }
    for (char *p = name; *p; p++) *p = tolower((unsigned char)*p);
    a->name = name;
    a->value = value;
    a->next = n->attrs;
    n->attrs = a;
}

/* Skip whitespace; return new position. */
static const char *skip_ws(const char *p) {
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

/* Parse a tag name: [a-zA-Z][a-zA-Z0-9-]* lower-cased into buf. */
static const char *parse_tag_name(const char *p, char *buf, size_t cap) {
    size_t n = 0;
    while (*p && (isalnum((unsigned char)*p) || *p == '-' || *p == '!')) {
        if (n + 1 < cap) buf[n++] = tolower((unsigned char)*p);
        p++;
    }
    buf[n] = '\0';
    return p;
}

/* Decode the handful of named entities that matter in real pages, and
   numeric character references. p points just past '&'; returns the number
   of input bytes consumed (still past '&'), or 0 when the sequence is not
   an entity and the '&' should be emitted literally. */
static size_t decode_entity(const char *p, char *out) {
    struct { const char *name; char ch; } named[] = {
        {"amp;", '&'}, {"lt;", '<'}, {"gt;", '>'}, {"quot;", '"'},
        {"apos;", '\''}, {"nbsp;", ' '}, {NULL, 0}
    };
    if (*p == '#') {
        const char *q = p + 1;
        int base = 10;
        if (*q == 'x' || *q == 'X') { base = 16; q++; }
        char *stop = NULL;
        long v = strtol(q, &stop, base);
        if (stop == q) return 0;    /* no digits: literal '&' */
        q = stop;
        if (*q == ';') q++;         /* the semicolon is optional */
        if (v > 0 && v < 128) *out = (char)v;
        else *out = ' ';
        return (size_t)(q - p);
    }
    for (int i = 0; named[i].name; i++) {
        size_t len = strlen(named[i].name);
        if (strncmp(p, named[i].name, len) == 0) {
            *out = named[i].ch;
            return len;
        }
    }
    return 0;
}

/* Append text to parent's last text child, or create one. */
static void add_text(struct node *parent, const char *start, const char *end) {
    while (start < end && isspace((unsigned char)*start)) start++;
    while (end > start && isspace((unsigned char)end[-1])) end--;
    if (start >= end) return;
    /* merge with previous text node */
    struct node *t = parent->last_child;
    if (!t || t->type != NODE_TEXT) {
        t = node_new(NODE_TEXT);
        t->text = strdup("");
        node_append(parent, t);
    }
    size_t old = strlen(t->text);
    size_t add = (size_t)(end - start);
    t->text = realloc(t->text, old + add + 2);
    if (!t->text) { perror("realloc"); exit(1); }
    if (old) t->text[old++] = ' ';
    /* copy with entity decoding */
    size_t w = old;
    for (const char *p = start; p < end; ) {
        if (*p == '&') {
            char ch;
            size_t used = decode_entity(p + 1, &ch);
            t->text[w++] = ch;
            /* advance past "&...;" when it looked like an entity */
            const char *semi = memchr(p, ';', (size_t)(end - p));
            if (p[1] == '#' && semi && semi - p < 12) p = semi + 1;
            else if (semi && semi - p < 8 && used) p = semi + 1;
            else p++;
        } else if (isspace((unsigned char)*p)) {
            if (w > 0 && t->text[w-1] != ' ') t->text[w++] = ' ';
            p++;
        } else {
            t->text[w++] = *p++;
        }
    }
    t->text[w] = '\0';
}

struct parser {
    const char *p;
    struct node *root;
    struct node *cur;       /* open-element stack top */
};

/* Parse attributes up to '>' or '/>'. p points just past the tag name. */
static const char *parse_attrs(struct parser *ps, struct node *el) {
    const char *p = ps->p;
    for (;;) {
        p = skip_ws(p);
        if (*p == '>' || *p == '\0') break;
        if (*p == '/') { p++; continue; }
        char name[128];
        const char *ns = p;
        while (*p && *p != '=' && !isspace((unsigned char)*p) && *p != '>' && *p != '/') p++;
        size_t nl = (size_t)(p - ns);
        if (nl >= sizeof name) nl = sizeof name - 1;
        memcpy(name, ns, nl);
        name[nl] = '\0';
        char *value = strdup("");
        p = skip_ws(p);
        if (*p == '=') {
            p = skip_ws(p + 1);
            free(value);
            if (*p == '"' || *p == '\'') {
                char q = *p++;
                const char *vs = p;
                while (*p && *p != q) p++;
                value = xstrndup(vs, (size_t)(p - vs));
                if (*p) p++;
            } else {
                const char *vs = p;
                while (*p && !isspace((unsigned char)*p) && *p != '>') p++;
                value = xstrndup(vs, (size_t)(p - vs));
            }
        }
        if (name[0]) attr_add(el, strdup(name), value);
        else free(value);
    }
    return p;
}

/* Parse raw text of script/style/title until matching close tag. */
static const char *parse_raw(struct parser *ps, const char *tag) {
    char close[160];
    snprintf(close, sizeof close, "</%s", tag);
    const char *start = ps->p;
    const char *p = ps->p;
    while (*p) {
        if (*p == '<' && strncasecmp(p, close, strlen(close)) == 0) break;
        p++;
    }
    if (p > start) {
        struct node *t = node_new(NODE_TEXT);
        t->text = xstrndup(start, (size_t)(p - start));
        node_append(ps->cur, t);
    }
    /* consume the close tag */
    const char *gt = strchr(p, '>');
    return gt ? gt + 1 : p;
}

static void handle_close_tag(struct parser *ps, const char *name) {
    /* Walk up the stack to the matching open element; implicitly close
       anything above it. */
    for (struct node *n = ps->cur; n != ps->root; n = n->parent) {
        if (n->tag && strcmp(n->tag, name) == 0) {
            ps->cur = n->parent;
            return;
        }
    }
    /* Unmatched close tag: ignore. */
}

struct node *html_parse(const char *source) {
    struct parser ps;
    ps.p = source;
    ps.root = node_new(NODE_ELEMENT);
    ps.root->tag = strdup("#document");
    ps.cur = ps.root;

    while (*ps.p) {
        const char *lt = strchr(ps.p, '<');
        if (!lt) { add_text(ps.cur, ps.p, ps.p + strlen(ps.p)); break; }
        if (lt > ps.p) add_text(ps.cur, ps.p, lt);

        if (strncmp(lt, "<!--", 4) == 0) {
            const char *e = strstr(lt + 4, "-->");
            ps.p = e ? e + 3 : lt + strlen(lt);
            continue;
        }
        if (lt[1] == '!' || lt[1] == '?') {  /* doctype, PI */
            const char *gt = strchr(lt, '>');
            ps.p = gt ? gt + 1 : lt + strlen(lt);
            continue;
        }
        if (lt[1] == '/') {
            char name[128];
            const char *gt = strchr(lt, '>');
            parse_tag_name(lt + 2, name, sizeof name);
            if (name[0]) handle_close_tag(&ps, name);
            ps.p = gt ? gt + 1 : lt + strlen(lt);
            continue;
        }

        /* open tag */
        char name[128];
        ps.p = parse_tag_name(lt + 1, name, sizeof name);
        if (!name[0]) { add_text(ps.cur, lt, lt + 1); ps.p = lt + 1; continue; }
        struct node *el = node_new(NODE_ELEMENT);
        el->tag = strdup(name);
        ps.p = parse_attrs(&ps, el);
        int selfclose = 0;
        if (ps.p > source && ps.p[-1] == '/') selfclose = 1;
        if (*ps.p == '>') ps.p++;
        node_append(ps.cur, el);
        if (!selfclose && !tag_in(name, void_tags)) {
            ps.cur = el;
            if (tag_in(name, raw_tags)) {
                ps.p = parse_raw(&ps, name);
                ps.cur = el->parent;
            }
        }
    }
    return ps.root;
}

void node_free(struct node *n) {
    if (!n) return;
    struct node *c = n->first_child;
    while (c) {
        struct node *next = c->next_sibling;
        node_free(c);
        c = next;
    }
    struct attr *a = n->attrs;
    while (a) {
        struct attr *next = a->next;
        free(a->name);
        free(a->value);
        free(a);
        a = next;
    }
    free(n->tag);
    free(n->text);
    free(n);
}

const char *node_attr(const struct node *n, const char *name) {
    for (struct attr *a = n->attrs; a; a = a->next)
        if (strcmp(a->name, name) == 0) return a->value;
    return NULL;
}

int node_is(const struct node *n, const char *tag) {
    return n->type == NODE_ELEMENT && n->tag && strcmp(n->tag, tag) == 0;
}

struct node *node_find(const struct node *n, const char *tag) {
    if (node_is(n, tag)) return (struct node *)n;
    for (struct node *c = n->first_child; c; c = c->next_sibling) {
        struct node *hit = node_find(c, tag);
        if (hit) return hit;
    }
    return NULL;
}
