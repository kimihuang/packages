/**
 * @file crc32.h
 * @brief CRC32 校验算法头文件
 *
 * 本文件定义了CRC32校验和计算的接口，用于：
 * - 配置数据完整性验证 (autorun_config_t.crc32)
 * - 数据传输错误检测
 * - 文件/内存块校验
 *
 * 算法标准: IEEE 802.3 (Ethernet, ZIP, GZIP, PNG, SATA等使用)
 * - 多项式 (Normal):    0x04C11DB7
 * - 多项式 (Reversed):  0xEDB88320
 * - 初始值:             0xFFFFFFFF
 * - 输入反转:           是
 * - 输出反转:           是
 * - 最终异或:           0xFFFFFFFF
 *
 * 实现方式:
 * - 查表法 (默认): 速度快，占用1KB查找表，适合大多数场景
 * - 计算法:        无额外内存占用，速度较慢，适合资源受限环境(BL1)
 *
 * 使用示例:
 * @code
 *   // 计算数据块的CRC32
 *   uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
 *   uint32_t crc = autorun_calc_crc32(data, sizeof(data));
 *
 *   // 增量计算 (分多次调用)
 *   uint32_t ctx = autorun_crc32_init();
 *   ctx = autorun_crc32_update(ctx, data1, len1);
 *   ctx = autorun_crc32_update(ctx, data2, len2);
 *   uint32_t final_crc = autorun_crc32_finalize(ctx);
 * @endcode
 *
 * @author AutoRun Framework Team
 * @version 1.0.0
 * @date 2026-01-01
 */

#ifndef CRC32_H
#define CRC32_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* ================================================================== */
/*                    编译配置宏                                      */
/* ================================================================== */

/**
 * @brief 启用查表法实现
 *
 * 定义后使用查找表计算CRC32，速度较快但占用1KB (256 * 4 bytes) 内存。
 * 未定义则使用纯计算法，无额外内存但速度慢约10倍。
 *
 * 默认启用查表法。
 */
#ifndef AUTORUN_CRC32_USE_TABLE
#define AUTORUN_CRC32_USE_TABLE   1
#endif

/**
 * @brief 启用增量API
 *
 * 定义后提供 init/update/finalize 接口，
 * 支持分多次计算大块数据的CRC32（流式处理）。
 *
 * 默认启用。
 */
#ifndef AUTORUN_CRC32_ENABLE_INCREMENTAL
#define AUTORUN_CRC32_ENABLE_INCREMENTAL  1
#endif

/* ================================================================== */
/*                    常量定义                                        */
/* ================================================================== */

/**
 * @brief CRC32多项式 (正常顺序)
 *
 * 标准IEEE 802.3多项式: x³² + x²⁶ + x²³ + x²² + x¹⁶ + x¹² + x¹¹ + x¹⁰ + x⁸ + x⁷ + x⁵ + x⁴ + x² + x + 1
 * 十六进制表示: 0x04C11DB7
 */
#define CRC32_POLYNOMIAL_NORMAL     0x04C11DB7UL

/**
 * @brief CRC32多项式 (位反转顺序)
 *
 * 用于查表法和位运算实现的反转形式。
 * 是 CRC32_POLYNOMIAL_NORMAL 的位反转结果。
 */
#define CRC32_POLYNOMIAL_REVERSED   0xEDB88320UL

/**
 * @brief CRC32初始值
 *
 * 所有CRC32计算开始前寄存器的初始值。
 */
#define CRC32_INITIAL_VALUE         0xFFFFFFFFUL

/**
 * @brief CRC32最终异或值
 *
 * 计算完成后与结果进行XOR的值。
 */
#define CRC32_FINAL_XOR_VALUE       0xFFFFFFFFUL

/* ================================================================== */
/*                   核心API函数声明                                   */
/* ================================================================== */

/**
 * @brief 计算数据块的CRC32校验值 (一次性接口)
 *
 * 这是最常用的接口，一次性完成整个数据块的CRC32计算。
 * 内部会自动处理初始化、更新和最终化的完整流程。
 *
 * @param[in] data   数据缓冲区指针 (不能为NULL)
 * @param[in] length 数据长度 (bytes), 可以为0
 *
 * @return uint32_t CRC32校验值
 *
 * @note 特殊情况:
 *       - data == NULL 或 length == 0: 返回 0x00000000
 *       - 空字符串 (""): 返回 0x00000000
 *
 * @warning 此函数不维护内部状态，每次调用都是独立的计算。
 *         对于需要增量计算的场景，请使用 init/update/finalize 接口。
 *
 * 性能:
 * - 查表法: 约4-5 clock cycles/byte (ARM Cortex-A系列)
 * - 计算法: 约40-50 clock cycles/byte
 *
 * 示例:
 * @code
 *   uint8_t config_data[1024];
 *   // ... 填充数据 ...
 *
 *   uint32_t crc = autorun_calc_crc32(config_data, sizeof(config_data));
 *   printf("CRC32 = 0x%08X\n", crc);
 * @endcode
 */
