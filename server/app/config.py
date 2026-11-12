# -*- coding: utf-8 -*-
"""配置加载。所有相对路径都相对于 server/ 目录解析。"""
import os

import yaml

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONFIG_PATH = os.environ.get("MAKOTO_CONFIG", os.path.join(ROOT, "config.yaml"))

with open(CONFIG_PATH, "r", encoding="utf-8") as f:
    CFG = yaml.safe_load(f)


def resolve(path: str) -> str:
    """把配置里的相对路径转成绝对路径。"""
    if not path:
        return path
    if os.path.isabs(path):
        return os.path.normpath(path)
    return os.path.normpath(os.path.join(ROOT, path))


def cfg_get(dotted: str, default=None):
    node = CFG
    for part in dotted.split("."):
        if not isinstance(node, dict) or part not in node:
            return default
        node = node[part]
    return node
