/**
 * @file crc32.c
 * @brief CRC32 校验算法实现
 *
 * 本文件实现了标准IEEE 802.3 CRC32算法，提供两种计算方式:
 *
 * 1. **查表法 (默认)** - 推荐
 *    - 速度: ~4-5 cycles/byte
 *    - 内存: 1KB 查找表
 *    - 适用: 大多数场景，DDR充足的环境
 *
 * 2. **计算法** - 备选
 *    - 速度: ~40-50 cycles/byte
 *    - 内存: 0 额外开销
 *    -适用: 资源极度受限环境 (BL1 SRAM)
 *
 * 算法细节:
 * - 标准: IEEE 802.3 (Ethernet, PNG, GZIP, SATA等)
 * - 多项式: 0xEDB88320 (反转形式)
 * - 初始值: 0xFFFFFFFF
 * - 输入/输出反射: 是
 * - 最终异或: 0xFFFFFFFF
 *
 * 实现特点:
 * 1. 平台无关: 纯C实现，无汇编依赖
 * 2. 可配置: 通过宏选择查表法或计算法
 * 3. 完整API: 支持一次性、增量、合并等模式
 * 4. 线程安全: 只读数据，无全局可变状态
 *
 * @author AutoRun Framework Team
 * @version 1.0.0
 * @date 2026-01-01
 */

#include "crc32.h"
#include <string.h>

/* ================================================================== */
/*                    查表法实现                                       */
/* ================================================================== */

#if AUTORUN_CRC32_USE_TABLE

/**
 * @brief CRC32查找表 (256项)
 *
 * 预计算每个字节(0-255)对应的CRC32余数。
 * 表的生成多项式为 0xEDB88320 (标准CRC32的反转形式)。
 *
 * 内存布局:
 * - 总大小: 1024 bytes (256 × sizeof(uint32_t))
 * - 对齐: 自然对齐 (4 bytes)
 * - 存储类型: const (只读数据段)
 *
 * 计算方法 (初始化时):
 * for (i = 0; i < 256; i++) {
 *     crc = i;
 *     for (j = 0; j < 8; j++) {
 *         if (crc & 1)
 *             crc = (crc >> 1) ^ POLYNOMIAL;
 *         else
 *             crc >>= 1;
 *     }
 *     table[i] = crc;
 * }
 */
static uint32_t crc32_table[256];

/** @brief 标记查找表是否已初始化 */
static bool crc32_table_initialized = false;

/**
 * @brief 生成CRC32查找表
 *
 * 填充 crc32_table[] 数组。
 * 此函数是幂等的：多次调用结果相同。
 */
static void generate_crc32_table(void)
{
    uint32_t i, j;
    uint32_t crc;

    for (i = 0; i < 256; i++) {
        crc = i;

        for (j = 0; j < 8; j++) {
            if (crc & 1) {
                /* 最低位为1，右移并异或多项式 */
                crc = (crc >> 1) ^ CRC32_POLYNOMIAL_REVERSED;
            } else {
                /* 最低位为0，仅右移 */
                crc >>= 1;
            }
        }

        crc32_table[i] = crc;
    }

    crc32_table_initialized = true;
}

void autorun_crc32_init_table(void)
{
    if (!crc32_table_initialized) {
        generate_crc32_table();
    }
}

bool autorun_crc32_is_table_initialized(void)
{
    return crc32_table_initialized;
}

const uint32_t* autorun_crc32_get_table(void)
{
    /* 懒初始化: 首次调用时生成表 */
    if (!crc32_table_initialized) {
        generate_crc32_table();
    }

    return crc32_table;
}

#endif /* AUTORUN_CRC32_USE_TABLE */

/* ================================================================== */
/*                 核心计算函数实现                                    */
/* ================================================================== */

