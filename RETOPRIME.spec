# -*- mode: python ; coding: utf-8 -*-

from PyInstaller.utils.hooks import collect_all

pymeshlab_datas, pymeshlab_bins, pymeshlab_hidden = collect_all("pymeshlab")
assimp_datas, assimp_bins, assimp_hidden = collect_all("assimp_py")

a = Analysis(
    ["retoprime/app.py"],
    pathex=["."],
    binaries=pymeshlab_bins + assimp_bins,
    datas=pymeshlab_datas + assimp_datas,
    hiddenimports=["retoprime.core", "retoprime.standalone_engine"] + pymeshlab_hidden + assimp_hidden,
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
