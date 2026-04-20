# Bug: elogcat Ctrl+C 无法终止进程

## 日期
2026-04-20

## 现象
`elogcat -d` 正常 dump 日志后，按 `Ctrl+C` 无法终止进程，持续输出日志，终端显示 `^C` 但进程不退出。

## 根因
1. **`signal()` 默认设置 `SA_RESTART`**: Linux 上 `signal(SIGINT, handler)` 会隐式设置 `SA_RESTART`，导致 `recv()` 被信号中断后自动重启，不返回 `EINTR`。`g_running` 虽被信号处理器置 0，但 `recv()` 永远阻塞在重启中。
2. **信号安全类型错误**: `volatile bool` 在信号处理器中不安全，应使用 `sig_atomic_t`。

## 修复
- `signal()` 替换为 `sigaction()`，`sa_flags = 0`（不设 `SA_RESTART`），确保 `recv()` 被中断后返回 `EINTR`。
- `volatile bool` 改为 `volatile sig_atomic_t`。
- `recv()` 成功返回后追加 `g_running` 检查，防止信号在 `recv()` 和循环条件之间到达。

## 关键代码
- `tools/elogcat.c` — `signal_handler()`, `setup_signals()`, `run_logcat()` 主循环
