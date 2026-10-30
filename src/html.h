#ifndef FERRET_HTML_H
#define FERRET_HTML_H

/* A parsed HTML document: a tree of elements and text runs. */

enum node_type { NODE_ELEMENT, NODE_TEXT };

struct attr {
    char *name;
    char *value;
    struct attr *next;
};

struct node {
    enum node_type type;
    char *tag;              /* element tag, lower-cased; NULL for text */
    char *text;             /* text content; NULL for elements */
    struct attr *attrs;
    struct node *parent;
    struct node *first_child;
    struct node *last_child;
    struct node *next_sibling;
};

struct node *html_parse(const char *source);
void node_free(struct node *n);

/* Collect elements in document order into a growable array. Iterative:
   pathologically deep documents must not blow the call stack. */
int node_collect_elements(struct node *root, struct node ***arr,
                          int *count, int *cap);

/* Element helpers */
const char *node_attr(const struct node *n, const char *name);
struct node *node_find(const struct node *n, const char *tag); /* first descendant */
int node_is(const struct node *n, const char *tag);

#endif