uint32_t autorun_calc_crc32(const uint8_t *data, uint32_t length);

#if AUTORUN_CRC32_ENABLE_INCREMENTAL

/**
 * @brief 初始化CRC32计算上下文
 *
 * 创建一个新的CRC32计算实例，返回初始状态值。
 * 必须与 autorun_crc32_update() 和 autorun_crc32_finalize() 配合使用。
 *
 * @return uint32_t 初始CRC32上下文值 (始终为 CRC32_INITIAL_VALUE)
 *
 * @note 此函数实际上只返回常量值，保留它是为了:
 *       1. API完整性和语义清晰
 *       2. 未来可能扩展为支持自定义初始值或多实例
 *       3. 与其他哈希算法的接口风格保持一致
 *
 * 示例:
 * @code
 *   uint32_t crc_ctx = autorun_crc32_init();
 * @endcode
 */
uint32_t autorun_crc32_init(void);

/**
 * @brief 更新CRC32计算 (增量处理数据块)
 *
 * 将新的数据块加入CRC32计算，可以多次调用来处理分段的数据。
 *
 * @param[in] ctx    当前CRC32上下文值 (来自init或上一次update的返回值)
 * @param[in] data   新的数据缓冲区指针 (不能为NULL)
 * @param[in] length 数据长度 (bytes), 可以为0
 *
 * @return uint32_t 更新后的CRC32上下文值
 *
 * @note 调用规则:
 *       1. 首先调用 autorun_crc32_init() 获取初始ctx
 *       2. 可以零次或多次调用此函数添加数据
 *       3. 最后调用 autorun_crc32_finalize() 获取最终CRC32值
 *
 * @warning ctx参数必须传入上一次返回的值，不能随意修改。
 *
 * 典型用途:
 * - 流式数据处理 (网络包、文件读取)
 * - 大块数据分片计算
 * - 数据到达时间不确定的场景
 *
 * 示例:
 * @code
 *   uint32_t ctx = autorun_crc32_init();
 *
 *   // 分3次添加数据
 *   ctx = autorun_crc32_update(ctx, chunk1, len1);
 *   ctx = autorun_crc32_update(ctx, chunk2, len2);
 *   ctx = autorun_crc32_update(ctx, chunk3, len3);
 *
 *   // 获取最终结果
 *   uint32_t crc = autorun_crc32_finalize(ctx);
 * @endcode
 */
uint32_t autorun_crc32_update(uint32_t ctx,
                              const uint8_t *data,
                              uint32_t length);

/**
 * @brief 完成CRC32计算并获取最终值
 *
 * 结束增量计算过程，执行最终的异或操作并返回CRC32校验值。
 *
 * @param[in] ctx 最终的CRC32上下文值 (来自最后一次update)
 *
 * @return uint32_t 最终的CRC32校验值
 *
 * @note 此函数执行的操作:
 *       1. 将ctx与 CRC32_FINAL_XOR_VALUE (0xFFFFFFFF) 进行异或
 *       2. 返回结果
 *
 * @warning 调用此函数后，ctx值不应再用于后续的update操作。
 *         如需重新计算，应重新调用 init() 开始新流程。
 *
 * 示例:
 * @code
 *   uint32_t ctx = autorun_crc32_init();
 *   ctx = autorun_crc32_update(ctx, data, length);
 *   uint32_t crc = autorun_crc32_finalize(ctx);
 *   printf("Final CRC32: 0x%08X\n", crc);
 * @endcode
 */
uint32_t autorun_crc32_finalize(uint32_t ctx);

#endif /* AUTORUN_CRC32_ENABLE_INCREMENTAL */

/* ================================================================== */
/*                     高级功能                                       */
/* ================================================================== */

/**
 * @brief 合并两个CRC32值
 *
 * 将两个独立计算的CRC32值合并为一个，等价于对拼接后的数据整体计算CRC32。
 *
 * 数学原理:
 * 如果 CRC(A) = crc1, CRC(B) = crc2,
 * 则 CRC(A || B) = crc32_merge(crc1, crc2, len_B)
 *
 * @param[in] crc1      第一个数据块的CRC32值
 * @param[in] crc2      第二个数据块的CRC32值
 * @param[in] len2      第二个数据块的长度 (bytes)
 *
 * @return uint32_t 合并后的CRC32值
 *
 * 应用场景:
 * - 并行计算: 多线程分别计算不同段的CRC，最后合并
 * - 增量更新: 已有旧数据的CRC，追加新数据时无需重算全部
 * - 分布式系统: 各节点计算本地CRC，中心节点合并
 *
 * @note 此函数需要查表法支持 (AUTORUN_CRC32_USE_TABLE=1)。
 *       如果未启用查表法，此函数不可用。
 *
 * 示例:
 * @code
 *   // 分别计算两段数据的CRC
 *   uint32_t crc1 = autorun_calc_crc32(data1, len1);  // "Hello "
 *   uint32_t crc2 = autorun_calc_crc32(data2, len2);  // "World"
 *
 *   // 合并结果应等于整体计算
 *   uint32_t merged = autorun_crc32_merge(crc1, crc2, len2);
 *   uint32_t expected = autorun_calc_crc32(combined, len1 + len2);
 *   assert(merged == expected);
 * @endcode
 */
