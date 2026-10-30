#include "css.h"

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static char *xstrndup(const char *s, size_t n) {
    char *p = malloc(n + 1);
    if (!p) { perror("malloc"); exit(1); }
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static const char *skip_ws_comments(const char *p) {
    for (;;) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (p[0] == '/' && p[1] == '*') {
            const char *e = strstr(p + 2, "*/");
            p = e ? e + 2 : p + strlen(p);
        } else return p;
    }
}

static int selector_parse(const char *s, size_t len, struct selector *sel) {
    memset(sel, 0, sizeof *sel);
    size_t i = 0;
    while (i < len) {
        if (sel->nparts >= 8) return 0;
        struct selector_part *part = &sel->parts[sel->nparts];
        memset(part, 0, sizeof *part);
        /* optional tag */
        size_t start = i;
        while (i < len && (isalnum((unsigned char)s[i]) || s[i] == '-' || s[i] == '*')) i++;
        if (i > start) {
            char *t = xstrndup(s + start, i - start);
            if (strcmp(t, "*") == 0) { free(t); t = NULL; }
            else for (char *c = t; *c; c++) *c = tolower((unsigned char)*c);
            part->tag = t;
        }
        /* .class and #id */
        while (i < len && (s[i] == '.' || s[i] == '#')) {
            char kind = s[i++];
            start = i;
            while (i < len && (isalnum((unsigned char)s[i]) || s[i] == '-' || s[i] == '_')) i++;
            char *v = xstrndup(s + start, i - start);
            if (kind == '.') { free(part->cls); part->cls = v; }
            else { free(part->id); part->id = v; }
        }
        if (!part->tag && !part->id && !part->cls) return 0;
        sel->nparts++;
        while (i < len && isspace((unsigned char)s[i])) i++;  /* descendant combinator */
    }
    return sel->nparts > 0;
}

static void selector_free(struct selector *sel) {
    for (int i = 0; i < sel->nparts; i++) {
        free(sel->parts[i].tag);
        free(sel->parts[i].id);
        free(sel->parts[i].cls);
    }
}

static struct declaration *parse_declarations(const char *p, const char *end) {
    struct declaration *list = NULL;
    while (p < end) {
        const char *colon = memchr(p, ':', (size_t)(end - p));
        if (!colon) break;
        const char *semi = memchr(colon, ';', (size_t)(end - colon));
        const char *vend = semi ? semi : end;
        /* trim */
        while (p < colon && isspace((unsigned char)*p)) p++;
        const char *pe = colon;
        while (pe > p && isspace((unsigned char)pe[-1])) pe--;
        const char *vs = colon + 1;
        while (vs < vend && isspace((unsigned char)*vs)) vs++;
        while (vend > vs && isspace((unsigned char)vend[-1])) vend--;
        if (pe > p && vend > vs) {
            struct declaration *d = malloc(sizeof *d);
            if (!d) { perror("malloc"); exit(1); }
            d->property = xstrndup(p, (size_t)(pe - p));
            for (char *c = d->property; *c; c++) *c = tolower((unsigned char)*c);
            d->value = xstrndup(vs, (size_t)(vend - vs));
            d->next = list;
            list = d;
        }
        p = semi ? semi + 1 : end;
    }
    return list;
}

struct stylesheet *css_parse(const char *source) {
    struct stylesheet *ss = calloc(1, sizeof *ss);
    if (!ss) { perror("calloc"); exit(1); }
    const char *p = source;
    struct rule **tail = &ss->rules;
    while (*p) {
        p = skip_ws_comments(p);
        if (!*p) break;
        if (*p == '@') {  /* @media etc: skip block */
            const char *b = strchr(p, '{');
            if (!b) break;
            int depth = 1;
            p = b + 1;
            while (*p && depth) { if (*p == '{') depth++; else if (*p == '}') depth--; p++; }
            continue;
        }
        const char *open = strchr(p, '{');
        if (!open) break;
        const char *close = strchr(open, '}');
        if (!close) break;
        /* split selector list on commas */
        const char *s = p;
        while (s < open) {
            const char *comma = memchr(s, ',', (size_t)(open - s));
            const char *send = comma ? comma : open;
            while (s < send && isspace((unsigned char)*s)) s++;
            while (send > s && isspace((unsigned char)send[-1])) send--;
            struct selector sel;
            if (selector_parse(s, (size_t)(send - s), &sel)) {
                struct rule *r = calloc(1, sizeof *r);
                if (!r) { perror("calloc"); exit(1); }
                r->sel = sel;
                r->decls = parse_declarations(open + 1, close);
                r->order = ss->count++;
                *tail = r;
                tail = &r->next;
            } else {
                selector_free(&sel);
            }
            s = comma ? comma + 1 : open;
        }
        p = close + 1;
    }
    return ss;
}

static void decls_free(struct declaration *d) {
    while (d) {
        struct declaration *next = d->next;
        free(d->property);
        free(d->value);
        free(d);
        d = next;
    }
}

void stylesheet_free(struct stylesheet *ss) {
    if (!ss) return;
    struct rule *r = ss->rules;
    while (r) {
        struct rule *next = r->next;
        selector_free(&r->sel);
        decls_free(r->decls);
        free(r);
        r = next;
    }
    free(ss);
}

