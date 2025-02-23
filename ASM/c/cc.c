#include "gfx.h"

#include "icetrap.h"
#include "z64.h"

#define s_add(a, b, max) (max - a < b ? max : a + b)
#define s_sub(a, b, min) (a - min < b ? min : a - b)

#define MAX_TURBO_SPEED 16.0f

void zu_void(void);
void zu_execute_game(int16_t entrance_index, uint16_t cutscene_index);
void zu_audio_cmd(uint32_t cmd);
void command_age(void);
void command_reload(void);
void gz_warp(int16_t entrance_index, uint16_t cutscene_index, int age);
_Bool push_object_proc(int16_t object_id);

uint8_t command_selection_index = 0;
uint8_t command_insertion_index = 0;
uint32_t command_ring_buffer[16] = {
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
	0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF,
};

uint16_t forced_boots = 0;
uint16_t forced_boots_frames = 0;

uint16_t timers[] = {
	0, 0, 0, 0, 0,
};

_Bool boot_change_allowed() {
	return forced_boots_frames == 0;
}

uint8_t frames_ready = 0;
inline uint32_t link_is_ready() {
    if ((z64_link.state_flags_1 & 0xFCAC2485) == 0 &&
        (z64_link.common.unk_flags_00 & 0x0001) &&
        (z64_link.state_flags_2 & 0x000C0000) == 0 &&
        (z64_event_state_1 & 0x20) == 0 &&
        (z64_game.camera_2 == 0)) {
        frames_ready++;
    }
    else {
        frames_ready = 0;
    }
    if (frames_ready >= 2) {
        frames_ready = 0;
        return 1;
    }
    return 0;
}

enum Commands {
	// Instant
	FREEZE,			// 0x00
	VOID_OUT,		// 0x01
	TOGGLE_AGE,		// 0x02
	KILL_PLAYER,	// 0x03
	HUGE,			// 0x04
	TINY,			// 0x05

	// States w/ durations
	OHKO,			// 0x06
	NO_HUD,			// 0x07
	NO_Z,			// 0x08
	TURBO,			// 0x09
	INVERT_CONTROLS,// 0x0A

	// Spawning things
	SPAWN_ARWING,	// 0x0B
	SPAWN_ENEMY,	// 0x0C

	// Add/Remove
	ADD_ENERGY,		// 0x0D
	REMOVE_ENERGY,	// 0x0E
	ADD_RUPEES,		// 0x0F
	REMOVE_RUPEES,	// 0x10

	ADD_AMMO = 0x7F,// Marker to start the "add" commands
	ADD_BOMBCHUS,	// 0x80
	ADD_STICKS,		// 0x81
	ADD_NUTS,		// 0x82
	ADD_BOMBS,		// 0x83
	ADD_ARROWS,		// 0x84
	ADD_SEEDS,		// 0x85

	REM_AMMO = 0xBF,// Marker to start the "remove" commands
	REMOVE_BOMBCHUS,// 0xC0
	REMOVE_STICKS,	// 0xC1
	REMOVE_NUTS,	// 0xC2
	REMOVE_BOMBS,	// 0xC3
	REMOVE_ARROWS,	// 0xC4
	REMOVE_SEEDS,	// 0xC5

	BOOTS 			= 0xDF,
	IRON_BOOTS 		= 0xE2,
	HOVER_BOOTS 	= 0xE3,
	F_BOOTS 		= 0xEF,

};

void apply_ongoing_effects() {
	if (forced_boots_frames > 0) {
		--forced_boots_frames;


		z64_file.equip_boots = forced_boots;

		if (forced_boots_frames == 0) {
			// Restore to kokiri boots when the time elapses
            z64_file.equip_boots = 1;
		}

        z64_UpdateEquipment(&z64_game, &z64_link);
	}

	if (timers[0] > 0) {
		--timers[0];

		// If this was the last frame of the OHKO mode, give the player 1 heart because we're nice like that.
		if (timers[0] == 0) {
			z64_file.energy = 16;
		}

		if (z64_file.energy == 0) {
			// Actually let us die if life is already at 0
			timers[0] = 0;
		} else {
			z64_file.energy = 1;
		}

	}

	if (timers[1] > 0) {
		--timers[1];

		z64_file.hud_flag = 0x001;
	}

	if (timers[2] > 0) {
		--timers[2];
		z64_game.common.input[0].pad_pressed.z = 0;
	}

	if (timers[3] > 0) {
		--timers[3];
		float vel = z64_link.linear_vel * 2.0f;
		// 27 is HESS speed I think?
		z64_link.linear_vel = vel > MAX_TURBO_SPEED ? MAX_TURBO_SPEED : vel;
	}

	if (timers[4] > 0) {
		--timers[4];
		z64_game.common.input[0].raw.x = -z64_game.common.input[0].raw.x;
		z64_game.common.input[0].raw.y = -z64_game.common.input[0].raw.y;
		z64_game.common.input[0].adjusted_x = -z64_game.common.input[0].adjusted_x;
		z64_game.common.input[0].adjusted_y = -z64_game.common.input[0].adjusted_y;
	}
}

