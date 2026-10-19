/* modules/checkpoint.h - WORKED EXAMPLE of extending the engine without modifying it.
 * Adds: a user action "respawn" (key R), a command `checkpoint`, and a stored position.
 * Uses only the public API: services, actions, bindings, commands, module hooks.
 * Register AFTER controls and player. */
#ifndef CHECKPOINT_H
#define CHECKPOINT_H
#include "game.h"
#include "controls.h"
#include "player.h"

static struct { v3 pos; int set,act_respawn; } CP;

static int cmd_checkpoint(game_t *g,int argc,char **argv,char *out,int outn){
  (void)argc;(void)argv;
  player_state_t *p=game_service(g,"player");
  CP.pos=p->pl.pos; CP.set=1;
  OUT("checkpoint saved at %.2f %.2f %.2f",CP.pos.x/65536.0,CP.pos.y/65536.0,CP.pos.z/65536.0);
  return 0;
}
static void checkpoint_init(game_t *g){
  memset(&CP,0,sizeof CP);
  CP.act_respawn=game_action_id(g,"respawn",1);       /* new action id from the user range */
  controls_bind('r',CP.act_respawn);                  /* default key; rebindable with `bind` */
  game_cmd_register(g,"checkpoint",cmd_checkpoint,"save current position as respawn point");
}
static void checkpoint_update(game_t *g){
  if(!CP.set||!g->act_pressed[CP.act_respawn]) return;
  player_state_t *p=game_service(g,"player");
  p->pl.pos=CP.pos; p->pl.vx=p->pl.vy=p->pl.vz=0; p->pl.ground=0;
}
static const module_t checkpoint_module={GAME_API_VERSION,"checkpoint",checkpoint_init,checkpoint_update,0,0,0};
#endif
