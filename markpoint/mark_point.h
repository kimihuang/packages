/**
 * @file mark_point.h
 * @brief 裸机环境轻量级内存打点模块（Header-Only）
 *
 * 在指定的 SRAM 区域记录程序执行路径的关键节点，用于分析程序执行流程。
 *
 * ## 设计目标
 * - SRAM 占用极小：每条记录 4 字节（默认紧凑模式）
 * - 零系统依赖：不依赖 malloc/stdlib/printf
 * - 零开销关闭：MARK_ENABLE 未定义时所有打点宏展开为空操作
 * - 自包含：控制块与数据位于同一段配置内存中，无需额外 .bss 全局变量
 *
 * ## 使用方式
 *
 *   // 1. 在某个 .c 文件中配置并包含
 *   #define MARK_BUF_ADDR      0x20018000   // 打点缓冲区起始地址
 *   #define MARK_BUF_SIZE      512          // 缓冲区总字节数
 *   #define MARK_OVERFLOW_WRAP 1            // 满后回绕覆盖（注释掉则满后停止）
 *   #define MARK_ENABLE                     // 使能打点（注释掉则关闭，零开销）
 *   #include "mark_point.h"
 *
 *   // 2. 在 main() 中初始化
 *   mark_init();
 *
 *   // 3. 在关键路径插入打点
 *   MARK_POINT(0x0001);                          // 程序到达此处
 *   MARK_POINT_VAL(0x0100, adc_value);            // 带附加数据
 *   MARK_POINT_TAG(0x0200, MK_TAG_WARN, err);     // 带级别标签
 *
 * ## 条目格式（紧凑模式，4 字节）
 *   [15:0]  id   — 打点标识，按模块划分（见下文建议）
 *   [15:0]  data — 附加数据（返回值、计数值、时间戳低16位等）
 *
 * ## 内存布局（缓冲区尾部 24 字节为控制块）
 *   +--------------------------------------+
 *   | mark_entry_t[0..N-1]                 |  <- 数据区（只读/写入）
 *   | mark_ctrl_t（magic|idx|max|wrapped）  |  <- 控制块（24B @ buf末尾）
 *   +--------------------------------------+
 *
 * ## Mark ID 分配建议
 *   0x0001~0x00FF : 启动/初始化流程
 *   0x0100~0x01FF : 主循环各阶段
 *   0x0200~0x02FF : 模块A
 *   0x1000~0x1FFF : 中断/异常入口
 *   0xF000~0xFFFF : 错误/异常路径
 *
 * ## 配置宏（在 #include 本文件前定义）
 *   MARK_BUF_ADDR      — 缓冲区起始物理地址（必须，uintptr_t 类型）
 *   MARK_BUF_SIZE      — 缓冲区总字节数（必须，>= 24 + MARK_ENTRY_SIZE）
 *   MARK_ENABLE        — 定义后打点生效（未定义时所有宏为空操作）
 *   MARK_OVERFLOW_WRAP — 定义后缓冲区满时回绕覆盖（默认满后停止）
 */

#ifndef MARK_POINT_H
#define MARK_POINT_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 *  一、可配置参数
 * ======================================================================== */

/* 每个打点条目大小：2=极简, 4=紧凑(默认), 8=扩展 */
#ifndef MARK_ENTRY_SIZE
#define MARK_ENTRY_SIZE 4
#endif

/* 控制块大小（固定 24 字节） */
#define MARK_CTRL_SIZE  24

/* ========================================================================
 *  二、数据结构
 * ======================================================================== */

/**
 * @brief 打点条目结构（trivially-copyable，可 memcpy 到 Flash 保存）
 *
 * MARK_ENTRY_SIZE=2（极简模式）：id(8bit)+data(8bit)，支持 256 个打点
 * MARK_ENTRY_SIZE=4（紧凑模式）：id(16bit)+data(16bit)，支持 65535 个打点
 * MARK_ENTRY_SIZE=8（扩展模式）：id(16bit)+seq(16bit)+ts(32bit)，含序号和时间戳
 */
#if MARK_ENTRY_SIZE == 2
typedef struct {
    uint8_t  id;    /**< 打点标识 0-255 */
    uint8_t  data;  /**< 附加数据 */
} mark_entry_t;

#elif MARK_ENTRY_SIZE == 4
typedef struct {
    uint16_t id;    /**< 打点标识 0-65535 */
    uint16_t data;  /**< 附加数据 / 时间戳低16位 / 返回值 */
} mark_entry_t;