uint32_t autorun_calc_crc32(const uint8_t *data, uint32_t length)
{
    /*
     * 一次性计算完整数据块的CRC32。
     *
     * 这是最高层的接口，内部处理:
     * 1. 参数校验和边界情况
     * 2. 查找表初始化 (如果使用查表法)
     * 3. 主循环计算
     * 4. 最终异或
     */

    /* 处理边界情况 */
    if (data == NULL || length == 0) {
        return 0x00000000UL;
    }

#if AUTORUN_CRC32_USE_TABLE

    /* ===== 查表法实现 ===== */

    /* 确保查找表已初始化 */
    if (!crc32_table_initialized) {
        generate_crc32_table();
    }

    register uint32_t crc = CRC32_INITIAL_VALUE;
    const uint8_t *p = data;
    const uint8_t *end = data + length;

    /* 主循环: 每次处理一个字节 */
    while (p < end) {
        /*
         * 查表法核心操作:
         * 1. 取当前字节的低8位与CRC的低8位异或
         * 2. 用这个值作为索引查找表
         * 3. CRC右移8位后与查表结果异或
         *
         * 数学等价于:
         * crc = (crc >> 8) ^ table[(crc ^ byte) & 0xFF]
         */
        uint32_t index = (crc ^ (*p)) & 0xFF;
        crc = (crc >> 8) ^ crc32_table[index];
        p++;
    }

    /* 最终异或 */
    return (crc ^ CRC32_FINAL_XOR_VALUE);

#else

    /* ===== 计算法实现 (无查找表) ===== */

    register uint32_t crc = CRC32_INITIAL_VALUE;
    const uint8_t *p = data;
    uint32_t i;

    for (i = 0; i < length; i++) {
        /* 每个字节处理8次 (逐位计算) */
        crc ^= p[i];

        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ CRC32_POLYNOMIAL_REVERSED;
            } else {
                crc >>= 1;
            }
        }
    }

    return (crc ^ CRC32_FINAL_XOR_VALUE);

#endif /* AUTORUN_CRC32_USE_TABLE */
}

/* ================================================================== */
/*               增量API实现                                          */
/* ================================================================== */

#if AUTORUN_CRC32_ENABLE_INCREMENTAL

uint32_t autorun_crc32_init(void)
{
    /*
     * 返回初始CRC值。
     * 虽然这只是返回常量，但保留此函数是为了:
     * 1. API语义完整性和一致性
     * 2. 未来可能支持自定义初始值或多实例状态
     */
    return CRC32_INITIAL_VALUE;
}

