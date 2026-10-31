/* Browser entry point: render HTML source in the page. The network stack
   (fetch.c) is meaningless without sockets; the page supplies the HTML. */
#include "html.h"
#include "css.h"
#include "render.h"

#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Same iterative <style> collection as the CLI. */
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

/* Render html_text at the given width; the text grid goes to
   Module.print line by line with ANSI styling. */
EMSCRIPTEN_KEEPALIVE
void ferret_render(const char *html_text, int width) {
    struct node *root = html_parse(html_text);
    char *css = NULL;
    size_t css_len = 0, css_cap = 0;
    collect_css(root, &css, &css_len, &css_cap);
    struct stylesheet *ss = css_parse(css ? css : "");
    free(css);
    struct render_opts opts = {width > 0 ? width : 80, 1};
    render_document(root, ss, &opts, "about:blank");
    stylesheet_free(ss);
    node_free(root);
}
