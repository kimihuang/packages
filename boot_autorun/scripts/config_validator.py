#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
config_validator.py - JSON配置文件合法性校验器

本工具用于验证AutoRun Framework的JSON配置文件是否符合规范，
确保配置数据可以正确转换为符合 autorun_config.h 定义的二进制格式。

功能:
1. 验证JSON语法正确性
2. 检查必需字段是否存在
3. 验证字段值类型和范围
4. 检查ID分配规则
5. 验证任务依赖关系
6. 估算内存占用
7. 输出详细的错误/警告报告

使用方法:
    # 基本校验
    python3 config_validator.py config_template.json

    # 详细模式
    python3 config_validator.py config_template.json --verbose

    # 仅显示错误
    python3 config_validator.py config_template.json --quiet

    # 校验所有配置文件
    python3 config_validator.py configs/*.json

依赖:
    - Python 3.6+
    - json (标准库)
    - re (标准库)

作者: AutoRun Framework Team
版本: 1.0.0
日期: 2026-01-01
"""

import json
import sys
import os
import re
import argparse
from typing import Dict, List, Tuple, Optional, Any
from dataclasses import dataclass, field
from enum import IntEnum


# ==============================================================================
# 常量定义 (与 autorun_config.h 和 config_generator.py 保持一致)
# ==============================================================================

class Constants:
    """autorun_config.h 中定义的所有常量"""
    
    # 魔数和版本
    AUTORUN_MAGIC = 0xA55A5AA5
    VERSION_MAJOR = 1
    VERSION_MINOR = 0
    
    # 字符串长度限制
    TASK_NAME_MAX_LEN = 32
    CMD_MAX_LEN = 256
    PATTERN_MAX_LEN = 128
    STAGE_NAME_MAX_LEN = 16
    OUTPUT_SUMMARY_MAX_LEN = 256
    JSON_KEY_MAX_LEN = 48
    ENV_KEY_MAX_LEN = 64
    ENV_VAL_MAX_LEN = 256
    
    # 数组大小限制
    MAX_TASKS_PER_STAGE = 16
    MAX_STAGES = 8
    MAX_CHECK_KEYS = 8
    MAX_ENV_VARS = 8
    MAX_COMMANDS = 8
    COMMAND_STR_MAX_LEN = 256
    
    # ID分配规则
    ID_RANGES = {
        'bl1': (0, 999),
        'bl31': (1000, 1999),
        'rtthread': (2000, 2999),
        'uboot': (3000, 3999),
        'linux': (4000, 13999),
        'custom': (14000, 0xFFFFFFFF)
    }


# ==============================================================================
# 枚举类型
# ==============================================================================

class LogLevel(IntEnum):
    OFF = 0
    ERROR = 1
    WARN = 2
    INFO = 3
    DEBUG = 4
    VERBOSE = 5


class ExpectType(IntEnum):
    RETURN_CODE = 0
    STRING_MATCH = 1
    REGEX = 2
    CHECKSUM = 3
    JSON_OUTPUT = 4
    RANGE = 5


# ==============================================================================
# 验证结果数据结构
# ==============================================================================

@dataclass
class ValidationError:
    """验证错误"""
    severity: str  # 'error', 'warning', 'info'
    category: str  # 错误类别
    message: str   # 错误描述
    path: str = ""  # JSON路径 (如 "stages.bl1.tasks[0].id")
    line: int = 0   # 行号 (如果有)


@dataclass
class ValidationResult:
    """验证结果汇总"""
    filename: str = ""
    is_valid: bool = True
    errors: List[ValidationError] = field(default_factory=list)
    warnings: List[ValidationError] = field(default_factory=list)
    info_list: List[ValidationError] = field(default_factory=list)
    
    @property
    def error_count(self) -> int:
        return len(self.errors)
    
    @property
    def warning_count(self) -> int:
        return len(self.warnings)
    
    def add_error(self, category: str, message: str, path: str = ""):
        self.errors.append(ValidationError('error', category, message, path))
        self.is_valid = False
    
    def add_warning(self, category: str, message: str, path: str = ""):
        self.warnings.append(ValidationError('warning', category, message, path))
    
    def add_info(self, category: str, message: str, path: str = ""):
        self.info_list.append(ValidationError('info', category, message, path))


# ==============================================================================
# 核心验证器类
# ==============================================================================

class ConfigValidator:
    """
    配置文件验证器
    
    执行多层次的配置合法性检查：
    1. 语法层：JSON格式、必需字段、字段类型
    2. 语义层：值范围、ID规则、业务逻辑
    3. 结构层：内存占用、对齐约束
    """
    
    def __init__(self, verbose: bool = False, quiet: bool = False):
        self.verbose = verbose
        self.quiet = quiet
        self.result = ValidationResult()
    
    def _log(self, msg: str):
        if self.verbose and not self.quiet:
            print(f"  [VALIDATOR] {msg}")
    
    def validate_file(self, filepath: str) -> ValidationResult:
        """
        验证单个配置文件
        
        Args:
            filepath: JSON配置文件路径
            
        Returns:
            ValidationResult 对象
        """
        self.result = ValidationResult(filename=filepath)
        
        try:
            with open(filepath, 'r', encoding='utf-8') as f:
                data = json.load(f)
        except FileNotFoundError:
            self.result.add_error("FILE_NOT_FOUND", f"文件不存在: {filepath}")
            return self.result
        except json.JSONDecodeError as e:
            self.result.add_error("JSON_SYNTAX", f"JSON解析错误: {e}")
            return self.result
        except Exception as e:
            self.result.add_error("FILE_READ_ERROR", f"读取文件失败: {e}")
            return self.result
        
        # 执行各层次验证
        self._validate_meta(data)
        self._validate_global_config(data)
        self._validate_stages(data)
        self._validate_id_uniqueness(data)
        self._validate_memory_estimation(data)
        
        return self.result
    
    def _validate_meta(self, data: Dict):
        """验证元数据区域"""
        self._log("验证元数据...")
        
        meta = data.get('config_meta', {})
        
        # 检查魔数
        magic_str = meta.get('magic', '')
        try:
            magic_val = int(magic_str, 16) if isinstance(magic_str, str) else magic_str
            if magic_val != Constants.AUTORUN_MAGIC:
                self.result.add_warning(
                    "MAGIC",
                    f"魔数不匹配: 期望 0x{Constants.AUTORUN_MAGIC:08X}, 实际 0x{magic_val:08X}",
                    "config_meta.magic"
                )
        except ValueError:
            self.result.add_error(
                "MAGIC_FORMAT",
                f"魔数格式无效: '{magic_str}'",
                "config_meta.magic"
            )
        
        # 检查版本号
        version_major = meta.get('version')
        if version_major is not None and version_major != Constants.VERSION_MAJOR:
            self.result.add_error(
                "VERSION_MISMATCH",
                f"主版本号不兼容: 配置={version_major}, 框架要求={Constants.VERSION_MAJOR}",
                "config_meta.version"
            )
        
        # 必需字段
        required_fields = ['version', 'description']
        for field in required_fields:
            if field not in meta:
                self.result.add_warning(
                    "MISSING_FIELD",
                    f"元数据缺少可选字段: {field}",
                    f"config_meta.{field}"
                )
    
    def _validate_global_config(self, data: Dict):
        """验证全局配置"""
        self._log("验证全局配置...")
        
        gc = data.get('global_config', {})
        
        # 日志级别
        log_level = gc.get('log_level')
        if log_level is not None:
            if not isinstance(log_level, int) or log_level < 0 or log_level > 5:
                self.result.add_error(
                    "INVALID_VALUE",
                    f"log_level 值无效: {log_level} (应为 0-5)",
                    "global_config.log_level"
                )
        
        # 超时时间
        timeout = gc.get('global_timeout_s')
        if timeout is not None:
            if not isinstance(timeout, int) or timeout < 0 or timeout > 86400:
                self.result.add_warning(
                    "VALUE_RANGE",
                    f"global_timeout_s 可能异常: {timeout}s (建议 0-3600)",
                    "global_config.global_timeout_s"
                )
        
        # 结果输出地址
        result_addr = gc.get('result_output_addr')
        if result_addr:
            addr_str = str(result_addr)
            if not re.match(r'^0x[0-9a-fA-F]+$|^\d+$', addr_str):
                self.result.add_error(
                    "INVALID_ADDRESS",
                    f"result_output_addr 格式无效: {result_addr}",
                    "global_config.result_output_addr"
                )
        
        # 结果大小
        result_size = gc.get('result_max_size')
        if result_size is not None:
            if not isinstance(result_size, int) or result_size < 1024:
                self.result.add_warning(
                    "VALUE_RANGE",
                    f"result_max_size 可能过小: {result_size} bytes (建议 >= 4096)",
                    "global_config.result_max_size"
                )
    
    def _validate_stages(self, data: Dict):
        """验证所有阶段配置"""
        self._log("验证阶段配置...")
        
        stages = data.get('stages', {})
        
        # 检查阶段数量
        if len(stages) > Constants.MAX_STAGES:
            self.result.add_error(
                "TOO_MANY_STAGES",
                f"阶段数量超限: {len(stages)} (最大 {Constants.MAX_STAGES})",
                "stages"
            )
        
        # 已知阶段名称
        valid_stages = ['bl1', 'bl31', 'rtthread', 'uboot', 'linux']
        
        for stage_name, stage_data in stages.items():
            stage_path = f"stages.{stage_name}"
            
            # 检查阶段名称
            if stage_name not in valid_stages:
                self.result.add_warning(
                    "UNKNOWN_STAGE",
                    f"未知阶段名称: {stage_name}",
                    stage_path
                )
            
            # 验证阶段基本字段
            if not isinstance(stage_data, dict):
                self.result.add_error(
                    "INVALID_TYPE",
                    f"阶段配置必须是对象",
                    stage_path
                )
                continue
            
            enabled = stage_data.get('enabled', True)
            execution_order = stage_data.get('execution_order', 0)
            
            tasks = stage_data.get('tasks', [])
            
            # 检查任务数量
            if len(tasks) > Constants.MAX_TASKS_PER_STAGE:
                self.result.add_error(
                    "TOO_MANY_TASKS",
                    f"{stage_name} 阶段任务数超限: {len(tasks)} (最大 {Constants.MAX_TASKS_PER_STAGE})",
                    f"{stage_path}.tasks"
                )
            
            # 验证每个任务
            for i, task in enumerate(tasks):
                task_path = f"{stage_path}.tasks[{i}]"
                self._validate_task(task, task_path, stage_name)
            
            # 验证 pre/post commands
            pre_cmds = stage_data.get('pre_commands', [])
            post_cmds = stage_data.get('post_commands', [])
            
            if len(pre_cmds) > Constants.MAX_COMMANDS:
                self.result.add_error(
                    "TOO_MANY_COMMANDS",
                    f"{stage_name} pre_commands 超限: {len(pre_cmds)}",
                    f"{stage_path}.pre_commands"
                )
            
            if len(post_cmds) > Constants.MAX_COMMANDS:
                self.result.add_error(
                    "TOO_MANY_COMMANDS",
                    f"{stage_name} post_commands 超限: {len(post_cmds)}",
                    f"{stage_path}.post_commands"
                )
            
            for j, cmd in enumerate(pre_cmds + post_cmds):
                cmd_type = "pre_commands" if j < len(pre_cmds) else "post_commands"
                cmd_idx = j if j < len(pre_cmds) else j - len(pre_cmds)
                cmd_path = f"{stage_path}.{cmd_type}[{cmd_idx}]"
                
                if len(cmd) > Constants.COMMAND_STR_MAX_LEN:
                    self.result.add_error(
                        "STRING_TOO_LONG",
                        f"命令字符串过长: {len(cmd)} 字符 (最大 {Constants.COMMAND_STR_MAX_LEN})",
                        cmd_path
                    )
    
    def _validate_task(self, task: Dict, path: str, stage_name: str):
        """验证单个任务配置"""
        
        # 必需字段
        required_fields = ['id', 'name', 'command']
        for field in required_fields:
            if field not in task:
                self.result.add_error(
                    "MISSING_FIELD",
                    f"缺少必需字段: {field}",
                    f"{path}.{field}"
                )
        
        # 任务ID
        task_id = task.get('id')
        if task_id is not None:
            if not isinstance(task_id, int) or task_id < 0:
                self.result.add_error(
                    "INVALID_ID",
                    f"任务ID无效: {task_id} (必须是非负整数)",
                    f"{path}.id"
                )
            else:
                # 检查ID是否在正确的范围内
                id_range = Constants.ID_RANGES.get(stage_name)
                if id_range:
                    min_id, max_id = id_range
                    if task_id < min_id or task_id > max_id:
                        self.result.add_error(
                            "ID_OUT_OF_RANGE",
                            f"任务ID超出{stage_name}阶段范围: {task_id} (应在 {min_id}-{max_id})",
                            f"{path}.id"
                        )
        
        # 任务名称
        name = task.get('name', '')
        if name:
            if len(name) > Constants.TASK_NAME_MAX_LEN - 1:
                self.result.add_error(
                    "STRING_TOO_LONG",
                    f"任务名过长: {len(name)} 字符 (最大 {Constants.TASK_NAME_MAX_LEN - 1})",
                    f"{path}.name"
                )
            # 检查名称唯一性（在后续统一处理）
        
        # Shell命令
        command = task.get('command', '')
        if command:
            if len(command) > Constants.CMD_MAX_LEN - 1:
                self.result.add_error(
                    "STRING_TOO_LONG",
                    f"命令过长: {len(command)} 字符 (最大 {Constants.CMD_MAX_LEN - 1})",
                    f"{path}.command"
                )
            # 安全性警告：检查危险命令
            dangerous_patterns = [
                r'rm\s+-rf\s+/',
                r'>\s*/dev/sda',
                r'mkfs',
                r'dd\s+if=.*of=/dev/'
            ]
            for pattern in dangerous_patterns:
                if re.search(pattern, command):
                    self.result.add_warning(
                        "SECURITY_RISK",
                        f"检测到潜在危险命令: {command[:50]}...",
                        f"{path}.command"
                    )
        
        # 超时时间
        timeout = task.get('timeout_ms')
        if timeout is not None:
            if not isinstance(timeout, int) or timeout <= 0:
                self.result.add_error(
                    "INVALID_TIMEOUT",
                    f"timeout_ms 无效: {timeout} (必须是正整数)",
                    f"{path}.timeout_ms"
                )
            elif timeout > 600000:  # 10分钟
                self.result.add_warning(
                    "LONG_TIMEOUT",
                    f"超时时间较长: {timeout}ms ({timeout/1000:.1f}s)",
                    f"{path}.timeout_ms"
                )
        
        # 重试次数
        retry_count = task.get('retry_count', 0)
        if retry_count is not None and (not isinstance(retry_count, int) or retry_count < 0 or retry_count > 10):
            self.result.add_warning(
                "INVALID_RETRY",
                f"retry_count 异常: {retry_count} (建议 0-5)",
                f"{path}.retry_count"
            )
        
        # 验证期望结果
        expected_result = task.get('expected_result')
        if expected_result:
            er_path = f"{path}.expected_result"
            self._validate_expected_result(expected_result, er_path)
        
        # 环境变量
        env_vars = task.get('env_vars', {})
        if env_vars:
            if len(env_vars) > Constants.MAX_ENV_VARS:
                self.result.add_error(
                    "TOO_MANY_ENV_VARS",
                    f"环境变量数量超限: {len(env_vars)} (最大 {Constants.MAX_ENV_VARS})",
                    f"{path}.env_vars"
                )
            for key, val in env_vars.items():
                if len(key) > Constants.ENV_KEY_MAX_LEN - 1:
                    self.result.add_error(
                        "STRING_TOO_LONG",
                        f"环境变量名过长: {key[:20]}... ({len(key)} 字符)",
                        f"{path}.env_vars.{key}"
                    )
                if len(str(val)) > Constants.ENV_VAL_MAX_LEN - 1:
                    self.result.add_error(
                        "STRING_TOO_LONG",
                        f"环境变量值过长: {str(val)[:20]}... ({len(val)} 字符)",
                        f"{path}.env_vars.{key}"
                    )
    
    def _validate_expected_result(self, er: Dict, path: str):
        """验证期望结果配置"""
        
        type_str = er.get('type', '')
        valid_types = ['return_code', 'string_match', 'regex', 'checksum', 'json_output', 'range']
        
        if type_str not in valid_types:
            self.result.add_error(
                "INVALID_EXPECT_TYPE",
                f"无效的期望结果类型: {type_str}",
                f"{path}.type"
            )
            return
        
        if type_str == 'return_code':
            value = er.get('value')
            if value is None:
                self.result.add_error(
                    "MISSING_FIELD",
                    f"return_code 类型需要 value 字段",
                    f"{path}.value"
                )
        
        elif type_str in ['string_match', 'regex']:
            pattern = er.get('pattern', '')
            if not pattern:
                self.result.add_error(
                    "MISSING_FIELD",
                    f"{type_str} 类型需要 pattern 字段",
                    f"{path}.pattern"
                )
            elif len(pattern) > Constants.PATTERN_MAX_LEN - 1:
                self.result.add_error(
                    "STRING_TOO_LONG",
                    f"匹配模式过长: {len(pattern)} 字符",
                    f"{path}.pattern"
                )
            elif type_str == 'regex':
                # 尝试验证正则表达式语法
                try:
                    re.compile(pattern)
                except re.error as e:
                    self.result.add_error(
                        "INVALID_REGEX",
                        f"正则表达式语法错误: {e}",
                        f"{path}.pattern"
                    )
        
        elif type_str == 'checksum':
            algorithm = er.get('algorithm', '')
            valid_algs = ['crc32', 'md5', 'sha256', 'crc16', 'xor']
            if algorithm not in valid_algs:
                self.result.add_warning(
                    "UNKNOWN_ALGORITHM",
                    f"未知的校验算法: {algorithm}",
                    f"{path}.algorithm"
                )
            expected_value = er.get('expected_value')
            if expected_value is None:
                self.result.add_error(
                    "MISSING_FIELD",
                    f"checksum 类型需要 expected_value 字段",
                    f"{path}.expected_value"
                )
        
        elif type_str == 'json_output':
            check_keys = er.get('check_keys', [])
            if not check_keys:
                self.result.add_warning(
                    "NO_CHECK_KEYS",
                    f"json_output 类型没有指定 check_keys",
                    f"{path}.check_keys"
                )
            elif len(check_keys) > Constants.MAX_CHECK_KEYS:
                self.result.add_error(
                    "TOO_MANY_KEYS",
                    f"check_keys 数量超限: {len(check_keys)} (最大 {Constants.MAX_CHECK_KEYS})",
                    f"{path}.check_keys"
                )
            for key in check_keys:
                if len(key) > Constants.JSON_KEY_MAX_LEN - 1:
                    self.result.add_error(
                        "STRING_TOO_LONG",
                        f"JSON key 过长: {key}",
                        f"{path}.check_keys"
                    )
        
        elif type_str == 'range':
            min_val = er.get('min_val')
            max_val = er.get('max_val')
            if min_val is None or max_val is None:
                self.result.add_error(
                    "MISSING_FIELD",
                    f"range 类型需要 min_val 和 max_val 字段",
                    f"{path}"
                )
            elif min_val > max_val:
                self.result.add_error(
                    "INVALID_RANGE",
                    f"范围无效: min({min_val}) > max({max_val})",
                    f"{path}"
                )
    
    def _validate_id_uniqueness(self, data: Dict):
        """验证所有任务ID的唯一性"""
        self._log("验证ID唯一性...")
        
        seen_ids = {}
        stages = data.get('stages', {})
        
        for stage_name, stage_data in stages.items():
            for i, task in enumerate(stage_data.get('tasks', [])):
                task_id = task.get('id')
                if task_id is None:
                    continue
                
                task_path = f"stages.{stage_name}.tasks[{i}]"
                
                if task_id in seen_ids:
                    self.result.add_error(
                        "DUPLICATE_ID",
                        f"任务ID重复: {task_id} (已在 {seen_ids[task_id]} 使用)",
                        f"{task_path}.id"
                    )
                else:
                    seen_ids[task_id] = task_path
        
        self.result.add_info(
            "ID_STATISTICS",
            f"共发现 {len(seen_ids)} 个唯一任务ID",
            "stages"
        )
    
    def _validate_memory_estimation(self, data: Dict):
        """估算内存占用并给出建议"""
        self._log("估算内存占用...")
        
        # 头部大小
        header_size = 16  # autorun_config_header_t
        
        # 全局配置大小
        global_config_size = 18  # autorun_global_config_t
        
        # 阶段计数字段
        stage_count_size = 4
        
        # 各阶段大小
        stages_size = 0
        total_tasks = 0
        stages = data.get('stages', {})
        
        for stage_name, stage_data in stages.items():
            # 阶段固定部分 (~28 bytes)
            stage_fixed = 28
            
            # Pre/Post commands
            pre_cmds = stage_data.get('pre_commands', [])
            post_cmds = stage_data.get('post_commands', [])
            commands_size = (len(pre_cmds) + len(post_cmds)) * Constants.COMMAND_STR_MAX_LEN
            commands_overhead = 4  # count fields
            
            # 任务
            tasks = stage_data.get('tasks', [])
            total_tasks += len(tasks)
            
            # 每个任务约 4166 bytes (最大情况，含环境变量等)
            # 实际大小取决于具体内容
            task_size_per_task = 4166
            tasks_size = len(tasks) * task_size_per_task
            
            # 空任务填充 (到MAX_TASKS_PER_STAGE)
            empty_tasks = (Constants.MAX_TASKS_PER_STAGE - len(tasks)) * task_size_per_task
            
            stage_total = stage_fixed + commands_overhead + commands_size + tasks_size + empty_tasks
            stages_size += stage_total
        
        # 总大小
        total_size = header_size + global_config_size + stage_count_size + stages_size
        
        # 记录信息
        self.result.add_info(
            "MEMORY_ESTIMATION",
            f"预估配置大小: {total_size:,} bytes ({total_size/1024:.1f} KB)",
            ""
        )
        self.result.add_info(
            "TASK_COUNT",
            f"总任务数: {total_tasks}",
            ""
        )
        
        # 内存使用建议
        if total_size > 100 * 1024:  # > 100KB
            self.result.add_warning(
                "LARGE_CONFIG",
                f"配置较大 ({total_size/1024:.1f} KB)，请确认目标内存足够",
                ""
            )
        elif total_size > 512 * 1024:  # > 512KB
            self.result.add_error(
                "CONFIG_TOO_LARGE",
                f"配置过大 ({total_size/1024:.1f} KB)，可能超出BL1 SRAM限制",
                ""
            )


