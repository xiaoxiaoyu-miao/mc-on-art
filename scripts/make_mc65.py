#!/usr/bin/env python3
"""26.2-client.jar（class v69，Java 25）-> mc65.jar（class v65），只给 javac 读用。

javac 21 读不了 v69 的字节码，但它只需要「能解析」；把 class 文件的
major version 从 69 改成 65 就能编过（d8 9.2.4 本身就吃 v69，dex 阶段不用这个副本）。

用法：python3 scripts/make_mc65.py
"""
import pathlib
import struct
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "26.2-client.jar"
DST = ROOT / "mc65.jar"
TARGET_MAJOR = 65


def main():
    if not SRC.exists():
        print(f"!! 缺 {SRC.name}（先跑 scripts/fetch_client_and_libs.py --client）")
        return 1
    n = 0
    with zipfile.ZipFile(SRC) as zin, zipfile.ZipFile(DST, "w", zipfile.ZIP_DEFLATED) as zout:
        for item in zin.infolist():
            data = zin.read(item.filename)
            if item.filename.endswith(".class") and len(data) >= 8:
                # 0..3 magic, 4..5 minor, 6..7 major
                major = struct.unpack(">H", data[6:8])[0]
                if major > TARGET_MAJOR:
                    data = data[:6] + struct.pack(">H", TARGET_MAJOR) + data[8:]
                    n += 1
            zout.writestr(item, data)
    print(f"ok: {DST.name}（改写了 {n} 个 class 的 major version）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
