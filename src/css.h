#ifndef FERRET_CSS_H
#define FERRET_CSS_H

#include "html.h"

/* A tiny CSS engine: element, class, id and descendant selectors,
   !important-free cascade by specificity then source order. */

struct declaration {
    char *property;
    char *value;
    struct declaration *next;
};

#define SELECTOR_MAX_CLASSES 4

struct selector_part {           /* one compound selector */
    char *tag;                   /* NULL or "*" = any */
    char *id;                    /* NULL = any */
    char *cls[SELECTOR_MAX_CLASSES]; /* every class must be present */
    int ncls;
};

struct selector {
    struct selector_part parts[8];
    int nparts;                  /* parts[nparts-1] is the subject */
};

struct rule {
    struct selector sel;
    struct declaration *decls;
    int order;
    struct rule *next;
};

struct stylesheet {
    struct rule *rules;
    int count;
};

struct stylesheet *css_parse(const char *source);
void stylesheet_free(struct stylesheet *ss);

/* The computed style for one element: a flat property list. */
struct style {
    struct declaration *decls;
};

/* Compute styles for every element of the tree into a parallel array
   keyed by pre-order index. Returns count written. */
int css_apply(const struct stylesheet *ss, struct node *root,
              struct style **out /* array of struct style */);
const char *style_get(const struct style *s, const char *property);
void styles_free(struct style *styles, int count);

#endif