z64_slot_t slots[6] = {
	Z64_SLOT_BOMBCHU,
	Z64_SLOT_STICK,
	Z64_SLOT_NUT,
	Z64_SLOT_BOMB,
	Z64_SLOT_BOW,
	Z64_SLOT_SLINGSHOT,
};

int wallet_capacity[] = {99, 200, 500, 999};
int stick_capacity[] = {1, 10, 20, 30, 1, 20, 30, 40};
int nut_capacity[] = {1, 20, 30, 40, 1, 0x7F, 1, 0x7F};
int bomb_bag_capacity[] = {1, 20, 30, 40, 1, 1, 1, 1};
int quiver_capacity[] = {1, 30, 40, 50, 1, 20, 30, 40};
int bullet_bag_capacity[] = {1, 30, 40, 50, 1, 10, 20, 30};
int chu_capacity[] = {50};

int* capacities[] = {
	chu_capacity,
	stick_capacity,
	nut_capacity,
	bomb_bag_capacity,
	quiver_capacity,
	bullet_bag_capacity,
};

void apply_cc() {
	// We'll pump a single command per frame because reasons.
	uint32_t* command_ptr = command_ring_buffer + command_selection_index;

	if (*command_ptr != 0xDEADBEEF) {
		// First 8 bits are the command type and last 24 are the payload.
		uint8_t command = (*command_ptr) >> 24;
		uint32_t payload = (*command_ptr) & 0xFFFFFF;

		if ((command & 0xE0) == 0xE0) {
			if(z64_file.link_age == ((command & 0x08) >> 3)) {
		forced_boots = command & 0x0F;
		forced_boots_frames = s_add(forced_boots_frames, payload, 0xFFFF);
		}
		} else if (command & 0x80) {
			uint8_t idx = command & 0x3F;
			if (command & 0x40) {
				z64_file.ammo[slots[idx]] = s_sub(z64_file.ammo[slots[idx]], payload, 0);
			} else {
				uint32_t upgrades[] = {
					0,
					z64_file.stick_upgrade,
					z64_file.nut_upgrade,
					z64_file.bomb_bag,
					z64_file.quiver,
					z64_file.bullet_bag,
				};
				z64_file.ammo[slots[idx]] = s_add(z64_file.ammo[slots[idx]], payload, capacities[idx][upgrades[idx]]);
			}
		} else {
			switch (command) {
				// Add/Remove
				case ADD_ENERGY:
					z64_file.energy = s_add(z64_file.energy, payload, z64_file.energy_capacity);
					break;
				case REMOVE_ENERGY:
					z64_file.energy = s_sub(z64_file.energy, payload, 16);
					break;
				case ADD_RUPEES:
				z64_file.rupees = s_add(z64_file.rupees, payload, wallet_capacity[z64_file.wallet]);
					break;
				case REMOVE_RUPEES:
					z64_file.rupees = s_sub(z64_file.rupees, payload, 0);
					break;

				// Instant
				case FREEZE:
					push_pending_ice_trap();
					break;
				case VOID_OUT:
					if (!link_is_ready()) goto bail;
					zu_void();
					break;
				case TOGGLE_AGE:
					if (!link_is_ready()) goto bail;
					command_age();
					break;
				case KILL_PLAYER:
					z64_file.energy = 0;
					break;
				case HUGE:
					z64_link.common.scale.x *= 2.f;
					z64_link.common.scale.y *= 2.f;
					z64_link.common.scale.z *= 2.f;
					break;
				case TINY:
					z64_link.common.scale.x *= 0.5f;
					z64_link.common.scale.y *= 0.5f;
					z64_link.common.scale.z *= 0.5f;
					break;

				// States w/ durations
				case OHKO:
				case NO_HUD:
	            case NO_Z:
	            case TURBO:
	            case INVERT_CONTROLS:
					timers[command - OHKO] = s_add(timers[command - OHKO], payload, 0xFFFF);
					break;

				// Spawning things
				case SPAWN_ARWING:
					if (!link_is_ready()) goto bail;
					z64_SpawnActor(&z64_game.actor_ctxt, &z64_game, 0x013B,
		                 z64_link.common.pos_2.x, z64_link.common.pos_2.y, z64_link.common.pos_2.z,
		                 z64_link.common.rot_2.x, z64_link.common.rot_2.y, z64_link.common.rot_2.z,
		                 0x0000);
					break;
				case SPAWN_ENEMY:
					if (push_object_proc((payload & 0x00FF00) >> 8)) {
						z64_SpawnActor(&z64_game.actor_ctxt, &z64_game, (payload & 0xFF0000) >> 16,
						               z64_link.common.pos_2.x, z64_link.common.pos_2.y, z64_link.common.pos_2.z,
						               z64_link.common.rot_2.x, z64_link.common.rot_2.y, z64_link.common.rot_2.z,
						               payload & 0x0000FF);
					}
					break;
			}
		}

		command_selection_index = (command_selection_index + 1) % 16;
		*command_ptr = 0xDEADBEEF;
	}

bail:
	apply_ongoing_effects();
}