#if AUTORUN_CRC32_USE_TABLE
uint32_t autorun_crc32_merge(uint32_t crc1, uint32_t crc2, uint32_t len2);
#endif

/**
 * @brief 验证CRC32是否匹配
 *
 * 快速检查存储的CRC32值与重新计算的结果是否一致。
 *
 * @param[in] data       数据缓冲区指针
 * @param[in] length     数据长度
 * @param[in] expected   期望的CRC32值
 *
 * @return bool true=匹配, false=不匹配
 *
 * @note 此函数是 autorun_calc_crc32() 的便捷包装，
 *       省去了调用者手动比较的步骤。
 *
 * 示例:
 * @code
 *   if (!autorun_crc32_verify(config_data, config_size, stored_crc)) {
 *       printf("CRC32 mismatch! Data may be corrupted.\n");
 *       return -1;
 *   }
 * @endcode
 */
bool autorun_crc32_verify(const uint8_t *data,
                          uint32_t length,
                          uint32_t expected);

/**
 * @brief 自定义CRC32计算 (带参数化选项)
 *
 * 提供完全可定制的CRC32计算接口，允许指定:
 * - 自定义初始值 (非标准0xFFFFFFFF)
 * - 是否跳过最终异或 (获取中间状态)
 *
 * @param[in] data          数据缓冲区指针
 * @param[in] length        数据长度
 * @param[in] initial_value 初始值 (通常为 CRC32_INITIAL_VALUE)
 * @param[in] finalize_xor  是否执行最终异或 (true=标准模式)
 *
 * @return uint32_t CRC32值 (根据finalize_xor决定是否异或)
 *
 * 高级用法:
 * @code
 *   // 获取未最终异或的中间值 (某些协议需要)
 *   uint32_t intermediate = autorun_crc32_custom(
 *       data, length, CRC32_INITIAL_VALUE, false);
 *
 *   // 使用非标准初始值
 *   uint32_t custom_crc = autorun_crc32_custom(
 *       data, length, 0x12345678, true);
 * @endcode
 */
uint32_t autorun_crc32_custom(const uint8_t *data,
                              uint32_t length,
                              uint32_t initial_value,
                              bool finalize_xor);

/* ================================================================== */
/*                      查表法相关                                    */
/* ================================================================== */

#if AUTORUN_CRC32_USE_TABLE

/**
 * @brief 获取CRC32查找表指针 (只读)
 *
 * 返回内部预计算的256项CRC32查找表。
 * 可用于外部优化实现或调试目的。
 *
 * @return const uint32_t* 指向256个uint32_t元素的数组 (只读)
 *
 * @note 表的内容在首次调用时初始化 (懒初始化)。
 *       之后所有调用返回相同的指针。
 *
 * 查找表结构:
 * - 索引: 0-255 (一个字节的所有可能值)
 * - 值: 对应的CRC32余数
 * - 大小: 1024 bytes (256 * 4 bytes)
 */
const uint32_t* autorun_crc32_get_table(void);

/**
 * @brief 手动初始化CRC32查找表
 *
 * 强制初始化或重新生成CRC32查找表。
 * 通常不需要手动调用，库会在首次使用时自动初始化。
 *
 * 用途:
 * - 在关键路径之前预热 (避免首次调用的延迟)
 * - 测试和调试 (确保表已正确生成)
 * - 多线程环境的显式同步
 */
void autorun_crc32_init_table(void);

/**
 * @brief 检查查找表是否已初始化
 *
 * @return bool true=已初始化, false=未初始化
 */
bool autorun_crc32_is_table_initialized(void);

#endif /* AUTORUN_CRC32_USE_TABLE */

/* ================================================================== */
/*                      版本信息                                      */
/* ================================================================== */

/**
 * @brief 获取CRC32模块版本信息
 *
 * @return const char* 版本字符串 (如 "1.0.0")
 */
const char* autorun_crc32_get_version(void);

/**
 * @brief 获取当前编译配置信息
 *
 * 返回描述当前编译特性的位图:
 * - bit 0: 查表法启用 (AUTORUN_CRC32_USE_TABLE)
 * - bit 1: 增量API启用 (AUTORUN_CRC32_ENABLE_INCREMENTAL)
 * - bit 2-31: 保留
 *
 * @return uint32_t 特性标志位图
 */
uint32_t autorun_crc32_get_capabilities(void);

#ifdef __cplusplus
}
#endif

#endif /* CRC32_H */
