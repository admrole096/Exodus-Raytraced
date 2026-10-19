CFLAGS=-O2 -std=c99 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -march=x86-64
INC=-Icore -Iframework -Imodules
CORE=core/engine.h core/shapes.h core/bsp.h core/obj.h core/text.h core/physics.h
FW=framework/input.h framework/game.h
MODS=modules/controls.h modules/world.h modules/player.h modules/hud.h modules/demo_scene.h modules/checkpoint.h

all: test_render test_physics_obj test_bsp test_framework objconv
test_render: tests/test_render.c $(CORE)
	$(CC) $(CFLAGS) $(INC) -o $@ $< -lm
test_physics_obj: tests/test_physics_obj.c $(CORE)
	$(CC) $(CFLAGS) $(INC) -o $@ $< -lm
test_bsp: tests/test_bsp.c $(CORE)
	$(CC) $(CFLAGS) $(INC) -o $@ $< -lm
test_framework: tests/test_framework.c $(CORE) $(FW) $(MODS)
	$(CC) $(CFLAGS) $(INC) -o $@ $< -lm
objconv: tools/objconv.c core/obj.h core/engine.h
	$(CC) $(CFLAGS) $(INC) -o $@ $< -lm
sdl: platform/sdl_main.c $(CORE) $(FW) $(MODS)
	$(CC) $(CFLAGS) $(INC) -o engine_sdl $< `sdl2-config --cflags --libs` -lm

# Frozen files: core/ and framework/ must not change. `make verify` checks, `make freeze` re-records (deliberate act).
verify:
	@sha256sum -c FROZEN.sha256 --quiet && echo "frozen files intact"
freeze:
	sha256sum core/*.h framework/*.h > FROZEN.sha256
test: verify test_render test_physics_obj test_bsp test_framework
	./test_physics_obj && ./test_bsp && ./test_framework
	./test_render
clean:
	rm -f test_render test_physics_obj test_bsp test_framework objconv engine_sdl *.ppm t.obj t.m3d
.PHONY: all sdl verify freeze test clean