#elif MARK_ENTRY_SIZE == 8
typedef struct {
    uint16_t id;        /**< 打点标识 */
    uint16_t seq;       /**< 自动递增序号 */
    uint32_t timestamp; /**< 时间戳（高精度）*/
} mark_entry_t;

#else
#error "MARK_ENTRY_SIZE must be 2, 4, or 8"
#endif

/**
 * @brief 打点控制块（位于缓冲区末尾 MARK_CTRL_SIZE 字节）
 */
typedef struct {
    uint32_t magic;       /**< 魔数 0x4D41524B ("MARK")，校验控制块有效性 */
    uint32_t write_idx;   /**< 当前写入位置 [0, max_entries) */
    uint32_t max_entries; /**< 最大条目数 = (MARK_BUF_SIZE - 24) / sizeof(mark_entry_t) */
    uint32_t wrapped;     /**< 回绕标志：0=未回绕, 1=已回绕（覆盖部分旧记录后）*/
    uint32_t seq;         /**< 内部递增序号（用于 MARK_POINT 的默认时间戳）*/
    uint32_t stopped;     /**< 停止标志：0=正常, 1=缓冲区满且已停止（仅 OVERFLOW_STOP 模式）*/
} mark_ctrl_t;

#define MARK_MAGIC 0x4D41524Bu  /* "MARK" */

/* ========================================================================
 *  三、内部辅助宏
 * ======================================================================== */

/* 编译期断言：确保缓冲区配置合法 */
#if !defined(MARK_BUF_ADDR) || !defined(MARK_BUF_SIZE)
#error "MARK_BUF_ADDR and MARK_BUF_SIZE must be defined before including mark_point.h"
#endif

#if MARK_BUF_SIZE < (MARK_CTRL_SIZE + MARK_ENTRY_SIZE)
#error "MARK_BUF_SIZE too small: need at least MARK_CTRL_SIZE + MARK_ENTRY_SIZE bytes"
#endif

/* 计算可容纳的最大条目数 */
#define _MARK_MAX_ENTRIES \
    ((MARK_BUF_SIZE - MARK_CTRL_SIZE) / sizeof(mark_entry_t))

/* 缓冲区总条目容量（便于外部使用） */
#define MARK_CAPACITY _MARK_MAX_ENTRIES

/* --------------------------------------------------------------------------
 *  内部辅助：获取数据区与控制块的指针
 *  注意：控制块位于缓冲区末尾 MARK_CTRL_SIZE 字节处
 * -------------------------------------------------------------------------- */
#define _MARK_BUF()  ((mark_entry_t *)(uintptr_t)(MARK_BUF_ADDR))
#define _MARK_CTRL() ((mark_ctrl_t  *)((uintptr_t)(MARK_BUF_ADDR) + MARK_BUF_SIZE - MARK_CTRL_SIZE))

/* ========================================================================
 *  四、核心 API（static inline，编译后内联展开）
 * ======================================================================== */

/**
 * @brief  初始化打点模块（上电后需调用一次）
 *
 * 将整个缓冲区（数据区 + 控制块）清零，写入魔数。
 * 重复调用等价于 mark_reset()。
 *
 * @return true=成功, false=参数/配置错误
 */
static inline bool mark_init(void)
{
    const uint32_t word_count = MARK_BUF_SIZE / 4;
    uint32_t *p = (uint32_t *)(uintptr_t)MARK_BUF_ADDR;

    /* 按字清零（比按字节循环更高效） */
    for (uint32_t i = 0; i < word_count; i++) {
        p[i] = 0;
    }

    /* 写入控制块 */
    mark_ctrl_t *ctrl = _MARK_CTRL();
    ctrl->magic       = MARK_MAGIC;
    ctrl->write_idx   = 0;
    ctrl->max_entries = _MARK_MAX_ENTRIES;
    ctrl->wrapped     = 0;
    ctrl->seq         = 0;
    ctrl->stopped     = 0;

    /* 处理尾部非字对齐字节 */
    const uint32_t remain = MARK_BUF_SIZE % 4;
    if (remain > 0) {
        uint8_t *pb = (uint8_t *)((uintptr_t)MARK_BUF_ADDR + word_count * 4);
        for (uint32_t i = 0; i < remain; i++) {
            pb[i] = 0;
        }
    }

    return true;
}

/**
 * @brief  检查打点模块是否已完成初始化
 * @return true=已初始化, false=未初始化或损坏
 */
