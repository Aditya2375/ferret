#include "render.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- inline elements that stay in the text flow ---- */
static int is_inline_tag(const char *tag) {
    static const char *const t[] = {
        "a", "b", "strong", "i", "em", "u", "s", "span", "code", "small",
        "sub", "sup", "abbr", "mark", "q", "cite", "time", "kbd", NULL
    };
    for (int i = 0; t[i]; i++) if (strcmp(tag, t[i]) == 0) return 1;
    return 0;
}

/* ---- output grid ---- */
struct grid {
    char **cells;       /* [row][col] chars */
    char **attrs;       /* [row][col] attr codes: 'b' bold, 'u' underline, digit = color */
    int rows, cols;
};

static void grid_init(struct grid *g, int cols) {
    g->rows = 0;
    g->cols = cols;
    g->cells = NULL;
    g->attrs = NULL;
}

static void grid_ensure(struct grid *g, int row) {
    while (row >= g->rows) {
        g->cells = realloc(g->cells, (size_t)(g->rows + 1) * sizeof *g->cells);
        g->attrs = realloc(g->attrs, (size_t)(g->rows + 1) * sizeof *g->attrs);
        g->cells[g->rows] = calloc((size_t)g->cols + 1, 1);
        g->attrs[g->rows] = calloc((size_t)g->cols + 1, 1);
        g->rows++;
    }
}

static void grid_put(struct grid *g, int row, int col, char ch, char attr) {
    if (col < 0 || col >= g->cols || row < 0) return;
    grid_ensure(g, row);
    g->cells[row][col] = ch;
    g->attrs[row][col] = attr;
}

/* ---- style interpretation ---- */
struct pen {
    char attr;          /* grid attr char */
    int indent;         /* current left indent in columns */
};

static int parse_px(const char *v, int dflt) {
    if (!v) return dflt;
    return (int)strtol(v, NULL, 10);
}

/* map common CSS colors to ANSI 30-37 */
static int ansi_color(const char *v) {
    if (!v) return -1;
    struct { const char *name; int code; } map[] = {
        {"black", 30}, {"red", 31}, {"green", 32}, {"yellow", 33},
        {"blue", 34}, {"magenta", 35}, {"cyan", 36}, {"white", 37},
        {"gray", 90}, {"grey", 90}, {"silver", 37}, {"maroon", 31},
        {"purple", 35}, {"lime", 32}, {"olive", 33}, {"navy", 34},
        {"teal", 36}, {"aqua", 36}, {"orange", 33}, {NULL, 0}
    };
    for (int i = 0; map[i].name; i++)
        if (strcasecmp(v, map[i].name) == 0) return map[i].code;
    return -1;
}

/* Compute the pen attr char for an element from its computed style. */
static char pen_for(const struct style *s, const char *tag) {
    const char *weight = style_get(s, "font-weight");
    const char *dec = style_get(s, "text-decoration");
    const char *color = style_get(s, "color");
    if (weight && (strcmp(weight, "bold") == 0 || atoi(weight) >= 600)) return 'b';
    if (dec && strstr(dec, "underline")) return 'u';
    int c = ansi_color(color);
    if (c >= 30 && c <= 37) return (char)('0' + (c - 30));
    if (c == 90) return '8';
    if (tag && (strcmp(tag, "b") == 0 || strcmp(tag, "strong") == 0)) return 'b';
    if (tag && (strcmp(tag, "u") == 0 || strcmp(tag, "a") == 0)) return 'u';
    return 0;
}

/* ---- the renderer ---- */
struct ctx {
    struct grid g;
    int x, y;           /* cursor */
    int width;
    struct node **els;  /* pre-order elements matching styles[] */
    struct style *styles;
    int nstyles;
    int link_count;
    struct { char *url; int n; } links[256];
};

static const struct style *style_of(struct ctx *c, const struct node *el) {
    /* elements were collected in pre-order; walk to find index. O(n^2) worst
       case is fine for documents of this scale. */
    for (int i = 0; i < c->nstyles; i++)
        if (c->els[i] == el) return &c->styles[i];
    return NULL;
}

static void newline(struct ctx *c) { c->x = 0; c->y++; }

static void blank_line(struct ctx *c) {
    if (c->y == 0 && c->x == 0 && c->g.rows == 0) return;  /* nothing on the page yet */
    newline(c);
    newline(c);
}

static void emit(struct ctx *c, const char *text, char attr, int indent) {
    /* word-wrap: split on spaces */
    const char *p = text;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char *e = p;
        while (*e && *e != ' ') e++;
        int wlen = (int)(e - p);
        if (c->x + wlen + (c->x > indent ? 1 : 0) > c->width && c->x > indent)
            newline(c);
        if (c->x < indent) c->x = indent;
        if (c->x > indent && c->x > 0) { grid_put(&c->g, c->y, c->x, ' ', 0); c->x++; }
        for (int i = 0; i < wlen && c->x < c->width; i++, c->x++)
            grid_put(&c->g, c->y, c->x, p[i], attr);
        p = e;
    }
}

