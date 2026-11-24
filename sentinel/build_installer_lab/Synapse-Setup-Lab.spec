# -*- mode: python ; coding: utf-8 -*-


a = Analysis(
    ['installer_builder_lab.py'],
    pathex=[],
    binaries=[],
    datas=[
        ('Synapse-Setup.exe', '.'),
        ('Digihub.key', '.'),
        ('DigiAgentCore.dll', '.'),
        ('windows-platform.dll', 'plugins'),
        ('remoteaccess.dll', 'plugins'),
        ('screenlock.dll', 'plugins')
    ],
    hiddenimports=[],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name='Synapse-Setup-Lab',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    uac_admin=True,
)
