# t32_linux_stability — Linux 稳定性分析体系

基于 Lauterbach TRACE32 + Linux OS Awareness，面向 Linux 内核稳定性问题（panic/oops、hang、内存异常）的分析脚本库。

> 定位：这是一套**人工分析工具链 + 可自动化 case** 的分层体系，与同级的 `t32_python`（「脚本即 case」的 CI 执行框架）**互补**：
> - `t32_python` 负责「自动化执行 + pass/fail + 报告」
> - 本体系负责「环境准备 + 稳定性分析工具 + 分析 SOP」
> - 本体系的 `cases/` 目录的脚本遵循 t32_python 的 pass/fail 约定，可直接登记进 `ci_testcase_seq.json` 被 t32_python 调度。

---

## 1. 设计原则（易扩展、易维护）

1. **分层**：`env`（环境准备）/ `tools`（工具库）/ `cases`（可自动化 case）/ `playbooks`（人工 SOP）四层，职责单一、互不越界。
2. **复用不复制**：工具脚本通过 `DO ~~/demo/.../xxx.cmm` 调用 Lauterbach 官方 demo 脚本，**不拷贝**官方文件，避免版本漂移（官方升级后自动生效）。
3. **配置集中**：所有路径常量集中在 `tools/common/config.cmm`（GLOBAL 变量），换环境/换 vmlinux 只改这一处。
4. **约定优于配置**：命名约定即语义（见下），目录即分类，新增能力=新增一个目录+一个脚本。
5. **工具可组合**：工具脚本无副作用（只设断点/只打印/只落盘），可在 case 或交互中按需 `DO` 组合。
6. **与 CI 解耦但可桥接**：本体系可独立手工用（PowerView 里 `DO` 脚本）；`cases/` 脚本同时遵循 `PRINT %ERROR` = fail 的约定，可挂进 t32_python。

### 命名约定

| 前缀 | 语义 | 示例 |
|------|------|------|
| `trap_*` | 设断点/观测点抓现场（无副作用） | `trap_panic.cmm`、`trap_segv.cmm` |
| `save_*` | 落盘现场（ramdump/core/trace） | `save_ramdump.cmm` |
| `show_*` | 只读展示（栈/任务/内存） | `show_stacks.cmm` |
| `analyze_*` | 离线分析（符号化/回溯） | （预留） |
| `setup_*` | 环境准备（一次性） | `env/sim_armv8_setup.cmm` |

---

## 2. 目录结构

```
t32_linux_stability/
├── README.md                     # 本文档
├── env.json                      # 环境启动配置（T32 可执行文件/端口等）
├── env/                          # 第 0 层：环境准备
│   └── sim_armv8_setup.cmm       #   加载 vmlinux + awareness 就绪（不 Go，可复用）
├── tools/                        # 第 1 层：稳定性工具库（按场景分子目录）
│   ├── common/
│   │   ├── config.cmm            #   全局路径配置（唯一需要改路径的地方）
│   │   └── disable_watchdogs.cmm #   封装官方 disable_watchdogs
│   ├── panic/                    #   panic/oops 场景
│   │   ├── trap_panic.cmm        #   断 panic/die/oops 入口
│   │   └── save_ramdump.cmm      #   封装官方 ramdump（命令行非交互）
│   ├── fault/                    #   内存异常场景
│   │   └── trap_segv.cmm         #   封装官方 segv（断 fault + 复原现场）
│   └── hang/                     #   hang/死锁场景
│       └── show_stacks.cmm       #   遍历所有 CPU 核打印 PC + 调用栈
├── cases/                        # 第 2 层：可自动化 case（可进 ci_testcase_seq.json）
│   ├── smoke.cmm                 #   无 license 冒烟：写内存读回校验（<50 条命令）
│   ├── boot_to_shell.cmm         #   启动 Linux 到 shell（环境冒烟，需 license）
│   └── inject_panic.cmm          #   触发 panic 并验证抓到现场（需 license）
├── playbooks/                    # 第 3 层：人工分析 SOP
│   └── panic_analysis.md         #   panic 分析标准流程
└── scripts/                      # 启动/运行脚本（参考 t32_python/ci）
    ├── lib.sh                    #   公共函数：load_env_json/gen_config_t32/wait_port/parse_args
    ├── start.sh                  #   读 env.json 启动 TRACE32，等待端口就绪
    ├── run.sh                    #   跑 case（--case 选择），端口未就绪自动先启动
    └── run_case.py               #   PYRCL 连接 + 执行 cases/<name>.cmm + 判 pass/fail
```

---

## 3. 路径配置（tools/common/config.cmm）

所有工具和 case 开头 `DO <config.cmm>`（内部用 `~~~~` 相对定位到 `tools/common/config.cmm`）加载 GLOBAL 变量：