static inline bool mark_is_ready(void)
{
    return _MARK_CTRL()->magic == MARK_MAGIC;
}

/**
 * @brief  记录一条打点（内部实现，通常通过宏调用）
 *
 * @param  id   打点标识 0-65535
 * @param  data 附加数据
 *
 * 行为：
 *  - 控制块无效（未初始化/损坏）时静默丢弃
 *  - STOP 模式下缓冲区满后停止写入（设置 stopped 标志）
 *  - WRAP  模式下缓冲区满后覆盖最旧记录（设置 wrapped 标志）
 */
static inline void mark_put(uint16_t id, uint16_t data)
{
    mark_ctrl_t *ctrl = _MARK_CTRL();

    /* 快速路径：控制块校验 */
    if (ctrl->magic != MARK_MAGIC) {
        return;
    }

    /* STOP 模式：已停止则不写入 */
#ifdef MARK_OVERFLOW_WRAP
    (void)0;  /* WRAP 模式：不停止 */
#else
    if (ctrl->stopped) {
        return;
    }
#endif

    uint32_t idx = ctrl->write_idx;

    /* 溢出检查 */
    if (idx >= ctrl->max_entries) {
#ifndef MARK_OVERFLOW_WRAP
        /* STOP 模式：标记停止，返回 */
        ctrl->stopped = 1;
        return;
#else
        /* WRAP 模式：回绕到起始位置，标记已回绕 */
        idx = 0;
        ctrl->wrapped = 1;
#endif
    }

    /* 写入条目 */
    mark_entry_t *buf = _MARK_BUF();
#if MARK_ENTRY_SIZE == 2
    buf[idx].id   = (uint8_t)(id & 0xFF);
    buf[idx].data = (uint8_t)(data & 0xFF);
#elif MARK_ENTRY_SIZE == 4
    buf[idx].id   = id;
    buf[idx].data = data;
#else /* MARK_ENTRY_SIZE == 8 */
    buf[idx].id        = id;
    buf[idx].seq       = (uint16_t)(ctrl->seq & 0xFFFF);
    buf[idx].timestamp = (uint32_t)data;  /* data 作为时间戳传入 */
#endif

    ctrl->write_idx = idx + 1;
    ctrl->seq++;  /* 序号递增（无论哪种模式） */
}

/**
 * @brief  以标签模式记录打点（id 高4位为标签，低12位为子ID）
 *
 * 标签编码放在 id 的高 4 位，配合 MARK_POINT_TAG 宏使用：
 *   bit[15:12] = 标签（INFO/WARN/ERR/MARK 等）
 *   bit[11:0]  = 子 ID（4096 种）
 */
enum {
    MK_TAG_MARK  = 0x0,  /**< 一般标记点 */
    MK_TAG_INFO  = 0x1,  /**< 信息 */
    MK_TAG_WARN  = 0x2,  /**< 警告 */
    MK_TAG_ERROR = 0x3,  /**< 错误 */
    MK_TAG_ENTER = 0x4,  /**< 函数进入 */
    MK_TAG_EXIT  = 0x5,  /**< 函数退出 */
    MK_TAG_IRQ   = 0x6,  /**< 中断入口 */
};

/**
 * @brief  获取当前已记录的有效条目总数
 * @return 条目数，未初始化时返回 0
 */
static inline uint32_t mark_count(void)
{
    mark_ctrl_t *ctrl = _MARK_CTRL();
    if (ctrl->magic != MARK_MAGIC) {
        return 0;
    }

    if (ctrl->wrapped) {
        return ctrl->max_entries;  /* 已回绕，缓冲区满 */
    }
    return ctrl->write_idx;  /* 未回绕，已写入条目数 */
}

/**
 * @brief  获取下次写入的索引位置（可用于扩展功能）
 * @return 下一次 write_idx，未初始化时返回 0
 */
static inline uint32_t mark_write_idx(void)
{
    mark_ctrl_t *ctrl = _MARK_CTRL();
    if (ctrl->magic != MARK_MAGIC) {
        return 0;
    }
    return ctrl->write_idx;
}

/**
 * @brief  获取内部递增序号
 * @return 当前序号值
 */
static inline uint32_t mark_seq_get(void)
{
    mark_ctrl_t *ctrl = _MARK_CTRL();
    if (ctrl->magic != MARK_MAGIC) {
        return 0;
    }
    return ctrl->seq;
}