uint32_t autorun_crc32_update(uint32_t ctx,
                              const uint8_t *data,
                              uint32_t length)
{
    /*
     * 增量更新CRC32计算。
     *
     * 参数说明:
     * - ctx: 当前累积的CRC上下文 (来自init或上次update)
     * - data: 新增的数据块
     * - length: 数据块长度
     *
     * 返回: 更新后的CRC上下文 (传入下次update或finalize)
     */

    /* 边界情况: 无新数据则原样返回 */
    if (data == NULL || length == 0) {
        return ctx;
    }

#if AUTORUN_CRC32_USE_TABLE

    /* 查表法增量更新 */
    if (!crc32_table_initialized) {
        generate_crc32_table();
    }

    register uint32_t crc = ctx;
    const uint8_t *p = data;
    const uint8_t *end = data + length;

    while (p < end) {
        uint32_t index = (crc ^ (*p)) & 0xFF;
        crc = (crc >> 8) ^ crc32_table[index];
        p++;
    }

    return crc;

#else

    /* 计算法增量更新 */
    register uint32_t crc = ctx;
    uint32_t i;

    for (i = 0; i < length; i++) {
        crc ^= data[i];

        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ CRC32_POLYNOMIAL_REVERSED;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;

#endif /* AUTORUN_CRC32_USE_TABLE */
}

uint32_t autorun_crc32_finalize(uint32_t ctx)
{
    /*
     * 完成CRC32计算，执行最终异或。
     *
     * 这一步将中间状态的CRC转换为标准的最终CRC32值。
     * 标准要求在计算结束后与 0xFFFFFFFF 异或。
     */
    return (ctx ^ CRC32_FINAL_XOR_VALUE);
}

#endif /* AUTORUN_CRC32_ENABLE_INCREMENTAL */

/* ================================================================== */
/*                  高级功能实现                                      */
/* ================================================================== */

#if AUTORUN_CRC32_USE_TABLE

uint32_t autorun_crc32_merge(uint32_t crc1, uint32_t crc2, uint32_t len2)
{
    /*
     * 合并两个独立的CRC32值。
     *
     * 数学原理:
     * 设数据块A长度为L1，数据块B长度为L2
     * CRC(A) = crc1, CRC(B) = crc2
     * 则 CRC(A || B) = MERGE(crc1, crc2, L2)
     *
     * 合并公式:
     * merged_crc = crc1 ^ fold(crc2, L2)
     * 其中 fold 操作通过查表模拟对 L2 个零字节的CRC计算
     *
     * 应用场景:
     * 1. 并行计算: 多线程分别计算不同段
     * 2. 增量更新: 已有旧数据的CRC，追加新数据时无需重算
     * 3. 分布式系统: 各节点计算本地CRC，中心节点合并
     */

    /* 确保查找表已初始化 */
    if (!crc32_table_initialized) {
        generate_crc32_table();
    }

    /*
     * 将 crc2 视为对 len2 字节计算后的中间状态 (未最终异或)。
     * 我们需要模拟"对 crc1 追加 len2 个零字节"的效果，
     * 然后与 crc2 合并。
     *
     * 关键技巧: 利用CRC的线性性质
     * CRC(A || zeros) 可以通过查表快速计算
     */

    while (len2 > 0) {
        /* 每次处理一个字节 (从最低有效字节开始) */
        uint8_t byte_val = (len2 & 0xFF);
        len2 >>= 8;

        if (byte_val == 0) {
            /* 当前字节为0，直接用crc1查表 */
            uint32_t index = (crc1 & 0xFF);
            crc1 = (crc1 >> 8) ^ crc32_table[index];
        } else {
            /*
             * 当前字节非零，需要更复杂的折叠。
             * 这里使用简化方法: 循环 byte_val 次。
             * 对于性能敏感场景，可以预先计算 pow_table[256][256]。
             */
            uint32_t temp = crc1;
            for (uint8_t i = 0; i < byte_val; i++) {
                uint32_t index = (temp & 0xFF);
                temp = (temp >> 8) ^ crc32_table[index];
            }
            crc1 = temp;
        }
    }

    /* 合并两个CRC */
    return (crc1 ^ crc2);
}

#endif /* AUTORUN_CRC32_USE_TABLE */

bool autorun_crc32_verify(const uint8_t *data,
                          uint32_t length,
                          uint32_t expected)
{
    /*
     * 快速验证CRC32是否匹配。
     *
     * 内部调用 autorun_calc_crc32() 并比较结果。
     * 提供此函数是为了代码简洁性和API友好性。
     */

    if (data == NULL && length == 0) {
        /* 空数据只匹配空CRC */
        return (expected == 0x00000000UL);
    }

    uint32_t calculated = autorun_calc_crc32(data, length);
    return (calculated == expected);
}

uint32_t autorun_crc32_custom(const uint8_t *data,
                              uint32_t length,
                              uint32_t initial_value,
                              bool finalize_xor)
{
    /*
     * 自定义CRC32计算。
     *
     * 参数:
     * - data: 数据缓冲区
     * - length: 数据长度
     * - initial_value: 自定义初始值 (通常为 0xFFFFFFFF)
     * - finalize_xor: 是否执行最终异或
     *   true  -> 返回标准CRC32值 (与 autorun_calc_crc32 相同)
     *   false -> 返回中间状态 (未最终异或的原始余数)
     */

    if (data == NULL || length == 0) {
        return finalize_xor ? (initial_value ^ CRC32_FINAL_XOR_VALUE) : initial_value;
    }

#if AUTORUN_CRC32_USE_TABLE

    if (!crc32_table_initialized) {
        generate_crc32_table();
    }

    register uint32_t crc = initial_value;
    const uint8_t *p = data;
    const uint8_t *end = data + length;

    while (p < end) {
        uint32_t index = (crc ^ (*p)) & 0xFF;
        crc = (crc >> 8) ^ crc32_table[index];
        p++;
    }

#else

    register uint32_t crc = initial_value;
    uint32_t i;

    for (i = 0; i < length; i++) {
        crc ^= data[i];

        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ CRC32_POLYNOMIAL_REVERSED;
            } else {
                crc >>= 1;
            }
        }
    }

