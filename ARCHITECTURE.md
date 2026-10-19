# Low-res 3D engine: architecture and how it works

A small software 3D engine in C99. Fixed-point integer maths, a scanline triangle rasterizer, a BSP for
draw order, real camera pitch (look up/down), player physics, an OBJ converter, and a module system so it
can grow without editing its own core.

Status in one line: core, framework, modules and all four test suites work and pass; the SDL window
program (`platform/sdl_main.c`) has been syntax-checked against a stub header but never run (the build
sandbox has no SDL2 or display). Section 17 lists every known limit.

---

## 1. Design goals

1. **Fast and light.** 160x100 8-bit palettized framebuffer, integer maths in the hot path, no SIMD, no
   floating point at runtime, no dependencies beyond libc and (for the window) SDL2.
2. **Low-res, low-poly on purpose.** Hard limits (256 vertices per mesh, 16 shades per hue) are features.
3. **Bake the expensive decisions offline.** Doom's idea, generalised to 3D: lighting is baked per
   triangle by the OBJ converter, draw order is baked into a BSP at world-build time. Runtime is a dumb
   loop over pre-decided data.
4. **Extension, not modification.** The core and the framework are frozen. New behaviour is a new module
   file plus one registration line (section 2).

---

## 2. The rule: extension, not modification

### What is frozen

| Layer | Files | Role |
|---|---|---|
| core | `core/engine.h` `shapes.h` `bsp.h` `obj.h` `text.h` `physics.h` | Maths, rasterizer, BSP, asset conversion, text, physics library |
| framework | `framework/input.h` `game.h` | Input state and the module system: **the entire plugin API** |

Their SHA-256 hashes are in `FROZEN.sha256`.

- `make verify` fails if any frozen file changed (it is also the first step of `make test`).
- `make freeze` re-records the hashes. That is a deliberate act: use it only for a real, reviewed change,
  and bump `GAME_API_VERSION` in `framework/game.h` if the plugin API itself changed.

### What is NOT frozen

`modules/`, `platform/`, `tools/`, `tests/` are ordinary code. Modules are meant to be added, and may be
edited, but the preferred way to add a feature is still a new module.

### How the core stays closed but extensible

Every place where you would otherwise edit the core is instead a data or callback hook:

| You want to... | Instead of editing the core, use |
|---|---|
| Add a feature that runs every frame | A module with an `update` hook |
| Draw 3D things or a 2D overlay | A module with `draw_world` / `draw_hud` hooks |
| Change physics numbers | `player_state_t.pp` (a `phys_params_t`, all runtime data), or the `gravity` command |
| Add new kinds of ground (heightmap, moving platform, water) | `player_add_floor_source(fn, ctx)`: any callback can provide ground |
| Share state between modules | `game_service_set` / `game_service` (named pointers) |
| Add a user-facing command | `game_cmd_register(name, fn, help)` |
| Add a new input action | `game_action_id(g, "name", 1)` allocates one in the user range |
| Change what key does what | `controls_bind` / `bind` command |
| Use a bigger font | Provide a `font_t` (data) and call `draw_text_f` |
| Add geometry at runtime | `world_add_mesh()` (BSP rebuilds before the next draw) |
| Add a new shape generator | A function in your module that fills `v3[]` + `tri_t[]` |

The proof is in `tests/test_framework.c`: an extension module defined *inside the test* adds a platform,
a command and an action using only the public API, and `modules/checkpoint.h` is a complete real example.

---

## 3. Repository map