/**
 * @brief  读取第 index 个条目（按时间顺序，0 为最旧）
 *
 * @param  index  条目索引 [0, mark_count()-1]
 * @param  entry  输出：条目内容（调用者提供存储空间）
 * @return true=成功, false=索引越界或模块未初始化
 *
 * 逻辑：
 *  - 未回绕：buf[0] 最旧，buf[write_idx-1] 最新
 *  - 已回绕：buf[write_idx] 最旧，buf[(write_idx-1) % max] 最新
 *            其中 write_idx 就是最旧条目的位置（即将被覆盖的位置）
 */
static inline bool mark_get(uint32_t index, mark_entry_t *entry)
{
    if (!entry) {
        return false;
    }

    mark_ctrl_t *ctrl = _MARK_CTRL();
    if (ctrl->magic != MARK_MAGIC) {
        return false;
    }

    uint32_t total = mark_count();
    if (index >= total) {
        return false;
    }

    uint32_t physical_idx;
    if (ctrl->wrapped) {
        /* 回绕模式：最旧条目在 write_idx 处 */
        physical_idx = (ctrl->write_idx + index) % ctrl->max_entries;
    } else {
        /* 线性模式：最旧条目在 0 处 */
        physical_idx = index;
    }

    mark_entry_t *buf = _MARK_BUF();
    *entry = buf[physical_idx];
    return true;
}

/**
 * @brief  获取最新的条目
 * @param  entry  输出：最新条目内容
 * @return true=成功, false=无记录或未初始化
 */
static inline bool mark_last(mark_entry_t *entry)
{
    uint32_t total = mark_count();
    if (total == 0) {
        return false;
    }
    return mark_get(total - 1, entry);
}

/**
 * @brief  重置打点缓冲区（等同于重新初始化）
 */
static inline void mark_reset(void)
{
    mark_init();
}

/**
 * @brief  检查是否发生过溢出
 * @return true=已溢出(wrapped 或 stopped), false=未溢出
 */
static inline bool mark_is_overflow(void)
{
    mark_ctrl_t *ctrl = _MARK_CTRL();
    if (ctrl->magic != MARK_MAGIC) {
        return false;
    }
#ifdef MARK_OVERFLOW_WRAP
    return ctrl->wrapped != 0;
#else
    return ctrl->stopped != 0;
#endif
}

/* ========================================================================
 *  五、用户打点宏
 * ======================================================================== */

#ifdef MARK_ENABLE

/** 记录一个打点（无附加数据，data=0） */
#define MARK_POINT(id)              mark_put((uint16_t)(id), 0)

/** 记录一个打点，附带 16-bit 数据 */
#define MARK_POINT_VAL(id, val)     mark_put((uint16_t)(id), (uint16_t)(val))

/** 记录一个打点，附带标签（id 的高 4 位被标签覆盖） */
#define MARK_POINT_TAG(tag, sub_id, val) \
    mark_put((uint16_t)(((tag) << 12) | ((sub_id) & 0xFFF)), (uint16_t)(val))

/** 记录函数进入（id = tag<<12 | line_number&0xFFF, data = 0） */
#define MARK_ENTER() \
    mark_put((uint16_t)((MK_TAG_ENTER << 12) | (__LINE__ & 0xFFF)), 0)

/** 记录函数退出（id = tag<<12 | line_number&0xFFF, data = ret_val） */
#define MARK_EXIT(ret) \
    mark_put((uint16_t)((MK_TAG_EXIT << 12) | (__LINE__ & 0xFFF)), (uint16_t)(ret))

#else  /* !MARK_ENABLE — 零开销：所有打点宏展开为空 */

#define MARK_POINT(id)              ((void)0)
#define MARK_POINT_VAL(id, val)     ((void)0)
#define MARK_POINT_TAG(tag, id, val) ((void)0)
#define MARK_ENTER()                ((void)0)
#define MARK_EXIT(ret)              ((void)0)

#endif /* MARK_ENABLE */

/* ========================================================================
 *  六、调试输出（可选，需用户提供输出原语）
 * ======================================================================== */

/**
 * 方式一：用户提供 printf，定义 MARK_DUMP_PRINTF 宏
 *
 *   #define MARK_DUMP_PRINTF
 *   #include "mark_point.h"
 *   mark_dump();  // 使用标准 printf 输出
 */

/**
 * 方式二：用户提供 putchar 函数，定义 MARK_DUMP_PUTCHAR 宏
 *
 *   int my_putchar(int c) { uart_send_byte(c); return c; }
 *   #define MARK_DUMP_PUTCHAR my_putchar
 *   #include "mark_point.h"
 *   mark_dump();      // 输出全部打点
 *   mark_dump_last(N); // 输出最近 N 条
 */

