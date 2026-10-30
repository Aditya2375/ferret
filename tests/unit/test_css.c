#include "../harness.h"
#include "css.h"

#include <string.h>

/* Collect elements in the same pre-order css_apply uses. */
static int collect(struct node *n, struct node **arr, int count) {
    if (n->type == NODE_ELEMENT && n->tag && n->tag[0] != '#')
        arr[count++] = n;
    for (struct node *c = n->first_child; c; c = c->next_sibling)
        count = collect(c, arr, count);
    return count;
}

static const struct style *find_style(struct node *root, struct style *styles,
                                      const char *tag, const char *id, const char *cls) {
    struct node *order[512];
    int count = collect(root, order, 0);
    for (int k = 0; k < count; k++) {
        struct node *e = order[k];
        if (tag && (!e->tag || strcmp(e->tag, tag) != 0)) continue;
        if (id) { const char *v = node_attr(e, "id"); if (!v || strcmp(v, id)) continue; }
        if (cls) { const char *v = node_attr(e, "class"); if (!v || !strstr(v, cls)) continue; }
        return &styles[k];
    }
    return NULL;
}

int main(void) {
    struct node *r = html_parse(
        "<html><head><style></style></head><body>"
        "<p class=\"big\">one</p><p id=\"two\">two</p><div><span>three</span></div>"
        "</body></html>");
    struct stylesheet *ss = css_parse(
        "p { color: red; margin: 0 }"
        ".big { color: blue }"
        "#two { color: green }"
        "div span { color: cyan }"
        "p.big { color: yellow }"
    );
    CHECK(ss->count == 5, "five rules parsed");

    struct style *styles = NULL;
    int n = css_apply(ss, r, &styles);

    const struct style *p1 = find_style(r, styles, "p", NULL, "big");
    CHECK_STR(style_get(p1, "color"), "yellow", "tag+class beats class and tag");
    CHECK_STR(style_get(p1, "margin"), "0", "tag rule still applies other props");
    const struct style *p2 = find_style(r, styles, "p", "two", NULL);
    CHECK_STR(style_get(p2, "color"), "green", "id beats tag");
    const struct style *sp = find_style(r, styles, "span", NULL, NULL);
    CHECK_STR(style_get(sp, "color"), "cyan", "descendant selector");

    styles_free(styles, n);
    stylesheet_free(ss);
    node_free(r);

    /* inline style wins */
    r = html_parse("<p style=\"color: pink\">x</p>");
    ss = css_parse("p { color: red }");
    styles = NULL;
    css_apply(ss, r, &styles);
    const struct style *p3 = find_style(r, styles, "p", NULL, NULL);
    CHECK_STR(style_get(p3, "color"), "pink", "inline style wins");
    styles_free(styles, 1);
    stylesheet_free(ss);
    node_free(r);

    /* at-rules skipped without breaking the next rule */
    ss = css_parse("@media screen { p { color: red } } h1 { color: blue }");
    r = html_parse("<h1>t</h1>");
    styles = NULL;
    css_apply(ss, r, &styles);
    const struct style *h1 = find_style(r, styles, "h1", NULL, NULL);
    CHECK_STR(style_get(h1, "color"), "blue", "rule after @media parses");
    styles_free(styles, 1);
    stylesheet_free(ss);
    node_free(r);

    TEST_RESULTS();
}