static void walk(struct ctx *c, struct node *n, char attr, int indent);

static void walk_children(struct ctx *c, struct node *n, char attr, int indent) {
    for (struct node *k = n->first_child; k; k = k->next_sibling)
        walk(c, k, attr, indent);
}

/* Render a simple table: fixed column widths from the widest cell. */
static void walk_table(struct ctx *c, struct node *table, char attr, int indent) {
    /* collect rows from thead/tbody sections and direct tr children */
    struct node *rows[64];
    int nrows = 0;
    for (struct node *sec = table->first_child; sec && nrows < 64; sec = sec->next_sibling) {
        if (node_is(sec, "tr")) {
            rows[nrows++] = sec;
        } else if (node_is(sec, "thead") || node_is(sec, "tbody") || node_is(sec, "tfoot")) {
            for (struct node *tr = sec->first_child; tr && nrows < 64; tr = tr->next_sibling)
                if (node_is(tr, "tr")) rows[nrows++] = tr;
        }
    }
    if (!nrows) return;

    /* extract cell text */
    char cells[64][16][128];
    int ncols = 0;
    for (int i = 0; i < nrows; i++) {
        int col = 0;
        for (struct node *td = rows[i]->first_child; td && col < 16; td = td->next_sibling) {
            if (!node_is(td, "td") && !node_is(td, "th")) continue;
            cells[i][col][0] = '\0';
            for (struct node *t = td->first_child; t; t = t->next_sibling) {
                if (t->type == NODE_TEXT && t->text) {
                    size_t cur = strlen(cells[i][col]);
                    snprintf(cells[i][col] + cur, sizeof cells[i][col] - cur, "%s", t->text);
                }
            }
            col++;
        }
        if (col > ncols) ncols = col;
    }
    if (!ncols) return;

    int w[16];
    for (int j = 0; j < ncols; j++) {
        w[j] = 0;
        for (int i = 0; i < nrows; i++) {
            int l = (int)strlen(cells[i][j]);
            if (l > w[j]) w[j] = l;
        }
        if (w[j] > 24) w[j] = 24;
    }

    blank_line(c);
    for (int i = 0; i < nrows; i++) {
        int x = indent;
        for (int j = 0; j < ncols; j++) {
            for (int k = 0; cells[i][j][k] && k < w[j]; k++)
                grid_put(&c->g, c->y, x++, cells[i][j][k], attr);
            x += w[j] - (int)strlen(cells[i][j]) > 0 ? w[j] - (int)strlen(cells[i][j]) : 0;
            if (j + 1 < ncols) x += 2;
        }
        newline(c);
        if (i == 0) {  /* header underline */
            int x = indent;
            for (int j = 0; j < ncols; j++) {
                for (int k = 0; k < w[j]; k++) grid_put(&c->g, c->y, x++, '-', 0);
                if (j + 1 < ncols) x += 2;
            }
            newline(c);
        }
    }
    newline(c);
}

