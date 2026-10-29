#!/usr/bin/env python3
"""把 FCL 的 MC 26.2 assets（objects+indexes）复制进探针 app 的私有目录。
MC 原生支持 objects/xx/hash 结构（IndexedAssetSource.createIndexFs + LinkFileSystem），
所以不需要解压成 minecraft/... 目录树。
"""
import json, os, shutil, sys

SRC = '/sdcard/FCL/.minecraft/assets'
INDEX = sys.argv[1] if len(sys.argv) > 1 else '32'
DST = sys.argv[2] if len(sys.argv) > 2 else '/data/data/com.example.mcart/files/assets'

os.makedirs(DST + '/objects', exist_ok=True)
os.makedirs(DST + '/indexes', exist_ok=True)

shutil.copy(SRC + '/indexes/' + INDEX + '.json', DST + '/indexes/' + INDEX + '.json')

d = json.load(open(SRC + '/indexes/' + INDEX + '.json'))
objs = d.get('objects', {})
n = skipped = missing = 0
for k, v in objs.items():
    h = v['hash']
    src = SRC + '/objects/' + h[:2] + '/' + h
    d2 = DST + '/objects/' + h[:2]
    if not os.path.exists(d2):
        os.makedirs(d2, exist_ok=True)
    tgt = d2 + '/' + h
    if os.path.exists(tgt):
        skipped += 1
        continue
    if not os.path.exists(src):
        missing += 1
        print('MISSING', k, h, flush=True)
        continue
    shutil.copy(src, tgt)
    n += 1

print('DONE copied=%d skipped=%d missing=%d total_entries=%d' % (n, skipped, missing, len(objs)), flush=True)
