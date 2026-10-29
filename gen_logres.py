#!/usr/bin/env python3
"""收集各 jar 的 META-INF/services 与 log4j 插件缓存，合并到 logres/（供 APK 打包）。
同名 service 文件会被合并去重（ServiceLoader 只认 APK 里唯一一份）。"""
import sys, os, glob, zipfile
from collections import OrderedDict

# 允许从任意 cwd 调用：统一以脚本所在目录（mc-work）为基准
os.chdir(os.path.dirname(os.path.abspath(__file__)))
out = sys.argv[1] if len(sys.argv) > 1 else "logres"
jars = ["mcpatched.jar"] + sorted(glob.glob("libs26/*.jar"))
extra = ["META-INF/org/apache/logging/log4j/core/config/plugins/Log4j2Plugins.dat"]

merged = OrderedDict()   # name -> list of lines
plain = {}               # name -> bytes

for j in jars:
    try:
        z = zipfile.ZipFile(j)
    except Exception:
        continue
    for it in z.infolist():
        nm = it.filename
        if it.is_dir():
            continue
        if nm.startswith("META-INF/services/"):
            txt = z.read(it).decode("utf-8", "replace")
            merged.setdefault(nm, [])
            for line in txt.splitlines():
                line = line.strip()
                if line and not line.startswith("#") and line not in merged[nm]:
                    merged[nm].append(line)
        elif nm in extra:
            plain[nm] = z.read(it)

os.makedirs(out, exist_ok=True)
n = 0
for nm, lines in merged.items():
    dst = os.path.join(out, nm)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    n += 1
for nm, data in plain.items():
    dst = os.path.join(out, nm)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, "wb") as f:
        f.write(data)
    n += 1
print(f"wrote {n} files -> {out}")
for nm in merged:
    if "log4j" in nm or "slf4j" in nm:
        print(f"  {nm}: {merged[nm]}")
