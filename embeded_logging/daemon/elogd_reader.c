/**
 * @file elogd_reader.c
 * @brief elogd Reader — 通过 SOCK_SEQPACKET 向 elogcat 推送日志
 *
 * 线程模型:
 *   elogd_reader_thread (主线程): accept loop, 每个 client 创建独立 pthread
 *   client_push_thread (per-client): 遍历所有 buffer → sendto (支持 log_mask)
 */

#define _GNU_SOURCE

#include "elogd.h"
#include "elog_buf.h"
#include "elog_def.h"
#include "elog_debug.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>

#define ELOG_READER_MAX_CLIENTS  8

extern volatile bool g_daemon_running;

/* per-client 状态 */
typedef struct {
    int            client_fd;
    size_t         read_pos[ELOG_ID_MAX];  /* 每个 buffer 独立读位置 */
    uint8_t        min_level;
    uint16_t       pid_filter;
    uint32_t       log_mask;    /* bitmask: bit i = 订阅 buffer i, 0 = 全部 */
    uint32_t       remaining;   /* 剩余可读条数, 0 = 无限 */
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

    size_t data_pos = (pos + 4) % rb->buf_capacity;

    if (data_pos + entry_len <= rb->buf_capacity) {
        memcpy(out, rb->buffer + pos, 4);
        memcpy(out + 4, rb->buffer + data_pos, entry_len);
    } else {
        size_t first = rb->buf_capacity - data_pos;
        memcpy(out, rb->buffer + pos, 4);
        memcpy(out + 4, rb->buffer + data_pos, first);
        memcpy(out + 4 + first, rb->buffer, entry_len - first);
    }

    return total;
}

/* 发送一条日志给客户端 */
static int send_log_entry(reader_client_t* client,
                           const uint8_t* entry_data, uint32_t entry_len) {
    if (entry_len < sizeof(elog_msg_header_t)) return -1;

    const elog_msg_header_t* hdr = (const elog_msg_header_t*)entry_data;
    if (client->min_level > 0 && hdr->level < client->min_level) return -1;
    if (client->pid_filter > 0 && hdr->pid != client->pid_filter) return -1;
    if (client->log_mask != 0 && !(client->log_mask & (1 << hdr->log_id))) return -1;

    ssize_t sent = send(client->client_fd, entry_data, entry_len, MSG_NOSIGNAL);
    if (sent < 0) return -1;
    return 0;
}

/* ===== per-client push 线程 ===== */

static void* client_push_thread(void* arg) {
    reader_client_t* client = (reader_client_t*)arg;

    while (client->active && g_daemon_running) {
        int sent_count = 0;

        /* 遍历所有 buffer */
        for (int bid = 0; bid < ELOG_ID_MAX; bid++) {
            if (!client->active || !g_daemon_running) break;

            /* log_mask 过滤: 0 = 全部 */
            if (client->log_mask != 0 && !(client->log_mask & (1 << bid))) continue;

            elog_ring_buf_t* rb = elogd_get_buf((elog_id_t)bid);
            if (!rb) continue;

            elog_mutex_lock(&rb->lock);

            /* 等待该 buffer 有新数据 */
            int wait_count = 0;
            while (client->read_pos[bid] == rb->write_pos &&
                   client->active && g_daemon_running && wait_count < 1) {
                elog_cond_timedwait(&rb->not_empty, &rb->lock, 100);
                wait_count++;
            }

            /* 从该 buffer 逐条读取并发送 */
            while (client->read_pos[bid] != rb->write_pos &&
                   client->active && g_daemon_running) {
                uint8_t entry_buf[sizeof(elog_msg_header_t) +
                                  ELOG_MAX_TAG_LEN + ELOG_MAX_MSG_LEN + 4];
                size_t total = read_entry(rb, client->read_pos[bid],
                                           entry_buf, sizeof(entry_buf));
                if (total == 0) break;

                uint32_t entry_len = ring_read_u32(rb->buffer, rb->buf_capacity,
                                                    client->read_pos[bid]);

                int ret = send_log_entry(client, entry_buf + 4, entry_len);

                client->read_pos[bid] = (client->read_pos[bid] + entry_total_size(entry_len))
                                         % rb->buf_capacity;

                if (ret == 0) sent_count++;
                if (ret < 0 && errno == EPIPE) { client->active = false; break; }

                if (client->remaining > 0) {
                    if (--client->remaining == 0) { client->active = false; break; }
                }

                if (sent_count >= 128) break;
            }

            elog_mutex_unlock(&rb->lock);
        }

        if (!client->active) {
            ELOG_DBG_READER("client disconnect: fd=%d sent=%d", client->client_fd, sent_count);
            break;
        }

        /* 所有 buffer 都空, 短暂休眠 */
        if (sent_count == 0) {
            ELOG_DBG_READER("client[%d]: idle, sleep 50ms", client->client_fd);
            usleep(50000);  /* 50ms */
        }
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

    unlink(g_daemon_reader_sock);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, g_daemon_reader_sock, sizeof(addr.sun_path) - 1);

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
        client->log_mask = req.log_mask;  /* 0 = 全部 */
        client->remaining = req.count;
        client->active = true;

        /* 设置每个 buffer 的初始 read_pos */
        for (int bid = 0; bid < ELOG_ID_MAX; bid++) {
            elog_ring_buf_t* rb = elogd_get_buf((elog_id_t)bid);
            if (rb) {
                elog_mutex_lock(&rb->lock);
                client->read_pos[bid] = req.tail ? rb->write_pos : 0;
                elog_mutex_unlock(&rb->lock);
            }
        }

        s_clients[s_client_count++] = client;
        elog_mutex_unlock(&s_clients_lock);

        ELOG_DBG_READER("client connect: fd=%d tail=%u mask=0x%x", client_fd, req.tail, req.log_mask);

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
    usleep(200000);

    close(fd);
    unlink(g_daemon_reader_sock);
    elog_mutex_destroy(&s_clients_lock);
    return NULL;
}