/**
 * 方式三：用户提供 puts 函数，定义 MARK_DUMP_PUTS 宏（内存最小）
 *
 *   void uart_puts(const char *s) { while(*s) uart_send_byte(*s++); }
 *   #define MARK_DUMP_PUTS uart_puts
 *   #include "mark_point.h"
 *   mark_dump();
 */

/* -------- 方式一：基于 printf 的输出 -------- */
#ifdef MARK_DUMP_PRINTF
#include <stdio.h>

static inline void mark_dump(void)
{
    mark_ctrl_t *ctrl = _MARK_CTRL();
    printf("--- Mark Dump (%lu entries, cap=%lu) ---\r\n",
           (unsigned long)mark_count(), (unsigned long)_MARK_MAX_ENTRIES);

    if (ctrl->stopped) printf("[OVERFLOW: stopped]\r\n");
    if (ctrl->wrapped)  printf("[OVERFLOW: wrapped]\r\n");

    uint32_t total = mark_count();
    for (uint32_t i = 0; i < total; i++) {
        mark_entry_t e;
        if (mark_get(i, &e)) {
#if MARK_ENTRY_SIZE == 2
            printf("  [%4lu] id=0x%02X data=0x%02X\r\n",
                   (unsigned long)i, e.id, e.data);
#elif MARK_ENTRY_SIZE == 4
            printf("  [%4lu] id=0x%04X data=0x%04X\r\n",
                   (unsigned long)i, e.id, e.data);
#else
            printf("  [%4lu] id=0x%04X seq=%u ts=%lu\r\n",
                   (unsigned long)i, e.id, e.seq, (unsigned long)e.timestamp);
#endif
        }
    }
    printf("--- End (%lu entries) ---\r\n", (unsigned long)total);
}

static inline void mark_dump_last(uint32_t n)
{
    uint32_t total = mark_count();
    if (n > total) n = total;
    uint32_t start = total - n;

    for (uint32_t i = start; i < total; i++) {
        mark_entry_t e;
        if (mark_get(i, &e)) {
#if MARK_ENTRY_SIZE <= 4
            printf("  [%4lu] id=0x%04X data=0x%04X\r\n",
                   (unsigned long)i, e.id, e.data);
#else
            printf("  [%4lu] id=0x%04X seq=%u ts=%lu\r\n",
                   (unsigned long)i, e.id, e.seq, (unsigned long)e.timestamp);
#endif
        }
    }
}

#endif /* MARK_DUMP_PRINTF */

/* -------- 方式二：基于 putchar 的输出（无 stdio 依赖）-------- */
#ifdef MARK_DUMP_PUTCHAR

/* 内部辅助：输出一个十六进制半字节 */
static inline void _mp_print_hex4(uint8_t v)
{
    v &= 0x0F;
    MARK_DUMP_PUTCHAR((v < 10) ? ('0' + v) : ('A' + v - 10));
}

/* 内部辅助：输出 16-bit 十六进制 */
static inline void _mp_print_hex16(uint16_t v)
{
    _mp_print_hex4((uint8_t)(v >> 12));
    _mp_print_hex4((uint8_t)(v >> 8));
    _mp_print_hex4((uint8_t)(v >> 4));
    _mp_print_hex4((uint8_t)(v));
}

/* 内部辅助：输出 32-bit 十进制（0-4294967295） */
static inline void _mp_print_dec32(uint32_t v)
{
    char buf[11];
    int pos = 0;
    if (v == 0) {
        MARK_DUMP_PUTCHAR('0');
        return;
    }
    while (v > 0) {
        buf[pos++] = '0' + (v % 10);
        v /= 10;
    }
    while (pos > 0) {
        MARK_DUMP_PUTCHAR(buf[--pos]);
    }
}

/* 内部辅助：输出字符串 */
static inline void _mp_print_str(const char *s)
{
    while (*s) {
        MARK_DUMP_PUTCHAR(*s++);
    }
}

/**
 * @brief 输出全部打点记录（putchar 版本，无 printf 依赖）
 */
