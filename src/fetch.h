#ifndef FERRET_FETCH_H
#define FERRET_FETCH_H

/* Fetch a document. Supports file:// and plain http:// (port 80). */
struct document {
    char *body;         /* NUL-terminated content */
    long size;
    char *final_url;    /* after redirects */
};

/* Returns NULL on failure with a message in err (err_cap bytes). */
struct document *fetch_url(const char *url, char *err, int err_cap);
void document_free(struct document *doc);

#endif
