# Exodus: Raytraced

A 3d game with Doom-like graphics with simple raytracing but full 3D (not "2.5D") about the 10 plagues.

# How the graphics work (simply)

Instead of just 1D of lines (left to right) measuring distance and scaling columns, it casts 2D of lines (top-left to bottom-right) and measuring the distance and displaying pixels so that the player has all 360° of freedom.

# 3D engine (for geeks🤓)

C99 software 3D engine: fixed point, scanline rasterizer, real look up/down, BSP draw order,
player physics, OBJ converter, module system. Core and framework are frozen; features are modules.

    make test          # verify frozen files + run all four test suites
    make sdl           # SDL2 window program (never run in the build sandbox, see docs)
    ./engine_sdl [model.obj|model.m3d]

Keys: W/A/S/D move, mouse look, Space jump, Ctrl fly down (physics off), P toggle physics, Esc quit.

Read `docs/ARCHITECTURE.md` for how everything works, how to write a module, and the known limits.