static int part_matches(const struct selector_part *part, const struct node *el) {
    if (part->tag && (!el->tag || strcmp(part->tag, el->tag) != 0)) return 0;
    if (part->id) {
        const char *id = node_attr(el, "id");
        if (!id || strcmp(id, part->id) != 0) return 0;
    }
    if (part->cls) {
        const char *cls = node_attr(el, "class");
        if (!cls) return 0;
        /* class attribute is a space-separated list */
        size_t want = strlen(part->cls);
        const char *p = cls;
        int found = 0;
        while (*p) {
            while (*p == ' ') p++;
            const char *e = p;
            while (*e && *e != ' ') e++;
            if ((size_t)(e - p) == want && strncmp(p, part->cls, want) == 0) { found = 1; break; }
            p = e;
        }
        if (!found) return 0;
    }
    return 1;
}

static int selector_matches(const struct selector *sel, const struct node *el) {
    if (!part_matches(&sel->parts[sel->nparts - 1], el)) return 0;
    /* walk ancestors for the remaining parts, right to left */
    int i = sel->nparts - 2;
    for (const struct node *a = el->parent; a && i >= 0; a = a->parent) {
        if (a->type != NODE_ELEMENT || !a->tag || a->tag[0] == '#') continue;
        if (part_matches(&sel->parts[i], a)) i--;
    }
    return i < 0;
}

static int specificity(const struct selector *sel) {
    int ids = 0, clss = 0, tags = 0;
    for (int i = 0; i < sel->nparts; i++) {
        if (sel->parts[i].id) ids++;
        if (sel->parts[i].cls) clss++;
        if (sel->parts[i].tag) tags++;
    }
    return ids * 100 + clss * 10 + tags;
}

/* Inline style attribute -> declarations. */
static struct declaration *parse_style_attr(const char *attr) {
    return parse_declarations(attr, attr + strlen(attr));
}

static void style_set(struct style *s, const char *property, const char *value) {
    for (struct declaration *d = s->decls; d; d = d->next) {
        if (strcmp(d->property, property) == 0) {
            free(d->value);
            d->value = strdup(value);
            return;
        }
    }
    struct declaration *d = malloc(sizeof *d);
    if (!d) { perror("malloc"); exit(1); }
    d->property = strdup(property);
    d->value = strdup(value);
    d->next = s->decls;
    s->decls = d;
}

/* Collect elements in pre-order. */
static int collect(struct node *n, struct node ***arr, int *count, int *cap) {
    if (n->type == NODE_ELEMENT && n->tag && n->tag[0] != '#') {
        if (*count >= *cap) {
            *cap = *cap ? *cap * 2 : 64;
            *arr = realloc(*arr, (size_t)*cap * sizeof **arr);
            if (!*arr) { perror("realloc"); exit(1); }
        }
        (*arr)[(*count)++] = n;
    }
    for (struct node *c = n->first_child; c; c = c->next_sibling)
        collect(c, arr, count, cap);
    return *count;
}

int css_apply(const struct stylesheet *ss, struct node *root, struct style **out) {
    struct node **els = NULL;
    int count = 0, cap = 0;
    collect(root, &els, &count, &cap);
    struct style *styles = calloc((size_t)count, sizeof *styles);
    if (!styles && count) { perror("calloc"); exit(1); }

    for (int i = 0; i < count; i++) {
        struct node *el = els[i];
        /* best declaration per property: (specificity, order) */
        struct { const char *property; const char *value; int spec, order; } best[64];
        int nbest = 0;
        for (struct rule *r = ss->rules; r; r = r->next) {
            if (!selector_matches(&r->sel, el)) continue;
            int spec = specificity(&r->sel);
            for (struct declaration *d = r->decls; d; d = d->next) {
                int j;
                for (j = 0; j < nbest; j++)
                    if (strcmp(best[j].property, d->property) == 0) break;
                if (j < nbest) {
                    if (spec > best[j].spec || (spec == best[j].spec && r->order > best[j].order)) {
                        best[j].value = d->value;
                        best[j].spec = spec;
                        best[j].order = r->order;
                    }
                } else if (nbest < 64) {
                    best[nbest].property = d->property;
                    best[nbest].value = d->value;
                    best[nbest].spec = spec;
                    best[nbest].order = r->order;
                    nbest++;
                }
            }
        }
        for (int j = 0; j < nbest; j++) style_set(&styles[i], best[j].property, best[j].value);
        /* inline style wins over everything */
        const char *inline_style = node_attr(el, "style");
        if (inline_style) {
            struct declaration *d = parse_style_attr(inline_style);
            for (struct declaration *k = d; k; k = k->next) style_set(&styles[i], k->property, k->value);
            decls_free(d);
        }
    }
    free(els);
    *out = styles;
    return count;
}

const char *style_get(const struct style *s, const char *property) {
    for (struct declaration *d = s->decls; d; d = d->next)
        if (strcmp(d->property, property) == 0) return d->value;
    return NULL;
}

void styles_free(struct style *styles, int count) {
    if (!styles) return;
    for (int i = 0; i < count; i++) decls_free(styles[i].decls);
    free(styles);
}