static void walk(struct ctx *c, struct node *n, char attr, int indent) {
    if (n->type == NODE_TEXT) {
        if (n->text) emit(c, n->text, attr, indent);
        return;
    }
    const char *tag = n->tag;
    if (!tag || tag[0] == '#') { walk_children(c, n, attr, indent); return; }
    if (strcmp(tag, "script") == 0 || strcmp(tag, "style") == 0 ||
        strcmp(tag, "head") == 0 || strcmp(tag, "title") == 0 ||
        strcmp(tag, "meta") == 0 || strcmp(tag, "link") == 0) return;

    const struct style *s = style_of(c, n);
    const char *display = s ? style_get(s, "display") : NULL;
    if (display && strcmp(display, "none") == 0) return;
    char myattr = s ? pen_for(s, tag) : pen_for(NULL, tag);
    if (!myattr) myattr = attr;

    if (strcmp(tag, "h1") == 0 || strcmp(tag, "h2") == 0 ||
        strcmp(tag, "h3") == 0 || strcmp(tag, "h4") == 0) {
        blank_line(c);
        walk_children(c, n, 'b', indent);
        blank_line(c);
    } else if (strcmp(tag, "p") == 0 || strcmp(tag, "div") == 0 ||
               strcmp(tag, "section") == 0 || strcmp(tag, "article") == 0 ||
               strcmp(tag, "header") == 0 || strcmp(tag, "footer") == 0 ||
               strcmp(tag, "main") == 0 || strcmp(tag, "blockquote") == 0) {
        if (c->x > indent) newline(c);
        int ind = strcmp(tag, "blockquote") == 0 ? indent + 4 : indent;
        walk_children(c, n, myattr, ind);
        newline(c);
    } else if (strcmp(tag, "br") == 0) {
        newline(c);
    } else if (strcmp(tag, "hr") == 0) {
        blank_line(c);
        for (int i = indent; i < c->width; i++) grid_put(&c->g, c->y, i, '-', 0);
        blank_line(c);
    } else if (strcmp(tag, "li") == 0) {
        if (c->x > indent) newline(c);
        if (c->x < indent) c->x = indent;
        emit(c, "*", 0, indent);
        c->x = indent + 2;
        walk_children(c, n, myattr, indent + 2);
        newline(c);
    } else if (strcmp(tag, "ul") == 0 || strcmp(tag, "ol") == 0) {
        if (c->x > indent) newline(c);
        walk_children(c, n, myattr, indent + 2);
    } else if (strcmp(tag, "table") == 0) {
        walk_table(c, n, myattr, indent);
    } else if (strcmp(tag, "a") == 0) {
        walk_children(c, n, myattr ? myattr : 'u', indent);
        const char *href = node_attr(n, "href");
        if (href && c->link_count < 256) {
            int num = ++c->link_count;
            c->links[num - 1].url = strdup(href);
            c->links[num - 1].n = num;
            char marker[8];
            snprintf(marker, sizeof marker, "[%d]", num);
            emit(c, marker, 'u', indent);
        }
    } else if (strcmp(tag, "img") == 0) {
        const char *alt = node_attr(n, "alt");
        char buf[160];
        snprintf(buf, sizeof buf, "[image: %s]", alt ? alt : "");
        emit(c, buf, 0, indent);
    } else if (strcmp(tag, "pre") == 0 || strcmp(tag, "code") == 0) {
        if (strcmp(tag, "pre") == 0 && c->x > indent) newline(c);
        for (struct node *k = n->first_child; k; k = k->next_sibling) {
            if (k->type == NODE_TEXT && k->text && strcmp(tag, "pre") == 0) {
                /* pre: keep raw line breaks */
                const char *p = k->text;
                while (*p) {
                    const char *nl = strchr(p, '\n');
                    const char *e = nl ? nl : p + strlen(p);
                    for (const char *q = p; q < e && c->x < c->width; q++, c->x++)
                        grid_put(&c->g, c->y, c->x, *q, 0);
                    if (nl) newline(c);
                    p = nl ? nl + 1 : e;
                }
            } else walk(c, k, 'b', indent);
        }
        if (strcmp(tag, "pre") == 0) newline(c);
    } else {
        walk_children(c, n, myattr, indent);
    }
}

int render_document(struct node *root, const struct stylesheet *ss,
                    const struct render_opts *opts, const char *base_url) {
    (void)base_url;
    struct ctx c;
    memset(&c, 0, sizeof c);
    c.width = opts && opts->width ? opts->width : 80;
    int color = !opts || opts->color;
    grid_init(&c.g, c.width);
    c.styles = NULL;
    c.nstyles = css_apply(ss, root, &c.styles);
    /* rebuild the same pre-order element list used by css_apply */
    {
        extern int ferret_collect(struct node *, struct node ***, int *, int *);
        int count = 0, cap = 0;
        ferret_collect(root, &c.els, &count, &cap);
        c.nstyles = count;
    }

    struct node *body = node_find(root, "body");
    walk(&c, body ? body : root, 0, 0);
    newline(&c);

    /* print with ANSI attrs */
    static const char *const codes[256] = {0};
    (void)codes;
    for (int r = 0; r < c.g.rows; r++) {
        char cur = -1;
        for (int col = 0; col < c.g.cols; col++) {
            char ch = c.g.cells[r][col];
            char a = c.g.attrs[r][col];
            if (!ch) ch = ' ';
            if (color) {
                if (a != cur) {
                    printf("\033[0m");
                    if (a == 'b') printf("\033[1m");
                    else if (a == 'u') printf("\033[4m");
                    else if (a >= '0' && a <= '7') printf("\033[3%cm", a);
                    else if (a == '8') printf("\033[90m");
                    cur = a;
                }
            }
            putchar(ch);
        }
        if (color) printf("\033[0m");
        putchar('\n');
    }
    if (c.link_count) {
        printf("\nLinks:\n");
        for (int i = 0; i < c.link_count; i++)
            printf("[%d] %s\n", c.links[i].n, c.links[i].url);
    }
    /* cleanup */
    for (int r = 0; r < c.g.rows; r++) { free(c.g.cells[r]); free(c.g.attrs[r]); }
    free(c.g.cells);
    free(c.g.attrs);
    for (int i = 0; i < c.link_count; i++) free(c.links[i].url);
    styles_free(c.styles, c.nstyles);
    free(c.els);
    return c.g.rows;
}

/* shared collect used by render to mirror css_apply order */
int ferret_collect(struct node *n, struct node ***arr, int *count, int *cap) {
    if (n->type == NODE_ELEMENT && n->tag && n->tag[0] != '#') {
        if (*count >= *cap) {
            *cap = *cap ? *cap * 2 : 64;
            *arr = realloc(*arr, (size_t)*cap * sizeof **arr);
            if (!*arr) { perror("realloc"); exit(1); }
        }
        (*arr)[(*count)++] = n;
    }
    for (struct node *c = n->first_child; c; c = c->next_sibling)
        ferret_collect(c, arr, count, cap);
    return *count;
}
