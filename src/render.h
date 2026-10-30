#ifndef FERRET_RENDER_H
#define FERRET_RENDER_H

#include "css.h"
#include "html.h"

/* Render a parsed + styled document as ANSI text on a terminal grid. */

struct render_opts {
    int width;      /* terminal columns (default 80) */
    int color;      /* emit ANSI color (default 1) */
};

/* Render the document; writes lines to stdout. Returns number of lines. */
int render_document(struct node *root, const struct stylesheet *ss,
                    const struct render_opts *opts, const char *base_url);

#endif
