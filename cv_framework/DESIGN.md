# CV Test Framework - 架构及模块设计

## 1. 总体架构

```mermaid
graph TB
    subgraph "用户入口"
        A[cv_test 可执行程序]
    end

    subgraph "测试框架核心层"
        B[Framework Core]
        B1[list.h - 双向链表]
        B2[framework.h/c - 框架核心]
        B3[runner.h/c - 测试运行器]
    end

    subgraph "测试定义层"
        C[宏定义 API]
        C1[TEST_CASE - 注册用例]
        C2[TEST_MODULE - 注册模块]
        C3[TEST_GROUP - 注册组]
        C4[SETUP / TEARDOWN - 钩子]
    end

    subgraph "测试用例层 - 2 Group x 3 Module x N Case"
        D1["group_math<br/>module_add / module_sub / module_mul"]
        D2["group_string<br/>module_concat / module_len / module_copy"]
    end

    A --> B
    C --> B
    B1 --> B2
    B2 --> B3
    D1 -.-> C
    D2 -.-> C

    style A fill:#f9f,stroke:#333,stroke-width:2px
    style B fill:#bbf,stroke:#333,stroke-width:2px
    style C fill:#bfb,stroke:#333,stroke-width:2px
    style D1 fill:#fbb,stroke:#333
    style D2 fill:#fbb,stroke:#333
```

---

## 2. 核心数据结构

### 2.1 三层结构关系

```mermaid
classDiagram
    class test_case {
        +char name[64]
        +void (*func)(void)
        +list_head case_node
        +int result
    }

    class test_module {
        +char name[64]
        +list_head case_list
        +list_head module_node
        +int case_count
        +void (*setup)(void)
        +void (*teardown)(void)
        +int pass_count
        +int fail_count
    }

    class test_group {
        +char name[64]
        +list_head module_list
        +list_head group_node
        +int module_count
        +void (*setup)(void)
        +void (*teardown)(void)
        +int pass_count
        +int fail_count
    }

    class test_framework {
        +list_head group_list
        +int group_count
        +int total_pass
        +int total_fail
        +int total_skip
    }

    test_framework "1" *-- "*" test_group : contains
    test_group "1" *-- "*" test_module : contains
    test_module "1" *-- "*" test_case : contains
```

### 2.2 双向链表节点布局

```mermaid
graph LR
    subgraph "test_group 链表"
        G1[group_head] --- G1n[prev/next]
        G2[group_math] --- G2n[prev/next]
        G3[group_string] --- G3n[prev/next]
        G1n --> G2 --> G3
    end

    subgraph "group_math 内 module 链表"
        M1[module_head] --- M1n[prev/next]
        M2[module_add] --- M2n[prev/next]
        M3[module_sub] --- M3n[prev/next]
        M4[module_mul] --- M4n[prev/next]
        M1n --> M2 --> M3 --> M4
    end

    subgraph "module_add 内 case 链表"
        C1[case_head] --- C1n[prev/next]
        C2[case_1_plus_1] --- C2n[prev/next]
        C3[case_neg_add] --- C3n[prev/next]
        C4[case_zero_add] --- C4n[prev/next]
        C1n --> C2 --> C3 --> C4
    end
```

---

## 3. 目录结构

```
cv_framework/
├── Makefile
├── README.md
├── include/
│   ├── cv_list.h            # Linux 风格双向链表
│   ├── cv_test.h            # 框架核心数据结构与 API 声明
│   └── cv_macros.h          # 用户层便捷宏定义
├── src/
│   ├── cv_test.c            # 框架核心实现（注册、运行、统计）
│   ├── cv_runner.c          # 测试运行器（遍历链表、调用钩子）
│   └── cv_main.c            # main 入口
├── tests/
│   ├── test_math.c          # group_math: 加减乘模块
│   └── test_string.c        # group_string: 拼接/长度/拷贝模块
└── output/
    └── cv_test              # 编译产物
```

---

## 4. 模块职责

### 4.1 `cv_list.h` — 双向链表

仿 Linux kernel `list_head`，提供：

| 函数 | 说明 |
|------|------|
| `LIST_HEAD(name)` | 定义并初始化链表头 |
| `INIT_LIST_HEAD(ptr)` | 初始化链表头 |
| `list_add(new, head)` | 头插法 |
| `list_add_tail(new, head)` | 尾插法 |
| `list_del(entry)` | 从链表中移除 |
| `list_entry(ptr, type, member)` | 通过成员指针获取宿主结构体 |
| `list_for_each(pos, head)` | 正向遍历 |
| `list_for_each_prev(pos, head)` | 反向遍历 |
| `list_for_each_safe(pos, n, head)` | 安全遍历（可中途删除） |

### 4.2 `cv_test.h` — 核心数据结构与 API