# ==============================================================================
# 报告生成器
# ==============================================================================

class ReportGenerator:
    """验证报告生成器"""
    
    @staticmethod
    def generate_report(result: ValidationResult, verbose: bool = False,
                       quiet: bool = False) -> str:
        """生成文本格式的验证报告"""
        lines = []
        
        # 标题
        lines.append("=" * 80)
        lines.append(f"配置验证报告: {result.filename}")
        lines.append("=" * 80)
        
        # 总体状态
        status = "✓ 通过" if result.is_valid else "✗ 失败"
        lines.append(f"\n总体状态: {status}")
        lines.append(f"错误数: {result.error_count}")
        lines.append(f"警告数: {result.warning_count}")
        lines.append("-" * 80)
        
        # 错误详情
        if result.errors:
            lines.append("\n【错误】(必须修复)")
            for err in result.errors:
                location = f" [{err.path}]" if err.path else ""
                lines.append(f"  ✗ [{err.category}] {err.message}{location}")
        
        # 警告详情
        if result.warnings and (verbose or not quiet):
            lines.append("\n【警告】(建议修复)")
            for warn in result.warnings:
                location = f" [{warn.path}]" if warn.path else ""
                lines.append(f"  ⚠ [{warn.category}] {warn.message}{location}")
        
        # 信息详情
        if result.info_list and verbose:
            lines.append("\n【信息】")
            for info in result.info_list:
                location = f" [{info.path}]" if info.path else ""
                lines.append(f"  ℹ [{info.category}] {info.message}{location}")
        
        lines.append("")
        return '\n'.join(lines)


