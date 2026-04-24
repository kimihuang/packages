# CV Test Framework 集成测试指南

本文档面向测试人员，介绍 `cv_test` 的命令行参数、测试层级结构及断言机制，帮助快速设计和执行测试用例。

---

## 1. 测试层级结构

框架采用三级组织结构：**Group -> Module -> Case**

```
Group (测试组)
  └── Module (测试模块)
        └── Case (测试用例)     ← 最小执行单元
```

### 注册宏（写在测试 .c 文件中）

| 宏 | 用途 | 示例 |
|---|---|---|
| `TEST_GROUP(name)` | 注册测试组 | `TEST_GROUP(group_i2c);` |
| `TEST_MODULE(group, name)` | 在组下注册模块 | `TEST_MODULE(group_i2c, module_transfer);` |
| `TEST_CASE(module, name)` | 在模块下注册用例 | `TEST_CASE(module_transfer, test_write_byte);` |

### Hook 宏（设置前置/后置钩子）

| 宏 | 作用域 | 示例 |
|---|---|---|
| `GROUP_PRE_TEST(grp, fn)` | 组级前置钩子 | `GROUP_PRE_TEST(group_i2c, i2c_init);` |
| `GROUP_POST_TEST(grp, fn)` | 组级后置钩子 | `GROUP_POST_TEST(group_i2c, i2c_cleanup);` |
| `MODULE_PRE_TEST(mod, fn)` | 模块级前置钩子 | `MODULE_PRE_TEST(module_transfer, tx_init);` |
| `MODULE_POST_TEST(mod, fn)` | 模块级后置钩子 | `MODULE_POST_TEST(module_transfer, tx_cleanup);` |
| `CASE_PRE_TEST(case, fn)` | 用例级前置钩子 | `CASE_PRE_TEST(test_write_cvcase, setup_buf);` |
| `CASE_POST_TEST(case, fn)` | 用例级后置钩子 | `CASE_POST_TEST(test_write_cvcase, free_buf);` |

执行顺序：`Group Pre -> Module Pre -> Case Pre -> Case Body -> Case Post -> Module Post -> Group Post`

---

## 2. 命令行参数完整参考

### 2.1 测试选择

| 参数 | 长选项 | 说明 | 示例 |
|---|---|---|---|
| `-g` | `--group` | 仅运行指定组，逗号分隔 | `-g group_i2c,group_timer` |
| `-m` | `--module` | 仅运行指定模块，逗号分隔 | `-m module_transfer` |
| `-c` | `--case` | 仅运行指定用例，逗号分隔 | `-c test_write_byte,test_read_byte` |
| `-k` | `--filter` | 按通配符过滤用例名（`*` 匹配任意字符） | `-k "test_i2c_*"` |

**过滤规则：**
- `-g`、`-m`、`-c` 支持逗号分隔多个名称
- `-k` 支持 `*` 通配符匹配（例如 `*timer*` 匹配所有含 timer 的用例）
- 多个选项可组合使用，取交集（例如 `-g group_i2c -c test_write_byte` 只运行 group_i2c 下的 test_write_byte）
- 未匹配的用例标记为 SKIP

### 2.2 执行控制

| 参数 | 长选项 | 说明 | 示例 |
|---|---|---|---|
| `-n` | `--count` | 重复执行 N 次 | `-n 100` |
| `-r` | `--repeat` | 持续重复，直到出现失败才停止 | `-r` |
| `-f` | `--fail-fast` | 遇到第一个失败用例立即中止 | `-f` |
| | `--timeout` | 单用例超时时间（秒） | `--timeout 5` |
| | `--shuffle` | 随机打乱执行顺序 | `--shuffle` |

**重复执行说明：**
- `-n N`：执行 N 轮，每轮结束后打印统计摘要
- `-r`：无限循环执行，遇到失败自动停止并打印多轮汇总
- 多轮模式下会输出 ROUND SUMMARY，包含每轮通过/失败数和最差一轮数据

### 2.3 输出控制

