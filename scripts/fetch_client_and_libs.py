#!/usr/bin/env python3
"""下载复刻所需的官方输入：MC 客户端 jar + 全部依赖库。

用法：
    python3 scripts/fetch_client_and_libs.py            # 只下载客户端 jar
    python3 scripts/fetch_client_and_libs.py --libs     # 同时下载 130 个依赖库到 libs26/
    python3 scripts/fetch_client_and_libs.py --libs --client

需要自己持有 Minecraft 正版授权；本脚本只从 Mojang 官方分发地址抓取，
下载后按 version json 里的 sha1 校验。
"""
import argparse
import hashlib
import json
import pathlib
import sys
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
VERSION_JSON = ROOT / "26.2.json"          # 26.2 的版本元数据（仓库内已带）
CLIENT_JAR = ROOT / "26.2-client.jar"
LIBS_DIR = ROOT / "libs26"
LIBS_LIST = ROOT / "libs_list.txt"


def sha1_of(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def download(url, dest, expected_sha1=None, label=""):
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists() and expected_sha1 and sha1_of(dest) == expected_sha1:
        print(f"  已存在且校验通过: {dest.name}")
        return True
    print(f"  下载 {label or dest.name} ...")
    tmp = dest.with_suffix(dest.suffix + ".part")
    try:
        urllib.request.urlretrieve(url, tmp)
    except Exception as e:
        print(f"  !! 下载失败: {e}")
        tmp.unlink(missing_ok=True)
        return False
    if expected_sha1:
        got = sha1_of(tmp)
        if got != expected_sha1:
            print(f"  !! sha1 不符: 期望 {expected_sha1} 实得 {got}")
            tmp.unlink(missing_ok=True)
            return False
    tmp.rename(dest)
    print(f"  ok {dest.name} ({dest.stat().st_size} bytes)")
    return True


def fetch_client():
    meta = json.loads(VERSION_JSON.read_text())
    c = meta["downloads"]["client"]
    print(f"客户端 jar（版本 {meta.get('id')}，{c['size']} bytes）")
    return download(c["url"], CLIENT_JAR, c["sha1"], "26.2-client.jar")


def fetch_libs():
    if not LIBS_LIST.exists():
        print("!! 缺 libs_list.txt")
        return False
    ok = 0
    rows = [ln for ln in LIBS_LIST.read_text().splitlines() if ln.strip() and not ln.startswith("#")]
    print(f"依赖库 {len(rows)} 个 -> {LIBS_DIR}")
    for i, ln in enumerate(rows, 1):
        try:
            coord, url = ln.split("\t")
        except ValueError:
            coord, url = ln, ln
        name = coord.split(":")[1] + "-" + coord.split(":")[2] + ".jar" if ":" in coord else url.rsplit("/", 1)[-1]
        dest = LIBS_DIR / name
        if dest.exists():
            ok += 1
            continue
        if download(url, dest, None, f"[{i}/{len(rows)}] {name}"):
            ok += 1
    print(f"依赖库完成: {ok}/{len(rows)}")
    return ok == len(rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--client", action="store_true", help="下载 26.2-client.jar")
    ap.add_argument("--libs", action="store_true", help="下载 libs26/*.jar")
    a = ap.parse_args()
    if not (a.client or a.libs):
        a.client = True
    bad = False
    if a.client:
        bad |= not fetch_client()
    if a.libs:
        bad |= not fetch_libs()
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
