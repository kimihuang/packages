# sort_app

C 语言实现的排序算法库，支持命令行参数驱动、基准测试、GTest 单元测试。

## 已实现算法

| 算法 | 时间复杂度 | 稳定性 | 特点 |
|------|-----------|--------|------|
| bubble | O(n^2) | 稳定 | 提前终止优化 |
| selection | O(n^2) | 不稳定 | - |
| insertion | O(n^2) | 稳定 | - |
| shell | O(n^1.3) | 不稳定 | Knuth 增量序列 |
| merge | O(n log n) | 稳定 | 递归分治 |
| quick | O(n log n) avg | 不稳定 | 三数取中 + 小数组插入排序 |
| heap | O(n log n) | 不稳定 | sift_down 建堆 |

## 构建与测试

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
ctest --output-on-failure
```

GTest 通过 CMake FetchContent 自动下载，无需额外安装。

## 命令行用法

```
Usage: sort_app [OPTIONS]

Options:
  -a <algorithm>  排序算法名称
  -n <size>       数组大小 (默认: 20)
  -s <seed>       随机种子 (默认: 基于时间)
  -r <range>      值范围 0..RANGE (默认: 999)
  -p              打印排序前后数组
  -l              列出所有可用算法
  -b              运行所有算法基准测试
  -h              显示帮助
```

### 示例

```bash
# 列出所有算法
./sort_app -l

# 快速排序 10 个元素并打印结果
./sort_app -a quick -n 10 -p -s 42

# 基准测试所有算法，5 万个元素
./sort_app -b -n 50000

# 使用指定随机种子和值范围
./sort_app -a merge -n 1000 -s 123 -r 9999
```

## 项目结构

```
sort_app/
├── CMakeLists.txt
├── include/sort/
│   ├── sort_types.h        # 统一类型定义
│   ├── sort_registry.h     # 算法注册表接口
│   ├── sort_bubble.h
│   ├── sort_selection.h
│   ├── sort_insertion.h
│   ├── sort_shell.h
│   ├── sort_merge.h
│   ├── sort_quick.h
│   └── sort_heap.h
├── src/
│   ├── main.c              # 主程序 (getopt 参数解析)
│   ├── sort_registry.c     # 算法注册表实现
│   └── sort_*.c            # 各排序算法实现
└── tests/
    ├── CMakeLists.txt
    ├── test_sort.cpp       # 正确性测试 (80 cases)
    └── test_bench.cpp      # 性能基准测试
```

## 扩展新算法

新增排序算法只需三步：

1. 在 `include/sort/` 添加头文件 `sort_xxx.h`，声明 `void sort_xxx(int *arr, size_t n);`
2. 在 `src/` 添加 `sort_xxx.c` 实现排序逻辑
3. 在 `src/sort_registry.c` 的 `g_algorithms` 数组中追加一行：

```c
{"xxx", "Xxx Sort - O(...) ...", sort_xxx},
```

无需修改 main.c 或测试代码，新算法自动支持命令行调用和基准测试。
