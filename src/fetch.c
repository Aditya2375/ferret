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
#else
    if (tls) {
        snprintf(err, err_cap,
                 "this ferret was built without TLS support; rebuild with make TLS=1");
        goto fail;
    }
#endif
    return 0;
fail:
    close(c->fd);
#ifdef USE_TLS
    if (c->ssl) SSL_free(c->ssl);
    if (c->ctx) SSL_CTX_free(c->ctx);
#endif
    return -1;
}

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

struct document *fetch_url(const char *url, char *err, int err_cap) {
    if (strncmp(url, "file://", 7) == 0)
        return fetch_file(url + 7, url, err, err_cap);
    if (strstr(url, "://") == NULL)  /* bare path: treat as local file */
        return fetch_file(url, url, err, err_cap);

    int tls;
    const char *p;
    if (strncmp(url, "http://", 7) == 0)  { tls = 0; p = url + 7; }
    else if (strncmp(url, "https://", 8) == 0) { tls = 1; p = url + 8; }
    else {
        snprintf(err, err_cap, "unsupported scheme (ferret speaks file://, http://, https://): %s", url);
        return NULL;
    }

    char host[256] = {0}, port[8], path[1024] = "/";
    snprintf(port, sizeof port, "%s", tls ? "443" : "80");
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
    struct conn c;
    if (conn_open(&c, host, port, tls, err, err_cap) != 0) return NULL;
    char req[2048];
    int reqlen = snprintf(req, sizeof req,
        "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: ferret/1.0\r\n"
        "Accept: text/html\r\nConnection: close\r\n\r\n", path, host);
    if (conn_write_all(&c, req, (size_t)reqlen) != 0) {
        conn_close(&c);
        snprintf(err, err_cap, "write to %s failed", host);
        return NULL;
    }
    size_t cap = 1 << 20, len = 0;
    char *buf = malloc(cap + 1);
    if (!buf) { perror("malloc"); exit(1); }
    ssize_t n;
    while ((n = conn_read(&c, buf + len, cap - len)) > 0) {
        len += (size_t)n;
        if (len == cap) {
            cap *= 2;
            buf = realloc(buf, cap + 1);
            if (!buf) { perror("realloc"); exit(1); }
        }
    }
    conn_close(&c);
    buf[len] = '\0';

    /* split headers */
    char *hdr_end = strstr(buf, "\r\n\r\n");
    if (!hdr_end) { free(buf); snprintf(err, err_cap, "malformed HTTP response from %s", host); return NULL; }
    int status = 0;
    sscanf(buf, "HTTP/%*d.%*d %d", &status);
    if ((status == 301 || status == 302 || status == 307 || status == 308) && redirects < 5) {
        char *loc = strcasestr(buf, "\r\nLocation:");
        if (loc) {
            loc += 11;
            while (*loc == ' ') loc++;
            char *eol = strstr(loc, "\r\n");
            if (eol) *eol = '\0';
            if (loc[0] == '/') { snprintf(path, sizeof path, "%s", loc); }
            else if (strstr(loc, "://")) {
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