```c
/* 测试用例 — 最小单位 */
typedef void (*test_func_t)(void);

typedef struct cv_test_case {
    char            name[64];
    test_func_t     func;
    struct list_head case_node;  /* 链接到 module->case_list */
    int             result;      /* 0=PASS, -1=FAIL, 1=SKIP */
} cv_test_case_t;

/* 测试模块 — 包含多个 test_case */
typedef struct cv_test_module {
    char            name[64];
    struct list_head case_list;   /* 挂载 cv_test_case */
    struct list_head module_node; /* 链接到 group->module_list */
    int             case_count;
    int             pass_count;
    int             fail_count;
    int             skip_count;
    /* 模块级钩子 */
    void (*setup)(void);
    void (*teardown)(void);
} cv_test_module_t;

/* 测试组 — 包含多个 test_module */
typedef struct cv_test_group {
    char            name[64];
    struct list_head module_list; /* 挂载 cv_test_module */
    struct list_head group_node;  /* 链接到 framework->group_list */
    int             module_count;
    int             pass_count;
    int             fail_count;
    int             skip_count;
    /* 组级钩子 */
    void (*setup)(void);
    void (*teardown)(void);
} cv_test_group_t;

/* 框架全局 */
typedef struct cv_test_framework {
    struct list_head group_list;
    int             group_count;
    int             total_pass;
    int             total_fail;
    int             total_skip;
} cv_test_framework_t;

/* API */
cv_test_group_t  *cv_group_register(const char *name);
cv_test_module_t *cv_module_register(cv_test_group_t *group, const char *name);
cv_test_case_t   *cv_case_register(cv_test_module_t *module,
                                    const char *name, test_func_t func);
void              cv_group_set_hooks(cv_test_group_t *g,
                                     void (*setup)(void), void (*teardown)(void));
void              cv_module_set_hooks(cv_test_module_t *m,
                                      void (*setup)(void), void (*teardown)(void));
int               cv_run_all(void);
```

### 4.3 `cv_macros.h` — 用户便捷宏

```c
#define TEST_GROUP(name)                       static cv_test_group_t *name = cv_group_register(#name)
#define TEST_MODULE(group, name)               static cv_test_module_t *name = cv_module_register(group, #name)
#define TEST_CASE(module, name)                static void name(void); \
                                                static cv_test_case_t *name##_ptr = cv_case_register(module, #name, name); \
                                                static void name(void)
#define MODULE_SETUP(module, fn)               cv_module_set_hooks(module, fn, NULL)
#define MODULE_TEARDOWN(module, fn)            cv_module_set_hooks(module, NULL, fn)
#define GROUP_SETUP(group, fn)                 cv_group_set_hooks(group, fn, NULL)
#define GROUP_TEARDOWN(group, fn)              cv_group_set_hooks(group, NULL, fn)
#define CV_ASSERT(cond)                        do { if (!(cond)) { ... } } while(0)
```

### 4.4 `cv_runner.c` — 运行器

执行流程：

```mermaid
flowchart TD
    START[cv_run_all] --> G_LOOP{遍历 group 链表}
    G_LOOP --> G_SETUP[group.setup if set]
    G_SETUP --> M_LOOP{遍历 module 链表}

    M_LOOP --> M_SETUP[module.setup if set]
    M_SETUP --> C_LOOP{遍历 case 链表}

    C_LOOP --> RUN[case.func]
    RUN --> CHECK{检查 case.result}
    CHECK -->|PASS| STAT_P[module.pass++]
    CHECK -->|FAIL| STAT_F[module.fail++]
    CHECK -->|SKIP| STAT_S[module.skip++]
    STAT_P --> C_LOOP
    STAT_F --> C_LOOP
    STAT_S --> C_LOOP

    C_LOOP -->|遍历完| M_TEARDOWN[module.teardown if set]
    M_TEARDOWN --> M_LOOP

    M_LOOP -->|遍历完| G_TEARDOWN[group.teardown if set]
    G_TEARDOWN --> G_LOOP

    G_LOOP -->|遍历完| SUMMARY[打印统计摘要]
    SUMMARY --> END[返回 total_fail == 0 ? 0 : 1]

    style START fill:#f9f,stroke:#333
    style END fill:#f9f,stroke:#333
    style SUMMARY fill:#bbf,stroke:#333
    style RUN fill:#bfb,stroke:#333
```

### 4.5 `cv_main.c` — 入口

```c
int main(int argc, char *argv[]) {
    /* 1. 自动收集所有测试用例（通过 static 初始化） */
    /* 2. 运行框架 */
    int ret = cv_run_all();
    return ret;
}
```

测试文件通过 `#include` 头文件后，宏定义展开为带 `__attribute__((constructor))` 的自动注册函数。由于纯 C 的 static 初始化顺序不确定，使用 **constructor 优先级** 保证注册顺序。

---

## 5. 自动注册机制

```mermaid
sequenceDiagram
    participant File as test_math.c
    participant Macro as TEST_CASE 宏
    participant Reg as 注册函数
    participant List as 双向链表

    File->>Macro: TEST_CASE(module_add, test_1_plus_1)
    Macro->>Macro: 展开为 constructor 函数
    Note over Macro: __attribute__((constructor(102)))
    File->>Reg: cv_case_register(module, "test_1_plus_1", test_1_plus_1)
    Reg->>List: list_add_tail(&case->case_node, &module->case_list)

    File->>Macro: TEST_CASE(module_add, test_neg_add)
    Macro->>Macro: 展开为 constructor 函数
    File->>Reg: cv_case_register(module, "test_neg_add", test_neg_add)
    Reg->>List: list_add_tail(&case->case_node, &module->case_list)
```

