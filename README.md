# meshAssembler MVP - Maya 2026

`meshAssembler` is a Maya `MPxNode` that assembles multiple deformed **source** meshes into one connected **target** mesh.

It does **not** perform topology merge. It uses a target orig shape as the output topology template and replaces only point positions from source meshes.

```text
sourceAMesh
sourceBMesh
targetMesh
        -> createMeshAssembler creates source/target Orig shapes
        -> meshAssembler -> targetMesh
```

## Source layout

```text
src/
├─ MeshAssemblerNode.h
├─ MeshAssemblerNode.cpp   # node attributes, mapping cache, compute
└─ MeshAssemblerCmd.cpp    # create/rebuild commands and plugin registration
```

## Build

```bat
cmake -S . -B build -DMAYA_LOCATION="C:/Program Files/Autodesk/Maya2026"
cmake --build build --config Release
```

Output:

```text
build/Release/meshAssembler.mll
```

## Load

```python
from maya import cmds
cmds.loadPlugin(r"C:/path/to/meshAssembler.mll")
```

## Commands

The plugin registers:

```text
node type: meshAssembler
create:    createMeshAssembler
rebuild:   rebuildMeshAssembler
```

## Create usage

Orig meshes are created automatically from the current mesh states.

Unlike earlier versions, orig snapshots are **not** separate hidden transforms. They are intermediate mesh shapes under the same transform as each source/target shape.

Example:

```text
source_mesh
├─ source_meshShape       # live shape
└─ source_meshShapeOrig   # generated intermediate orig shape
```

For the target:

```text
target_mesh
├─ target_meshShape       # receives meshAssembler.outputMesh
└─ target_meshShapeOrig   # generated intermediate topology/mapping template
```

## Selection-order mode

If no `-target` / `-source` arguments are specified, the command uses the current selection order:

```text
selected[0..n-2] = sources
selected[n-1]    = target
```

Example:

```mel
select -r "sourceAMesh" "sourceBMesh" "targetMesh";
createMeshAssembler;
```

Python equivalent:

```python
from maya import cmds

cmds.select("sourceAMesh", "sourceBMesh", "targetMesh", r=True)
node = cmds.createMeshAssembler()
```

## Explicit argument mode

```mel
createMeshAssembler \
    -target "targetMesh" \
    -source "sourceAMesh" \
    -source "sourceBMesh";
```

Python equivalent:

```python
from maya import cmds

node = cmds.createMeshAssembler(
    target="targetMesh",
    source=["sourceAMesh", "sourceBMesh"],
)
```

`tolerance` is optional. If omitted, the node default is used.

```mel
createMeshAssembler \
    -target "targetMesh" \
    -source "sourceAMesh" \
    -source "sourceBMesh" \
    -tolerance 0.0001;
```

## Optional name

`-name` is optional. If omitted, Maya auto-names the node.

```mel
createMeshAssembler \
    -name "body_meshAssembler" \
    -target "targetMesh" \
    -source "sourceAMesh" \
    -source "sourceBMesh";
```

## What `createMeshAssembler` does

For each target/source mesh:

```text
liveShape -> liveShapeOrig
```

The generated `Orig` shape is an intermediate object under the same transform as the live shape.

Then the command connects:

```text
targetMeshShapeOrig.outMesh -> meshAssembler.targetOrigMesh
sourceMeshShapeOrig.outMesh -> meshAssembler.source[i].origMesh
sourceMeshShape.outMesh     -> meshAssembler.source[i].mesh
meshAssembler.outputMesh    -> targetMeshShape.inMesh
```

## Force mapping rebuild

```mel
rebuildMeshAssembler -node "meshAssembler1";
```

Python:

```python
cmds.rebuildMeshAssembler(node="meshAssembler1")
```

## Create command flags

```text
-target    / -t    <target mesh or transform>    optional if using selection-order mode
-source    / -s    <source mesh or transform>    multi-use, optional if using selection-order mode
-name      / -n    <node name>                   optional; omitted = Maya auto-name
-tolerance / -tol  <double>                      optional, default 0.0001
```

If `-target` / `-source` are omitted, select source meshes first and the target mesh last, then run `createMeshAssembler`.

Transforms are accepted. The command resolves the first non-intermediate mesh shape under each transform.

## Behavior

For each vertex of `targetMeshShapeOrig`, the node searches matching vertices on source `Orig` shapes by object-space position.

- one matching source: copy corresponding live source point
- multiple matching sources: average corresponding live source points
- no matching source: keep target orig point

The output mesh uses the topology of `targetMeshShapeOrig`.

## Node attributes

The command wires these automatically:

```text
targetOrigMesh  : mesh
source[]        : compound array
    origMesh    : mesh
    mesh        : mesh
matchTolerance  : double  # default 0.0001
rebuildTrigger  : int     # internal dirty/rebuild trigger
outputMesh      : mesh
```

Manual attribute wiring is still possible, but normal use should go through `createMeshAssembler`.

## Naming rules in this version

- `target` means the connected mesh that receives the assembled result.
- `source` means each live/deformed segmented mesh used to supply point positions.
- In selection-order mode, the last selected mesh is treated as `target`; all previous selected meshes are treated as `source`.
- `targetOrigMesh` is connected from the generated `targetMeshShapeOrig` intermediate shape.
- `source[i].origMesh` is connected from the generated `sourceMeshShapeOrig` intermediate shape.
- `source[i].mesh` is the live source mesh.

## Important limitations

- The mapping is position-based.
- Selection-order mode depends on Maya's active selection order. If the order is not preserved in your Maya preferences/session, use explicit `-target` and `-source` flags.
- Runtime does not perform closest point search or topology rebuild.
- If the source meshes' topology changes after creation, create the assembler again.
- `MTypeId(0x0013F001)` is temporary. Replace it with your own registered ID before production use.