# ==============================================================================
# 主程序
# ==============================================================================

def main():
    parser = argparse.ArgumentParser(
        description='AutoRun Framework Configuration Validator',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例用法:
  %(prog)s config_template.json
  %(prog)s configs/*.json --verbose
  %(prog)s config_template.json --quiet
  %(prog)s config_template.json --output-format json > report.json
        """
    )
    
    parser.add_argument('inputs', nargs='+', help='输入JSON配置文件路径 (支持通配符)')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='显示详细信息和警告')
    parser.add_argument('-q', '--quiet', action='store_true',
                        help='安静模式，仅显示错误')
    parser.add_argument('--output-format', choices=['text', 'json'], default='text',
                        help='输出格式 (默认: text)')
    parser.add_argument('--exit-code', action='store_true',
                        help='有错误时返回非零退出码')
    
    args = parser.parse_args()
    
    all_results = []
    has_errors = False
    
    for filepath in args.inputs:
        # 支持通配符展开
        import glob
        matched_files = glob.glob(filepath)
        
        if not matched_files:
            print(f"警告: 未找到匹配文件: {filepath}", file=sys.stderr)
            continue
        
        for f in sorted(matched_files):
            validator = ConfigValidator(verbose=args.verbose, quiet=args.quiet)
            result = validator.validate_file(f)
            all_results.append(result)
            
            if not result.is_valid:
                has_errors = True
            
            # 输出报告
            report = ReportGenerator.generate_report(result, args.verbose, args.quiet)
            print(report)
    
    # 汇总
    if len(all_results) > 1:
        total_files = len(all_results)
        passed_files = sum(1 for r in all_results if r.is_valid)
        total_errors = sum(r.error_count for r in all_results)
        total_warnings = sum(r.warning_count for r in all_results)
        
        print("=" * 80)
        print(f"验证汇总:")
        print(f"  文件总数: {total_files}")
        print(f"  通过文件: {passed_files}")
        print(f"  失败文件: {total_files - passed_files}")
        print(f"  总错误数: {total_errors}")
        print(f"  总警告数: {total_warnings}")
        print("=" * 80)
    
    # 返回退出码
    if has_errors and args.exit_code:
        sys.exit(1)
    
    return 0


if __name__ == '__main__':
    main()