注册优先级：`group(101)` < `module(102)` < `case(103)`，确保先注册组、再模块、最后用例。

---

## 6. 控制台输出示例

```
===========================================
  CV Test Framework v1.0
===========================================

[GROUP] group_math
  [MODULE] module_add ................ SETUP
    [PASS] test_1_plus_1
    [PASS] test_neg_add
    [PASS] test_zero_add
  [MODULE] module_add ................ TEARDOWN
  [MODULE] module_sub ................ SETUP
    [PASS] test_5_minus_3
    [PASS] test_neg_minus_neg
  [MODULE] module_sub ................ TEARDOWN
  [MODULE] module_mul ................ SETUP
    [PASS] test_2_times_3
    [FAIL] test_overflow  <-- expected 0, got -1
  [MODULE] module_mul ................ TEARDOWN

[GROUP] group_string
  [MODULE] module_concat ................ SETUP
    [PASS] test_basic_concat
    [PASS] test_empty_concat
  [MODULE] module_concat ................ TEARDOWN
  [MODULE] module_len ................ SETUP
    [PASS] test_ascii_len
    [PASS] test_empty_len
  [MODULE] module_len ................ TEARDOWN
  [MODULE] module_copy ................ SETUP
    [PASS] test_strcpy_basic
    [PASS] test_overlap_copy
  [MODULE] module_copy ................ TEARDOWN

===========================================
  SUMMARY
===========================================
  Groups:  2  |  Modules: 6  |  Cases: 14
  PASS: 13  |  FAIL: 1  |  SKIP: 0
===========================================
```

---

## 7. 示例测试代码

```c
/* tests/test_math.c */
#include "cv_macros.h"

TEST_GROUP(group_math);

/* --- module_add --- */
TEST_MODULE(group_math, module_add);

static void add_setup(void)   { printf("  [SETUP] module_add\n"); }
static void add_teardown(void){ printf("  [TEARDOWN] module_add\n"); }
MODULE_SETUP(module_add, add_setup);
MODULE_TEARDOWN(module_add, add_teardown);

TEST_CASE(module_add, test_1_plus_1) {
    CV_ASSERT(1 + 1 == 2);
}

TEST_CASE(module_add, test_neg_add) {
    CV_ASSERT(-1 + -1 == -2);
}

/* --- module_sub --- */
TEST_MODULE(group_math, module_sub);

TEST_CASE(module_sub, test_5_minus_3) {
    CV_ASSERT(5 - 3 == 2);
}
```

---

## 8. Makefile 构建设计

```makefile
CC      = gcc
CFLAGS  = -Wall -Wextra -Iinclude
SRCDIR  = src
TESTDIR = tests
OBJDIR  = build
TARGET  = output/cv_test

SRCS    = $(wildcard $(SRCDIR)/*.c)
TESTS   = $(wildcard $(TESTDIR)/*.c)
OBJS    = $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SRCS))
TESTOBJS= $(patsubst $(TESTDIR)/%.c, $(OBJDIR)/%.o, $(TESTS))

$(TARGET): $(OBJS) $(TESTOBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/%.o: $(TESTDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR):
	mkdir -p $(OBJDIR) output

clean:
	rm -rf $(OBJDIR) $(TARGET)

.PHONY: clean run
run: $(TARGET)
	./$(TARGET)
```

---

## 9. 依赖关系

```mermaid
graph BT
    cv_main[cv_main.c] --> cv_test[cv_test.c]
    cv_runner[cv_runner.c] --> cv_test
    cv_test --> cv_list[cv_list.h]
    test_math[test_math.c] --> cv_macros[cv_macros.h]
    test_string[test_string.c] --> cv_macros
    cv_macros --> cv_test
    cv_main --> cv_runner

    style cv_list fill:#ffd,stroke:#333
    style cv_test fill:#bbf,stroke:#333
    style cv_macros fill:#bfb,stroke:#333
```

---

## 10. 设计要点总结

| 设计决策 | 说明 |
|----------|------|
| Linux 双向链表 | `list_head` 嵌入结构体，零开销，支持安全遍历与删除 |
| 三层结构 | Group → Module → Case，层次清晰，钩子粒度可控 |
| `__attribute__((constructor))` | 编译期自动注册，用户无需手动调用注册函数 |
| 优先级控制 | group(101) < module(102) < case(103)，保证注册顺序 |
| CV_ASSERT 宏 | 自动捕获文件名、行号、条件表达式 |
| 模块化编译 | 框架与测试分离，添加新测试只需新建 .c 文件 |
| 统计汇总 | 每个 module/group 独立统计，框架级汇总，便于 CI 判定 |
