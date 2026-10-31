#define _GNU_SOURCE
#include "fetch.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef USE_TLS
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

/* A connection is a plain socket, optionally wrapped in TLS. */
struct conn {
    int fd;
#ifdef USE_TLS
    SSL *ssl;
    SSL_CTX *ctx;
#endif
};

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

#define CONNECT_TIMEOUT_SECS 10
#define IO_TIMEOUT_SECS      15

static int set_io_timeouts(int fd) {
    struct timeval tv = { .tv_sec = IO_TIMEOUT_SECS, .tv_usec = 0 };
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) < 0) return -1;
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv) < 0) return -1;
    return 0;
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
        /* non-blocking connect with a deadline: an unresponsive host must
         * not hang the browser forever */
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int ok = connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (ok < 0 && errno == EINPROGRESS) {
            fd_set wf;
            FD_ZERO(&wf);
            FD_SET(fd, &wf);
            struct timeval tv = { .tv_sec = CONNECT_TIMEOUT_SECS, .tv_usec = 0 };
            if (select(fd + 1, NULL, &wf, NULL, &tv) > 0) {
                int soerr = 0;
                socklen_t sl = sizeof soerr;
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
                ok = soerr ? -1 : 0;
            } else {
                ok = -1;    /* timed out or select failed */
            }
        }
        if (flags >= 0) fcntl(fd, F_SETFL, flags);
        if (ok == 0 && set_io_timeouts(fd) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static int conn_open(struct conn *c, const char *host, const char *port, int tls,
                     char *err, int err_cap) {
    memset(c, 0, sizeof *c);
#ifndef USE_TLS
    if (tls) {
        snprintf(err, err_cap,
                 "this ferret was built without TLS support; rebuild with make TLS=1");
        return -1;
    }
#endif
    c->fd = connect_tcp(host, port);
    if (c->fd < 0) {
        snprintf(err, err_cap, "cannot connect to %s:%s", host, port);
        return -1;
    }
#ifdef USE_TLS
    if (tls) {
        c->ctx = SSL_CTX_new(TLS_client_method());
        if (!c->ctx) { snprintf(err, err_cap, "TLS setup failed"); goto fail; }
        SSL_CTX_set_default_verify_paths(c->ctx);
        c->ssl = SSL_new(c->ctx);
        if (!c->ssl) { snprintf(err, err_cap, "TLS setup failed"); goto fail; }
        SSL_set_tlsext_host_name(c->ssl, host);          /* SNI */
        SSL_set1_host(c->ssl, host);                     /* verify the cert matches */
        SSL_set_verify(c->ssl, SSL_VERIFY_PEER, NULL);
        SSL_set_fd(c->ssl, c->fd);
        if (SSL_connect(c->ssl) != 1) {
            unsigned long e = ERR_get_error();
            char buf[128] = "certificate problem";
            if (e) ERR_error_string_n(e, buf, sizeof buf);
            snprintf(err, err_cap, "TLS handshake with %s failed: %s", host, buf);
            goto fail;
        }
    }
#endif
    return 0;
#ifdef USE_TLS
fail:
    close(c->fd);
    if (c->ssl) SSL_free(c->ssl);
    if (c->ctx) SSL_CTX_free(c->ctx);
    return -1;
#endif
}

/* largest response body ferret will hold in memory */
#define FERRET_MAX_BODY (8 << 20)

static void conn_close(struct conn *c) {
#ifdef USE_TLS
    if (c->ssl) { SSL_shutdown(c->ssl); SSL_free(c->ssl); }
    if (c->ctx) SSL_CTX_free(c->ctx);
#endif
    close(c->fd);
}

static ssize_t conn_read(struct conn *c, void *buf, size_t n) {
#ifdef USE_TLS
    if (c->ssl) {
        int r = SSL_read(c->ssl, buf, (int)n);
        return r > 0 ? r : 0;   /* clean close or error both end the body */
    }
#endif
    return read(c->fd, buf, n);
}

static int conn_write_all(struct conn *c, const char *buf, size_t n) {
    while (n) {
        ssize_t w;
#ifdef USE_TLS
        if (c->ssl) {
            int r = SSL_write(c->ssl, buf, (int)n);
            w = r > 0 ? r : -1;
        } else
#endif
            w = write(c->fd, buf, n);
        if (w <= 0) return -1;
        buf += w;
        n -= (size_t)w;
    }
    return 0;
}

struct url_parts {
    int  tls;
    char host[256];
    char port[8];
    char path[1024];
    char full[2048];    /* current URL, for the document's final_url */
};

static int parse_http_url(const char *url, struct url_parts *u,
                          char *err, int err_cap) {
    const char *p;
    if (strncmp(url, "http://", 7) == 0)       { u->tls = 0; p = url + 7; }
    else if (strncmp(url, "https://", 8) == 0) { u->tls = 1; p = url + 8; }
    else {
        snprintf(err, err_cap, "unsupported scheme (ferret speaks file://, http://, https://): %s", url);
        return -1;
    }
    u->host[0] = '\0';
    snprintf(u->port, sizeof u->port, "%s", u->tls ? "443" : "80");
    const char *slash = strchr(p, '/');
    const char *hend = slash ? slash : p + strlen(p);
    const char *colon = memchr(p, ':', (size_t)(hend - p));
    if (colon) {
        snprintf(u->host, sizeof u->host, "%.*s", (int)(colon - p), p);
        snprintf(u->port, sizeof u->port, "%.*s", (int)(hend - colon - 1), colon + 1);
    } else {
        snprintf(u->host, sizeof u->host, "%.*s", (int)(hend - p), p);
    }
    snprintf(u->path, sizeof u->path, "%s", slash ? slash : "/");
    if (!u->host[0]) { snprintf(err, err_cap, "no host in %s", url); return -1; }
    snprintf(u->full, sizeof u->full, "%s", url);
    return 0;
}

static void rebuild_full(struct url_parts *u) {
    int default_port = (u->tls && strcmp(u->port, "443") == 0) ||
                       (!u->tls && strcmp(u->port, "80") == 0);
    snprintf(u->full, sizeof u->full, "%s://%s%s%s%s",
             u->tls ? "https" : "http", u->host,
             default_port ? "" : ":", default_port ? "" : u->port, u->path);
}

/* Resolve a redirect Location against the current URL (RFC 3986 5.2):
 * an absolute URI replaces everything, "/..." replaces the path, and
 * anything else merges into the current path's directory. */
static int resolve_redirect(struct url_parts *u, const char *loc,
                            char *err, int err_cap) {
    if (strstr(loc, "://"))
        return parse_http_url(loc, u, err, err_cap);
    if (loc[0] == '/' && loc[1] == '/') {
        /* scheme-relative target: keep the current scheme */
        char abs[2048];
        const char *scheme = u->tls ? "https" : "http";
        size_t slen = strlen(scheme), llen = strlen(loc);
        if (slen + 1 + llen + 1 > sizeof abs) goto toolong;
        memcpy(abs, scheme, slen);
        abs[slen] = ':';
        memcpy(abs + slen + 1, loc, llen + 1);
        return parse_http_url(abs, u, err, err_cap);
    }
    if (loc[0] == '/') {
        if (strlen(loc) >= sizeof u->path) goto toolong;
        memcpy(u->path, loc, strlen(loc) + 1);
    } else {
        char *last = strrchr(u->path, '/');   /* path always starts with / */
        size_t dirlen = (size_t)(last + 1 - u->path);
        if (dirlen + strlen(loc) >= sizeof u->path) goto toolong;
        snprintf(u->path + dirlen, sizeof u->path - dirlen, "%s", loc);
    }
    rebuild_full(u);
    return 0;
toolong:
    snprintf(err, err_cap, "redirect target too long: %.64s...", loc);
    return -1;
}

struct document *fetch_url(const char *url, char *err, int err_cap) {
    if (strncmp(url, "file://", 7) == 0)
        return fetch_file(url + 7, url, err, err_cap);
    if (strstr(url, "://") == NULL)  /* bare path: treat as local file */
        return fetch_file(url, url, err, err_cap);

    struct url_parts u;
    if (parse_http_url(url, &u, err, err_cap) != 0)
        return NULL;

    int status = 0;
    char *buf = NULL;
    size_t len = 0;
    char *hdr_end = NULL;
    int redirects = 0;
    for (;;) {
        struct conn c;
        if (conn_open(&c, u.host, u.port, u.tls, err, err_cap) != 0) return NULL;
        char req[2048];
        int reqlen = snprintf(req, sizeof req,
            "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: ferret/1.0\r\n"
            "Accept: text/html\r\nConnection: close\r\n\r\n", u.path, u.host);
        if (conn_write_all(&c, req, (size_t)reqlen) != 0) {
            conn_close(&c);
            snprintf(err, err_cap, "write to %s failed", u.host);
            return NULL;
        }
        size_t cap = 1 << 20;
        len = 0;
        buf = malloc(cap + 1);
        if (!buf) { perror("malloc"); exit(1); }
        ssize_t n;
        while ((n = conn_read(&c, buf + len, cap - len)) > 0) {
            len += (size_t)n;
            if (len == cap) {
                /* a text browser has no business holding unbounded pages:
                   stop at a hard ceiling instead of eating all memory */
                if (cap >= FERRET_MAX_BODY) {
                    conn_close(&c);
                    free(buf);
                    snprintf(err, err_cap, "response from %s exceeds the %d MiB limit",
                             u.host, FERRET_MAX_BODY >> 20);
                    return NULL;
                }
                cap *= 2;
                if (cap > FERRET_MAX_BODY) cap = FERRET_MAX_BODY;
                buf = realloc(buf, cap + 1);
                if (!buf) { perror("realloc"); exit(1); }
            }
        }
        conn_close(&c);
        buf[len] = '\0';

        hdr_end = strstr(buf, "\r\n\r\n");
        if (!hdr_end) {
            free(buf);
            snprintf(err, err_cap, "malformed HTTP response from %s", u.host);
            return NULL;
        }
        status = 0;
        sscanf(buf, "HTTP/%*d.%*d %d", &status);
        if ((status == 301 || status == 302 || status == 307 || status == 308)
                && redirects < 5) {
            char *loc = strcasestr(buf, "\r\nLocation:");
            if (loc) {
                loc += 11;
                while (*loc == ' ') loc++;
                char *eol = strstr(loc, "\r\n");
                char locbuf[2048];
                snprintf(locbuf, sizeof locbuf, "%.*s",
                         eol ? (int)(eol - loc) : (int)strlen(loc), loc);
                free(buf);
                buf = NULL;
                if (resolve_redirect(&u, locbuf, err, err_cap) != 0)
                    return NULL;
                redirects++;    /* one counter across absolute and relative hops */
                continue;
            }
        }
        break;
    }
    if (status != 200) {
        free(buf);
        snprintf(err, err_cap, "HTTP %d from %s%s", status, u.host, u.path);
        return NULL;
    }
    long blen = (long)(len - (size_t)(hdr_end + 4 - buf));
    memmove(buf, hdr_end + 4, (size_t)blen);
    buf[blen] = '\0';
    return doc_new(buf, blen, u.full);
}