// Everything below this is lifted almost verbatim from gz, with the exception of things that were different in rando's z64.h
void zu_void(void)
{
  z64_file.temp_swch_flags = z64_game.temp_swch_flags;
  z64_file.temp_collect_flags = z64_game.temp_collect_flags;
  z64_file.void_flag = 1;
  zu_execute_game(z64_file.void_entrance, 0x0000);
}

void zu_execute_game(int16_t entrance_index, uint16_t cutscene_index)
{
  if (entrance_index != z64_file.entrance_index ||
      cutscene_index != z64_file.cutscene_index)
  {
    z64_file.seq_index = -1;
    z64_file.night_sfx = -1;
  }
  z64_file.entrance_index = entrance_index;
  z64_file.cutscene_index = cutscene_index;
  z64_file.game_mode = 0;
  if (z64_file.minigame_state == 1)
    z64_file.minigame_state = 3;
  z64_game.entrance_index = entrance_index;
  z64_ctxt.state_continue = 0;
  z64_ctxt.next_ctor = z64_state_ovl_tab[3].vram_ctor;
  z64_ctxt.next_size = z64_state_ovl_tab[3].ctxt_size;
}

void zu_audio_cmd(uint32_t cmd)
{
  uint32_t* z64_audio_cmd_buf = (uint32_t*)0x80124800;
  z64_audio_cmd_buf[(*((uint8_t*)0x801043B0))++] = cmd;
}

void command_age(void)
{
  int age = z64_file.link_age;
  z64_file.link_age = z64_game.link_age;
  z64_game.link_age = !z64_game.link_age;
  z64_SwitchAgeEquips();
  z64_file.link_age = age;
  for (int i = 0; i < 4; ++i)
    if (z64_file.button_items[i] != Z64_ITEM_NULL)
      z64_UpdateItemButton(&z64_game, i);
  z64_UpdateEquipment(&z64_game, &z64_link);
  zu_execute_game(z64_file.entrance_index, 0x0);
}

void gz_warp(int16_t entrance_index, uint16_t cutscene_index, int age)
{
  if (age == 0)
    age = z64_game.link_age;
  else
    --age;
  if (z64_game.link_age == age)
    z64_file.link_age = age;
  else
    z64_game.link_age = age;
  zu_execute_game(entrance_index, cutscene_index);
}

_Bool push_object_proc(int16_t object_id)
{
  size_t max_objects = sizeof(z64_game.obj_ctxt.objects) /
                       sizeof(*z64_game.obj_ctxt.objects) - 3;
  if (z64_game.obj_ctxt.n_objects > max_objects)
	return 0;

  if (object_id == 0)
    return 0;
  int n = z64_game.obj_ctxt.n_objects++;
  z64_mem_obj_t *obj = z64_game.obj_ctxt.objects;
  obj[n].id = -object_id;
  obj[n].getfile.vrom_addr = 0;
  size_t size = z64_object_table[object_id].vrom_end -
                z64_object_table[object_id].vrom_start;
  obj[n + 1].data = (char *)obj[n].data + size;
  return 1;
}
