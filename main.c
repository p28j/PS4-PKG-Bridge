#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/stat.h>

#define SERVER_PORT 8080
#define BACKLOG 4
#define BUF_SIZE (128 * 1024)
#define BENCHMARK_LIMIT_MBPS 30.0
#define OUT_PATH "/data/PS4PKGBridge/upload.bin"

static volatile uint64_t g_bytes = 0;
static volatile int g_uploading = 0;
static volatile double g_speed_mbps = 0.0;
static volatile double g_avg_mbps = 0.0;
static volatile uint64_t g_started_us = 0;
static volatile uint64_t g_finished_us = 0;
static volatile int g_last_ok = 0;

static uint64_t now_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

static void send_all(int fd, const char *p, size_t n) {
    while (n) {
        ssize_t w = send(fd, p, n, 0);
        if (w <= 0) return;
        p += w;
        n -= (size_t)w;
    }
}

static void send_text(int fd, int code, const char *type, const char *body) {
    char hdr[512];
    int len = (int)strlen(body);
    snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d OK\r\nContent-Type: %s\r\nContent-Length: %d\r\nConnection: close\r\nAccess-Control-Allow-Origin: *\r\n\r\n",
        code, type, len);
    send_all(fd, hdr, strlen(hdr));
    send_all(fd, body, (size_t)len);
}

static int header_value(const char *request, const char *name, char *out, size_t out_sz) {
    const char *p = strcasestr(request, name);
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    ++p;
    while (*p == ' ' || *p == '\t') ++p;
    const char *e = strstr(p, "\r\n");
    if (!e) return 0;
    size_t n = (size_t)(e - p);
    if (n >= out_sz) n = out_sz - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    return 1;
}

static long long content_length(const char *request) {
    char v[64];
    if (!header_value(request, "Content-Length", v, sizeof(v))) return -1;
    return atoll(v);
}

static const char *find_body(const char *request) {
    const char *p = strstr(request, "\r\n\r\n");
    return p ? p + 4 : NULL;
}

static void json_state(int fd) {
    char body[1024];
    uint64_t elapsed = 0;
    if (g_started_us) {
        uint64_t end = g_uploading ? now_us() : g_finished_us;
        elapsed = end > g_started_us ? end - g_started_us : 1;
    }
    double avg = elapsed ? ((double)g_bytes / (double)elapsed) : 0.0;
    avg *= 1000000.0 / (1024.0 * 1024.0);
    snprintf(body, sizeof(body),
        "{\"port\":%d,\"uploading\":%s,\"bytes\":%llu,\"speed_mbps\":%.2f,\"avg_mbps\":%.2f,\"pass_30mbps\":%s}",
        SERVER_PORT,
        g_uploading ? "true" : "false",
        (unsigned long long)g_bytes,
        g_speed_mbps,
        avg,
        g_last_ok ? "true" : "false");
    send_text(fd, 200, "application/json", body);
}

static void handle_upload(int fd, const char *request, size_t req_len) {
    long long total = content_length(request);
    if (total < 0) {
        send_text(fd, 411, "text/plain", "Content-Length required\n");
        return;
    }

    mkdir("/data/PS4PKGBridge", 0777);
    int out = open(OUT_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out < 0) {
        send_text(fd, 500, "text/plain", "Cannot open output path\n");
        return;
    }

    char *buf = (char *)malloc(BUF_SIZE);
    if (!buf) {
        close(out);
        send_text(fd, 500, "text/plain", "No buffer memory\n");
        return;
    }

    g_bytes = 0;
    g_speed_mbps = 0.0;
    g_avg_mbps = 0.0;
    g_started_us = now_us();
    g_finished_us = 0;
    g_uploading = 1;
    g_last_ok = 0;

    const char *body = find_body(request);
    size_t body_have = body ? req_len - (size_t)(body - request) : 0;
    if (body_have > (size_t)total) body_have = (size_t)total;

    if (body_have) {
        ssize_t w = write(out, body, body_have);
        if (w > 0) g_bytes += (uint64_t)w;
    }

    while (g_bytes < (uint64_t)total) {
        uint64_t remain = (uint64_t)total - g_bytes;
        size_t want = remain > BUF_SIZE ? BUF_SIZE : (size_t)remain;
        ssize_t r = recv(fd, buf, want, 0);
        if (r <= 0) break;
        ssize_t written = write(out, buf, (size_t)r);
        if (written != r) break;
        g_bytes += (uint64_t)written;

        uint64_t elapsed = now_us() - g_started_us;
        if (elapsed >= 250000) {
            g_speed_mbps = ((double)g_bytes * 1000000.0 / (double)elapsed) / (1024.0 * 1024.0);
        }
    }

    free(buf);
    close(out);
    g_finished_us = now_us();
    g_uploading = 0;
    uint64_t elapsed = g_finished_us > g_started_us ? g_finished_us - g_started_us : 1;
    g_avg_mbps = ((double)g_bytes * 1000000.0 / (double)elapsed) / (1024.0 * 1024.0);
    g_speed_mbps = g_avg_mbps;
    g_last_ok = (g_bytes == (uint64_t)total && g_avg_mbps >= BENCHMARK_LIMIT_MBPS);

    char body_json[512];
    snprintf(body_json, sizeof(body_json),
        "{\"status\":\"%s\",\"bytes\":%llu,\"total\":%lld,\"avg_mbps\":%.2f,\"threshold_mbps\":%.2f,\"path\":\"%s\"}",
        g_bytes == (uint64_t)total ? "complete" : "incomplete",
        (unsigned long long)g_bytes, total, g_avg_mbps, BENCHMARK_LIMIT_MBPS, OUT_PATH);
    send_text(fd, 200, "application/json", body_json);
}

static void handle_client(int fd) {
    char *req = (char *)malloc(256 * 1024);
    if (!req) { close(fd); return; }
    size_t used = 0;
    while (used + 1 < 256 * 1024) {
        ssize_t r = recv(fd, req + used, 256 * 1024 - used - 1, 0);
        if (r <= 0) { free(req); close(fd); return; }
        used += (size_t)r;
        req[used] = '\0';
        if (strstr(req, "\r\n\r\n")) break;
    }

    char method[16] = {0};
    char path[256] = {0};
    sscanf(req, "%15s %255s", method, path);

    if (!strcmp(method, "OPTIONS")) {
        send_text(fd, 200, "text/plain", "OK");
    } else if (!strcmp(method, "GET") && !strcmp(path, "/api/state")) {
        json_state(fd);
    } else if (!strcmp(method, "POST") && !strcmp(path, "/api/upload")) {
        handle_upload(fd, req, used);
    } else if (!strcmp(method, "GET") && !strcmp(path, "/")) {
        const char *html = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 36\r\nConnection: close\r\n\r\nPS4 PKG Bridge Phase 1 Benchmark";
        send_all(fd, html, strlen(html));
    } else {
        send_text(fd, 404, "text/plain", "Not found\n");
    }

    free(req);
    close(fd);
}

int main(void) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return 1;
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(SERVER_PORT);

    if (bind(s, (struct sockaddr *)&a, sizeof(a)) < 0) { close(s); return 2; }
    if (listen(s, BACKLOG) < 0) { close(s); return 3; }

    for (;;) {
        struct sockaddr_in c;
        socklen_t clen = sizeof(c);
        int fd = accept(s, (struct sockaddr *)&c, &clen);
        if (fd >= 0) handle_client(fd);
    }
    return 0;
}
