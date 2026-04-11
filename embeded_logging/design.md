# 嵌入式日志框架 (elog) — 设计文档

> 借鉴 Android system/logging (logd + liblog) 的架构设计，适配资源受限的嵌入式环境。

---

## 目录

- [1. 设计目标与约束](#1-设计目标与约束)
- [2. 分层架构总览](#2-分层架构总览)
- [3. 目录结构](#3-目录结构)
- [4. 核心数据结构](#4-核心数据结构)
- [5. 核心层设计](#5-核心层设计)
- [6. 缓冲层设计](#6-缓冲层设计)
- [7. 传输层设计](#7-传输层设计)
- [8. 过滤与裁剪](#8-过滤与裁剪)
- [9. 持久化设计](#9-持久化设计)
- [10. elogd 守护进程](#10-elogd-守护进程)
- [11. 日志写入流程](#11-日志写入流程)
- [12. 日志读取流程](#12-日志读取流程)
- [13. elogd 初始化流程](#13-elogd-初始化流程)
- [14. 部署方案](#14-部署方案)
- [15. 编译期配置](#15-编译期配置)
- [16. 运行时 API](#16-运行时-api)
- [17. 与 Android logging 的设计对照](#17-与-android-logging-的设计对照)

---

## 1. 设计目标与约束

### 1.1 设计目标

| 目标 | 说明 |
|------|------|
| **低资源占用** | RAM < 16KB（最小配置），ROM < 8KB |
| **零拷贝/低拷贝** | 热路径尽量避免内存拷贝 |
| **实时安全** | ISR 中可调用非阻塞日志接口 |
| **多输出目标** | 同时输出到 UART/文件/网络/RTOS 队列 |
| **多级过滤** | 全局级别 + 模块级别 + 标签级别 |
| **优先级裁剪** | 高优先级日志受保护，低优先级日志优先丢弃 |
| **掉电保持** | 关键日志可持久化到 Flash/NVRAM |
| **可扩展** | 传输层、缓冲区、格式化器均可插拔替换 |
| **两种部署** | 同时支持 Linux（有 MMU）和 RTOS/裸机（无 MMU） |

### 1.2 与 Android logging 的关键差异

| 维度 | Android logging | 嵌入式 elog |
|------|----------------|-------------|
| 内存模型 | 堆分配（std::list, 堆对象） | 栈/静态分配优先，Ring Buffer |
| 线程模型 | 多线程 + pthread + 条件变量 | 可选 RTOS 任务 / 裸机轮询 |
| IPC | Unix Domain Socket | UDS / 共享内存 / RTOS 队列 / UART |
| 压缩 | Zstd/Zlib（运行时压缩 Chunk） | 可选 miniz/lz4，默认关闭 |
| Event 日志 | TLV 二进制格式 | 保留 TLV 格式，简化 tag 映射 |
| 统计 | 8 维度哈希表 | 轻量计数器数组 |

### 1.3 借鉴的设计模式

| Android 模式 | 嵌入式适配 |
|-------------|-----------|
| LogBuffer 抽象接口 + 多实现 | LogBuffer 抽象 → RingLogBuffer / ShmLogBuffer |
| write_to_log() 双路分发 | TransportRegistry 多路分发 |
| PruneList 高/低优先级裁剪 | LogPrune 同样的两级策略 |
| atomic_int CAS 延迟初始化 | 保留 CAS 模式，适配裸机（关中断） |
| LogWriter 抽象 + 多种输出 | LogWriter 抽象 → UartWriter/FileWriter/NetWriter |
| android_log_header_t packed 头 | LogMessage packed 结构体 |
| LogStatistics 多维统计 | LogStatistics 轻量计数器 |
| FrameworkCommand 宏注册 | 简化为函数指针数组 |

---

## 2. 分层架构总览

```
┌─────────────────────────────────────────────────────────────┐
│                    应用层 Application Layer                   │
│   elog_debug/elog_info/elog_warn/elog_error/elog_assert     │
│   ELOG_D / ELOG_I / ELOG_W / ELOG_E / ELOG_TAG_*          │
│   elog_event_begin / elog_event_end                         │
├─────────────────────────────────────────────────────────────┤
│                    核心层 Core Layer                         │
│   ┌──────────────┐ ┌──────────────┐ ┌──────────────┐       │
│   │ Dispatcher   │ │ LogFilter    │ │ Formatter    │       │
│   │ (函数指针分发)│ │ (级别+标签)   │ │ (文本/二进制) │       │
│   └──────────────┘ └──────────────┘ └──────────────┘       │
├─────────────────────────────────────────────────────────────┤
│                    缓冲层 Buffer Layer                       │
│   ┌──────────────────────┐ ┌──────────────────────┐       │
│   │ RingLogBuffer        │ │ ShmLogBuffer         │       │
│   │ (单进程/ISR 安全)     │ │ (多进程共享内存)      │       │
│   └──────────────────────┘ └──────────────────────┘       │
├─────────────────────────────────────────────────────────────┤
│                    传输层 Transport Layer                    │
│   ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌────────┐ ┌──────┐│
│   │  UART   │ │  File   │ │  Net    │ │  CAN   │ │ RTOS ││
│   │Transport│ │Transport│ │Transport│ │Transport│ │Queue ││
│   └─────────┘ └─────────┘ └─────────┘ └────────┘ └──────┘│
├─────────────────────────────────────────────────────────────┤
│                  后端服务层 Backend Service Layer            │
│   elogd 守护进程 / elogcat CLI / 日志采集器                   │
├─────────────────────────────────────────────────────────────┤
│                  持久化层 Storage Layer                      │
│   FAT32/LittleFS 文件 / pstore NVRAM / Flash 环形分区        │
├─────────────────────────────────────────────────────────────┤
│                  配置层 Config Layer                         │
│   编译期: Kconfig / 头文件宏定义                               │
│   运行时: elog_set_level() / elog_add_transport()           │
└─────────────────────────────────────────────────────────────┘
```

> 完整分层架构图: [`embedded_logging_arch.mmd`](embedded_logging_arch.mmd)
>
> 数据流图: [`embedded_logging_dataflow.mmd`](embedded_logging_dataflow.mmd)

---

## 3. 目录结构

```
embedded_logging/
├── docs/
│   ├── design.md                    # 本文档
│   ├── embedded_logging_arch.mmd    # 分层架构图
│   ├── embedded_logging_class.mmd   # 类体系图
│   ├── embedded_logging_dataflow.mmd # 数据流图
│   ├── embedded_logging_write_seq.mmd # 写入时序图
│   ├── embedded_logging_read_seq.mmd  # 读取时序图
│   ├── embedded_logging_init_seq.mmd  # 初始化时序图
│   └── embedded_logging_deployment.mmd # 部署场景图
├── include/
│   └── elog/
│       ├── elog.h                   # 公共 API 头文件
│       ├── elog_config.h            # 编译期配置（用户修改）
│       ├── elog_def.h               # 内部类型定义
│       ├── elog_buf.h               # LogBuffer 接口
│       ├── elog_transport.h         # LogTransport 接口
│       ├── elog_filter.h            # LogFilter 接口
│       ├── elog_format.h            # Formatter 接口
│       ├── elog_stats.h             # LogStatistics 接口
│       ├── elog_prune.h             # LogPrune 接口
│       └── elog_event.h             # Event 日志接口
├── src/
│   ├── elog.c                       # 核心实现 (API + Dispatcher + Filter)
│   ├── elog_format.c                # 格式化器实现
│   ├── elog_filter.c                # 过滤器实现
│   ├── elog_stats.c                 # 统计实现
│   ├── elog_prune.c                 # 裁剪策略实现
│   ├── elog_event.c                 # Event 日志实现
│   ├── buf/
│   │   ├── elog_buf_ring.c          # RingLogBuffer
│   │   └── elog_buf_shm.c           # ShmLogBuffer
│   └── transport/
│       ├── elog_transport_uart.c    # UART 输出
│       ├── elog_transport_file.c    # 文件输出
│       ├── elog_transport_net.c     # 网络输出
│       ├── elog_transport_can.c     # CAN 总线输出
│       └── elog_transport_rtos.c    # RTOS 队列输出
├── daemon/
│   ├── elogd.c                      # 守护进程 main
│   ├── elogd_listener.c             # 日志接收 (SOCK_DGRAM)
│   ├── elogd_reader.c               # 日志推送 (SOCK_SEQPACKET)
│   ├── elogd_cmd.c                  # 命令处理 (SOCK_STREAM)
│   └── elogd_service.c              # Binder/DBus 服务
├── tools/
│   ├── elogcat.c                    # 日志查看工具
│   ├── elogdump.c                   # 二进制转文本
│   └── elogctl.c                    # 运行时配置工具
├── port/
│   ├── elog_port.h                  # 平台适配层头文件
│   ├── elog_port_linux.c            # Linux 实现
│   ├── elog_port_freertos.c         # FreeRTOS 实现
│   └── elog_port_bare.c             # 裸机实现
├── CMakeLists.txt
├── Kconfig                          # 编译选项配置
└── Makefile
```

---

## 4. 核心数据结构

### 4.1 日志级别

```c
typedef enum {
    ELOG_LEVEL_VERBOSE = 0,   /* 详细调试信息 */
    ELOG_LEVEL_DEBUG   = 1,   /* 调试信息 */
    ELOG_LEVEL_INFO    = 2,   /* 一般信息 */
    ELOG_LEVEL_WARN    = 3,   /* 警告 */
    ELOG_LEVEL_ERROR   = 4,   /* 错误 */
    ELOG_LEVEL_FATAL   = 5,   /* 致命错误 */
    ELOG_LEVEL_NONE    = 6,   /* 关闭所有日志 */
} elog_level_t;
```

### 4.2 日志 ID（缓冲区类型）

```c
typedef enum {
    ELOG_ID_MAIN    = 0,    /* 主日志缓冲区 */
    ELOG_ID_RADIO   = 1,    /* 无线电/通信模块 */
    ELOG_ID_EVENTS  = 2,    /* 事件日志 (二进制) */
    ELOG_ID_SYSTEM  = 3,    /* 系统日志 */
    ELOG_ID_CRASH   = 4,    /* 崩溃日志 */
    ELOG_ID_KERNEL  = 5,    /* 内核日志 */
    ELOG_ID_MAX
} elog_id_t;
```

> 借鉴 Android `log_id_t`，根据嵌入式场景精简（去掉 STATS/SECURITY，增加 KERNEL）。

### 4.3 LogMessage（packed 结构体）

```c
/* 借鉴 Android LogBufferElement，packed 减少内存占用 */
#pragma pack(push, 1)
typedef struct {
    uint8_t  log_id;          /* ELOG_ID_MAIN 等 */
    uint8_t  level;           /* ELOG_LEVEL_INFO 等 */
    uint32_t timestamp;       /* Unix epoch seconds (or uptime ticks) */
    uint16_t pid;             /* 进程 ID */
    uint16_t tid;             /* 线程 ID */
    uint16_t line;            /* 源码行号 */
    uint16_t tag_len;         /* tag 长度 */
    uint16_t msg_len;         /* 消息长度 */
    /* 后续为变长数据: tag[tag_len] + msg[msg_len] */
} elog_msg_header_t;
#pragma pack(pop)

/* 线路上完整消息 = header + tag + msg */
/* sizeof(header) = 16 bytes (ARM32), 对齐友好 */
```

> 借鉴 Android 的 `android_log_header_t` + `logger_entry` 双头设计，合并为单头减少开销。Android 在 liblog→logd 传两个 header（`android_log_header_t` 11B + `logger_entry` 可变），嵌入式合并为 16B packed 头。

### 4.4 Event 日志二进制格式

保留 Android 的 TLV 格式，精简类型集：

```
Event 元素编码:
  ┌──────────┬──────────┐
  │ type:u8  │ data:var │
  └──────────┴──────────┘

  type=0 (INT32):  1B type + 4B value = 5 bytes
  type=1 (INT64):  1B type + 8B value = 9 bytes
  type=2 (STRING): 1B type + 4B len  + N bytes = 5+N bytes
  type=3 (FLOAT):  1B type + 4B value = 5 bytes
  type=4 (LIST):   1B type + 1B count + elements
```

---

## 5. 核心层设计

### 5.1 Dispatcher（函数指针分发）

```c
/* 借鉴 Android __android_log_write_log_message 中的 logger_function 函数指针 */
typedef void (*elog_logger_func_t)(const elog_msg_header_t* hdr,
                                    const char* tag,
                                    const char* msg);

/* 可替换的全局日志后端（策略模式） */
static elog_logger_func_t s_logger_func = elog_default_logger;

void elog_set_logger(elog_logger_func_t func);
```

> Android 使用 `__android_logger_function` 函数指针实现可替换的日志后端。elog 保留此模式，默认指向 `elog_default_logger`（走 Buffer→Transport 路径），用户可替换为自定义实现（如直接串口输出、发送到自定义队列等）。

### 5.2 LogFilter（多级过滤）

```c
/* 标签级别映射 */
typedef struct {
    const char* tag;
    uint8_t     level;
} elog_tag_level_t;

typedef struct {
    uint8_t            global_level;            /* 全局最低级别 */
    elog_tag_level_t   tag_levels[ELOG_MAX_TAGS]; /* 标签级别白名单 */
    uint8_t            tag_count;               /* 已注册标签数 */
    bool               color_enabled;           /* 彩色输出 */
    bool               timestamp_enabled;       /* 时间戳 */
    bool               source_location;         /* 源码位置 */
} elog_filter_t;
```

**过滤逻辑**（同 Android `LogStatistics::ShouldPrune` 的阈值思路）:

```
should_log(level, tag):
  if level < global_level: return false
  if tag in tag_levels:
    if level < tag_levels[tag]: return false
  return true
```

### 5.3 LogFormatter（格式化器）

```c
typedef struct {
    char  buf[ELOG_MAX_FORMAT_LEN];
    int   pos;
} elog_format_ctx_t;

/* 文本格式化（借鉴 logcat 输出格式）:
 * 01-01 08:00:00.123 I  1234  567 sensor  : temperature=25
 * MM-DD HH:MM:SS.mmm L PID TID TAG    : message
 */
int elog_format_text(elog_format_ctx_t* ctx, const elog_msg_header_t* hdr,
                     const char* tag, const char* msg);

/* 二进制格式化（用于网络传输/文件存储，紧凑格式）:
 * header(16B) + tag + msg，无格式化开销
 */
int elog_format_binary(elog_format_ctx_t* ctx, const elog_msg_header_t* hdr,
                       const char* tag, const char* msg);
```

---

## 6. 缓冲层设计

### 6.1 LogBuffer 抽象接口

```c
typedef struct elog_buf {
    int  (*log)(struct elog_buf* self, const elog_msg_header_t* hdr,
                const char* tag, const char* msg);
    int  (*flush)(struct elog_buf* self, elog_writer_t* writer,
                  elog_filter_t* filter);
    void (*clear)(struct elog_buf* self);
    size_t (*size)(struct elog_buf* self);
    size_t (*capacity)(struct elog_buf* self);
    bool  (*is_empty)(struct elog_buf* self);
} elog_buf_t;
```

> 借鉴 Android `LogBuffer` 纯虚接口，使用 C 函数指针模拟虚函数表（vtable），避免 C++ 运行时开销。Android 用 C++ 虚函数实现 `Log()` / `FlushTo()` / `Clear()` 等 7 个方法，elog 精简为 6 个。

### 6.2 RingLogBuffer（默认实现）

```c
typedef struct {
    elog_buf_t base;                    /* vtable */

    uint8_t*  buffer;                   /* 数据区域 */
    size_t    capacity;                 /* 总容量 */
    size_t    write_pos;                /* 写入位置 */
    size_t    read_pos;                 /* 读取位置 */
    size_t    count;                    /* 当前条目数 */
    bool      overwrite;                /* true: 覆写旧日志; false: 丢弃新日志 */

    /* 同步原语（通过 port 层适配） */
    elog_mutex_t lock;
    elog_cond_t  not_empty;             /* 用于 reader wait */
} elog_ring_buf_t;
```

**与 Android SerializedLogBuffer 的对比**:

| 维度 | Android SerializedLogBuffer | elog RingLogBuffer |
|------|---------------------------|-------------------|
| 存储 | `list<SerializedLogChunk>` × log_id | 单一连续环形缓冲区 |
| 分配 | 每个 Chunk 堆分配 | 静态分配或单次 malloc |
| 压缩 | Chunk 写满后 Zstd 压缩 | 可选，默认关闭 |
| 读者追踪 | reader_ref_count_ + readers_ | 单 reader（嵌入式简化） |
| 裁剪单位 | 整 Chunk 释放 | 逐条覆写/丢弃 |

### 6.3 ShmLogBuffer（多进程共享内存）

```c
typedef struct {
    elog_buf_t base;

    int       shm_fd;
    char*     shm_addr;
    size_t    shm_size;
    elog_sem_t sem;                     /* POSIX 信号量 */
    bool      is_owner;                 /* 创建者负责 unlink */
} elog_shm_buf_t;
```

> 用于有 MMU 的嵌入式 Linux 多进程场景，借鉴 Android logd 通过 Unix Socket + SCM_CREDENTIALS 实现多进程日志汇聚，但简化为共享内存直写（减少一次拷贝）。

---

## 7. 传输层设计

### 7.1 LogTransport 抽象接口

```c
typedef struct elog_transport {
    int  (*open)(struct elog_transport* self);
    void (*close)(struct elog_transport* self);
    int  (*write)(struct elog_transport* self, const uint8_t* data, size_t len);
    int  (*flush)(struct elog_transport* self);
    bool (*is_open)(struct elog_transport* self);
    const char* (*name)(struct elog_transport* self);
} elog_transport_t;
```

> 借鉴 Android 的 `android_log_transport_write` 函数指针结构体（name / logMask / open / close / write），扩展为更完整的 vtable。

### 7.2 TransportRegistry（多路分发）

```c
/* 借鉴 Android write_to_log() 中 LogdWrite + PmsgWrite 的双路分发，
   扩展为 N 路分发 */
typedef struct {
    elog_transport_t* transports[ELOG_MAX_TRANSPORTS];
    uint8_t          count;
    elog_mutex_t     lock;
} elog_transport_registry_t;

int  elog_transport_register(elog_transport_t* transport);
int  elog_transport_unregister(elog_transport_t* transport);
void elog_transport_dispatch(const uint8_t* data, size_t len);
void elog_transport_flush_all(void);
```

**与 Android write_to_log() 的对比**:

Android 在 `logger_write.cpp:199` 中硬编码双路:
```c
ret = LogdWrite(log_id, &ts, vec, nr);  // 主路径
PmsgWrite(log_id, &ts, vec, nr);         // 附加路径 (fire-and-forget)
```

elog 改为注册表模式，支持任意数量传输目标:
```c
for (i = 0; i < registry.count; i++) {
    registry.transports[i]->write(data, len);
}
```

### 7.3 具体传输实现

#### UartTransport

```c
typedef struct {
    elog_transport_t base;

    int       fd;              /* UART 设备文件描述符 */
    uint32_t  baudrate;
    uint8_t*  dma_tx_buf;      /* DMA 发送缓冲区 */
    size_t    dma_tx_len;
    bool      dma_enabled;
    elog_mutex_t dma_lock;
} elog_uart_transport_t;
```

- 借鉴 Android `LogdWrite` 通过 Unix Socket `writev()` + `SCM_CREDENTIALS` 传递身份信息
- 嵌入式简化为 UART `write()` + DMA 异步发送
- 非 DMA 模式下使用环形 TX 缓冲区避免阻塞

#### FileTransport

```c
typedef struct {
    elog_transport_t base;

    int       fd;
    char      filepath[ELOG_PATH_MAX];
    size_t    file_size;
    size_t    max_file_size;    /* 单文件最大大小 */
    uint8_t   max_files;        /* 滚动文件数量 */
    bool      compress;         /* 写入时压缩 */
    bool      binary;           /* 二进制/文本模式 */
} elog_file_transport_t;
```

- 借鉴 Android `PmsgWrite` 写入 `/dev/pmsg0` 的持久化思路
- 增加滚动日志（rotate）和可选压缩

#### NetTransport

```c
typedef struct {
    elog_transport_t base;

    int       sock_fd;
    uint8_t   proto;           /* ELOG_NET_UDP / ELOG_NET_TCP */
    char      host[64];
    uint16_t  port;
    bool      connected;
    uint32_t  reconnect_interval_ms;
} elog_net_transport_t;
```

- UDP 模式: `sendto()` 无连接，适合日志上报
- TCP 模式: 长连接 + 自动重连，适合远程日志服务

#### RtosQueueTransport

```c
typedef struct {
    elog_transport_t base;

    elog_queue_t queue;         /* RTOS 消息队列句柄 */
    int         max_depth;
    elog_task_t consumer_task;  /* 消费者任务 */
    elog_transport_t* downstream; /* 消费者转发的目标 Transport */
} elog_rtos_queue_transport_t;
```

- 用于 ISR → Logger Task 的解耦
- 借鉴 Android `LogReaderThread` 的条件变量等待机制，改用 RTOS 队列信号量

---

## 8. 过滤与裁剪

### 8.1 LogPrune（优先级裁剪）

```c
/* 借鉴 Android PruneList 的 ~ / ~! / ~1000/! 语法 */
typedef struct {
    const char* tag;
    bool        is_negated;     /* ~ 前缀: 否定匹配 → 保护 */
    uint16_t    pid;            /* 可选: /PID 粒度 */
} elog_prune_rule_t;

typedef struct {
    elog_prune_rule_t high_rules[ELOG_MAX_PRUNE_RULES];  /* 保护规则 (~) */
    uint8_t          high_count;
    elog_prune_rule_t low_rules[ELOG_MAX_PRUNE_RULES];   /* 低优先级规则 */
    uint8_t          low_count;
    uint32_t         prune_threshold_pct; /* 触发裁剪的阈值 (%) */
} elog_prune_t;
```

**裁剪策略**（借鉴 Android `PruneList`）:

```
缓冲区使用率 > threshold (默认 90%):
  1. 遍历旧日志
  2. 如果 tag 匹配 low_rules → 优先丢弃
  3. 如果 tag 匹配 high_rules (~ 前缀) → 最后丢弃
  4. 未匹配任何规则 → 按时间顺序丢弃
```

**Android 默认裁剪规则**: `"~! ~1000/!"` — 保护系统进程和 system UID 下所有进程

**嵌入式默认裁剪规则**: `"~elogd ~crash"` — 保护日志守护进程和崩溃日志

### 8.2 LogStatistics（轻量统计）

```c
typedef struct {
    uint32_t total_count[ELOG_ID_MAX];      /* 每个缓冲区总写入数 */
    uint32_t dropped_count[ELOG_ID_MAX];    /* 每个缓冲区丢弃数 */
    uint32_t level_count[6];                /* 按级别统计 */
    uint32_t buffer_usage;                  /* 当前缓冲区使用量 */
    uint32_t buffer_peak;                   /* 缓冲区使用峰值 */
} elog_stats_t;
```

> 借鉴 Android `LogStatistics` 的 8 维度哈希表（uidTable / pidTable / tidTable / tagTable 等），精简为纯计数器数组。Android 的多维哈希表在 64KB+ RAM 的设备上合理，但嵌入式通常只有 16-256KB RAM。

---

## 9. 持久化设计

### 9.1 三级持久化

```
┌─────────────────────────────────────────────┐
│ Level 0: 内存缓冲区 (RingLogBuffer)          │
│   - 最快，掉电丢失                            │
│   - 适用于实时调试                             │
├─────────────────────────────────────────────┤
│ Level 1: 文件系统 (FAT32/LittleFS)           │
│   - 滚动日志 + 可选压缩                      │
│   - 适用于长期存储                             │
├─────────────────────────────────────────────┤
│ Level 2: 原始 Flash / NVRAM (pstore)         │
│   - 掉电保持，最小写入开销                     │
│   - 适用于崩溃恢复（最后 N 条日志）              │
└─────────────────────────────────────────────┘
```

### 9.2 借鉴 Android PmsgWrite

Android 的 `PmsgWrite` 写入 `/dev/pmsg0`（pstore RAM），用于崩溃后恢复日志。

嵌入式对应方案:

| 方案 | 适用场景 | 实现 |
|------|---------|------|
| Flash 环形分区 | 带 NOR/NAND Flash 的设备 | raw flash driver，磨损均衡 |
| NVRAM / RTC RAM | 带 SRAM 保留区的 MCU | 直接内存映射 |
| EEPROM / FRAM | 小容量持久化需求 | I2C/SPI 接口 |
| pstore (Linux) | 运行 Linux 的嵌入式设备 | `/sys/fs/pstore/pmsg-ramoops-0` |

### 9.3 pstore 写入流程

```
elog_pmsg_write():
  1. 构造: elog_msg_header_t + tag + msg
  2. 添加 pmsg 头 (magic + len + pid + uid)
  3. writev() 到 /dev/pmsg0 或 raw flash
  4. 非 debug 模式仅允许 CRASH/EVENTS 级别
```

> 与 Android `PmsgWrite` 相同的策略：非 debug 构建只持久化关键日志。

---

## 10. elogd 守护进程

> 完整设计借鉴 Android `logd/main.cpp` 的启动序列。

### 10.1 启动序列

```
elogd main():
  1. elog_ring_buf_create(capacity)         → 创建 RingLogBuffer
  2. elog_reader_list_create()              → 创建 ReaderList
  3. elog_stats_create()                    → 创建 LogStatistics
  4. elog_prune_init(default_rules)         → 初始化裁剪规则
  5. elogd_cmd_listener_start(buf, stats)   → /var/run/elogd.sock (SOCK_STREAM)
  6. elogd_log_listener_start(buf, readers) → /var/run/elogdw.sock (SOCK_DGRAM)
  7. elogd_reader_start(buf, readers)       → /var/run/elogdr.sock (SOCK_SEQPACKET)
  8. elogd_service_register(readers)        → 注册系统服务 (Binder/DBus)
  9. event_loop(poll)                       → 进入主事件循环
```

### 10.2 三端口架构（借鉴 logd）

| Socket | 类型 | 用途 | 对应 Android |
|--------|------|------|-------------|
| `/var/run/elogd` | SOCK_STREAM | 命令控制 (clear/size/stats/prune) | `/dev/socket/logd` |
| `/var/run/elogdw` | SOCK_DGRAM | 日志写入 (客户端→elogd) | `/dev/socket/logdw` |
| `/var/run/elogdr` | SOCK_SEQPACKET | 日志读取 (elogcat←elogd) | `/dev/socket/logdr` |

### 10.3 命令协议

```
客户端发送 ASCII 命令 → elogd CmdListener 处理:

  clear <log_id>              清空指定缓冲区
  getBufSize <log_id>         获取缓冲区大小
  setBufSize <log_id> <size>  设置缓冲区大小
  getStatistics               获取统计信息
  getPruneList                获取裁剪规则
  setPruneList <rules>        设置裁剪规则
  exit                        断开连接
```

> 借鉴 Android `CommandListener` 的 `FrameworkCommand` 宏注册模式，简化为函数指针数组分发。

---

## 11. 日志写入流程

> 详见 [`embedded_logging_write_seq.mmd`](embedded_logging_write_seq.mmd)

```
1. App: ELOG_I("sensor", "temp=%d", value)
   ↓
2. elog_write(level, tag, fmt, ...):
   - va_list 格式化消息
   - 构造 LogMessage (header + tag + msg)
   ↓
3. elog_filter_check(level, tag):
   - global_level 过滤
   - tag 级别过滤
   - 不通过则直接返回
   ↓
4. elog_buf_log(buffer, &msg):
   - 加锁
   - 检查空间:
     a. 有空间 → memcpy 到 ring buffer
     b. 无空间 → 检查裁剪策略:
        - low priority tag → 丢弃/覆写
        - high priority tag → 覆写最旧的非保护条目
   - 更新统计 (total_count / dropped_count)
   - 解锁
   ↓
5. elog_transport_dispatch(&msg):
   - 遍历所有注册的 Transport
   - 并行分发到 UART / File / Net / Queue
   ↓
6. 各 Transport write():
   - UartTransport: format_text() → UART TX (DMA)
   - FileTransport: format_binary() → fwrite() → rotate if needed
   - NetTransport: sendto() / send()
   - RtosQueueTransport: queue_send() → wake consumer task
```

### 11.1 ISR 安全写入

```c
/* ISR 中调用的非阻塞版本 */
int elog_write_isr(uint8_t level, const char* tag, const char* msg, uint16_t msg_len);

/* 特点:
 * - 不使用互斥锁（改用关中断或原子操作）
 * - 不使用可重入的格式化（消息需预格式化）
 * - Ring buffer 满时直接覆写最旧条目
 * - 仅写入内存缓冲区，不触发 Transport 输出
 *   （输出由 Logger Task 在中断返回后处理）
 */
```

---

## 12. 日志读取流程

> 详见 [`embedded_logging_read_seq.mmd`](embedded_logging_read_seq.mmd)

```
1. elogcat: elog_reader_init(mode=TAIL, tail=100)
   - 注册为 buffer 的 reader
   ↓
2. 读取循环:
   elog_reader_read(reader, &msg, timeout_ms):
   a. buf.flush(writer, filter):
      - 加锁
      - 从 read_pos 遍历 ring buffer
      - 对每条 entry 调用 filter.should_log()
      - 通过过滤的 entry → writer.write()
      - 更新 read_pos
      - 解锁
   b. 如果 flush 返回 0 (无新日志):
      - wait on condition_variable (timeout_ms)
      - 超时返回 0
   c. 如果 flush 返回 N (有 N 条新日志):
      - 返回 N
   ↓
3. elogcat: elog_reader_free(reader)
   - 注销 reader
```

### 12.1 读者管理（借鉴 Android LogReaderList）

```c
typedef struct {
    elog_reader_thread_t* readers[ELOG_MAX_READERS];
    uint8_t              count;
    elog_mutex_t         lock;
} elog_reader_list_t;

/* 新日志到达时唤醒所有等待的读者 */
void elog_reader_list_notify(elog_reader_list_t* list, uint32_t log_mask);
```

---

## 13. elogd 初始化流程

> 详见 [`embedded_logging_init_seq.mmd`](embedded_logging_init_seq.mmd)

```
┌─ 创建核心组件 ──────────────────────────┐
│ RingLogBuffer(256KB)                    │
│ ReaderList()                            │
│ LogStatistics()                         │
│ LogPrune("~elogd ~crash")              │
└──────────────┬──────────────────────────┘
               ↓
┌─ 启动服务端口 ──────────────────────────┐
│ CmdListener  → /var/run/elogd  (STREAM) │ ← 命令控制
│ LogListener  → /var/run/elogdw (DGRAM)  │ ← 接收日志
│ LogReader    → /var/run/elogdr (SEQPACKET)│ ← 推送日志
└──────────────┬──────────────────────────┘
               ↓
┌─ 注册系统服务 ──────────────────────────┐
│ elogd_native_service_register()          │
└──────────────┬──────────────────────────┘
               ↓
┌─ 主事件循环 ────────────────────────────┐
│ while (running) {                       │
│     poll(sockets, -1);                  │
│     // 处理写入请求 → buf.log()          │
│     // 通知读者   → reader_list.notify() │
│     // 推送日志   → reader.flush()       │
│     // 处理命令   → cmd_handler()        │
│ }                                       │
└─────────────────────────────────────────┘
```

---

## 14. 部署方案

> 详见 [`embedded_logging_deployment.mmd`](embedded_logging_deployment.mmd)

### 14.1 方案 A: 有 MMU（嵌入式 Linux）

```
┌─────────────────────────────────┐
│         用户空间                  │
│  App A (libelog.so)              │
│  App B (libelog.so)              │
│  elogcat                         │
├─────────────────────────────────┤
│         elogd 守护进程            │
│  LogListener ← writev()          │
│  RingLogBuffer                   │
│  LogReader   → push to elogcat   │
│  FileWriter  → /var/log/elog/    │
├─────────────────────────────────┤
│         内核空间                  │
│  klog (printk → /proc/kmsg)     │
└─────────────────────────────────┘
  IPC: Unix Domain Socket + SCM_CREDENTIALS
```

- 多进程通过 Unix Socket 写入 elogd
- elogd 统一管理缓冲区、裁剪、持久化
- 借鉴 Android logd 完整架构

### 14.2 方案 B: 无 MMU（RTOS / 裸机）

```
┌─────────────────────────────────┐
│  Task A  ──┐                    │
│  Task B  ──┤── RingLogBuffer ──┐ │
│  ISR    ──┘   (全局静态)       │ │
│                                │ │
│            Logger Task ────────┘ │
│              ├─ UartTransport    │
│              ├─ FileTransport    │
│              └─ NetTransport     │
└─────────────────────────────────┘
  同步: Mutex / 临界区 / 关中断
  通信: RTOS Queue (ISR → Logger)
```

- 无守护进程，所有组件静态链接到固件
- ISR 使用非阻塞写入（关中断保护）
- Logger Task 低优先级运行，统一处理输出
- 无 Unix Socket，通过全局 buffer + RTOS 队列通信

### 14.3 方案 C: 最小配置（极小资源）

```
  仅启用:
  - RingLogBuffer (4KB, 静态数组)
  - UartTransport (无 DMA)
  - LogFilter (仅全局级别)
  - 无裁剪，无统计，无持久化

  ROM: ~4KB  RAM: ~6KB
```

---

## 15. 编译期配置

通过 `elog_config.h` 或 Kconfig 控制，借鉴 Android logd 的编译选项。

```c
/* ===== 缓冲区配置 ===== */
#define ELOG_BUFFER_SIZE          65536    /* Ring buffer 大小 (bytes) */
#define ELOG_MAX_MSG_LEN          1024     /* 单条日志最大长度 */
#define ELOG_MAX_TAG_LEN          32       /* 标签最大长度 */

/* ===== 功能裁剪 ===== */
#define ELOG_COLOR_ENABLE         1        /* 彩色输出 */
#define ELOG_TIMESTAMP_ENABLE     1        /* 时间戳 */
#define ELOG_SOURCE_LOCATION      0        /* 源码位置 (file:line) */
#define ELOG_EVENT_ENABLE         1        /* Event 日志 */
#define ELOG_STATS_ENABLE         1        /* 统计功能 */
#define ELOG_PRUNE_ENABLE         0        /* 优先级裁剪 */

/* ===== 传输层裁剪 ===== */
#define ELOG_TRANSPORT_UART       1        /* UART 输出 */
#define ELOG_TRANSPORT_FILE       1        /* 文件输出 */
#define ELOG_TRANSPORT_NET        0        /* 网络输出 */
#define ELOG_TRANSPORT_CAN        0        /* CAN 总线输出 */
#define ELOG_TRANSPORT_RTOS_QUEUE 0        /* RTOS 队列 */

/* ===== 守护进程（仅 Linux） ===== */
#define ELOG_DAEMON_ENABLE        1        /* 编译 elogd */
#define ELOG_MAX_READERS          8        /* 最大同时读者数 */
#define ELOG_MAX_TRANSPORTS       4        /* 最大传输目标数 */

/* ===== 默认值 ===== */
#define ELOG_LEVEL_DEFAULT        ELOG_LEVEL_DEBUG
#define ELOG_FILE_MAX_SIZE        (256 * 1024)  /* 256KB */
#define ELOG_FILE_MAX_FILES       4               /* 4 个滚动文件 */
```

---

## 16. 运行时 API

### 16.1 初始化与配置

```c
/* 初始化（RTOS/裸机方案必须调用，Linux elogd 方案可选） */
int elog_init(void);
void elog_deinit(void);

/* 设置全局日志级别 */
void elog_set_level(elog_level_t level);
elog_level_t elog_get_level(void);

/* 设置标签级别 */
int elog_set_tag_level(const char* tag, elog_level_t level);
int elog_reset_tag_level(const char* tag);

/* 替换日志后端 */
void elog_set_logger(elog_logger_func_t func);

/* 注册/注销传输目标 */
int elog_add_transport(elog_transport_t* transport);
int elog_remove_transport(elog_transport_t* transport);

/* 裁剪规则管理 */
int elog_prune_set_rules(const char* rules);
int elog_prune_get_rules(char* buf, size_t len);
```

### 16.2 日志输出

```c
/* 基础 API */
void elog_write(elog_level_t level, const char* tag, const char* fmt, ...);
void elog_vwrite(elog_level_t level, const char* tag, const char* fmt, va_list ap);

/* 级别快捷方式 */
#define elog_debug(tag, fmt, ...)   elog_write(ELOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)
#define elog_info(tag, fmt, ...)    elog_write(ELOG_LEVEL_INFO, tag, fmt, ##__VA_ARGS__)
#define elog_warn(tag, fmt, ...)    elog_write(ELOG_LEVEL_WARN, tag, fmt, ##__VA_ARGS__)
#define elog_error(tag, fmt, ...)   elog_write(ELOG_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__)
#define elog_fatal(tag, fmt, ...)   elog_write(ELOG_LEVEL_FATAL, tag, fmt, ##__VA_ARGS__)

/* 宏版本（编译期级别过滤，零开销） */
#define ELOG_D(tag, fmt, ...)  do { \
    if (ELOG_LEVEL_DEBUG >= ELOG_LEVEL_DEFAULT) \
        elog_write(ELOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__); \
} while(0)

#define ELOG_I(tag, fmt, ...)  do { \
    if (ELOG_LEVEL_INFO >= ELOG_LEVEL_DEFAULT) \
        elog_write(ELOG_LEVEL_INFO, tag, fmt, ##__VA_ARGS__); \
} while(0)

/* ISR 安全版本 */
int elog_write_isr(elog_level_t level, const char* tag,
                   const char* msg, uint16_t msg_len);

/* 断言 */
#define elog_assert(cond) do { \
    if (!(cond)) { \
        elog_write(ELOG_LEVEL_FATAL, "assert", \
                   "%s failed at %s:%d", #cond, __FILE__, __LINE__); \
        elog_assert_hook(); \
    } \
} while(0)
```

### 16.3 日志读取

```c
typedef struct elog_reader elog_reader_t;

/* 创建/销毁 reader */
elog_reader_t* elog_reader_create(uint8_t mode, uint32_t tail, uint16_t pid);
void elog_reader_destroy(elog_reader_t* reader);

/* 读取日志 (阻塞/非阻塞) */
int elog_reader_read(elog_reader_t* reader, char* buf, size_t len, int timeout_ms);

/* 读取模式 */
#define ELOG_READ_NONBLOCK  0x01
#define ELOG_READ_PSTORE    0x02    /* 从 pstore 恢复上次启动日志 */
#define ELOG_READ_BINARY    0x04    /* 二进制格式 */
```

### 16.4 Event 日志

```c
typedef struct elog_event_ctx elog_event_ctx_t;

/* 创建 Event 上下文 */
elog_event_ctx_t* elog_event_create(uint32_t event_id);

/* 添加参数（链式调用） */
int elog_event_add_int32(elog_event_ctx_t* ctx, int32_t value);
int elog_event_add_int64(elog_event_ctx_t* ctx, int64_t value);
int elog_event_add_float(elog_event_ctx_t* ctx, float value);
int elog_event_add_string(elog_event_ctx_t* ctx, const char* str);

/* 提交 Event */
int elog_event_submit(elog_event_ctx_t* ctx);
void elog_event_destroy(elog_event_ctx_t* ctx);

/* 使用示例:
 * elog_event_ctx_t* e = elog_event_create(EVENT_SENSOR_READING);
 * elog_event_add_int32(e, sensor_id);
 * elog_event_add_float(e, temperature);
 * elog_event_add_string(e, unit);
 * elog_event_submit(e);
 * elog_event_destroy(e);
 */
```

---

## 17. 与 Android logging 的设计对照

### 17.1 架构对照表

| 模块 | Android | elog | 适配说明 |
|------|---------|------|---------|
| 客户端库 | liblog.so | libelog.a / libelog.so | 可静态/动态链接 |
| 守护进程 | logd | elogd | 仅 Linux 部署 |
| 日志工具 | logcat | elogcat | 多缓冲区 + 着色 |
| 写入 Socket | /dev/socket/logdw | /var/run/elogdw | Unix DGRAM |
| 读取 Socket | /dev/socket/logdr | /var/run/elogdr | Unix SEQPACKET |
| 命令 Socket | /dev/socket/logd | /var/run/elogd | Unix STREAM |
| 缓冲区 | LogBuffer (heap) | RingLogBuffer (static) | 堆→静态 |
| 条目格式 | LogBufferElement | elog_msg_header_t | packed struct |
| 传输分发 | write_to_log() 双路 | TransportRegistry 多路 | 注册表模式 |
| 函数指针分发 | __android_logger_function | elog_logger_func_t | 策略模式 |
| 延迟初始化 | atomic_int CAS | atomic_int CAS / 关中断 | 两种实现 |
| 优先级裁剪 | PruneList (~ ~!) | LogPrune (~ rules) | 保留语法 |
| 统计 | LogStatistics (hash) | LogStatistics (array) | 简化维度 |
| Event 日志 | TLV binary | TLV binary | 保留格式 |
| 持久化 | pmsg0 (pstore) | Flash/NVRAM/pstore | 多后端 |
| 身份传递 | SCM_CREDENTIALS | 可选 SCM_CREDS / pid 参数 | 简化 |
| 慢读者处理 | KickReader (3级) | 丢弃/断开 (2级) | 简化 |
| 压缩 | Zstd/Zlib | 可选 miniz/lz4 | 默认关闭 |

### 17.2 关键设计差异分析

**为什么用 Ring Buffer 替代 Chunk 链表?**

Android `SerializedLogBuffer` 使用 `list<SerializedLogChunk>`，每个 Chunk 堆分配。这在有 MMU + 虚拟内存 + 内存充足的 Android 设备上合理。嵌入式设备:
- 无 MMU → 无虚拟内存，堆碎片致命
- RAM 有限 → 固定大小 Ring Buffer 可预测内存
- 实时要求 → 连续内存缓存友好

**为什么用函数指针替代 C++ 虚函数?**

- 避免 C++ 运行时开销（vtable、RTTI、异常处理）
- 支持 C 编译器（许多嵌入式工具链 C++ 支持不完整）
- 函数指针在 bare-metal 中更可控

**为什么 ISR 路径不走 Transport?**

Android 没有 ISR 概念（用户空间不能关中断）。嵌入式 ISR 中:
- 不能阻塞（不能调用 write() / send()）
- 执行时间必须极短
- 所以 ISR 只写 Ring Buffer，Logger Task 负责输出

---

## 附录: 图表索引

| 图表 | 文件 | 说明 |
|------|------|------|
| 分层架构图 | [`embedded_logging_arch.mmd`](embedded_logging_arch.mmd) | 6 层架构总览 |
| 类体系图 | [`embedded_logging_class.mmd`](embedded_logging_class.mmd) | 完整 C/C++ 类与结构体关系 |
| 数据流图 | [`embedded_logging_dataflow.mmd`](embedded_logging_dataflow.mmd) | 端到端数据流 |
| 写入时序图 | [`embedded_logging_write_seq.mmd`](embedded_logging_write_seq.mmd) | 日志写入完整调用链 |
| 读取时序图 | [`embedded_logging_read_seq.mmd`](embedded_logging_read_seq.mmd) | 日志读取循环 |
| 初始化时序图 | [`embedded_logging_init_seq.mmd`](embedded_logging_init_seq.mmd) | elogd 启动序列 |
| 部署场景图 | [`embedded_logging_deployment.mmd`](embedded_logging_deployment.mmd) | Linux vs RTOS 部署对比 |