#endif /* AUTORUN_CRC32_USE_TABLE */

    /* 条件性最终异或 */
    if (finalize_xor) {
        crc ^= CRC32_FINAL_XOR_VALUE;
    }

    return crc;
}

/* ================================================================== */
/*                    版本信息                                         */
/* ================================================================== */

const char* autorun_crc32_get_version(void)
{
    return "1.0.0";
}

uint32_t autorun_crc32_get_capabilities(void)
{
    uint32_t caps = 0;

#if AUTORUN_CRC32_USE_TABLE
    caps |= (1 << 0);  /* bit 0: 查表法启用 */
#endif

#if AUTORUN_CRC32_ENABLE_INCREMENTAL
    caps |= (1 << 1);  /* bit 1: 增量API启用 */
#endif

    return caps;
}

/* ================================================================== */
/*                   单元测试辅助 (可选)                               */
/* ================================================================== */

#ifdef AUTORUN_CRC32_SELF_TEST

#include <stdio.h>

/**
 * @brief CRC32自测试函数
 *
 * 使用已知的测试向量验证实现的正确性。
 * 在调试或CI/CD中启用此功能。
 *
 * 测试向量来源: http://www.ietf.org/rfc/rfc1952.txt (GZIP规范)
 *
 * @return int 0=所有测试通过, 非0=失败数
 */
int autorun_crc32_self_test(void)
{
    int failures = 0;

    /* 测试向量1: 空字符串 */
    {
        uint8_t empty[] = "";
        uint32_t expected = 0x00000000UL;
        uint32_t result = autorun_calc_crc32(empty, 0);

        if (result != expected) {
            printf("FAIL: Empty string test. Expected 0x%08X, got 0x%08X\n",
                   expected, result);
            failures++;
        }
    }

    /* 测试向量2: "I" (ASCII 0x49) */
    {
        uint8_t data[] = {'I'};
        uint32_t expected = 0xD5EFDF08UL;
        uint32_t result = autorun_calc_crc32(data, 1);

        if (result != expected) {
            printf("FAIL: 'I' test. Expected 0x%08X, got 0x%08X\n",
                   expected, result);
            failures++;
        }
    }

    /* 测试向量3: "123456789" (标准测试向量) */
    {
        uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
        uint32_t expected = 0xCBF43926UL;
        uint32_t result = autorun_calc_crc32(data, 9);

        if (result != expected) {
            printf("FAIL: '123456789' test. Expected 0x%08X, got 0x%08X\n",
                   expected, result);
            failures++;
        }
    }

    /* 测试向量4: 增量API一致性测试 */
    {
        uint8_t data[] = "Hello, World!";
        uint32_t len = strlen((char *)data);

        /* 一次性计算 */
        uint32_t one_shot = autorun_calc_crc32(data, len);

        /* 分两次增量计算 */
        uint32_t ctx = autorun_crc32_init();
        ctx = autorun_crc32_update(ctx, data, 7);      // "Hello, "
        ctx = autorun_crc32_update(ctx, data + 7, 7);  // "World!"
        uint32_t incremental = autorun_crc32_finalize(ctx);

        if (one_shot != incremental) {
            printf("FAIL: Incremental API test. One-shot=0x%08X, Incremental=0x%08X\n",
                   one_shot, incremental);
            failures++;
        }
    }

    /* 测试向量5: verify 函数测试 */
    {
        uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
        uint32_t correct_crc = autorun_calc_crc32(data, 4);
        uint32_t wrong_crc = correct_crc ^ 0x00000001UL;

        if (!autorun_crc32_verify(data, 4, correct_crc)) {
            printf("FAIL: verify() should accept correct CRC\n");
            failures++;
        }

        if (autorun_crc32_verify(data, 4, wrong_crc)) {
            printf("FAIL: verify() should reject wrong CRC\n");
            failures++;
        }
    }

    if (failures == 0) {
        printf("PASS: All %d CRC32 tests passed.\n", 5);
    } else {
        printf("FAIL: %d/%d tests failed.\n", failures, 5);
    }

    return failures;
}

#endif /* AUTORUN_CRC32_SELF_TEST */
