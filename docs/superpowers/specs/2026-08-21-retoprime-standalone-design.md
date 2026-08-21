# RETOPRIME Standalone Design

Date: 21 August 2026  
Status: User-approved design awaiting implementation-plan approval

## Product Goal

RETOPRIME is a self-contained Windows desktop application for converting a high-resolution FBX or OBJ mesh into a lower-resolution, editable quad cage. It installs and runs without Blender or another DCC package.

The central experience follows the supplied reference: a shaded grey high-poly model remains visible while a blue quad cage is generated and displayed directly over its surface. The user can adjust generation settings, inspect the topology, edit problem areas, and export the result.

## Version 1 Scope

Version 1 will provide:

- A native Windows 10/11 x64 installer.
- FBX and OBJ import.
- Automatic field-aligned quad remeshing.
- A real-time 3D viewport showing high-poly, low-poly, or both.
- Adjustable retopology settings with sliders and presets.
- Symmetry and feature-edge guidance.
- Selection and movement of generated vertices, edges, and faces.
- A density brush for asking for more or less topology in selected regions.
- Projection of edited cage vertices back onto the source surface.
- FBX and OBJ export.
- A distinctive RETOPRIME executable, desktop, installer, and taskbar icon.

Version 1 will not attempt UV unwrapping, texture baking, rig transfer, animation transfer, or sculpting. These can be separate later milestones after the retopology workflow is stable.

## Technical Architecture

RETOPRIME will be a native C++20 application made from four isolated components:

1. **Desktop shell** — Qt Widgets supplies the main window, menus, controls, file dialogs, progress reporting, and Windows integration.
2. **Viewport** — a custom OpenGL viewport renders the high-poly source and quad cage, handles camera navigation and picking, and displays vertices, edges, faces, symmetry, and density overlays.
3. **Mesh pipeline** — Assimp imports and exports FBX/OBJ data; RETOPRIME normalises scene transforms and separates geometry from materials and metadata.
4. **Retopology engine** — an embedded, adapted Instant Meshes field-aligned remeshing core generates orientation/position fields and extracts quad-dominant topology. RETOPRIME wraps this in a stable engine interface so the algorithm can be improved later without rewriting the UI.

All runtime libraries and engine code required by the application will be included in the installer. No Blender, Python installation, external command-line program, or online service is required.

## Interface

### Main layout

- **Top bar:** RETOPRIME logo, File, Edit, View, Help, Import and Export.
- **Left toolbar:** Select, Move, Smooth, Density Brush, Relax, Project, Symmetry and camera framing.
- **Centre viewport:** the largest area; dark neutral background with the high-poly model in grey and generated cage in blue.
- **Right control panel:** Retopology, Display and Export sections.
- **Bottom status bar:** input/output counts, processing progress, selection mode, warnings and ready state.

### Viewport controls

- Left-drag: orbit.
- Middle-drag: pan.
- Mouse wheel: zoom.
- F: frame selected/all.
- 1/2/3: vertex, edge and face selection.
- High-poly, cage, wireframe and X-ray visibility toggles.
- Before/after split comparison.
- Blue translucent cage faces, dark edges, and readable vertex points similar to the supplied reference.

### Retopology settings

The right panel will expose:

- Target polygon count: 500 to 2,000,000.
- Overall topology density.
- Minimum and maximum face size.
- Adaptive detail.
- Surface projection strength.
- Smoothness.
- Feature preservation.
- Sharp-edge sensitivity.
- Quad regularity.
- Symmetry tolerance.
- Symmetry axis: Off, X, Y, or Z.

Sliders update an estimate immediately. Expensive topology generation starts only when the user presses **Generate Quad Cage**, preventing accidental long recalculations.

### Editing

After generation the user can:

- Select and move cage vertices, edges, and faces.
- Brush regions to increase or decrease density before regeneration.
- Relax selected vertices while keeping them projected to the high-poly surface.
- Re-project selected or all vertices.
- Mirror edits across the chosen symmetry axis.
- Undo and redo editing operations.

## Processing Flow

1. The user imports an FBX or OBJ.
2. RETOPRIME validates the file and builds an internal indexed mesh.
3. The viewport displays the high-poly source and reports triangle count, scale and mesh warnings.
4. The user sets target count and topology controls or applies a preset.
5. The engine calculates the orientation field, position field and quad-dominant output.
6. The generated cage is projected to the original surface and displayed in blue.
7. The user reviews and edits the cage, using density painting and regeneration where needed.
8. RETOPRIME validates the output and exports FBX or OBJ.

Processing runs on a worker thread. The interface remains responsive and supports cancellation between engine stages.

## Presets

- Game Prop: 5,000 faces.
- Character: 25,000 faces.
- Hero Asset: 75,000 faces.
- Film/High Detail: 250,000 faces.
- Custom.

Presets only initialise sliders; users can change any setting.

## Logo and Windows Identity

The icon will use a bold geometric **R** constructed from connected quad faces in bright RETOPRIME blue, placed on a dark rounded-square background. A simplified small-size version will preserve a strong R silhouette without tiny grid details.

Deliverables will include:

- A multi-resolution `.ico` file containing 16, 24, 32, 48, 64, 128 and 256 px sizes.
- The icon embedded in `RETOPRIME.exe`.
- Desktop and Start menu shortcut icons.
- Installer branding and title-bar icon.
- A larger transparent PNG for application branding.

## Error Handling

- Unsupported or corrupt files show a plain-language error and leave the current project unchanged.
- Empty scenes or scenes without mesh geometry are rejected.
- Non-manifold, disconnected and degenerate source geometry produces warnings and offers safe preprocessing.
- Out-of-memory and engine failures do not overwrite an existing export.
- Exports are first written to a temporary file and moved into place only after successful validation.
- Crash-recovery data records the last imported path and editable cage state without duplicating the high-poly source file.

## Packaging

Inno Setup will create a signed-ready `RETOPRIME-Setup.exe` containing the application, Qt runtime libraries, Assimp, the remeshing engine, licences, and uninstall support. The installer will offer Start menu and desktop shortcuts. A portable ZIP can be produced from the same release build, but the installer is the primary deliverable.

## Testing and Acceptance

Automated tests will cover:

- FBX/OBJ import and export round trips.
- Settings validation and presets.
- Remeshing deterministic fixtures.
- Symmetry and projection calculations.
- Undo/redo mesh edits.
- Output validation and safe file replacement.
- Installer creation and clean install/uninstall on Windows.

The release is accepted when a clean Windows 10/11 machine with no Blender installed can install RETOPRIME, import the test high-poly meshes, generate and edit a visible quad cage, export a readable FBX and OBJ, and uninstall cleanly.

## Licensing

The design uses permissively licensed Instant Meshes algorithm code and Assimp. Qt will be dynamically linked and distributed with the notices and relinking requirements required by its LGPL terms, unless a commercial Qt licence is later supplied. A third-party notices file will ship with the installer.
