/**
 * @file elogd_reader.c
 * @brief elogd Reader — 通过 SOCK_SEQPACKET 向 elogcat 推送日志
 *
 * 线程模型:
 *   elogd_reader_thread (主线程): accept loop, 每个 client 创建独立 pthread
 *   client_push_thread (per-client): condvar wait → flush_range → sendto
 */

#define _GNU_SOURCE

#include "elogd.h"
#include "elog_buf.h"
#include "elog_def.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>

#define ELOG_READER_MAX_CLIENTS  8

extern elog_ring_buf_t* g_daemon_rb;
extern volatile bool g_daemon_running;

/* per-client 状态 */
typedef struct {
    int            client_fd;
    size_t         read_pos;
    uint8_t        min_level;
    uint16_t       pid_filter;
    uint32_t       remaining;  /* 剩余可读条数, 0 = 无限 */
    volatile bool  active;
} reader_client_t;

static reader_client_t* s_clients[ELOG_READER_MAX_CLIENTS];
static int s_client_count = 0;
static elog_mutex_t s_clients_lock;

/* ===== 内部辅助 ===== */

static size_t entry_total_size(uint32_t entry_len) {
    return 4 + ((entry_len + 3) & ~3U);
}

static uint32_t ring_read_u32(const uint8_t* buf, size_t cap, size_t pos) {
    uint32_t val;
    if (pos + 4 <= cap) {
        memcpy(&val, buf + pos, 4);
    } else {
        uint8_t tmp[4];
        size_t first = cap - pos;
        memcpy(tmp, buf + pos, first);
        memcpy(tmp + first, buf, 4 - first);
        memcpy(&val, tmp, 4);
    }
    return val;
}

/* 从 ring buffer 读取一条 entry 到 out_buf, 返回总字节数 (0 = 结束) */
static size_t read_entry(const elog_ring_buf_t* rb, size_t pos,
                          uint8_t* out, size_t out_size) {
    if (pos == rb->write_pos) return 0;

    uint32_t entry_len = ring_read_u32(rb->buffer, rb->buf_capacity, pos);
    if (entry_len == 0) return 0;

    size_t total = entry_total_size(entry_len);
    if (total > out_size) return 0;

    /* 读取 prefix (4B) + entry_len bytes */
    size_t prefix_pos = pos;
    size_t data_pos = (pos + 4) % rb->buf_capacity;

    if (data_pos + entry_len <= rb->buf_capacity) {
        /* 不跨边界 */
        memcpy(out, rb->buffer + prefix_pos, 4);
        memcpy(out + 4, rb->buffer + data_pos, entry_len);
    } else {
        /* 跨边界 */
        size_t first = rb->buf_capacity - data_pos;
        memcpy(out, rb->buffer + prefix_pos, 4);
        memcpy(out + 4, rb->buffer + data_pos, first);
        memcpy(out + 4 + first, rb->buffer, entry_len - first);
    }

    return total;
}

/* 发送一条日志给客户端 (只发送 header + tag + msg, 不含 4B prefix) */
static int send_log_entry(reader_client_t* client,
                           const uint8_t* entry_data, uint32_t entry_len) {
    /* 应用过滤 */
    if (entry_len < sizeof(elog_msg_header_t)) return -1;

    const elog_msg_header_t* hdr = (const elog_msg_header_t*)entry_data;
    if (client->min_level > 0 && hdr->level < client->min_level) return -1;
    if (client->pid_filter > 0 && hdr->pid != client->pid_filter) return -1;

    /* SEQPACKET: 每个 datagram 是完整的一条日志 */
    ssize_t sent = send(client->client_fd, entry_data, entry_len, MSG_NOSIGNAL);
    if (sent < 0) return -1;
    return 0;
}

/* ===== per-client push 线程 ===== */

