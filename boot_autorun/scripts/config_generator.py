#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
config_generator.py - JSON配置文件 → 二进制配置数据转换器

本工具将JSON格式的配置文件转换为符合 autorun_config.h 定义的二进制结构体。
生成的二进制数据可直接烧录到Target侧内存（SRAM/DDR），供自动运行框架读取执行。

功能:
1. 解析JSON配置文件 (config_template.json 或 configs/config_autorun_*.json)
2. 校验配置合法性和字段约束
3. 生成符合 autorun_config_t 结构体定义的二进制数据
4. 计算CRC32校验值并填充到头部
5. 支持输出完整配置或单阶段配置
6. 支持多种输出格式 (raw binary, C header, hex dump)

使用方法:
    # 基本用法：转换完整配置
    python3 config_generator.py config_template.json -o output.bin

    # 仅提取BL1阶段配置
    python3 config_generator.py config_template.json --stage bl1 -o bl1_config.bin

    # 输出C语言头文件格式
    python3 config_generator.py config_template.json -o autorun_config_data.h --format c-header

    # 显示详细信息
    python3 config_generator.py config_template.json --verbose

依赖:
    - Python 3.6+
    - json (标准库)
    - struct (标准库)
    - binascii (标准库)

作者: AutoRun Framework Team
版本: 1.0.0
日期: 2026-01-01
"""

import json
import struct
import binascii
import argparse
import sys
import os
from typing import Dict, List, Tuple, Optional, Any, Union
from dataclasses import dataclass, field
from enum import IntEnum


# ==============================================================================
# 常量定义 (与 autorun_config.h 保持一致)
# ==============================================================================

class Constants:
    """autorun_config.h 中定义的所有常量"""
    
    # 魔数
    AUTORUN_MAGIC = 0xA55A5AA5
    AUTORUN_RESULT_MAGIC = 0xBEEFBEEF
    
    # 版本号
    VERSION_MAJOR = 1
    VERSION_MINOR = 0
    VERSION_PATCH = 0
    
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


# ==============================================================================
# 枚举类型 (与 autorun_config.h 保持一致)
# ==============================================================================

class LogLevel(IntEnum):
    """日志级别"""
    OFF = 0
    ERROR = 1
    WARN = 2
    INFO = 3
    DEBUG = 4
    VERBOSE = 5


class ExpectType(IntEnum):
    """期望结果类型"""
    RETURN_CODE = 0
    STRING_MATCH = 1
    REGEX = 2
    CHECKSUM = 3
    JSON_OUTPUT = 4
    RANGE = 5
    RESERVED_6 = 6
    RESERVED_7 = 7


class ChecksumAlg(IntEnum):
    """校验算法"""
    CRC32 = 0
    MD5 = 1
    SHA256 = 2
    CRC16 = 3
    XOR = 4


class TaskStatus(IntEnum):
    """任务状态 (用于结果)"""
    PENDING = 0
    RUNNING = 1
    SUCCESS = 2
    FAILED = 3
    TIMEOUT = 4
    SKIPPED = 5
    ERROR = 6


class ConditionOp(IntEnum):
    """条件操作符"""
    EQUAL = 0
    NOT_EQUAL = 1
    GREATER = 2
    GREATER_EQ = 3
    LESS = 4
    LESS_EQ = 5
    CONTAINS = 6
    START_WITH = 7
    END_WITH = 8
    REGEX_MATCH = 9


# ==============================================================================
# 数据类定义 (对应 autorun_config.h 的结构体)
# ==============================================================================

@dataclass
class ExpectedResult:
    """期望结果 (对应 expected_result_t)"""
    expect_type: ExpectType
    
    # Return code
    expected_code: int = 0
    
    # String match / Regex
    pattern: str = ""
    
    # Checksum
    checksum_alg: ChecksumAlg = ChecksumAlg.CRC32
    checksum_expected_value: int = 0
    checksum_data_source: str = "stdout"
    
    # JSON output
    check_keys: List[str] = field(default_factory=list)
    key_count: int = 0
    value_conditions: List[Dict] = field(default_factory=list)
    
    # Range
    range_min_val: int = 0
    range_max_val: int = 0
    range_extract_regex: str = ""


@dataclass
class EnvVar:
    """环境变量 (对应 env_var_t)"""
    key: str = ""
    val: str = ""


@dataclass
class TaskConfig:
    """任务配置 (对应 task_config_t)"""
    id: int
    name: str
    command: str
    enabled: bool = True
    critical: bool = False
    timeout_ms: int = 5000
    retry_count: int = 0
    retry_delay_ms: int = 1000
    expected_result: ExpectedResult = None
    log_output: bool = True
    env_vars: List[EnvVar] = field(default_factory=list)


@dataclass
class StageConfig:
    """阶段配置 (对应 stage_config_t)"""
    stage_name: str
    enabled: bool = True
    execution_order: int = 0
    pre_commands: List[str] = field(default_factory=list)
    post_commands: List[str] = field(default_factory=list)
    tasks: List[TaskConfig] = field(default_factory=list)


@dataclass
class GlobalConfig:
    """全局配置 (对应 autorun_global_config_t)"""
    log_level: int = LogLevel.INFO
    continue_on_error: bool = False
    global_timeout_s: int = 300
    result_output_addr: int = 0x12340000
    result_max_size: int = 8192
    halt_on_completion: bool = False


@dataclass
class ConfigHeader:
    """配置头部 (对应 autorun_config_header_t)"""
    magic: int = Constants.AUTORUN_MAGIC
    version_major: int = Constants.VERSION_MAJOR
    version_minor: int = Constants.VERSION_MINOR
    config_size: int = 0
    crc32: int = 0


@dataclass
class AutorunConfig:
    """完整配置 (对应 autorun_config_t)"""
    header: ConfigHeader = None
    global_config: GlobalConfig = None
    stage_count: int = 0
    stages: List[StageConfig] = field(default_factory=list)


# ==============================================================================
# 二进制编码器 (严格按照 autorun_config.h 结构体布局)
# ==============================================================================

class BinaryEncoder:
    """
    二进制编码器
    
    将 Python 数据对象序列化为符合 autorun_config.h 定义的二进制格式。
    使用 struct 模块确保字节序和数据类型正确性。
    """
    
    def __init__(self):
        self.buffer = bytearray()
        self.verbose = False
    
    def _log(self, msg: str):
        """打印详细日志"""
        if self.verbose:
            print(f"  [ENCODER] {msg}")
    
    @staticmethod
    def _pad_string(s: str, max_len: int) -> bytes:
        """
        将字符串填充到固定长度 (含终止符)
        
        规则:
        - 如果字符串长度 < max_len-1，用 \\0 填充剩余空间
        - 如果字符串长度 >= max_len-1，截断并添加终止符
        - 总是返回恰好 max_len 字节
        """
        encoded = s.encode('utf-8')[:max_len - 1]
        return encoded.ljust(max_len, b'\x00')
    
    def encode_expected_result(self, result: ExpectedResult) -> bytes:
        """
        编码期望结果 (expected_result_t)
        
        内存布局:
        ┌──────────────────┬─────────────────────────────┐
        │ type (1 byte)     │ union (变长, ~1297 bytes)   │
        └──────────────────┴─────────────────────────────┘
        """
        buf = bytearray()
        
        # type (1 byte)
        buf += struct.pack('<B', result.expect_type)
        
        # 联合体 (根据 type 选择不同的成员)
        if result.expect_type == ExpectType.RETURN_CODE:
            # return_code.expected_code (4 bytes)
            buf += struct.pack('<i', result.expected_code)
            # 填充到联合体最大大小 (~1297 bytes)
            buf += b'\x00' * (1297 - 4)
            
        elif result.expect_type in [ExpectType.STRING_MATCH, ExpectType.REGEX]:
            # pattern (128 bytes)
            buf += self._pad_string(result.pattern, Constants.PATTERN_MAX_LEN)
            # 填充剩余部分
            buf += b'\x00' * (1297 - Constants.PATTERN_MAX_LEN)
            
        elif result.expect_type == ExpectType.CHECKSUM:
            # checksum 结构体
            buf += struct.pack('<B', result.checksum_alg)      # algorithm (1 byte)
            buf += struct.pack('<I', result.checksum_expected_value)  # expected_value (4 bytes)
            buf += self._pad_string(result.checksum_data_source, 64)  # data_source (64 bytes)
            # 填充剩余
            buf += b'\x00' * (1297 - 1 - 4 - 64)
            
        elif result.expect_type == ExpectType.JSON_OUTPUT:
            # json_output 结构体 (最大的成员)
            # check_keys[8][48] = 384 bytes
            for i in range(Constants.MAX_CHECK_KEYS):
                if i < len(result.check_keys):
                    buf += self._pad_string(result.check_keys[i], Constants.JSON_KEY_MAX_LEN)
                else:
                    buf += b'\x00' * Constants.JSON_KEY_MAX_LEN
            
            # key_count (1 byte)
            buf += struct.pack('<B', result.key_count)
            
            # value_conditions[8] (每个约 120 bytes, 共 ~960 bytes)
            for i in range(Constants.MAX_CHECK_KEYS):
                if i < len(result.value_conditions):
                    cond = result.value_conditions[i]
                    buf += self._pad_string(cond.get('key_name', ''), Constants.JSON_KEY_MAX_LEN)  # 48 bytes
                    buf += struct.pack('<B', cond.get('op', 0))  # op (1 byte)
                    buf += self._pad_string(cond.get('expected_value', ''), 64)  # expected_value (64 bytes)
                    buf += struct.pack('<?', cond.get('enabled', False))  # enabled (1 byte)
                    # 填充到每个条件固定大小
                    buf += b'\x00' * (120 - 48 - 1 - 64 - 1)
                else:
                    buf += b'\x00' * 120
            
            # 确保总大小为 1297 bytes
            current_size = len(buf)
            if current_size < 1297:
                buf += b'\x00' * (1297 - current_size)
                
        elif result.expect_type == ExpectType.RANGE:
            # range 结构体
            buf += struct.pack('<i', result.range_min_val)   # min_val (4 bytes)
            buf += struct.pack('<i', result.range_max_val)   # max_val (4 bytes)
            buf += self._pad_string(result.range_extract_regex, 64)  # extract_regex (64 bytes)
            # 填充剩余
            buf += b'\x00' * (1297 - 4 - 4 - 64)
        else:
            # 未知类型，填充零
            buf += b'\x00' * 1297
        
        self._log(f"ExpectedResult: type={result.expect_type.name}, size={len(buf)}")
        return bytes(buf)
    
    def encode_env_var(self, env: EnvVar) -> bytes:
        """
        编码环境变量 (env_var_t)
        
        内存布局: key(64) + val(256) = 320 bytes
        """
        buf = bytearray()
        buf += self._pad_string(env.key, Constants.ENV_KEY_MAX_LEN)   # 64 bytes
        buf += self._pad_string(env.val, Constants.ENV_VAL_MAX_LEN)  # 256 bytes
        return bytes(buf)
    
    def encode_task(self, task: TaskConfig) -> bytes:
        """
        编码任务配置 (task_config_t)
        
        内存布局估算: ~4166 bytes
        """
        buf = bytearray()
        
        # 基本信息
        buf += struct.pack('<I', task.id)                          # id (4 bytes)
        buf += self._pad_string(task.name, Constants.TASK_NAME_MAX_LEN)  # name (32 bytes)
        buf += self._pad_string(task.command, Constants.CMD_MAX_LEN)     # command (256 bytes)
        
        # 执行控制
        buf += struct.pack('<?', task.enabled)                      # enabled (1 byte)
        buf += struct.pack('<?', task.critical)                     # critical (1 byte)
        buf += struct.pack('<I', task.timeout_ms)                   # timeout_ms (4 bytes)
        buf += struct.pack('<B', task.retry_count)                  # retry_count (1 byte)
        buf += struct.pack('<I', task.retry_delay_ms)               # retry_delay_ms (4 bytes)
        
        # 期望结果 (~1297 bytes)
        if task.expected_result:
            buf += self.encode_expected_result(task.expected_result)
        else:
            buf += b'\x00' * 1298  # type(1) + union(1297)
        
        # 日志
        buf += struct.pack('<?', task.log_output)                  # log_output (1 byte)
        buf += b'\x00' * 3                                          # reserved (3 bytes)
        
        # 环境变量
        buf += struct.pack('<B', len(task.env_vars))               # env_count (1 byte)
        for i in range(Constants.MAX_ENV_VARS):
            if i < len(task.env_vars):
                buf += self.encode_env_var(task.env_vars[i])       # 320 bytes each
            else:
                buf += b'\x00' * 320
        
        self._log(f"Task: id={task.id}, name={task.name}, size={len(buf)}")
        return bytes(buf)
    
    def encode_stage(self, stage: StageConfig) -> bytes:
        """
        编码阶段配置 (stage_config_t)
        
        内存布局估算: ~70775 bytes (含16个任务的最大情况)
        """
        buf = bytearray()
        
        # 阶段标识
        buf += self._pad_string(stage.stage_name, Constants.STAGE_NAME_MAX_LEN)  # 16 bytes
        buf += struct.pack('<?', stage.enabled)                       # enabled (1 byte)
        buf += struct.pack('<B', stage.execution_order)              # execution_order (1 byte)
        
        # Pre/Post commands
        pre_cmds = stage.pre_commands[:Constants.MAX_COMMANDS]
        post_cmds = stage.post_commands[:Constants.MAX_COMMANDS]
        
        buf += struct.pack('<B', len(pre_cmds))                      # pre_command_count (1 byte)
        buf += struct.pack('<B', len(post_cmds))                     # post_command_count (1 byte)
        
        # pre_commands[8][256] = 2048 bytes
        for i in range(Constants.MAX_COMMANDS):
            if i < len(pre_cmds):
                buf += self._pad_string(pre_cmds[i], Constants.COMMAND_STR_MAX_LEN)
            else:
                buf += b'\x00' * Constants.COMMAND_STR_MAX_LEN
        
        # post_commands[8][256] = 2048 bytes
        for i in range(Constants.MAX_COMMANDS):
            if i < len(post_cmds):
                buf += self._pad_string(post_cmds[i], Constants.COMMAND_STR_MAX_LEN)
            else:
                buf += b'\x00' * Constants.COMMAND_STR_MAX_LEN
        
        # 任务列表
        tasks = stage.tasks[:Constants.MAX_TASKS_PER_STAGE]
        buf += struct.pack('<B', len(tasks))                         # task_count (1 byte)
        buf += b'\x00' * 2                                           # reserved (2 bytes)
        
        # tasks[16] (每任务 ~4166 bytes)
        for i in range(Constants.MAX_TASKS_PER_STAGE):
            if i < len(tasks):
                buf += self.encode_task(tasks[i])
            else:
                # 空任务填充 (预估大小)
                buf += b'\x00' * 4166
        
        self._log(f"Stage: {stage.stage_name}, tasks={len(tasks)}, size={len(buf)}")
        return bytes(buf)
    
    def encode_global_config(self, gc: GlobalConfig) -> bytes:
        """
        编码全局配置 (autorun_global_config_t)
        
        内存布局: 18 bytes
        """
        buf = bytearray()
        buf += struct.pack('<B', gc.log_level)                      # log_level (1 byte)
        buf += struct.pack('<?', gc.continue_on_error)             # continue_on_error (1 byte)
        buf += struct.pack('<H', gc.global_timeout_s)               # global_timeout_s (2 bytes)
        buf += struct.pack('<I', gc.result_output_addr)             # result_output_addr (4 bytes)
        buf += struct.pack('<I', gc.result_max_size)                # result_max_size (4 bytes)
        buf += struct.pack('<?', gc.halt_on_completion)            # halt_on_completion (1 byte)
        buf += b'\x00' * 5                                           # reserved (5 bytes)
        
        self._log(f"GlobalConfig: size={len(buf)}")
        return bytes(buf)
    
    def encode_header(self, header: ConfigHeader) -> bytes:
        """
        编码配置头部 (autorun_config_header_t)
        
        内存布局: 16 bytes
        """
        buf = bytearray()
        buf += struct.pack('<I', header.magic)                      # magic (4 bytes)
        buf += struct.pack('<H', header.version_major)              # version_major (2 bytes)
        buf += struct.pack('<H', header.version_minor)              # version_minor (2 bytes)
        buf += struct.pack('<I', header.config_size)               # config_size (4 bytes)
        buf += struct.pack('<I', header.crc32)                     # crc32 (4 bytes)
        
        self._log(f"Header: magic=0x{header.magic:08X}, version={header.version_major}.{header.version_minor}")
        return bytes(buf)
    
    def encode_full_config(self, config: AutorunConfig) -> bytes:
        """
        编码完整配置 (autorun_config_t)
        
        返回完整的二进制数据，包含正确的CRC32
        """
        # 先构建不含CRC的数据
        buf = bytearray()
        
        # Header (先填0作为CRC占位)
        temp_header = ConfigHeader(
            magic=config.header.magic,
            version_major=config.header.version_major,
            version_minor=config.header.version_minor,
            config_size=0,  # 后面计算
            crc32=0         # 占位
        )
        buf += self.encode_header(temp_header)
        
        # Global config
        buf += self.encode_global_config(config.global_config)
        
        # Stage count
        buf += struct.pack('<I', config.stage_count)
        
        # Stages[8]
        stages = config.stages[:Constants.MAX_STAGES]
        for i in range(Constants.MAX_STAGES):
            if i < len(stages):
                buf += self.encode_stage(stages[i])
            else:
                # 空阶段填充
                buf += b'\x00' * 70775
        
        # 计算实际大小
        actual_size = len(buf)
        
        # 计算CRC32 (不包含crc32字段本身)
        crc = binascii.crc32(bytes(buf)) & 0xFFFFFFFF
        
        # 更新header中的size和crc
        struct.pack_into('<I', buf, 12, actual_size)  # offset 12 = config_size
        struct.pack_into('<I', buf, 16, crc)          # offset 16 = crc32
        
        self._log(f"Full config: total_size={actual_size}, crc32=0x{crc:08X}")
        return bytes(buf)


# ==============================================================================
# JSON 解析器
# ==============================================================================

class JsonParser:
    """JSON配置文件解析器"""
    
    def __init__(self, verbose: bool = False):
        self.verbose = verbose
    
    def _log(self, msg: str):
        if self.verbose:
            print(f"  [PARSER] {msg}")
    
    def parse_expected_result(self, json_data: Dict) -> ExpectedResult:
        """解析期望结果"""
        type_str = json_data.get('type', 'return_code')
        type_map = {
            'return_code': ExpectType.RETURN_CODE,
            'string_match': ExpectType.STRING_MATCH,
            'regex': ExpectType.REGEX,
            'checksum': ExpectType.CHECKSUM,
            'json_output': ExpectType.JSON_OUTPUT,
            'range': ExpectType.RANGE
        }
        
        expect_type = type_map.get(type_str, ExpectType.RETURN_CODE)
        result = ExpectedResult(expect_type=expect_type)
        
        if expect_type == ExpectType.RETURN_CODE:
            result.expected_code = json_data.get('value', 0)
            
        elif expect_type in [ExpectType.STRING_MATCH, ExpectType.REGEX]:
            result.pattern = json_data.get('pattern', '')
            
        elif expect_type == ExpectType.CHECKSUM:
            alg_str = json_data.get('algorithm', 'crc32')
            alg_map = {
                'crc32': ChecksumAlg.CRC32,
                'md5': ChecksumAlg.MD5,
                'sha256': ChecksumAlg.SHA256,
                'crc16': ChecksumAlg.CRC16,
                'xor': ChecksumAlg.XOR
            }
            result.checksum_alg = alg_map.get(alg_str, ChecksumAlg.CRC32)
            result.checksum_expected_value = int(json_data.get('expected_value', '0'), 0)
            result.checksum_data_source = json_data.get('data_source', 'stdout')
            
        elif expect_type == ExpectType.JSON_OUTPUT:
            result.check_keys = json_data.get('check_keys', [])
            result.key_count = len(result.check_keys)
            
            conditions = json_data.get('conditions', {})
            for key, cond in conditions.items():
                op_map = {
                    '==': ConditionOp.EQUAL,
                    '!=': ConditionOp.NOT_EQUAL,
                    '>': ConditionOp.GREATER,
                    '>=': ConditionOp.GREATER_EQ,
                    '<': ConditionOp.LESS,
                    '<=': ConditionOp.LESS_EQ
                }
                result.value_conditions.append({
                    'key_name': key,
                    'op': op_map.get(cond.get('op', '=='), ConditionOp.EQUAL),
                    'expected_value': str(cond.get('value', '')),
                    'enabled': True
                })
                
        elif expect_type == ExpectType.RANGE:
            result.range_min_val = json_data.get('min_val', 0)
            result.range_max_val = json_data.get('max_val', 0)
            result.range_extract_regex = json_data.get('extract_regex', '')
        
        return result
    
    def parse_env_vars(self, json_data: Dict) -> List[EnvVar]:
        """解析环境变量"""
        env_vars = []
        for key, val in json_data.items():
            env_vars.append(EnvVar(key=key, val=val))
        return env_vars
    
    def parse_task(self, json_data: Dict) -> TaskConfig:
        """解析单个任务"""
        task = TaskConfig(
            id=json_data.get('id', 0),
            name=json_data.get('name', ''),
            command=json_data.get('command', ''),
            enabled=json_data.get('enabled', True),
            critical=json_data.get('critical', False),
            timeout_ms=json_data.get('timeout_ms', 5000),
            retry_count=json_data.get('retry_count', 0),
            retry_delay_ms=json_data.get('retry_delay_ms', 1000),
            log_output=json_data.get('log_output', True)
        )
        
        # 解析期望结果
        if 'expected_result' in json_data:
            task.expected_result = self.parse_expected_result(json_data['expected_result'])
        
        # 解析环境变量
        if 'env_vars' in json_data:
            task.env_vars = self.parse_env_vars(json_data['env_vars'])
        
        self._log(f"Parsed task: id={task.id}, name={task.name}")
        return task
    
    def parse_stage(self, name: str, json_data: Dict) -> StageConfig:
        """解析单个阶段"""
        stage = StageConfig(
            stage_name=name,
            enabled=json_data.get('enabled', True),
            execution_order=json_data.get('execution_order', 0),
            pre_commands=json_data.get('pre_commands', []),
            post_commands=json_data.get('post_commands', [])
        )
        
        # 解析任务列表
        for task_json in json_data.get('tasks', []):
            stage.tasks.append(self.parse_task(task_json))
        
        self._log(f"Parsed stage: {name}, tasks={len(stage.tasks)}")
        return stage
    
    def parse_global_config(self, json_data: Dict) -> GlobalConfig:
        """解析全局配置"""
        level_str = str(json_data.get('log_level', 3))
        gc = GlobalConfig(
            log_level=int(level_str),
            continue_on_error=json_data.get('continue_on_error', False),
            global_timeout_s=json_data.get('global_timeout_s', 300),
            result_output_addr=int(json_data.get('result_output_addr', '0x12340000'), 0),
            result_max_size=json_data.get('result_max_size', 8192),
            halt_on_completion=json_data.get('halt_on_completion', False)
        )
        return gc
    
    def parse_file(self, filepath: str) -> AutorunConfig:
        """解析完整的JSON配置文件"""
        self._log(f"Parsing file: {filepath}")
        
        with open(filepath, 'r', encoding='utf-8') as f:
            data = json.load(f)
        
        # 解析元数据
        meta = data.get('config_meta', {})
        
        # 构建配置对象
        config = AutorunConfig()
        
        # Header
        config.header = ConfigHeader(
            magic=int(meta.get('magic', f'0x{Constants.AUTORUN_MAGIC:X}'), 0),
            version_major=meta.get('version', Constants.VERSION_MAJOR),
            version_minor=0,  # 使用次版本号位置存储兼容信息
            config_size=0,
            crc32=0
        )
        
        # Global config
        if 'global_config' in data:
            config.global_config = self.parse_global_config(data['global_config'])
        else:
            config.global_config = GlobalConfig()
        
        # Stages
        stages_data = data.get('stages', {})
        for stage_name, stage_data in stages_data.items():
            config.stages.append(self.parse_stage(stage_name, stage_data))
        
        config.stage_count = len(config.stages)
        
        self._log(f"Total stages: {config.stage_count}")
        return config


# ==============================================================================
# 输出格式化器
# ==============================================================================

class OutputFormatter:
    """输出格式化器"""
    
    @staticmethod
    def to_raw_binary(data: bytes, output_path: str):
        """输出原始二进制文件"""
        with open(output_path, 'wb') as f:
            f.write(data)
        print(f"✓ Binary file written: {output_path} ({len(data)} bytes)")
    
    @staticmethod
    def to_c_header(data: bytes, output_path: str, var_name: str = "autorun_config_bin"):
        """输出C语言头文件格式"""
        lines = []
        lines.append("/**")
        lines.append(" * @file autorun_config_data.h")
        lines.append(" * @brief 自动运行框架配置数据 (由 config_generator.py 自动生成)")
        lines.append(" *")
        lines.append(f" * 大小: {len(data)} bytes")
        lines.append(f" * 生成时间: 自动生成")
        lines.append(" */")
        lines.append("")
        lines.append("#ifndef AUTORUN_CONFIG_DATA_H")
        lines.append("#define AUTORUN_CONFIG_DATA_H")
        lines.append("")
        lines.append("#include <stdint.h>")
        lines.append("")
        lines.append(f"#ifdef __cplusplus")
        lines.append(f'extern "C" {{')
        lines.append("#endif")
        lines.append("")
        lines.append(f"/** 配置数据二进制数组 */")
        data_size = len(data)
        lines.append("static const uint8_t " + var_name + "[" + str(data_size) + "] = {")
        
        # 每16字节一行
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            hex_values = ', '.join(f'0x{b:02X}' for b in chunk)
            addr = i
            lines.append(f"    /* 0x{addr:04X} */ {{ {hex_values} }},")
        
        lines.append("};")
        lines.append("")
        lines.append(f"/** 配置数据大小 */")
        lines.append(f"#define AUTORUN_CONFIG_DATA_SIZE  {len(data)}")
        lines.append("")
        lines.append(f"#ifdef __cplusplus")
        lines.append(f"}}")
        lines.append("#endif")
        lines.append("")
        lines.append("#endif /* AUTORUN_CONFIG_DATA_H */")
        lines.append("")
        
        with open(output_path, 'w') as f:
            f.write('\n'.join(lines))
        
        print(f"✓ C header file written: {output_path}")
    
    @staticmethod
    def to_hex_dump(data: bytes, output_path: str = None):
        """输出十六进制转储格式"""
        lines = []
        lines.append(f"Hex Dump ({len(data)} bytes):")
        lines.append("=" * 77)
        
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            hex_part = ' '.join(f'{b:02X}' for b in chunk[:8])
            hex_part += '  ' + ' '.join(f'{b:02X}' for b in chunk[8:])
            ascii_part = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
            lines.append(f"{i:08X}  {hex_part:<49s}  |{ascii_part}|")
        
        result = '\n'.join(lines)
        
        if output_path:
            with open(output_path, 'w') as f:
                f.write(result + '\n')
            print(f"✓ Hex dump written: {output_path}")
        else:
            print(result)
        
        return result


# ==============================================================================
# 主程序
# ==============================================================================

def main():
    parser = argparse.ArgumentParser(
        description='AutoRun Framework Configuration Generator',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例用法:
  %(prog)s config_template.json -o output.bin
  %(prog)s configs/config_autorun_bl1.json --stage bl1 -o bl1.bin
  %(prog)s config_template.json -o config_data.h --format c-header
  %(prog)s config_template.json --verbose --format hex-dump
        """
    )
    
    parser.add_argument('input', help='输入JSON配置文件路径')
    parser.add_argument('-o', '--output', default=None, help='输出文件路径')
    parser.add_argument('--stage', default=None, 
                        help='仅提取指定阶段 (bl1/bl31/rtthread/uboot/linux)')
    parser.add_argument('--format', choices=['binary', 'c-header', 'hex-dump'], 
                        default='binary', help='输出格式 (默认: binary)')
    parser.add_argument('--var-name', default='autorun_config_bin',
                        help='C头文件中的变量名 (仅用于c-header格式)')
    parser.add_argument('-v', '--verbose', action='store_true', 
                        help='显示详细处理过程')
    parser.add_argument('--validate-only', action='store_true',
                        help='仅验证配置，不生成输出')
    
    args = parser.parse_args()
    
    # 检查输入文件
    if not os.path.exists(args.input):
        print(f"错误: 输入文件不存在: {args.input}", file=sys.stderr)
        sys.exit(1)
    
    try:
        # 解析JSON
        print(f"解析配置文件: {args.input}")
        json_parser = JsonParser(verbose=args.verbose)
        config = json_parser.parse_file(args.input)
        
        # 如果指定了阶段过滤
        if args.stage:
            filtered_stages = [s for s in config.stages if s.stage_name == args.stage]
            if not filtered_stages:
                print(f"错误: 未找到阶段 '{args.stage}'", file=sys.stderr)
                sys.exit(1)
            config.stages = filtered_stages
            config.stage_count = 1
            print(f"已过滤至阶段: {args.stage}")
        
        # 统计信息
        total_tasks = sum(len(s.tasks) for s in config.stages)
        print(f"配置统计:")
        print(f"  阶段数: {config.stage_count}")
        print(f"  任务总数: {total_tasks}")
        for stage in config.stages:
            print(f"    - {stage.stage_name}: {len(stage.tasks)} 个任务 ({'启用' if stage.enabled else '禁用'})")
        
        if args.validate_only:
            print("\n✓ 配置验证通过!")
            return
        
        # 编码为二进制
        encoder = BinaryEncoder()
        encoder.verbose = args.verbose
        binary_data = encoder.encode_full_config(config)
        
        # 输出
        if not args.output and args.format != 'hex-dump':
            # 默认输出文件名
            base = os.path.splitext(os.path.basename(args.input))[0]
            args.output = f"{base}.bin"
        
        if args.format == 'binary':
            OutputFormatter.to_raw_binary(binary_data, args.output)
        elif args.format == 'c-header':
            OutputFormatter.to_c_header(binary_data, args.output or 'autorun_config_data.h', args.var_name)
        elif args.format == 'hex-dump':
            OutputFormatter.to_hex_dump(binary_data, args.output)
        
        print("\n✓ 配置生成完成!")
        
    except json.JSONDecodeError as e:
        print(f"错误: JSON解析失败 - {e}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"错误: {e}", file=sys.stderr)
        if args.verbose:
            import traceback
            traceback.print_exc()
        sys.exit(1)


if __name__ == '__main__':
    main()
