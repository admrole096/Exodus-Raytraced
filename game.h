/* framework/game.h - FROZEN. The module system. This file is the whole "plugin API".
 *
 * A module is a struct of optional hooks. Register it once; the framework calls the hooks every frame in
 * REGISTRATION ORDER. Modules share state through named services and expose features through named commands.
 * New features = new module file + one game_register() line in the platform's main. Nothing else is edited. */
#ifndef GAME_H
#define GAME_H
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "engine.h"
#include "input.h"

#define GAME_API_VERSION 1
#define MAX_MODULES  32
#define MAX_SERVICES 32
#define MAX_COMMANDS 64
#define MAX_ACTIONS  48      /* 0..15 built-in, 16..47 user actions (game_action_id) */

enum { ACT_FORWARD, ACT_BACK, ACT_LEFT, ACT_RIGHT, ACT_JUMP, ACT_DOWN, ACT_TOGGLE_PHYSICS, ACT_QUIT,
       ACT_USER0=16 };

typedef struct game game_t;
typedef struct {
  int api;                              /* must equal GAME_API_VERSION */
  const char *name;
  void (*init)(game_t *g);              /* at registration: register services/commands, read earlier modules' services */
  void (*update)(game_t *g);            /* once per frame, logic. g->dt is the clamped delta (seconds, 16.16) */
  void (*draw_world)(game_t *g);        /* after framebuffer clear: 3D drawing, in registration order */
  void (*draw_hud)(game_t *g);          /* after all draw_world hooks: 2D overlay */
  void (*shutdown)(game_t *g);          /* reverse registration order */
} module_t;

typedef int (*cmd_fn)(game_t *g,int argc,char **argv,char *out,int outn);   /* return 0 ok, <0 error; text -> out */

struct game {
  cam_t cam;                            /* written by whoever owns the viewpoint (player module) */
  int yaw_i,pitch_i;                    /* view angles, written by controls; uint16 turn units (65536 = 360 deg) */
  fx dt; uint32_t raw_dt_ms,frame;      /* dt clamped to 50 ms; raw_dt_ms unclamped (for FPS) */
  input_t in;                           /* filled by the platform before game_frame */
  uint8_t act_down[MAX_ACTIONS], act_pressed[MAX_ACTIONS];   /* action state, filled by the controls module */
  fx fog_start,fog_end; int running;
  const module_t *mods[MAX_MODULES]; int nmods;
  struct { const char *name; void *ptr; } svc[MAX_SERVICES]; int nsvc;
  struct { const char *name; cmd_fn fn; const char *help; } cmd[MAX_COMMANDS]; int ncmd;
  const char *act_name[MAX_ACTIONS];
};

#define OUT(...) do{ if(out&&outn>0) snprintf(out,outn,__VA_ARGS__); }while(0)

/* ---- services: modules publish a pointer under a name; later modules look it up ---- */
static void game_service_set(game_t *g,const char *name,void *p){
  for(int i=0;i<g->nsvc;i++) if(!strcmp(g->svc[i].name,name)){ g->svc[i].ptr=p; return; }
  if(g->nsvc<MAX_SERVICES){ g->svc[g->nsvc].name=name; g->svc[g->nsvc++].ptr=p; }
}
static void *game_service(game_t *g,const char *name){
  for(int i=0;i<g->nsvc;i++) if(!strcmp(g->svc[i].name,name)) return g->svc[i].ptr;
  return 0;
}

/* ---- actions: names <-> ids. create=1 allocates a new id in the user range ---- */
static int game_action_id(game_t *g,const char *name,int create){
  for(int i=0;i<MAX_ACTIONS;i++) if(g->act_name[i]&&!strcmp(g->act_name[i],name)) return i;
  if(create) for(int i=ACT_USER0;i<MAX_ACTIONS;i++) if(!g->act_name[i]){ g->act_name[i]=name; return i; }
  return -1;
}

/* ---- commands: name -> function. The future console just calls game_exec(). ---- */
static int game_cmd_register(game_t *g,const char *name,cmd_fn fn,const char *help){
  for(int i=0;i<g->ncmd;i++) if(!strcmp(g->cmd[i].name,name)) return -1;
  if(g->ncmd>=MAX_COMMANDS) return -1;
  g->cmd[g->ncmd].name=name; g->cmd[g->ncmd].fn=fn; g->cmd[g->ncmd++].help=help; return 0;
}
static int game_exec(game_t *g,const char *line,char *out,int outn){
  char buf[256],*argv[9]; int argc=0;
  if(out&&outn>0) out[0]=0;
  strncpy(buf,line,sizeof buf-1); buf[sizeof buf-1]=0;
  for(char *p=strtok(buf," \t\r\n");p&&argc<8;p=strtok(0," \t\r\n")) argv[argc++]=p;
  if(!argc) return 0;
  for(int i=0;i<g->ncmd;i++) if(!strcmp(g->cmd[i].name,argv[0])) return g->cmd[i].fn(g,argc,argv,out,outn);
  OUT("unknown command: %s",argv[0]); return -1;
}

static int cmd_help(game_t *g,int argc,char **argv,char *out,int outn){
  (void)argc;(void)argv; int n=0;
  if(out&&outn>0) out[0]=0;
  for(int i=0;i<g->ncmd&&out&&n<outn-1;i++)
    n+=snprintf(out+n,outn-n,"%s - %s\n",g->cmd[i].name,g->cmd[i].help?g->cmd[i].help:"");
  return 0;
}
static int cmd_fog(game_t *g,int argc,char **argv,char *out,int outn){
  if(argc==3){ g->fog_start=FXF(atof(argv[1])); g->fog_end=FXF(atof(argv[2])); }
  OUT("fog %.1f %.1f",g->fog_start/65536.0,g->fog_end/65536.0); return 0;
}

static void game_init(game_t *g){
  memset(g,0,sizeof *g);
  g->fog_start=FXF(10); g->fog_end=FXF(40); g->running=1;
  static const char *names[]={"forward","back","left","right","jump","down","toggle_physics","quit"};
  for(int i=0;i<8;i++) g->act_name[i]=names[i];
  game_cmd_register(g,"help",cmd_help,"list commands");
  game_cmd_register(g,"fog",cmd_fog,"fog <start> <end> : fog / render distance");
}
static int game_register(game_t *g,const module_t *m){
  if(!m||m->api!=GAME_API_VERSION||g->nmods>=MAX_MODULES) return -1;
  g->mods[g->nmods++]=m;
  if(m->init) m->init(g);
  return 0;
}
static void game_shutdown(game_t *g){
  for(int i=g->nmods-1;i>=0;i--) if(g->mods[i]->shutdown) g->mods[i]->shutdown(g);
}

/* One frame. The platform calls input_begin_frame(), pumps events into g->in, then calls this. */
static void game_frame(game_t *g,uint32_t raw_ms){
  g->raw_dt_ms=raw_ms;
  uint32_t c=raw_ms>50?50:raw_ms; g->dt=(fx)(((int64_t)c<<16)/1000);
  for(int i=0;i<g->nmods;i++) if(g->mods[i]->update) g->mods[i]->update(g);
  clear_fb(BG_INDEX);
  for(int i=0;i<g->nmods;i++) if(g->mods[i]->draw_world) g->mods[i]->draw_world(g);
  for(int i=0;i<g->nmods;i++) if(g->mods[i]->draw_hud) g->mods[i]->draw_hud(g);
  g->frame++;
}
#endif