```
engine/
  Makefile                  build, test, verify, freeze
  FROZEN.sha256             hashes of core/ and framework/
  README.md                 quick start
  docs/ARCHITECTURE.md      this file
  core/                     FROZEN
    engine.h                fixed point, trig table, palette, framebuffer, camera transform, clipping, rasterizer
    shapes.h                box_mesh() primitive generator
    bsp.h                   BSP build (spawn time) + back-to-front draw (runtime)
    obj.h                   OBJ -> mesh conversion, .m3d binary format, C-array export, baked lighting
    text.h                  bitmap text (font as data)
    physics.h               player physics library (params, floor sources, step, fly)
  framework/                FROZEN
    input.h                 platform-neutral input state, key codes, key names
    game.h                  game_t, module_t, services, commands, actions, game_frame()
  modules/                  the features (replaceable)
    controls.h              key bindings, mouse look -> actions + view angles
    world.h                 static world: triangles + BSP + ground provider
    player.h                player body: actions -> physics -> camera
    hud.h                   FPS counter
    demo_scene.h            example content: ground, cube, optional model file
    checkpoint.h            worked example of an extension
  platform/sdl_main.c       composition root + SDL2 layer (window, events, blit)
  tools/objconv.c           command-line OBJ converter
  tests/                    test_render, test_physics_obj, test_bsp, test_framework
```

Everything is header-only with `static` functions, compiled as one translation unit per program. There is no
link step and no separate object files. Consequence: module state lives in `static` globals inside each
header, so each module is a singleton (one instance per program).

---

## 4. Conventions and units

Getting these wrong is the most common source of bugs, so they are all here.

**Fixed point.** `fx` is `int32_t` in 16.16 format: 65536 = 1.0. `FXF(x)` converts a literal.
`fxmul(a,b)` is `(int64)a*b >> 16`. Anywhere two `fx` values are multiplied the product is widened to
int64 first. Range is about +/-32768 units; resolution 1/65536.

**World units.** One unit is roughly a metre: the player is 1.6 units to the eye, a jump rises about 1.2.

**Coordinate system.** Left-handed: **x right, y up, z forward**. The camera looks down +z at yaw 0.

**Angles.** `uint16_t`, where 65536 = a full turn. `DEG(d)` converts degrees. Positive yaw turns right
(toward +x); positive pitch looks up. The sine table has 1024 entries (about 0.35 degrees per step),
indexed by `angle >> 6`; cosine is the sine table shifted by 256 entries. There is no interpolation, so
very slow turns move in 0.35-degree steps.

**Framebuffer and palette.** `fb[SW*SH]` is 8-bit, with `SW=160`, `SH=100`. A pixel value is
`hue*16 + shade`: 16 hues, 16 shades each. Shade 0 is the full colour; shade 15 is the fog colour
(28,32,48) for every hue. Shade `s` of a hue is a linear blend of the hue's colour toward fog by `s/15`.
The framebuffer is cleared to index 15 (`BG_INDEX`), which is why distant geometry fades into the
background. `blit_rgb` converts to ARGB through `pal[256]`.

**Screen space.** Origin top-left, y down. Pixel sample points are at integer coordinates.

**Winding.** A triangle is front-facing when its vertices go counter-clockwise as seen from outside, in the
left-handed view above. The outward normal is `(c - a) x (b - a)`. On screen (y down) a front-facing
triangle has *negative* signed area, which is what the backface test relies on.

---

## 5. The rendering pipeline (core/engine.h)

### 5.1 Camera transform: `to_view`

For a world point `p` and camera `(pos, yaw, pitch)`:

```
d  = p - pos
x1 =  d.x*cos(yaw) - d.z*sin(yaw)
z1 =  d.x*sin(yaw) + d.z*cos(yaw)
y2 =  d.y*cos(pitch) - z1*sin(pitch)
z2 =  d.y*sin(pitch) + z1*cos(pitch)
view = (x1, y2, z2)
```

This is real pitch (a rotation about the view x axis), **not** the y-shear trick Doom-style engines use,
so there is no distortion at steep angles and the pitch limit is only what the controls clamp (85 degrees).
`camxf_init` precomputes the four trig values once per frame.

### 5.2 Per-triangle path: `draw_tri_view`

Input: three view-space vertices, a hue, a baked brightness, and the fog range. Steps:

1. **Cull by distance and visibility.** Skip if the farthest vertex is behind the near plane, or the
   nearest vertex is beyond `fog_end` (this is the render distance).
2. **Near-plane clip** (`NEAR_Z = 0.25`). Sutherland-Hodgman against `z >= NEAR_Z`, giving 3 or 4 vertices.
   The new vertex is always interpolated *from the inside vertex*, so two triangles sharing an edge clip it
   identically. (An earlier version interpolated in edge order and left 1-pixel cracks along shared edges.)