| 参数 | 长选项 | 说明 | 示例 |
|---|---|---|---|
| `-v` | `--verbose` | 详细输出（打印所有 hook 执行信息） | `-v` |
| `-s` | `--silent` | 静默模式（仅打印最终 SUMMARY） | `-s` |
| | `--list` | 列出所有已注册的测试（不执行） | `--list` |
| | `--list-detail` | 列出所有已注册测试的详细信息（不执行） | `--list-detail` |
| | `--color` | 启用彩色输出 | `--color` |
| | `--no-color` | 禁用彩色输出 | `--no-color` |

### 2.4 其他

| 参数 | 长选项 | 说明 |
|---|---|---|
| `-h` | `--help` | 显示帮助信息 |

---

## 3. 常用测试场景命令示例

### 查看已注册的测试

```bash
# 列出所有测试名称
./cv_test --list

# 列出所有测试详细信息
./cv_test --list-detail
```

### 按层级筛选执行

```bash
# 只运行 group_i2c 组下所有测试
./cv_test -g group_i2c

# 只运行 module_transfer 模块下所有测试
./cv_test -m module_transfer

# 只运行指定的单个用例
./cv_test -c test_write_byte

# 运行多个指定用例
./cv_test -c test_write_byte,test_read_byte,test_invalid_addr
```

### 通配符过滤

```bash
# 运行所有名称含 "i2c" 的用例
./cv_test -k "test_i2c_*"

# 运行所有名称含 "timer" 的用例
./cv_test -k "*timer*"

# 组合使用：在 group_i2c 下匹配通配符
./cv_test -g group_i2c -k "*multi*"
```

### 执行控制

```bash
# 快速失败模式：遇到第一个失败立即停止
./cv_test -f

# 重复执行 1000 轮（用于压力/稳定性测试）
./cv_test -n 1000

# 持续执行直到失败（用于复现偶发问题）
./cv_test -r

# 随机打乱顺序执行（消除用例间依赖隐患）
./cv_test --shuffle

# 组合：随机顺序 + 快速失败 + 详细输出
./cv_test --shuffle -f -v
```

### 输出模式

```bash
# 详细模式：显示所有 hook 执行过程
./cv_test -v

# 静默模式：仅显示最终 PASS/FAIL/SKIP 统计
./cv_test -s

# 彩色输出
./cv_test --color
```

---

## 4. 用例参数化

框架支持为每个用例传入自定义参数（`void *data`），适用于数据驱动测试。

### 定义参数结构

```c
typedef struct {
    uint8_t slave_addr;
    uint8_t data;
} i2c_param_t;
```

### 编写参数化用例

```c
TEST_CASE(module_i2c, test_write_byte)
{
    i2c_param_t *p = (i2c_param_t *)data;   // 框架自动传入
    int ret = i2c_write(p->slave_addr, p->data);
    CV_ASSERT_EQ(ret, 0);
}
```

### 通过构造函数设置参数

```c
static i2c_param_t param_write = { .slave_addr = 0x50, .data = 0xAB };

__attribute__((constructor(104)))
static void __cv_setdata_test_write(void) {
    cv_case_set_data(test_write_byte_cvcase, &param_write);
}
```

> **注意：** 构造函数优先级必须 >= 104（用例注册为 103），确保用例已注册后再设置参数。

---

## 5. 断言宏

| 宏 | 说明 | 示例 |
|---|---|---|
| `CV_ASSERT(cond)` | 条件为真则通过 | `CV_ASSERT(ret == 0);` |
| `CV_ASSERT_EQ(a, b)` | 相等断言（打印实际值） | `CV_ASSERT_EQ(count, 5);` |
| `CV_ASSERT_NE(a, b)` | 不等断言 | `CV_ASSERT_NE(ptr, NULL);` |
| `CV_ASSERT_NULL(ptr)` | 断言为空指针 | `CV_ASSERT_NULL(result);` |
| `CV_ASSERT_NOT_NULL(ptr)` | 断言非空指针 | `CV_ASSERT_NOT_NULL(buf);` |

断言失败时会自动记录文件名、行号和错误信息，用例结果标记为 FAIL。

---

## 6. 测试结果状态

| 状态 | 说明 |
|---|---|
| PASS | 用例通过 |
| FAIL | 断言失败 |
| SKIP | 用例被过滤条件排除 |
| ERROR | 用例执行异常 |

退出码：`0` = 全部通过，`1` = 存在失败或错误