| 变量 | 含义 | 默认值 |
|------|------|--------|
| `&STB_T32_DEMO` | TRACE32 Linux demo 根 | `~~/demo/arm/kernel/linux` |
| `&STB_VMLINUX` | 内核镜像（带符号） | `<demo>/board/simulator/armv8/kernel/vmlinux` |
| `&STB_DTB` | 设备树 | `<demo>/board/simulator/armv8/kernel/trace32_simulator.dtb` |
| `&STB_ROOTFS` | ramdisk | `<demo>/board/simulator/armv8/ramdisk/rootfs.ext4` |
| `&STB_AWARENESS` | Linux awareness 扩展 | `<demo>/awareness/linux.t32` |
| `&STB_CPU_TYPE` | 目标 CPU | `CortexA53` |
| `&STB_OUT_DIR` | 现场落盘目录 | `./ramdump` |

路径记号（重要）：

- `~~`  = TRACE32 安装根（本例 `/home/lion/t32/`），**仅用于引用 demo**（`~~/demo/...`）
- `~~~~` = **当前脚本所在目录**，用于项目内脚本互相引用（本项目不在 T32 根下，故内部统一用 `~~~~/../` 相对定位，不依赖 `~~`）

**换真机 / 换内核版本时，只改 `config.cmm` 即可**，其余脚本不用动。

---

## 4. 使用流程

### 4.0 脚本启动（推荐，参考 t32_python/ci）

先安装 PYRCL（一次性，本地 wheel 随 TRACE32 提供，本项目已放 t32_python/tools/）：

```bash
python -m pip install ../t32_python/tools/lauterbach_trace32_rcl-1.0.7-py3-none-any.whl
```

然后一条命令启动 T32 并跑指定 case（端口未就绪会自动先启动 T32）：

```bash
./scripts/run.sh --case boot_to_shell            # 跑 cases/boot_to_shell.cmm
./scripts/run.sh --case inject_panic --timeout 180
```

`run.sh` 内部：读 `env.json` → 若 API 端口未就绪则调 `start.sh` 启动 TRACE32 → `run_case.py` 用 PYRCL 连接 → `DO cases/<name>.cmm` → 按 `PRINT %ERROR` 判 pass/fail。

分开两步也可（先启动、再多次跑不同 case，避免重复启动）：

```bash
./scripts/start.sh                              # 启动 TRACE32（后台），等待端口就绪
./scripts/run.sh --case boot_to_shell           # 跑 case（T32 已就绪则直接连接）
./scripts/run.sh --case inject_panic
```

`T32_VENV` 环境变量可指定装了 PYRCL 的 venv（默认用 PATH 中的 python3）。

### 4.1 交互式（PowerView 手工）

先在 PowerView 命令行 `CD` 到本项目根目录（或用绝对路径 DO）：

```cmm
DO env/sim_armv8_setup.cmm              ; 1. 环境准备：加载 vmlinux + awareness 就绪
DO tools/common/disable_watchdogs.cmm    ; 2. 关 watchdog/RCU stall（调试时防干扰）
DO tools/panic/trap_panic.cmm            ; 3. 断 panic/die/oops
Go                                       ; 4. 运行，等待命中
; 命中后：
DO tools/panic/save_ramdump.cmm          ; 5. 保存现场（生成 restore_<ts>.cmm）
```

或者只跑一条自包含 case（内部已含 setup）：

```cmm
DO cases/boot_to_shell.cmm              ; setup + boot + 检查
```

### 4.2 自动化（挂进 t32_python）

把 `env/sim_armv8_setup.cmm` 的内容作为环境 `env.json` 的 `startup_cmm`，再把 `cases/*.cmm` 登记进 `ci_testcase_seq.json`（`critical: true` 用于 boot 冒烟作为前置）。

---

## 5. 如何扩展

- **加一个新稳定性场景**（如内存泄漏）：`mkdir tools/mem` → 写 `tools/mem/trap_kmemleak.cmm`（遵循命名约定），即可被其他 case 组合使用。
- **加一条可自动化 case**：写 `cases/<name>.cmm`（遵循 `PRINT %ERROR` = fail），登记进 t32_python 的 `ci_testcase_seq.json`。
- **换环境**（真机/新内核）：改 `tools/common/config.cmm` 的路径 + `cpu_type`，再新建一个 `env/setup_<board>.cmm`（复用同一套 tools）。
- **沉淀分析经验**：往 `playbooks/` 加 `<场景>_analysis.md`，记录「现象 → 定位 → 根因」的标准套路。

---

## 6. 前置条件

- TRACE32 软件（Linux demo 需要 `VERSION.BUILD >= 144217`，见官方 `ramdump.cmm` 的 `checkSanity`）。
- Linux demo 资源已解压（`board/simulator/armv8/kernel/vmlinux` 等存在；若缺失需先解压 `board/simulator/armv8/bin.zip`）。
- 当前默认面向 **TRACE32 模拟器（SIM，Cortex-A53）**；真机需调整 `config.cmm` 与 setup 脚本的 `pbi`/接口配置。
- **license**：`boot_to_shell` / `inject_panic` 的 setup 有几十条命令，无 license 的 session 限脚本 50 条命令（`RLM Error: No authorization`），需有效 license 才能跑通。**无 license 时用 `smoke` case 验证框架**（`./scripts/run.sh --case smoke`，<50 条命令）。