3. **Project.** `sx = SW/2 + x*FOCAL/z`, `sy = SH/2 - y*FOCAL/z`, in 16.16 pixels. `FOCAL = 80` gives a
   90-degree horizontal field of view (about 64 degrees vertical). Results are clamped to +/-30000 pixels
   so they fit in int32. There is no guard-band clip, so a huge polygon very close to the camera can distort
   slightly.
4. **Backface cull** with the signed area of each fan triangle in integer pixels.
5. **Colour.** `tri_color` computes one colour per triangle (flat shading):
   `fog = 0` if the average view z is at or below `fog_start`, 15 at or above `fog_end`, linear between.
   `shade = min(15, bright + fog)`; index = `hue*16 + shade`. Fog is evaluated per triangle, not per pixel,
   so very large triangles show a visible step in shade.
6. **Fill** (`fill_tri`): vertices sorted by y; for each integer scanline the left and right edge x are
   computed by integer interpolation from the *upper* vertex of each edge; `xs = ceil(xa)`,
   `xe = ceil(xb)` (exclusive); the span is written with `memset`. Using the same rule on both sides of
   every edge means adjacent triangles neither overlap nor leave gaps.

### 5.3 Meshes: `render_mesh`

Transforms all vertices of a `v3[]` once, then calls `draw_tri_view` for each `tri_t` in list order
(painter's algorithm). Use it for anything small and unsorted; use the BSP for the static world.

### 5.4 Data types

```c
typedef struct { fx x,y,z; } v3;
typedef struct { uint8_t a,b,c,hue,bright; } tri_t;   // vertex indices (so max 256 verts), palette row, baked shade 0..15
typedef struct { v3 pos; uint16_t yaw,pitch; } cam_t;
```

`MAXV = 512` bounds the stack array in `render_mesh`; `tri_t` indices are `uint8_t`, so a mesh really tops
out at 256 vertices.

---

## 6. Assets: OBJ conversion (core/obj.h, tools/objconv.c)

### 6.1 What the loader does

`obj_load(path, scale, hue, &mesh)` reads `v` and `f` lines (everything else is ignored):

- Vertex positions are multiplied by `scale` and rounded to 16.16.
- Faces may use `a`, `a/b`, `a//c`, `a/b/c` forms and **negative (relative) indices**. Polygons with more
  than 3 vertices are triangulated as a fan.
- **Handedness:** OBJ is right-handed with CCW front faces; this engine is left-handed. The loader negates z
  and keeps the winding, which preserves outward-facing triangles (verified by rendering).
- **Baked lighting:** each triangle's `bright` (0..7) comes from its normal against a fixed light direction
  `(0.40, 0.80, -0.45)`: `bright = floor((1 - max(0, n.L)) * 7)`. Faces toward the light get 0 (brightest).
- Return codes: 0 ok, -1 cannot open, -2 more than 256 vertices, -3 no geometry.

The loader uses stdio, malloc, doubles and `strtod`. That is fine because it only runs at load or spawn time.
It does **not** weld duplicate vertices, drop degenerate triangles, or read normals, UVs or materials.

### 6.2 `.m3d` binary format (little-endian host assumed)

```
offset  size       content
0       4          "M3D1"
4       2          u16 vertex count nv (<= 256)
6       2          u16 triangle count nt
8       nv * 12    vertices: int32 x, y, z (16.16)
8+12nv  nt * 5     triangles: u8 a, b, c, hue, bright
```

### 6.3 C-array export

`objconv in.obj out.h scale hue --c` writes `mesh_v[]` and `mesh_t[]` initializers, so a model can be
compiled in with no loader at runtime.

### 6.4 CLI

```
objconv in.obj out.m3d [scale] [hue]          binary mesh
objconv in.obj out.h   [scale] [hue] --c      C arrays
```
`hue` is 0-15 (palette row). Meshes over 256 vertices are rejected with a message.

### 6.5 Primitive generator

`box_mesh(cx,cy,cz, hx,hy,hz, hue, v[8], t[12])` builds an axis-aligned box. Vertex `i` has bit0 = +x,
bit1 = +y, bit2 = +z. Faces (CCW from outside) are
`{0,1,3,2}` -z, `{5,4,6,7}` +z, `{1,5,7,3}` +x, `{4,0,2,6}` -x, `{2,3,7,6}` +y, `{1,0,4,5}` -y, each split
into two triangles, with baked shades `{2,5,3,4,0,6}` so faces are distinguishable.

---

## 7. The BSP (core/bsp.h)

Purpose: draw the static world in correct back-to-front order **without a z-buffer and without sorting at
runtime**. The ordering decision is made once, when the tree is built.

### 7.1 Data

```c
typedef struct { v3 v[3]; uint8_t hue,bright; } wtri_t;      // world triangle: carries its own 3 vertices
typedef struct { fx nx,ny,nz,d; int front,back,first,count; } bsp_node;   // plane n.p = d
```
World triangles carry inline vertices (not indices) because splitting creates new vertices, which would not
fit the 8-bit index scheme of `tri_t`.

### 7.2 Build (`bsp_build`, spawn time, uses doubles)

1. Drop zero-area triangles.
2. **Choose a splitter** among about 16 evenly spaced candidates. Score = `splits*8 + |front - back|`
   (few splits first, then balance).
3. **Classify** every triangle against the plane with epsilon 0.004 units: front, back, coplanar, or
   straddling.
4. Coplanar triangles are stored in this node. Front and back lists recurse. Straddlers are **split** along
   the plane (Sutherland-Hodgman) into a front polygon and a back polygon, each fan-triangulated; slivers
   with zero area are discarded; new vertices are rounded to 16.16.
5. Terminates because every node consumes at least its splitter triangle, and a plane used once cannot be
   chosen again below it.

Cost scales with triangle count and with how many triangles intersect: interpenetrating meshes are split and
grow the triangle count (the demo scene went from 48 to 72 triangles with 12 splits, built in about 0.1 ms).

### 7.3 Runtime (`bsp_draw`)

At each node compute which side the camera is on:
`side = ((nx*cx + ny*cy + nz*cz) >> 16) - d`. If `side > 0` the camera is in front, so draw the **back**
subtree first, then this node's coplanar triangles, then the front subtree; otherwise mirror it. Backface
culling hides everything that faces away. This is a pure painter's order: some overdraw, no depth tests.

There is no subtree culling yet, so the whole tree is walked every frame (distance culling still happens per
triangle in `draw_tri_view`).

### 7.4 Evidence it is right

`tests/test_bsp.c` renders a scene (ground, two interpenetrating cubes, a pillar) from five cameras with
(a) naive list order, (b) the BSP, and (c) a per-pixel z-buffer reference using doubles. The BSP matched the
reference to within 0.01% of pixels on all five (exactly 0.00% on four). Naive list order was also close in
that scene (0.25-0.56% off) because it is a simple scene; the BSP's value is that it is correct for *any*
static arrangement.

---

## 8. Physics (core/physics.h)

A player-only model: a point at the feet, gravity, jumping, acceleration/friction, and ground collision.
**No wall collision** (by design, for now): the player walks through the sides of objects.

### 8.1 Parameters (`phys_params_t`, all runtime data)

| Field | Default | Meaning |
|---|---|---|
| `gravity` | 20.0 | units/s^2 |
| `jump_v` | 7.0 | initial upward speed (apex about v^2/2g = 1.2 units) |
| `step_up` | 0.5 | tallest ledge walked up without jumping |
| `run_speed` | 5.0 | units/s |
| `ground_acc` | 40.0 | acceleration toward wanted velocity on ground (also acts as friction) |
| `air_acc` | 8.0 | same, in the air (less control) |
| `fly_speed` | 8.0 | noclip speed |
| `eye_h` | 1.6 | eye height above feet |
| `kill_y` | -60.0 | below this the player is put back at y = 5 |

### 8.2 Ground providers (`floor_source`)

```c
typedef int (*floor_fn)(void *ctx, fx px, fx pz, fx ymax, fx *out);   // 1 if a surface exists at (px,pz) with y <= ymax
typedef struct { floor_fn fn; void *ctx; } floor_source;
```
`floor_query_sources` asks every source and takes the highest answer. Shipped providers: `mesh_floor`
(for a `mesh_ref`) and `world_floor` (the world module's triangles). Anything else (a heightmap, a moving
platform) is just another callback registered with `player_add_floor_source`.

### 8.3 `tri_floor_test`

A triangle counts as floor if it faces up and contains the point in the XZ plane. "Faces up" is
`edge2(a,b,c) > 0` where `edge2` is the 2D cross product in (x,z). Containment uses the three edge
functions (inclusive, so shared edges don't leak). Height is the barycentric interpolation of the vertex
heights. Products are right-shifted by 8 before multiplying by y so int64 cannot overflow on large
triangles.

### 8.4 `player_step` (variable delta)

1. Wanted velocity = `(sin*fwd + cos*strafe, cos*fwd - sin*strafe) * run_speed` (times 0.7071 when moving
   diagonally).
2. Horizontal velocity moves toward it by `acc*dt` per axis (`ground_acc` or `air_acc`).
3. Jump: if the jump input is held and the player is grounded, `vy = jump_v`.
4. `vy -= gravity*dt` (semi-implicit Euler: velocity first, then position).
5. `newy = oldy + vy*dt`; move x and z.
6. If falling or level (`vy <= 0`), query the ground at the new x,z with `ymax = oldy + step_up`. If a surface
   `h` is found and `newy <= h`, snap to it (grounded, `vy = 0`). Otherwise the player is airborne.
7. Kill plane check.

Because the ray starts from the *old* height, a fast fall cannot tunnel through a floor between frames.
`game_frame` clamps dt to 50 ms, and the tests confirm an 8-unit drop at that dt still lands correctly.
Because of discretisation the real jump apex is about 1.17 units, 5% under the ideal 1.225.

### 8.5 Fly mode

`player_fly` (physics off) moves at `fly_speed` along the facing direction, with up/down from the jump and
crouch actions; no gravity, no collision.

---

## 9. The framework (framework/game.h and input.h)

### 9.1 `game_t`: the shared context

Holds the camera (`cam`), view angles (`yaw_i`, `pitch_i`), time (`dt` as 16.16 seconds already clamped to
50 ms, `raw_dt_ms` unclamped, `frame`), the input state (`in`), action state (`act_down[]`,
`act_pressed[]`), fog distances, the `running` flag, and the registries (modules, services, commands,
action names).

### 9.2 Module lifecycle

```c
typedef struct {
  int api;                       // must equal GAME_API_VERSION, else game_register() refuses it
  const char *name;
  void (*init)(game_t*);         // called inside game_register(): publish services, register commands
  void (*update)(game_t*);       // every frame, logic
  void (*draw_world)(game_t*);   // every frame, 3D (after the framebuffer is cleared)
  void (*draw_hud)(game_t*);     // every frame, 2D overlay, after all draw_world hooks
  void (*shutdown)(game_t*);     // reverse registration order
} module_t;
```
Any hook may be `NULL`. **Order = registration order**, and `init` runs immediately at registration, so a
module can only look up services published by modules registered *before* it.

### 9.3 One frame (`game_frame`)

```
platform:  input_begin_frame(&g.in)        clears pressed/released edges and mouse deltas
platform:  pump events -> input_key / input_mouse
framework: game_frame(&g, raw_ms)
             1. raw_dt_ms = raw_ms;  dt = min(raw_ms, 50 ms) as 16.16 seconds
             2. every module's update(), in registration order
             3. clear_fb(BG_INDEX)
             4. every module's draw_world(), in registration order
             5. every module's draw_hud(), in registration order
             6. frame++
platform:  blit_rgb -> texture -> present
```
Input edges (`pressed`, `released`) are valid for exactly one frame. In tests the order must be
`input_begin_frame`, then `input_key`, then `game_frame`; reversing it erases the edge.

### 9.4 Services

`game_service_set(g, "name", ptr)` publishes a pointer; `game_service(g, "name")` looks it up (NULL if
absent). Shipped services: `"controls"`, `"world"`, `"player"`, `"hud"`. A service is just a pointer to a
struct the publishing module defines, so modules agree on the struct by including that module's header.

### 9.5 Commands

`game_cmd_register(g, name, fn, help)` and `game_exec(g, "line", out, outn)`. The line is split on spaces/tabs
into up to 8 arguments; the command writes its reply into `out`; the return value is 0 for success, negative
for an error. Unknown commands return -1 with "unknown command: X". The future console is just a text box that
calls `game_exec`.

### 9.6 Actions and bindings

Raw keys never reach game logic directly. The `controls` module maps keys to **actions**; modules read
`g->act_down[ACT_JUMP]` (held) and `g->act_pressed[ACT_JUMP]` (edge). Built-in ids: `ACT_FORWARD`,
`ACT_BACK`, `ACT_LEFT`, `ACT_RIGHT`, `ACT_JUMP`, `ACT_DOWN`, `ACT_TOGGLE_PHYSICS`, `ACT_QUIT` (0-7; ids up to 15
are reserved). `game_action_id(g, "name", 1)` creates a new action in the user range (16 to 47) and returns
its id. Several keys can map to one action and one key can map to several actions (max 64 bindings).

### 9.7 Input (`input.h`)

`input_t` holds `down[256]`, `pressed[256]`, `released[256]` and the mouse deltas. Key codes are lowercase
ASCII for printable keys plus `K_SPACE`, `K_ESC`, `K_ENTER`, `K_TAB`, `K_BACKSPACE`, `K_LCTRL`, `K_RCTRL`,
`K_LSHIFT`, `K_RSHIFT`, `K_LALT`, `K_RALT`, the arrows, and F1-F12 (`K_F1 + n`).
`key_from_name("space")` and friends are used by the `bind` command. The platform fills this struct; nothing
in the engine knows about SDL.

---

## 10. The shipped modules

Registration order used by `platform/sdl_main.c` (and why):
`controls` -> `world` -> `demo_scene` -> `player` -> `hud`.
Controls first so actions are fresh when the player reads them; world before content and player because both
look up the `"world"` service in their init.

| Module | Hooks | Publishes | Commands | Notes |
|---|---|---|---|---|
| `controls` | init, update | `"controls"` | `bind`, `unbind`, `sens` | Clears and recomputes `act_*` each frame; mouse look (yaw wraps, pitch clamped +/-85 degrees); Esc sets `running = 0` |
| `world` | init, update, draw_world, shutdown | `"world"` | none | Holds source triangles and the BSP. `world_add_mesh()` sets a dirty flag; the update hook rebuilds the BSP before the next draw. Draws with the BSP using the game's fog range |
| `demo_scene` | init | none | none | Adds a 60x60 ground slab (top at y = -1), a cube, and optionally a model from argv (`.obj` or `.m3d`) |
| `player` | init, update | `"player"` | `physics`, `gravity`, `tp` | Reads actions, runs physics against its floor sources (the world is registered as one), writes `g->cam`. Spawns at (0, 2, -5) |
| `hud` | init, update, draw_hud | `"hud"` | `showfps` | FPS recomputed every 500 ms from the **unclamped** frame time; drawn top centre in white with a dim shadow |
| `checkpoint` | init, update | none | `checkpoint` | Worked example, not registered by default. Adds a user action `respawn` bound to R |

Built-in framework commands: `help`, `fog <start> <end>`.

---

## 11. The platform layer (platform/sdl_main.c)

The only file that knows about SDL. Its job: create the window (5x scale, nearest-neighbour logical size
160x100), translate SDL events into `input_t` (`map_key`), measure the frame time, call `game_frame`, convert
the 8-bit framebuffer to ARGB and present. It is also the **composition root**: the one place that lists
which modules exist, via `game_register` lines. Adding a feature means adding an `#include` and a line here.

Usage: `./engine_sdl [model.obj | model.m3d]`.

---

## 12. Reference

### Keys (default bindings)

| Key | Action |
|---|---|
| W / S | forward / back |
| A / D | strafe left / right |
| Mouse | look (yaw; pitch clamped to +/-85 degrees) |
| Space | jump (fly up when physics is off) |
| Left or right Ctrl | fly down (physics off) |
| P | toggle physics / noclip fly |
| Esc | quit |
| R | respawn (only if the `checkpoint` module is registered) |

### Commands

| Command | Effect |
|---|---|
| `help` | list commands |
| `fog <start> <end>` | fog start and render distance (units) |
| `bind <key> <action>` | bind a key (`w`, `space`, `lctrl`, `f5`, ...) to an action by name |
| `unbind <key>` | remove all bindings for that key |
| `sens <n>` | mouse sensitivity (turn units per mouse count; default 20) |
| `physics [on\|off]` | query or set physics |
| `gravity <v>` | set gravity (units/s^2) |
| `tp <x> <y> <z>` | teleport the player (velocity reset) |
| `showfps [0\|1]` | show or hide the FPS counter |
| `checkpoint` | save the position (checkpoint module) |

Action names: `forward back left right jump down toggle_physics quit` plus any a module creates.

### Compile-time limits

`MAX_MODULES 32`, `MAX_SERVICES 32`, `MAX_COMMANDS 64`, `MAX_ACTIONS 48`, key bindings 64, floor sources 8,
command arguments 8, command line 255 characters, `MAXV 512`, 256 vertices per mesh.

### Tunable constants (core/engine.h)

`SW`, `SH` (resolution), `FOCAL` (field of view), `NEAR_Z`, `SCR_LIM`, `BG_INDEX`. Changing them means
editing a frozen file (and re-running `make freeze`): resolution is the one thing deliberately not runtime.

---

## 13. How to write a new module (worked example)

`modules/checkpoint.h` is complete and tested. Its anatomy:

```c
#include "game.h"
#include "controls.h"      // for controls_bind
#include "player.h"        // for player_state_t

static struct { v3 pos; int set,act_respawn; } CP;                 // module-private state

static int cmd_checkpoint(game_t *g,int argc,char **argv,char *out,int outn){
  player_state_t *p = game_service(g,"player");                    // look up another module by name
  CP.pos = p->pl.pos; CP.set = 1;
  OUT("checkpoint saved ...");                                     // reply text for the console
  return 0;
}
static void checkpoint_init(game_t *g){
  CP.act_respawn = game_action_id(g,"respawn",1);                  // new action from the user range
  controls_bind('r', CP.act_respawn);                              // default key
  game_cmd_register(g,"checkpoint",cmd_checkpoint,"save position");
}
static void checkpoint_update(game_t *g){
  if(!CP.set || !g->act_pressed[CP.act_respawn]) return;
  player_state_t *p = game_service(g,"player");
  p->pl.pos = CP.pos; p->pl.vx = p->pl.vy = p->pl.vz = 0; p->pl.ground = 0;
}
static const module_t checkpoint_module =
  { GAME_API_VERSION, "checkpoint", checkpoint_init, checkpoint_update, 0, 0, 0 };
```

To enable it: `#include "checkpoint.h"` in `platform/sdl_main.c` and add
`game_register(&G,&checkpoint_module);` after the player module. Nothing in `core/` or `framework/` changes.

Checklist for a new module:
1. One header in `modules/`, state in `static` variables, a `static const module_t` at the bottom.
2. `api` field = `GAME_API_VERSION`.
3. Publish a service only if others need your state; look up services only for modules registered earlier.
4. Name commands and actions distinctively (duplicate command names are rejected).
5. Add a test in `tests/` that drives it with `game_frame`, `input_key` and `game_exec`.

---

## 14. Testing

| Binary | Verifies |
|---|---|
| `test_render` | Renders four views (pitch down, level, up, orbit) to PPM and benchmarks a 36-triangle scene (about 0.003 ms/frame, flat colour). Checksum-stable across the refactor |
| `test_physics_obj` | Fall, jump apex, landing on a cube top and on a pyramid apex and slope, walking off a ledge, no tunnelling at dt = 50 ms, run speed; OBJ parsing (quads, slashes, negative indices), `.m3d` round trip, HUD text render |
| `test_bsp` | BSP vs z-buffer reference from five cameras (<= 0.01% mismatch) and BSP render speed |
| `test_framework` | Module registration and API rejection, services, every command, headless frames, key rebinding, key toggles, user actions, an extension module adding ground, a command and an action, runtime world modification, the checkpoint example, quit |

`make test` runs `verify` first, then all four. Tests dump a few `.ppm` images into the working directory.

---

## 15. Build

```
make            # build tests and objconv
make test       # verify frozen files, then run all suites
make sdl        # needs SDL2 (sdl2-config)
make verify     # frozen-file check only
make freeze     # re-record hashes after a deliberate core/framework change
```
Flags: `-O2 -std=c99 -Wall -Wextra`, `-march=x86-64` (baseline, no AVX). The code builds with no warnings.

---

## 16. How the planned features plug in (nothing here needs a frozen-file edit)

| Feature | Plan |
|---|---|
| **Console** | A module with a `draw_hud` hook (text box, a bigger `font_t`) and an `update` hook that feeds typed characters to `game_exec`. Typed text needs a source: key codes in `input_t` cover letters, digits, space, backspace and enter, but not shifted symbols. If proper text input is needed, the platform can publish a `"textinput"` service (a character queue) that the console reads. |
| **Runtime shape spawning** | A module with `spawn` / `remove` commands: shape generators fill `v3[]` + `tri_t[]`, an overlap test runs against the world's triangle list first, then `world_add_mesh()` (the BSP rebuilds before the next draw). Removal needs a `world_remove`-style helper: the world module is not frozen, so adding it there is fine. |
| **Textures** | A new additive file (for example `core/texfill.h`) with a textured span filler, plus a module that supplies texture data. The flat filler stays untouched. |
| **Scenes (enter/leave a house)** | A scene-manager module that clears and refills the world through the `"world"` service and saves/restores the dynamic-body list. |
| **Dynamic bodies** | A module with `update` (integration, collision against floor sources) and `draw_world` (draw after the BSP). These cannot live in the BSP, so they need a small z-buffer, added as a new core file. Max 16 per scene, per the earlier design. |
| **Culling by distance for simulation** | A module that puts distant bodies to sleep, using the same fog distance the framework already carries. |

---

## 17. Known limits and honest caveats

- **The SDL program has never been run.** It compiles in principle (syntax-checked against a stub header with
  `-Wall -Wextra`); linking, window creation, the event pump and mouse look are untested.
- **Flat shading and per-triangle fog.** Large triangles show a shade step. Fix: per-scanline depth shading
  or subdividing big polygons.
- **No texture mapping yet.** Everything is flat colour with baked per-face brightness.
- **No wall collision, no dynamic bodies, no console, no spawning yet** (see section 16).
- **256 vertices per mesh.** Comes from 8-bit triangle indices; the converter rejects larger models.
- **Whole BSP is walked every frame**, no subtree culling.
- **Sine table has 0.35-degree steps** (no interpolation).
- **Screen coordinates are clamped to +/-30000 px**, with no guard-band clip, so a very large polygon right
  at the near plane can distort.
- **The BSP builder and the OBJ loader use doubles and malloc.** Fine at spawn/load time; a fixed memory
  arena is the planned replacement once scenes load and unload.
- **`.m3d` assumes a little-endian host.**
- **Modules are singletons** (state in static globals), and hook order is registration order, so dependencies
  are by convention (documented per module), not enforced.
- **Frozen-file enforcement is a hash check**, not a compiler guarantee: it catches accidental edits and
  makes deliberate ones explicit.
- **Render distance and fog distance are the same number** (`fog_end`), and fog colour is a compile-time
  constant (`fog_rgb` in `core/engine.h`).
- **The font only has digits and F, P, S.** A bigger font is a data-only extension (`font_t`).
- **Physics is player-only.** The jump apex is about 5% below the ideal value because of integration.
