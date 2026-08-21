# -*- mode: python ; coding: utf-8 -*-

a = Analysis(
    ["retoprime/app.py"],
    pathex=["."],
    binaries=[],
    datas=[("retoprime/blender_worker.py", ".")],
    hiddenimports=["retoprime.core"],
    noarchive=False,
)
pyz = PYZ(a.pure)
exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name="RETOPRIME",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    console=False,
)
