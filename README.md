# RETOPRIME

RETOPRIME is a Windows desktop front end for Blender's QuadriFlow retopology engine.

## What it does

- Imports FBX and OBJ meshes
- Generates quad-based lower-resolution topology
- Supports target face count, symmetry, sharp-edge preservation, and surface projection
- Exports FBX or OBJ

## Requirements

- Windows 10 or 11
- Blender 4.0 or newer installed in its normal location

## Build the EXE

Run `build_windows.bat`. The executable is created at `dist\RETOPRIME.exe`.

## Use

1. Open `RETOPRIME.exe`.
2. Select an FBX or OBJ model.
3. Choose the target face count and options.
4. Select an output file.
5. Press **RETOPOLOGISE**.

QuadriFlow can take several minutes on dense meshes. Save other Blender work before processing very large assets.
