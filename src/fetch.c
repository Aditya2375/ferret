#define _GNU_SOURCE
#include "fetch.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static struct document *doc_new(char *body, long size, const char *url) {
    struct document *d = calloc(1, sizeof *d);
    if (!d) { perror("calloc"); exit(1); }
    d->body = body;
    d->size = size;
    d->final_url = strdup(url);
    return d;
}

void document_free(struct document *d) {
    if (!d) return;
    free(d->body);
    free(d->final_url);
    free(d);
}

static struct document *fetch_file(const char *path, const char *url, char *err, int cap) {
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, cap, "cannot open %s", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *body = malloc((size_t)size + 1);
    if (!body) { perror("malloc"); exit(1); }
    if (fread(body, 1, (size_t)size, f) != (size_t)size) {
        fclose(f); free(body);
        snprintf(err, cap, "cannot read %s", path);
        return NULL;
    }
    fclose(f);
    body[size] = '\0';
    return doc_new(body, size, url);
}

static int connect_tcp(const char *host, const char *port) {
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0) return -1;
    int fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

struct document *fetch_url(const char *url, char *err, int err_cap) {
    if (strncmp(url, "file://", 7) == 0)
        return fetch_file(url + 7, url, err, err_cap);
    if (strstr(url, "://") == NULL)  /* bare path: treat as local file */
        return fetch_file(url, url, err, err_cap);
    if (strncmp(url, "http://", 7) != 0) {
        snprintf(err, err_cap, "unsupported scheme (ferret speaks file:// and http://): %s", url);
        return NULL;
    }

    /* http://host[:port]/path */
    const char *p = url + 7;
    char host[256] = {0}, port[8] = "80", path[1024] = "/";
    const char *slash = strchr(p, '/');
    const char *hend = slash ? slash : p + strlen(p);
    const char *colon = memchr(p, ':', (size_t)(hend - p));
    if (colon) {
        snprintf(host, sizeof host, "%.*s", (int)(colon - p), p);
        snprintf(port, sizeof port, "%.*s", (int)(hend - colon - 1), colon + 1);
    } else {
        snprintf(host, sizeof host, "%.*s", (int)(hend - p), p);
    }
    if (slash) snprintf(path, sizeof path, "%s", slash);
    if (!host[0]) { snprintf(err, err_cap, "no host in %s", url); return NULL; }

    int redirects = 0;
redirect:;
    int fd = connect_tcp(host, port);
    if (fd < 0) { snprintf(err, err_cap, "cannot connect to %s:%s", host, port); return NULL; }
    char req[2048];
    int reqlen = snprintf(req, sizeof req,
        "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: ferret/1.0\r\n"
        "Accept: text/html\r\nConnection: close\r\n\r\n", path, host);
    if (write(fd, req, (size_t)reqlen) != reqlen) {
        close(fd); snprintf(err, err_cap, "write to %s failed", host); return NULL;
    }
    size_t cap = 1 << 20, len = 0;
    char *buf = malloc(cap + 1);
    if (!buf) { perror("malloc"); exit(1); }
    ssize_t n;
    while ((n = read(fd, buf + len, cap - len)) > 0) {
        len += (size_t)n;
        if (len == cap) {
            cap *= 2;
            buf = realloc(buf, cap + 1);
            if (!buf) { perror("realloc"); exit(1); }
        }
    }
    close(fd);
    buf[len] = '\0';

    /* split headers */
    char *hdr_end = strstr(buf, "\r\n\r\n");
    if (!hdr_end) { free(buf); snprintf(err, err_cap, "malformed HTTP response from %s", host); return NULL; }
    int status = 0;
    sscanf(buf, "HTTP/%*d.%*d %d", &status);
    if ((status == 301 || status == 302) && redirects < 5) {
        char *loc = strcasestr(buf, "\r\nLocation:");
        if (loc) {
            loc += 11;
            while (*loc == ' ') loc++;
            char *eol = strstr(loc, "\r\n");
            if (eol) *eol = '\0';
            /* only same-host relative/absolute redirects */
            if (loc[0] == '/') { snprintf(path, sizeof path, "%s", loc); }
            else if (strncmp(loc, "http://", 7) == 0) {
                free(buf);
                return fetch_url(loc, err, err_cap);
            }
            redirects++;
            free(buf);
            goto redirect;
        }
    }
    if (status != 200) {
        free(buf);
        snprintf(err, err_cap, "HTTP %d from %s%s", status, host, path);
        return NULL;
    }
    long blen = (long)(len - (size_t)(hdr_end + 4 - buf));
    memmove(buf, hdr_end + 4, (size_t)blen);
    buf[blen] = '\0';
    return doc_new(buf, blen, url);
}
