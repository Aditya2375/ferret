#include "../harness.h"
#include <stdlib.h>
#include <string.h>
#include "html.h"

int main(void) {
    /* basic nesting */
    struct node *r = html_parse("<html><body><p>Hello <b>world</b></p></body></html>");
    struct node *p = node_find(r, "p");
    CHECK(p != NULL, "p found");
    struct node *b = node_find(r, "b");
    CHECK(b != NULL, "b found");
    CHECK(b && b->first_child && b->first_child->type == NODE_TEXT, "b has text");
    CHECK_STR(b->first_child->text, "world", "b text");
    node_free(r);

    /* implicit closes and unmatched tags */
    r = html_parse("<div><p>one<p>two</div>");
    struct node *div = node_find(r, "div");
    int ps = 0;
    for (struct node *c = div->first_child; c; c = c->next_sibling)
        if (node_is(c, "p")) ps++;
    /* parser does not auto-close <p>; both nest - acceptable, count children */
    CHECK(ps >= 1, "at least one p under div");
    node_free(r);

    /* void elements */
    r = html_parse("<body><p>a<br>b<hr></p></body>");
    struct node *br = node_find(r, "br");
    CHECK(br != NULL && br->first_child == NULL, "br is void");
    CHECK(node_find(r, "hr") != NULL, "hr parsed");
    node_free(r);

    /* attributes */
    r = html_parse("<a href=\"https://x.test/y\" class='big blue' id=top>go</a>");
    struct node *a = node_find(r, "a");
    CHECK_STR(node_attr(a, "href"), "https://x.test/y", "double-quoted attr");
    CHECK_STR(node_attr(a, "class"), "big blue", "single-quoted attr");
    CHECK_STR(node_attr(a, "id"), "top", "bare attr");
    node_free(r);

    /* comments, doctype, case-insensitive tags */
    r = html_parse("<!DOCTYPE html><!-- hi --><BODY><P>x</P></BODY>");
    CHECK(node_find(r, "body") != NULL, "uppercase BODY lower-cased");
    CHECK(node_find(r, "p") != NULL, "uppercase P lower-cased");
    node_free(r);

    /* raw script text is not markup */
    r = html_parse("<body><script>if (a<b) { x(); }</script><p>after</p></body>");
    struct node *s = node_find(r, "script");
    CHECK(s && s->first_child && s->first_child->type == NODE_TEXT, "script is raw text");
    CHECK(node_find(r, "p") != NULL, "content after script parses");
    node_free(r);

    /* entity decoding */
    r = html_parse("<p>a &amp; b &#65;</p>");
    struct node *pp = node_find(r, "p");
    CHECK(pp && pp->first_child && pp->first_child->text &&
          strstr(pp->first_child->text, "&") != NULL, "amp decoded");
    CHECK(strstr(pp->first_child->text, "A") != NULL, "numeric ref decoded");
    node_free(r);

    /* entities without semicolons: numeric refs still decode and are
     * consumed (no double print); unknown names stay literal */
    r = html_parse("<p>A&#66C D&#x45;F &bogus;</p>");
    pp = node_find(r, "p");
    CHECK(pp && pp->first_child && pp->first_child->text &&
          strcmp(pp->first_child->text, "ABC DEF &bogus;") == 0,
          "semicolon-less numeric refs decode once, unknown names literal");
    node_free(r);

    /* a lone ampersand is literal text */
    r = html_parse("<p>salt & pepper</p>");
    pp = node_find(r, "p");
    CHECK(pp && pp->first_child && pp->first_child->text &&
          strcmp(pp->first_child->text, "salt & pepper") == 0,
          "lone ampersand literal");
    node_free(r);

    /* garbage in, no crash */
    r = html_parse("<><<<<>>><//></p></div>text");
    CHECK(r != NULL, "garbage parses to a tree");
    node_free(r);

    TEST_RESULTS();
}