static void* client_push_thread(void* arg) {
    reader_client_t* client = (reader_client_t*)arg;

    while (client->active && g_daemon_running) {
        if (!g_daemon_rb) {
            usleep(100000);
            continue;
        }

        elog_mutex_lock(&g_daemon_rb->lock);

        /* 等待新数据 */
        while (client->read_pos == g_daemon_rb->write_pos &&
               client->active && g_daemon_running) {
            elog_cond_timedwait(&g_daemon_rb->not_empty, &g_daemon_rb->lock, 1000);
        }

        /* 逐条发送 */
        int sent_count = 0;
        while (client->read_pos != g_daemon_rb->write_pos &&
               client->active && g_daemon_running) {
            uint8_t entry_buf[sizeof(elog_msg_header_t) + ELOG_MAX_TAG_LEN + ELOG_MAX_MSG_LEN + 4];
            size_t total = read_entry(g_daemon_rb, client->read_pos,
                                       entry_buf, sizeof(entry_buf));
            if (total == 0) break;

            uint32_t entry_len = ring_read_u32(g_daemon_rb->buffer,
                                                g_daemon_rb->buf_capacity,
                                                client->read_pos);

            /* entry_buf 布局: [4B prefix][header+tag+msg], 只发送后面的部分 */
            int ret = send_log_entry(client, entry_buf + 4, entry_len);

            /* 推进 read_pos */
            client->read_pos = (client->read_pos + entry_total_size(entry_len))
                               % g_daemon_rb->buf_capacity;

            if (ret == 0) sent_count++;
            if (ret < 0 && errno == EPIPE) {
                client->active = false;
                break;
            }

            /* count 限制 */
            if (client->remaining > 0) {
                if (--client->remaining == 0) {
                    client->active = false;
                    break;
                }
            }

            /* 防止一次性发送太多, 给其他线程机会 */
            if (sent_count >= 128) break;
        }

        elog_mutex_unlock(&g_daemon_rb->lock);
    }

    /* 清理 */
    close(client->client_fd);

    elog_mutex_lock(&s_clients_lock);
    for (int i = 0; i < s_client_count; i++) {
        if (s_clients[i] == client) {
            s_clients[i] = s_clients[s_client_count - 1];
            s_client_count--;
            break;
        }
    }
    elog_mutex_unlock(&s_clients_lock);

    free(client);
    return NULL;
}

/* ===== accept 主线程 ===== */

void* elogd_reader_thread(void* arg) {
    (void)arg;
    elog_mutex_init(&s_clients_lock);

    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        perror("elogd_reader: socket");
        return NULL;
    }

    unlink(ELOG_DAEMON_READER_SOCK);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, ELOG_DAEMON_READER_SOCK, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("elogd_reader: bind");
        close(fd);
        return NULL;
    }

    if (listen(fd, ELOG_READER_MAX_CLIENTS) < 0) {
        perror("elogd_reader: listen");
        close(fd);
        return NULL;
    }

    while (g_daemon_running) {
        int client_fd = accept(fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            if (!g_daemon_running) break;
            perror("elogd_reader: accept");
            continue;
        }

        /* 接收 read_request 握手 */
        elog_read_request_t req;
        memset(&req, 0, sizeof(req));
        ssize_t n = recv(client_fd, &req, sizeof(req), 0);
        if (n < (ssize_t)sizeof(req)) {
            close(client_fd);
            continue;
        }

        if (req.version != ELOG_READ_PROTOCOL_VERSION) {
            close(client_fd);
            continue;
        }

        /* 检查客户端数量限制 */
        elog_mutex_lock(&s_clients_lock);
        if (s_client_count >= ELOG_READER_MAX_CLIENTS) {
            elog_mutex_unlock(&s_clients_lock);
            close(client_fd);
            continue;
        }

        /* 创建 per-client 状态 */
        reader_client_t* client = calloc(1, sizeof(reader_client_t));
        if (!client) {
            elog_mutex_unlock(&s_clients_lock);
            close(client_fd);
            continue;
        }

        client->client_fd = client_fd;
        client->min_level = req.min_level;
        client->pid_filter = req.pid_filter;
        client->remaining = req.count;  /* 0 = 无限 */
        client->active = true;

        /* 设置初始 read_pos */
        if (g_daemon_rb) {
            elog_mutex_lock(&g_daemon_rb->lock);
            client->read_pos = req.tail ? g_daemon_rb->write_pos : 0;
            elog_mutex_unlock(&g_daemon_rb->lock);
        }

        s_clients[s_client_count++] = client;
        elog_mutex_unlock(&s_clients_lock);

        /* 创建 push 线程 */
        pthread_t tid;
        pthread_create(&tid, NULL, client_push_thread, client);
        pthread_detach(tid);
    }

    /* 关闭所有客户端 */
    elog_mutex_lock(&s_clients_lock);
    for (int i = 0; i < s_client_count; i++) {
        if (s_clients[i]) s_clients[i]->active = false;
    }
    elog_mutex_unlock(&s_clients_lock);
    usleep(200000);  /* 给线程退出时间 */

    close(fd);
    unlink(ELOG_DAEMON_READER_SOCK);
    elog_mutex_destroy(&s_clients_lock);
    return NULL;
}
