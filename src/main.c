#include "css.h"
#include "fetch.h"
#include "html.h"
#include "render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *f) {
    fprintf(f,
        "ferret - a terminal web browser\n"
        "\n"
        "usage: ferret <url|path> [-w cols] [--no-color]\n"
        "\n"
        "  <url|path>  http:// URL, file:// URL, or a local HTML file\n"
        "  -w cols     render width in columns (default 80)\n"
        "  --no-color  plain output without ANSI styling\n"
        "\n"
        "The document is fetched, parsed into a DOM, styled with its\n"
        "inline <style> CSS, and rendered as text with links numbered\n"
        "and listed at the end.\n");
}

/* Pull <style> blocks and <link>-free inline CSS out of the tree.
 * Iterative pre-order: deeply nested documents must not blow the stack. */
static void collect_css(struct node *root, char **buf, size_t *len, size_t *cap) {
    struct node *n = root;
    while (n) {
        if (node_is(n, "style")) {
            for (struct node *c = n->first_child; c; c = c->next_sibling) {
                if (c->type == NODE_TEXT && c->text) {
                    size_t add = strlen(c->text);
                    if (*len + add + 2 > *cap) {
                        *cap = (*cap + add + 2) * 2;
                        *buf = realloc(*buf, *cap);
                        if (!*buf) { perror("realloc"); exit(1); }
                    }
                    memcpy(*buf + *len, c->text, add);
                    *len += add;
                    (*buf)[(*len)++] = '\n';
                    (*buf)[*len] = '\0';
                }
            }
        }
        if (n->first_child) { n = n->first_child; continue; }
        while (n != root && !n->next_sibling) n = n->parent;
        n = (n == root) ? NULL : n->next_sibling;
    }
}

int main(int argc, char **argv) {
    const char *url = NULL;
    struct render_opts opts = {80, 1};
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) opts.width = atoi(argv[++i]);
        else if (strcmp(argv[i], "--no-color") == 0) opts.color = 0;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) { usage(stdout); return 0; }
        else if (!url) url = argv[i];
        else { usage(stderr); return 2; }
    }
    if (!url) { usage(stderr); return 2; }

    char err[512] = {0};
    struct document *doc = fetch_url(url, err, sizeof err);
    if (!doc) { fprintf(stderr, "ferret: %s\n", err); return 1; }

    struct node *root = html_parse(doc->body);
    char *css = NULL;
    size_t css_len = 0, css_cap = 0;
    collect_css(root, &css, &css_len, &css_cap);
    struct stylesheet *ss = css_parse(css ? css : "");
    free(css);

    render_document(root, ss, &opts, doc->final_url);

    stylesheet_free(ss);
    node_free(root);
    document_free(doc);
    return 0;
}