static inline void mark_dump(void)
{
    _mp_print_str("--- Mark Dump ---\r\n");

    uint32_t total = mark_count();
    for (uint32_t i = 0; i < total; i++) {
        mark_entry_t e;
        if (mark_get(i, &e)) {
            _mp_print_str("  [");
            _mp_print_dec32(i);
            _mp_print_str("] id=0x");
#if MARK_ENTRY_SIZE == 2
            _mp_print_hex4(e.id >> 4);
            _mp_print_hex4(e.id);
            _mp_print_str(" data=0x");
            _mp_print_hex4(e.data >> 4);
            _mp_print_hex4(e.data);
#else
            _mp_print_hex16(e.id);
            _mp_print_str(" data=0x");
            _mp_print_hex16(e.data);
#endif
            _mp_print_str("\r\n");
        }
    }
    _mp_print_str("--- End (");
    _mp_print_dec32(total);
    _mp_print_str(" entries) ---\r\n");
}

static inline void mark_dump_last(uint32_t n)
{
    uint32_t total = mark_count();
    if (n > total) n = total;
    uint32_t start = total - n;

    for (uint32_t i = start; i < total; i++) {
        mark_entry_t e;
        if (mark_get(i, &e)) {
            _mp_print_str("  [");
            _mp_print_dec32(i);
            _mp_print_str("] id=0x");
#if MARK_ENTRY_SIZE == 2
            _mp_print_hex4(e.id >> 4);
            _mp_print_hex4(e.id);
            _mp_print_str(" data=0x");
            _mp_print_hex4(e.data >> 4);
            _mp_print_hex4(e.data);
#else
            _mp_print_hex16(e.id);
            _mp_print_str(" data=0x");
            _mp_print_hex16(e.data);
#endif
            _mp_print_str("\r\n");
        }
    }
}

#endif /* MARK_DUMP_PUTCHAR */

/* -------- 方式三：基于 puts 的输出（内存占用最小）-------- */
#ifdef MARK_DUMP_PUTS

static inline void _mp_puts_hex16(uint16_t v, char *buf)
{
    static const char hex[] = "0123456789ABCDEF";
    buf[0] = '0'; buf[1] = 'x';
    buf[2] = hex[(v >> 12) & 0xF];
    buf[3] = hex[(v >>  8) & 0xF];
    buf[4] = hex[(v >>  4) & 0xF];
    buf[5] = hex[(v >>  0) & 0xF];
    buf[6] = '\0';
}

static inline void _mp_puts_dec32(uint32_t v, char *buf)
{
    char tmp[11];
    int pos = 0;
    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    while (v) { tmp[pos++] = '0' + (v % 10); v /= 10; }
    int i;
    for (i = 0; i < pos; i++) buf[i] = tmp[pos - 1 - i];
    buf[pos] = '\0';
}

static inline void mark_dump(void)
{
    char buf_id[8], buf_data[8], buf_idx[12];
    MARK_DUMP_PUTS("--- Mark Dump ---\r\n");

    uint32_t total = mark_count();
    for (uint32_t i = 0; i < total; i++) {
        mark_entry_t e;
        if (mark_get(i, &e)) {
            _mp_puts_hex16(e.id, buf_id);
            _mp_puts_hex16(e.data, buf_data);
            _mp_puts_dec32(i, buf_idx);
            MARK_DUMP_PUTS("  [");
            MARK_DUMP_PUTS(buf_idx);
            MARK_DUMP_PUTS("] id=");
            MARK_DUMP_PUTS(buf_id);
            MARK_DUMP_PUTS(" data=");
            MARK_DUMP_PUTS(buf_data);
            MARK_DUMP_PUTS("\r\n");
        }
    }
    MARK_DUMP_PUTS("--- End ---\r\n");
}

static inline void mark_dump_last(uint32_t n)
{
    char buf_id[8], buf_data[8], buf_idx[12];
    uint32_t total = mark_count();
    if (n > total) n = total;
    uint32_t start = total - n;

    for (uint32_t i = start; i < total; i++) {
        mark_entry_t e;
        if (mark_get(i, &e)) {
            _mp_puts_hex16(e.id, buf_id);
            _mp_puts_hex16(e.data, buf_data);
            _mp_puts_dec32(i, buf_idx);
            MARK_DUMP_PUTS("  [");
            MARK_DUMP_PUTS(buf_idx);
            MARK_DUMP_PUTS("] id=");
            MARK_DUMP_PUTS(buf_id);
            MARK_DUMP_PUTS(" data=");
            MARK_DUMP_PUTS(buf_data);
            MARK_DUMP_PUTS("\r\n");
        }
    }
}

#endif /* MARK_DUMP_PUTS */

#endif /* MARK_POINT_H */
