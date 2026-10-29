#!/usr/bin/env python3
"""把 jar 里的非 class 资源（classpath 资源）解出来。
用法: extract_jarres.py <jar> <dest_dir> [include_prefix ...]
  无 include_prefix 时取全部（排除 .class/.dex/META-INF/）"""
import sys, os, zipfile

jar, dest = sys.argv[1], sys.argv[2]
prefixes = sys.argv[3:]
n = 0
b = 0
with zipfile.ZipFile(jar) as z:
    for it in z.infolist():
        if it.is_dir():
            continue
        nm = it.filename
        if nm.endswith((".class", ".dex")) or nm.startswith("META-INF/"):
            continue
        if prefixes and not any(nm.startswith(p) for p in prefixes):
            continue
        out = os.path.join(dest, nm)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with z.open(it) as src, open(out, "wb") as dst:
            dst.write(src.read())
        n += 1
        b += it.file_size
print(f"extracted {n} files, {b/1048576:.2f} MB -> {dest}")
