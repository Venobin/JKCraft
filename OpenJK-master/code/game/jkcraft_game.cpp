#include "g_local.h"
#include "b_local.h"
#include "jkcraft_game.h"
#include "../jkcraft/jkc_protocol.h"

#include <algorithm>
#include <cmath>
#include <cstring>

extern gentity_t* NPC_Spawn_Do(gentity_t* ent, qboolean fullSpawnNow);
extern void NPC_PrecacheByClassName(const char* type);
extern void G_SetEnemy(gentity_t* self, gentity_t* enemy);
extern void WP_ForcePowerDrain(gentity_t* self, forcePowers_t forcePower, int overrideAmt);
extern void WP_ForcePowerStop(gentity_t* self, forcePowers_t forcePower);
extern void Use_BinaryMover(gentity_t* ent, gentity_t* other, gentity_t* activator);
extern bool in_camera;

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
#if defined(_WIN32)
	HANDLE mapping = nullptr;
	jkcraft::protocol::Header* header = nullptr;
	cvar_t* puppet = nullptr;
	cvar_t* unitsPerBlock = nullptr;
	cvar_t* cameraSync = nullptr;
	cvar_t* preloading = nullptr;
	cvar_t* combatTest = nullptr;
	float lastFov = -1.0f;
	std::uint32_t lastCameraMode = ~0u;
	bool applyingMinecraftEvent = false;
	bool loggedActors = false;
	bool loggedActorHit = false;
	bool loggedPlayerHurt = false;
	bool loggedExplosion = false;
	bool loggedArrow = false;
	bool loggedActorPush = false;
	bool loggedExactCamera = false;
	bool loggedForceState = false;
	bool loggedPropHit = false;
	bool loggedNpcSight = false;
	std::uint32_t loggedWeaponReactions = 0;
	int combatTestLevel = -1;
	int combatTestNpc = ENTITYNUM_NONE;
	int combatTestDamageAt = 0;
	std::uint32_t Read32(const std::uint32_t* value);
	bool OpenLink();
	int cellEntityNumbers[jkcraft::protocol::kMaxMinecraftCells]{};
	jkcraft::protocol::MinecraftCell cellEntities[jkcraft::protocol::kMaxMinecraftCells]{};
	std::uint32_t cellWorldId = 0;
	std::uint32_t cellSequence = 0;
	int cellLevelTime = 0;
	bool cellsInitialized = false;
	int npcLavaNext[MAX_GENTITIES]{};

	void ClearMinecraftCells(bool freeEntities)
	{
		for (std::uint32_t i = 0; i < jkcraft::protocol::kMaxMinecraftCells; ++i)
		{
			const int number = cellEntityNumbers[i];
			if (freeEntities && number > 0 && number < globals.num_entities)
			{
				gentity_t* entity = &g_entities[number];
				if (entity->inuse && entity->classname && !std::strcmp(entity->classname, "jkcraft_minecraft_cell"))
				{
					G_FreeEntity(entity);
				}
			}
			cellEntityNumbers[i] = 0;
		}
		cellSequence = 0;
	}

	void AffectNpcsByMinecraftFluids(float scale)
	{
		const float half = scale * 0.5f;
		for (int n = 1; n < globals.num_entities; ++n)
		{
			gentity_t* actor = &g_entities[n];
			if (!actor->inuse || !actor->NPC || !actor->client || actor->health <= 0) continue;
			for (std::uint32_t i = 0; i < jkcraft::protocol::kMaxMinecraftCells; ++i)
			{
				if (!cellEntityNumbers[i] || cellEntities[i].kind < 2) continue;
				const auto& cell = cellEntities[i];
				const float cx = (cell.x + 0.5f) * scale;
				const float cy = -(cell.z + 0.5f) * scale;
				const float cz = (cell.y + 0.5f) * scale;
				if (actor->currentOrigin[0] + actor->maxs[0] < cx - half ||
					actor->currentOrigin[0] + actor->mins[0] > cx + half ||
					actor->currentOrigin[1] + actor->maxs[1] < cy - half ||
					actor->currentOrigin[1] + actor->mins[1] > cy + half ||
					actor->currentOrigin[2] + actor->maxs[2] < cz - half ||
					actor->currentOrigin[2] + actor->mins[2] > cz + half) continue;
				// A gentle buoyant/outward current keeps NPCs from walking through a liquid cell.
				actor->client->ps.velocity[2] = std::max(actor->client->ps.velocity[2], cell.kind == 2 ? 55.0f : 35.0f);
				float dx = actor->currentOrigin[0] - cx;
				float dy = actor->currentOrigin[1] - cy;
				const float length = std::sqrt(dx * dx + dy * dy);
				if (length > 0.01f)
				{
					actor->client->ps.velocity[0] += dx / length * 10.0f;
					actor->client->ps.velocity[1] += dy / length * 10.0f;
				}
				if (cell.kind == 3 && !(actor->watertype & CONTENTS_LAVA))
				{
					if (npcLavaNext[n] > level.time + 1000) npcLavaNext[n] = 0;
					if (level.time >= npcLavaNext[n])
					{
						npcLavaNext[n] = level.time + 500;
						G_Damage(actor, nullptr, nullptr, nullptr, nullptr, 8, 0, MOD_LAVA);
					}
				}
				break;
			}
		}
	}

	void SyncMinecraftCells(float scale, std::uint32_t worldId)
	{
		if (cellsInitialized && level.time < cellLevelTime)
		{
			// G_InitGame has already replaced the old entity array.
			ClearMinecraftCells(false);
		}
		cellLevelTime = level.time;
		cellsInitialized = true;
		if (!OpenLink() || !header || header->version != jkcraft::protocol::kVersion)
		{
			ClearMinecraftCells(true);
			return;
		}
		const auto* source = reinterpret_cast<const jkcraft::protocol::MinecraftCells*>(
			reinterpret_cast<const std::uint8_t*>(header) + jkcraft::protocol::kMinecraftCellsOffset);
		jkcraft::protocol::MinecraftCells snapshot{};
		bool valid = false;
		for (int attempt = 0; attempt < 5; ++attempt)
		{
			const std::uint32_t before = Read32(&source->seq);
			if (before & 1u) continue;
			std::memcpy(&snapshot, source, sizeof(snapshot));
			MemoryBarrier();
			if (before == Read32(&source->seq)) { valid = true; break; }
		}
		if (!valid || snapshot.worldId != worldId || snapshot.count > jkcraft::protocol::kMaxMinecraftCells)
		{
			ClearMinecraftCells(true);
			return;
		}
		if (cellWorldId == worldId && cellSequence == snapshot.seq) return;
		cellWorldId = worldId;
		cellSequence = snapshot.seq;
		for (std::uint32_t i = 0; i < jkcraft::protocol::kMaxMinecraftCells; ++i)
		{
			const auto cell = i < snapshot.count ? snapshot.cells[i] : jkcraft::protocol::MinecraftCell{};
			const int number = cellEntityNumbers[i];
			gentity_t* entity = number > 0 && number < globals.num_entities ? &g_entities[number] : nullptr;
			if (entity && (!entity->inuse || !entity->classname || std::strcmp(entity->classname, "jkcraft_minecraft_cell"))) entity = nullptr;
			if (entity && (i >= snapshot.count || std::memcmp(&cell, &cellEntities[i], sizeof(cell)) != 0))
			{
				G_FreeEntity(entity);
				entity = nullptr;
			}
			if (i >= snapshot.count || cell.kind < 1 || cell.kind > 3)
			{
				cellEntityNumbers[i] = 0;
				continue;
			}
			if (!entity)
			{
				entity = G_Spawn();
				if (!entity) { cellEntityNumbers[i] = 0; continue; }
				entity->classname = "jkcraft_minecraft_cell";
				entity->svFlags |= SVF_NOCLIENT;
				const float half = scale * 0.5f;
				VectorSet(entity->mins, -half, -half, -half);
				VectorSet(entity->maxs, half, half, half);
				entity->contents = cell.kind == 1 ? CONTENTS_SOLID | CONTENTS_MONSTERCLIP | CONTENTS_BOTCLIP :
					cell.kind == 2 ? CONTENTS_WATER : CONTENTS_LAVA;
				vec3_t origin{(cell.x + 0.5f) * scale, -(cell.z + 0.5f) * scale, (cell.y + 0.5f) * scale};
				G_SetOrigin(entity, origin);
				gi.linkentity(entity);
				cellEntityNumbers[i] = entity->s.number;
				cellEntities[i] = cell;
			}
		}
	}

	std::uint32_t Read32(const std::uint32_t* value)
	{
		return static_cast<std::uint32_t>(InterlockedCompareExchange(
			reinterpret_cast<volatile LONG*>(const_cast<std::uint32_t*>(value)), 0, 0));
	}

	std::uint64_t Read64(const std::uint64_t* value)
	{
		return static_cast<std::uint64_t>(InterlockedCompareExchange64(
			reinterpret_cast<volatile LONG64*>(const_cast<std::uint64_t*>(value)), 0, 0));
	}

	void Write32(std::uint32_t* value, std::uint32_t next)
	{
		InterlockedExchange(reinterpret_cast<volatile LONG*>(value), static_cast<LONG>(next));
	}

	void Write64(std::uint64_t* value, std::uint64_t next)
	{
		InterlockedExchange64(reinterpret_cast<volatile LONG64*>(value), static_cast<LONG64>(next));
	}

	bool OpenLink()
	{
		if (header)
		{
			return true;
		}
		// InterlockedCompareExchange is used for tear-free reads and therefore needs
		// a writable view even though this module does not publish protocol fields.
		mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, jkcraft::protocol::kMappingName);
		if (!mapping)
		{
			return false;
		}
		header = static_cast<jkcraft::protocol::Header*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));
		if (!header)
		{
			CloseHandle(mapping);
			mapping = nullptr;
			return false;
		}
		return true;
	}

	bool ReadMinecraftState(jkcraft::protocol::MinecraftState& state)
	{
		if (!OpenLink() || header->magic != jkcraft::protocol::kMagic || header->version != jkcraft::protocol::kVersion)
		{
			return false;
		}
		const std::uint64_t now = GetTickCount64();
		const std::uint64_t heartbeat = Read64(&header->minecraftHeartbeatMs);
		if (!heartbeat || now < heartbeat || now - heartbeat >= 3000)
		{
			return false;
		}
		const auto* source = reinterpret_cast<const jkcraft::protocol::MinecraftState*>(
			reinterpret_cast<const std::uint8_t*>(header) + jkcraft::protocol::kMinecraftStateOffset);
		for (int attempt = 0; attempt < 5; ++attempt)
		{
			const std::uint32_t before = Read32(&source->seq);
			if (before & 1u)
			{
				continue;
			}
			std::memcpy(&state, source, sizeof(state));
			MemoryBarrier();
			const std::uint32_t after = Read32(&source->seq);
			if (before == after && !(after & 1u))
			{
				return true;
			}
		}
		return false;
	}

	bool ReadHostState(jkcraft::protocol::HostState& state)
	{
		if (!OpenLink() || header->magic != jkcraft::protocol::kMagic ||
			header->version != jkcraft::protocol::kVersion)
		{
			return false;
		}
		const auto* source = reinterpret_cast<const jkcraft::protocol::HostState*>(
			reinterpret_cast<const std::uint8_t*>(header) + jkcraft::protocol::kStateOffset);
		for (int attempt = 0; attempt < 5; ++attempt)
		{
			const std::uint32_t before = Read32(&source->seq);
			if (before & 1u) continue;
			std::memcpy(&state, source, sizeof(state));
			MemoryBarrier();
			const std::uint32_t after = Read32(&source->seq);
			if (before == after && !(after & 1u)) return true;
		}
		return false;
	}

	bool TeleportAcknowledged(std::uint32_t acknowledgement)
	{
		if (!acknowledgement || !header)
		{
			return false;
		}
		const auto* state = reinterpret_cast<const jkcraft::protocol::HostState*>(
			reinterpret_cast<const std::uint8_t*>(header) + jkcraft::protocol::kStateOffset);
		for (int attempt = 0; attempt < 5; ++attempt)
		{
			const std::uint32_t before = Read32(&state->seq);
			if (before & 1u)
			{
				continue;
			}
			const std::uint32_t requested = state->teleportSeq;
			MemoryBarrier();
			const std::uint32_t after = Read32(&state->seq);
			if (before == after && !(after & 1u))
			{
				return requested != 0u && acknowledgement == requested;
			}
		}
		return false;
	}

	bool HostCinematic()
	{
		if (!preloading)
		{
			preloading = gi.cvar("jkc_preloading", "0", 0);
		}
		const bool remoteView = g_entities[0].inuse && g_entities[0].client &&
			g_entities[0].client->ps.viewEntity > 0 &&
			g_entities[0].client->ps.viewEntity < ENTITYNUM_WORLD;
		// The engine's live snapshot may have resumed gameplay while the game DLL
		// still carries the previous map's camera/view-entity state.
		if (JKCraft_ShouldReleaseStaleCamera()) return false;
		return in_camera || remoteView || (preloading && preloading->integer != 0);
	}

	void ApplyCameraSettings(const jkcraft::protocol::MinecraftState& state, float scale)
	{
		if (!cameraSync || !cameraSync->integer)
		{
			return;
		}

		if (state.fovDeg >= 1.0f && state.fovDeg <= 160.0f &&
			(lastFov < 0.0f || state.fovDeg < lastFov - 0.02f || state.fovDeg > lastFov + 0.02f))
		{
			gi.cvar_set("cg_fov", va("%.3f", state.fovDeg));
			lastFov = state.fovDeg;
		}

		const std::uint32_t cameraMode = state.cameraMode <= 2u ? state.cameraMode : 0u;
		// Minecraft supplies the exact first/back/front camera transform. Keep JA's own
		// third-person solver disabled: running both solvers wastes a full native camera
		// pass and may briefly draw/animate the hidden Jedi model.
		cvar_t* currentThirdPerson = gi.cvar("cg_thirdperson", "0", 0);
		cvar_t* currentAngle = gi.cvar("cg_thirdPersonAngle", "0", 0);
		if (cameraMode != lastCameraMode || currentThirdPerson->integer != 0 || currentAngle->integer != 0)
		{
			gi.cvar_set("cg_thirdperson", "0");
			gi.cvar_set("cg_thirdPersonAngle", "0");
			lastCameraMode = cameraMode;
		}
		(void)scale;
	}

	void PublishActors(gentity_t* player, float scale)
	{
		auto* table = reinterpret_cast<jkcraft::protocol::ActorTable*>(
			reinterpret_cast<std::uint8_t*>(header) + jkcraft::protocol::kActorTableOffset);
		const std::uint32_t nextSeq = (Read32(&table->seq) + 2u) & ~1u;
		Write32(&table->seq, nextSeq - 1u);
		std::uint32_t count = 0;
		const float range = scale * 96.0f;
		const float rangeSquared = range * range;
		for (int i = 1; i < globals.num_entities && count < jkcraft::protocol::kMaxActors; ++i)
		{
			gentity_t* actor = &g_entities[i];
			if (!actor->inuse || !actor->client || !actor->NPC)
			{
				continue;
			}
			vec3_t delta;
			VectorSubtract(actor->currentOrigin, player->currentOrigin, delta);
			if (VectorLengthSquared(delta) > rangeSquared)
			{
				continue;
			}
			auto& out = table->actors[count++];
			std::memset(&out, 0, sizeof(out));
			out.formId = static_cast<std::uint32_t>(actor->s.number);
			if (actor->client->playerTeam == TEAM_ENEMY)
			{
				out.flags |= jkcraft::protocol::kActorHostile;
			}
			if (actor->health <= 0)
			{
				out.flags |= jkcraft::protocol::kActorDead;
			}
			if (actor->flags & FL_GODMODE)
			{
				out.flags |= jkcraft::protocol::kActorEssential;
			}
			if (actor->enemy)
			{
				out.flags |= jkcraft::protocol::kActorInCombat;
			}
			out.x = actor->currentOrigin[0] / scale;
			out.y = (actor->currentOrigin[2] - 24.0f) / scale;
			out.z = -actor->currentOrigin[1] / scale;
			out.yaw = AngleNormalize360(-actor->currentAngles[YAW] - 90.0f);
			out.width = std::max(0.25f, (actor->maxs[0] - actor->mins[0]) / scale);
			out.height = std::max(0.5f, (actor->maxs[2] - actor->mins[2]) / scale);
			out.healthFrac = actor->max_health > 0 ?
				std::max(0.0f, static_cast<float>(actor->health) / actor->max_health) : 1.0f;
			out.level = 1;
			const char* name = actor->NPC_type && actor->NPC_type[0] ? actor->NPC_type : actor->classname;
			if (name)
			{
				Q_strncpyz(out.name, name, sizeof(out.name));
			}
		}
		table->count = count;
		MemoryBarrier();
		Write32(&table->seq, nextSeq);
		if (count && !loggedActors)
		{
			loggedActors = true;
			gi.Printf("JKCraft: published %u nearby Jedi Academy actors to Minecraft\n", count);
		}
	}

	void PublishForceState(gentity_t* player)
	{
		auto* state = reinterpret_cast<jkcraft::protocol::ForceState*>(
			reinterpret_cast<std::uint8_t*>(header) + jkcraft::protocol::kForceStateOffset);
		const std::uint32_t nextSeq = (Read32(&state->seq) + 2u) & ~1u;
		Write32(&state->seq, nextSeq - 1u);
		state->powersKnown = static_cast<std::uint32_t>(player->client->ps.forcePowersKnown);
		state->powersActive = static_cast<std::uint32_t>(player->client->ps.forcePowersActive);
		state->forcePower = player->client->ps.forcePower;
		state->forcePowerMax = player->client->ps.forcePowerMax;
		state->levitationLevel = player->client->ps.forcePowerLevel[FP_LEVITATION];
		state->reserved0 = 0;
		state->reserved1 = 0;
		MemoryBarrier();
		Write32(&state->seq, nextSeq);
		if (!loggedForceState)
		{
			loggedForceState = true;
			gi.Printf("JKCraft: Force bridge active (known 0x%x, energy %d/%d, jump level %d)\n",
				state->powersKnown, state->forcePower, state->forcePowerMax, state->levitationLevel);
		}
	}

	gentity_t* ActorById(std::uint32_t id)
	{
		if (!id || id >= static_cast<std::uint32_t>(globals.num_entities))
		{
			return nullptr;
		}
		gentity_t* actor = &g_entities[id];
		return actor->inuse && actor->client && actor->NPC ? actor : nullptr;
	}

	void MinecraftPointToJA(float x, float y, float z, float scale, vec3_t out)
	{
		out[0] = x * scale;
		out[1] = -z * scale;
		out[2] = y * scale;
	}

	void ApplyMinecraftExplosion(const jkcraft::protocol::GameEvent& event,
		gentity_t* player, float scale)
	{
		vec3_t origin;
		MinecraftPointToJA(event.a, event.b, event.c, scale, origin);
		const float radius = std::max(0.5f, std::min(event.d, 16.0f)) * scale;
		const float maximumDamage = std::max(10.0f, event.d * 10.0f);
		G_PlayEffect(event.d >= 3.0f ? "env/med_explode" : "env/small_explode", origin);

		// Actor damage is already delivered precisely by Minecraft through EV_HIT_ACTOR.
		// Only damage JA props/breakables here, otherwise every NPC would be hit twice.
		gentity_t* nearby[MAX_GENTITIES];
		const int count = G_RadiusList(origin, radius, nullptr, qtrue, nearby);
		for (int i = 0; i < count; ++i)
		{
			gentity_t* target = nearby[i];
			if (!target || target->client || !target->contents || !CanDamage(target, origin))
			{
				continue;
			}
			vec3_t direction;
			VectorSubtract(target->currentOrigin, origin, direction);
			const float distance = VectorNormalize(direction);
			const int damage = static_cast<int>(std::ceil(maximumDamage * std::max(0.0f, 1.0f - distance / radius)));
			if (damage > 0)
			{
				G_Damage(target, player, player, direction, origin, damage, DAMAGE_RADIUS,
					MOD_EXPLOSIVE_SPLASH);
			}
		}
		if (!loggedExplosion)
		{
			loggedExplosion = true;
			gi.Printf("JKCraft: first Minecraft explosion rendered in JA (radius %.1f blocks, %d nearby props)\n",
				event.d, count);
		}
	}

	void ApplyMinecraftArrowImpact(const jkcraft::protocol::GameEvent& event, float scale)
	{
		gentity_t* actor = ActorById(event.formId);
		if (!actor || actor->health <= 0)
		{
			return;
		}
		vec3_t origin;
		MinecraftPointToJA(event.a, event.b, event.c, scale, origin);
		float pitch = 0.0f;
		std::memcpy(&pitch, &event.flags, sizeof(pitch));
		constexpr float degreesToRadians = 0.01745329251994329577f;
		const float yawRadians = event.d * degreesToRadians;
		const float pitchRadians = pitch * degreesToRadians;
		const float horizontal = std::cos(pitchRadians);
		vec3_t direction{
			std::sin(yawRadians) * horizontal,
			-std::cos(yawRadians) * horizontal,
			std::sin(pitchRadians)};
		VectorNormalize(direction);
		G_PlayEffect("disruptor/flesh_impact", origin, direction);
		if (!loggedArrow)
		{
			loggedArrow = true;
			gi.Printf("JKCraft: first Minecraft arrow impact rendered on JA actor %u (variant %u)\n",
				event.formId, event.weapon);
		}
	}

	void ApplyMinecraftPropHit(const jkcraft::protocol::GameEvent& event,
		gentity_t* player, float scale)
	{
		if (!player || !player->client || !std::isfinite(event.a) || event.a <= 0.0f)
		{
			return;
		}
		vec3_t start, forward, end;
		const bool arrow = (event.flags & jkcraft::protocol::kHitProjectile) != 0 &&
			event.weapon == jkcraft::protocol::kWeaponArrow;
		if (arrow)
		{
			if (!std::isfinite(event.b) || !std::isfinite(event.c) || !std::isfinite(event.d)) return;
			vec3_t impact;
			MinecraftPointToJA(event.b, event.c, event.d, scale, impact);
			VectorSubtract(impact, player->client->renderInfo.eyePoint, forward);
			if (VectorNormalize(forward) <= 0.0f) return;
			VectorMA(impact, -16.0f, forward, start);
			VectorMA(impact, 16.0f, forward, end);
		}
		else
		{
			VectorCopy(player->client->renderInfo.eyePoint, start);
			AngleVectors(player->client->ps.viewangles, forward, nullptr, nullptr);
			const float reachBlocks = std::isfinite(event.d) ?
				std::max(1.0f, std::min(event.d, 8.0f)) : 5.0f;
			VectorMA(start, reachBlocks * scale, forward, end);
		}

		trace_t trace{};
		gi.trace(&trace, start, vec3_origin, vec3_origin, end, player->s.number,
			MASK_SHOT, (EG2_Collision)0, 0);
		if (trace.entityNum < 0 || trace.entityNum >= ENTITYNUM_WORLD)
		{
			return;
		}
		gentity_t* target = &g_entities[trace.entityNum];
		// Living actors use the normal proxy combat bridge. This path is deliberately
		// restricted to native JA props so one click can never damage an NPC twice.
		if (!target->inuse || target->client || !target->takedamage || target->health <= 0)
		{
			return;
		}

		const float minecraftDamage = std::max(0.0f,
			std::min(event.a, jkcraft::protocol::kMaxMinecraftDamagePerTick));
		int damage = std::max(1, static_cast<int>(std::ceil(
			minecraftDamage * jkcraft::protocol::kMinecraftToJediDamage)));
		// The mission's exploding crate is a one-hit trigger. Preserve its normal
		// death callback, splash damage and target activation by using G_Damage.
		if (target->classname && !Q_stricmp(target->classname, "misc_exploding_crate"))
		{
			damage = std::max(damage, target->health);
		}
		const bool cuttingTool = event.weapon == jkcraft::protocol::kWeaponAxe ||
			event.weapon == jkcraft::protocol::kWeaponBlade;
		// Axes and swords stand in for the saber against mission props marked SABERONLY.
		// G_Damage invokes the object's stock pain/death callbacks and G_UseTargets, so
		// destroying a required tree advances the original mission script normally.
		G_Damage(target, player, player, forward, trace.endpos, damage,
			DAMAGE_NO_KNOCKBACK, arrow ? MOD_BOWCASTER : cuttingTool ? MOD_SABER : MOD_MELEE);
		if (!loggedPropHit)
		{
			loggedPropHit = true;
			gi.Printf("JKCraft: first Minecraft hit applied to JA prop %d (%s, %d damage, weapon %u)\n",
				trace.entityNum, target->classname ? target->classname : "unknown", damage, event.weapon);
		}
	}

	void DrainGameEvents(gentity_t* player, float scale)
	{
		auto* ring = reinterpret_cast<std::uint8_t*>(header) + jkcraft::protocol::kEventRingOffset;
		auto* headPtr = reinterpret_cast<std::uint64_t*>(ring + jkcraft::protocol::kEventRingHeadOffset);
		auto* tailPtr = reinterpret_cast<std::uint64_t*>(ring + jkcraft::protocol::kEventRingTailOffset);
		const std::uint64_t head = Read64(headPtr);
		std::uint64_t tail = Read64(tailPtr);
		while (tail < head && head - tail <= jkcraft::protocol::kEventRingEntries)
		{
			const auto* event = reinterpret_cast<const jkcraft::protocol::GameEvent*>(
				ring + jkcraft::protocol::kEventRingDataOffset) +
				(tail & (jkcraft::protocol::kEventRingEntries - 1));
			if (event->type == jkcraft::protocol::kEventHitActor)
			{
				gentity_t* actor = ActorById(event->formId);
				if (actor && actor->health > 0 && (event->a > 0.0f || event->d > 0.0f))
				{
					vec3_t direction{
						std::isfinite(event->b) ? event->b : 0.0f,
						std::isfinite(event->c) ? -event->c : 0.0f,
						0.0f };
					if (VectorNormalize(direction) == 0.0f)
					{
						VectorSubtract(actor->currentOrigin, player->currentOrigin, direction);
						VectorNormalize(direction);
					}
					const float minecraftDamage = std::isfinite(event->a) ?
						std::max(0.0f, std::min(event->a, jkcraft::protocol::kMaxMinecraftDamagePerTick)) : 0.0f;
					const int damage = minecraftDamage > 0.0f ? std::max(1, static_cast<int>(
						std::ceil(minecraftDamage * jkcraft::protocol::kMinecraftToJediDamage))) : 0;
					const bool critical = (event->flags & jkcraft::protocol::kHitCritical) != 0;
					const bool fire = (event->flags & jkcraft::protocol::kHitFire) != 0;
					int meansOfDeath = MOD_MELEE;
					if (fire)
					{
						meansOfDeath = MOD_LAVA;
					}
					else if (event->weapon == jkcraft::protocol::kWeaponArrow ||
						event->weapon == jkcraft::protocol::kWeaponPierce ||
						(event->flags & jkcraft::protocol::kHitProjectile))
					{
						meansOfDeath = MOD_BOWCASTER;
					}
					if (damage > 0)
					{
						G_Damage(actor, player, player, direction, actor->currentOrigin, damage,
							DAMAGE_NO_KNOCKBACK, meansOfDeath);
						if (fire && actor->health > 0)
						{
							G_PlayEffect("boba/fthrw", actor->currentOrigin, direction);
						}
					}
					float reaction = std::isfinite(event->d) ? std::max(0.0f, event->d) * 50.0f : 0.0f;
					if (event->weapon == jkcraft::protocol::kWeaponAxe)
					{
						reaction *= 1.25f;
					}
					else if (event->weapon == jkcraft::protocol::kWeaponBlunt)
					{
						reaction *= 1.5f;
					}
					else if (event->weapon == jkcraft::protocol::kWeaponArrow ||
						event->weapon == jkcraft::protocol::kWeaponPierce)
					{
						reaction *= 0.75f;
					}
					if (critical)
					{
						reaction = std::max(reaction * 1.35f, 35.0f);
						G_PlayEffect("disruptor/flesh_impact", actor->currentOrigin, direction);
					}
					if (reaction > 0.0f && actor->health > 0)
					{
						// Apply push separately so zero-damage Minecraft contacts still move the actor.
						const float knockback = std::max(1.0f, std::min(reaction, 120.0f));
						G_ApplyKnockback(actor, direction, knockback);
						if (!loggedActorPush)
						{
							loggedActorPush = true;
							gi.Printf("JKCraft: first Minecraft knockback applied to JA actor %u (strength %.1f, damage %d)\n",
								event->formId, knockback, damage);
						}
					}
					const std::uint32_t weaponBit = event->weapon <= jkcraft::protocol::kWeaponArrow ?
						(1u << event->weapon) : (1u << 31);
					if (damage > 0 && !(loggedWeaponReactions & weaponBit))
					{
						loggedWeaponReactions |= weaponBit;
						gi.Printf("JKCraft: Minecraft weapon reaction actor %u weapon %u flags 0x%x mod %d push %.1f\n",
							event->formId, event->weapon, event->flags, meansOfDeath,
							std::max(0.0f, std::min(reaction, 120.0f)));
					}
					if (!loggedActorHit && damage > 0)
					{
						loggedActorHit = true;
						gi.Printf("JKCraft: first Minecraft hit applied to JA actor %u (%d damage)\n",
							event->formId, damage);
					}
				}
			}
			else if (event->type == jkcraft::protocol::kEventExplosion)
			{
				ApplyMinecraftExplosion(*event, player, scale);
			}
			else if (event->type == jkcraft::protocol::kEventArrowStuck)
			{
				ApplyMinecraftArrowImpact(*event, scale);
			}
			else if (event->type == jkcraft::protocol::kEventForceJump)
			{
				const bool known = (player->client->ps.forcePowersKnown & (1 << FP_LEVITATION)) != 0;
				if (known && player->client->ps.forcePowerLevel[FP_LEVITATION] > 0 &&
					player->client->ps.forcePower >= 10)
				{
					WP_ForcePowerDrain(player, FP_LEVITATION, 10);
					// Minecraft performs the actual jump. Do not leave OpenJK's native Levitation
					// state active: it would make WP_ForcePowersUpdate think a power is in use
					// forever because the native movement command never completes this jump.
					WP_ForcePowerStop(player, FP_LEVITATION);
					player->client->ps.forceJumpCharge = 0;
					player->client->ps.forceJumpZStart = 0;
					player->client->ps.forcePowerDuration[FP_LEVITATION] = 0;
					player->client->ps.forcePowerRegenDebounceTime = level.time + 500;
					player->client->sess.missionStats.forceUsed[FP_LEVITATION]++;
					gi.Printf("JKCraft: Force Jump accepted (level %d, energy now %d/%d)\n",
						player->client->ps.forcePowerLevel[FP_LEVITATION],
						player->client->ps.forcePower, player->client->ps.forcePowerMax);
				}
			}
			else if (event->type == jkcraft::protocol::kEventHitProp)
			{
				ApplyMinecraftPropHit(*event, player, scale);
			}
			else if (event->type == jkcraft::protocol::kEventPlayerDied && player->health > 0)
			{
				gentity_t* attacker = ActorById(event->formId);
				if (!attacker)
				{
					attacker = &g_entities[ENTITYNUM_WORLD];
				}
				applyingMinecraftEvent = true;
				G_Damage(player, attacker, attacker, nullptr, player->currentOrigin,
					player->health + 10000, DAMAGE_NO_PROTECTION | DAMAGE_NO_ARMOR | DAMAGE_NO_KNOCKBACK,
					MOD_UNKNOWN);
				applyingMinecraftEvent = false;
				gi.Printf("JKCraft: Minecraft player death applied to JA player (attacker %u)\n",
					event->formId);
			}
			++tail;
		}
		if (head - tail > jkcraft::protocol::kEventRingEntries)
		{
			tail = head;
		}
		Write64(tailPtr, tail);
	}

	bool PushHurt(int kind, int damage, int attackerId, int flags)
	{
		auto* ring = reinterpret_cast<std::uint8_t*>(header) + jkcraft::protocol::kInputRingOffset;
		auto* headPtr = reinterpret_cast<std::uint64_t*>(ring + jkcraft::protocol::kInputRingHeadOffset);
		auto* tailPtr = reinterpret_cast<std::uint64_t*>(ring + jkcraft::protocol::kInputRingTailOffset);
		const std::uint64_t head = Read64(headPtr);
		const std::uint64_t tail = Read64(tailPtr);
		if (head - tail >= jkcraft::protocol::kInputRingEntries)
		{
			return false;
		}
		auto* event = reinterpret_cast<jkcraft::protocol::InputEvent*>(
			ring + jkcraft::protocol::kInputRingDataOffset) +
			(head & (jkcraft::protocol::kInputRingEntries - 1));
		event->type = jkcraft::protocol::kInputHurt;
		event->code = static_cast<std::uint16_t>(kind);
		event->a = damage * 100;
		event->b = attackerId;
		event->c = flags;
		MemoryBarrier();
		Write64(headPtr, head + 1);
		return true;
	}

	bool PushPickup(int kind, int quantity)
	{
		auto* ring = reinterpret_cast<std::uint8_t*>(header) + jkcraft::protocol::kInputRingOffset;
		auto* headPtr = reinterpret_cast<std::uint64_t*>(ring + jkcraft::protocol::kInputRingHeadOffset);
		auto* tailPtr = reinterpret_cast<std::uint64_t*>(ring + jkcraft::protocol::kInputRingTailOffset);
		const std::uint64_t head = Read64(headPtr);
		const std::uint64_t tail = Read64(tailPtr);
		if (head - tail >= jkcraft::protocol::kInputRingEntries)
		{
			return false;
		}
		auto* event = reinterpret_cast<jkcraft::protocol::InputEvent*>(
			ring + jkcraft::protocol::kInputRingDataOffset) +
			(head & (jkcraft::protocol::kInputRingEntries - 1));
		event->type = jkcraft::protocol::kInputPickup;
		event->code = static_cast<std::uint16_t>(kind);
		event->a = quantity;
		event->b = 0;
		event->c = 0;
		MemoryBarrier();
		Write64(headPtr, head + 1);
		return true;
	}

	void MaybeSpawnCombatTest(gentity_t* player)
	{
		if (!combatTest)
		{
			combatTest = gi.cvar("jkc_combat_test", "0", CVAR_TEMP);
		}
		if (!combatTest->integer)
		{
			return;
		}
		if (combatTestLevel == level.globalTime)
		{
			if (combatTestDamageAt && level.time >= combatTestDamageAt)
			{
				combatTestDamageAt = 0;
				gentity_t* npc = ActorById(combatTestNpc);
				if (npc && npc->health > 0)
				{
					vec3_t direction;
					VectorSubtract(player->currentOrigin, npc->currentOrigin, direction);
					VectorNormalize(direction);
					G_Damage(player, npc, npc, direction, player->currentOrigin, 15, 0, MOD_MELEE);
					gi.Printf("JKCraft: combat test delivered a 15-damage NPC strike\n");
				}
			}
			return;
		}
		combatTestLevel = level.globalTime;
		combatTestNpc = ENTITYNUM_NONE;
		combatTestDamageAt = 0;

		gentity_t* spawner = G_Spawn();
		if (!spawner)
		{
			gi.Printf("JKCraft: combat test could not allocate an NPC spawner\n");
			return;
		}
		vec3_t forward, end, floor;
		trace_t trace;
		AngleVectors(player->client->ps.viewangles, forward, nullptr, nullptr);
		VectorNormalize(forward);
		VectorMA(player->currentOrigin, 80.0f, forward, end);
		gi.trace(&trace, player->currentOrigin, nullptr, nullptr, end, player->s.number,
			MASK_SOLID, (EG2_Collision)0, 0);
		VectorCopy(trace.endpos, end);
		VectorCopy(end, floor);
		floor[2] -= 128.0f;
		gi.trace(&trace, end, nullptr, nullptr, floor, player->s.number,
			MASK_SOLID, (EG2_Collision)0, 0);
		VectorCopy(trace.endpos, end);
		end[2] += 24.0f;
		G_SetOrigin(spawner, end);
		VectorCopy(spawner->currentOrigin, spawner->s.origin);
		spawner->s.angles[YAW] = AngleNormalize360(player->client->ps.viewangles[YAW] + 180.0f);
		spawner->NPC_type = G_NewString("stormtrooper");
		spawner->NPC_targetname = G_NewString("jkcraft_combat_test");
		spawner->count = 1;
		spawner->wait = 500;
		NPC_PrecacheByClassName(spawner->NPC_type);
		gentity_t* npc = NPC_Spawn_Do(spawner, qtrue);
		if (!npc)
		{
			gi.Printf("JKCraft: combat test failed to spawn stormtrooper\n");
			return;
		}
		G_SetEnemy(npc, player);
		combatTestNpc = npc->s.number;
		combatTestDamageAt = level.time + 2000;
		gi.Printf("JKCraft: combat test spawned stormtrooper %d %.1f units from the player\n",
			npc->s.number, Distance(npc->currentOrigin, player->currentOrigin));
	}
#endif
}

void JKCraft_Precache()
{
#if defined(_WIN32)
	G_EffectIndex("boba/fthrw");
	G_EffectIndex("disruptor/flesh_impact");
	G_EffectIndex("env/med_explode");
	G_EffectIndex("env/small_explode");
#endif
}

bool JKCraft_OverrideFirstPersonCamera(float viewOrigin[3], float viewAngles[3])
{
#if defined(_WIN32)
	if (!viewOrigin || !viewAngles || !cameraSync || !cameraSync->integer || HostCinematic())
	{
		return false;
	}
	jkcraft::protocol::MinecraftState state{};
	if (!ReadMinecraftState(state) || !(state.flags & jkcraft::protocol::kMinecraftInWorld) ||
		(state.flags & jkcraft::protocol::kMinecraftDead))
	{
		return false;
	}
	const float scale = unitsPerBlock && unitsPerBlock->value > 0.001f ? unitsPerBlock->value : 32.0f;
	// The camera must recover as soon as gameplay resumes after a skipped cinematic.
	// The teleport acknowledgement can lag behind the resumed HUD/input for a few frames;
	// it still gates native player movement, but must not leave JA's camera on screen.
	ApplyCameraSettings(state, scale);
	if (state.cameraMode != 0u && state.cameraMode <= 2u &&
		std::isfinite(state.cameraX) && std::isfinite(state.cameraY) && std::isfinite(state.cameraZ) &&
		std::isfinite(state.cameraYaw) && std::isfinite(state.cameraPitch) &&
		std::abs(state.cameraX - state.x) < 16.0 &&
		std::abs(state.cameraY - state.y) < 16.0 &&
		std::abs(state.cameraZ - state.z) < 16.0)
	{
		viewOrigin[0] = static_cast<float>(state.cameraX * scale);
		viewOrigin[1] = static_cast<float>(-state.cameraZ * scale);
		viewOrigin[2] = static_cast<float>(state.cameraY * scale);
		viewAngles[PITCH] = state.cameraPitch;
		viewAngles[YAW] = AngleNormalize360(-state.cameraYaw - 90.0f);
		viewAngles[ROLL] = 0.0f;
		return true;
	}
	const bool exactEye = std::isfinite(state.eyeX) && std::isfinite(state.eyeY) && std::isfinite(state.eyeZ) &&
		std::abs(state.eyeX - state.x) < 4.0 && std::abs(state.eyeY - state.y) < 4.0 &&
		std::abs(state.eyeZ - state.z) < 4.0;
	const float eyeHeight = std::isfinite(state.eyeHeight) ?
		std::max(0.1f, std::min(state.eyeHeight, 3.0f)) : 1.62f;
	viewOrigin[0] = static_cast<float>((exactEye ? state.eyeX : state.x) * scale);
	viewOrigin[1] = static_cast<float>(-(exactEye ? state.eyeZ : state.z) * scale);
	viewOrigin[2] = static_cast<float>((exactEye ? state.eyeY : state.y + eyeHeight) * scale);
	// Use the same live Minecraft angles as its movement and interaction ray.  Waiting for the
	// next OpenJK game tick here made the rendered crosshair trail behind the actual target.
	viewAngles[PITCH] = state.pitch;
	viewAngles[YAW] = AngleNormalize360(-state.yaw - 90.0f);
	viewAngles[ROLL] = 0.0f;

	// Minecraft's GameRenderer.bobView transform, converted from view-matrix motion to
	// an equivalent physical camera motion in OpenJK coordinates.
	const float phase = std::isfinite(state.bobPhase) ? state.bobPhase : 0.0f;
	const float amount = std::isfinite(state.bobAmount) ?
		std::max(0.0f, std::min(state.bobAmount, 2.0f)) : 0.0f;
	if (amount > 0.0f)
	{
		const float wave = std::sin(phase * static_cast<float>(M_PI));
		const float lift = std::abs(std::cos(phase * static_cast<float>(M_PI))) * amount;
		vec3_t right;
		AngleVectors(viewAngles, nullptr, right, nullptr);
		VectorMA(viewOrigin, -wave * amount * 0.5f * scale, right, viewOrigin);
		viewOrigin[2] += lift * scale;
		viewAngles[PITCH] -= std::abs(std::cos(phase * static_cast<float>(M_PI) - 0.2f) * amount) * 5.0f;
		viewAngles[ROLL] -= wave * amount * 3.0f;
	}
	if (!loggedExactCamera)
	{
		loggedExactCamera = true;
		gi.Printf("JKCraft: exact Minecraft first-person camera active (eye %.3f, bob %.3f)\n",
			eyeHeight, amount);
	}
	return true;
#else
	(void)viewOrigin; (void)viewAngles;
	return false;
#endif
}

bool JKCraft_ForceFirstPerson()
{
#if defined(_WIN32)
	if (!cameraSync || !cameraSync->integer || HostCinematic())
	{
		return false;
	}
	jkcraft::protocol::MinecraftState state{};
	return ReadMinecraftState(state) &&
		(state.flags & jkcraft::protocol::kMinecraftInWorld) &&
		!(state.flags & jkcraft::protocol::kMinecraftDead);
#else
	return false;
#endif
}

bool JKCraft_HideNativePlayer()
{
#if defined(_WIN32)
	if (!cameraSync || !cameraSync->integer || HostCinematic())
	{
		return false;
	}
	jkcraft::protocol::MinecraftState state{};
	return ReadMinecraftState(state) &&
		(state.flags & jkcraft::protocol::kMinecraftInWorld) &&
		!(state.flags & jkcraft::protocol::kMinecraftDead);
#else
	return false;
#endif
}

bool JKCraft_ShouldReleaseStaleCamera()
{
#if defined(_WIN32)
	static std::uint64_t gameplaySince = 0;
	const bool remoteView = g_entities[0].inuse && g_entities[0].client &&
		g_entities[0].client->ps.viewEntity > 0 &&
		g_entities[0].client->ps.viewEntity < ENTITYNUM_WORLD;
	cvar_t* cinematic = gi.cvar("jkc_cinematic", "0", 0);
	cvar_t* preload = gi.cvar("jkc_preloading", "0", 0);
	if ((!in_camera && !remoteView && (!preload || !preload->integer)) ||
		!cameraSync || !cameraSync->integer ||
		(cinematic && cinematic->integer != 0))
	{
		gameplaySince = 0;
		return false;
	}
	jkcraft::protocol::HostState host{};
	jkcraft::protocol::MinecraftState minecraft{};
	if (!ReadHostState(host) || !ReadMinecraftState(minecraft) ||
		(host.flags & (jkcraft::protocol::kInGame | jkcraft::protocol::kLoading |
			jkcraft::protocol::kCinematic | jkcraft::protocol::kMenuOpen)) != jkcraft::protocol::kInGame ||
		!(minecraft.flags & jkcraft::protocol::kMinecraftInWorld) ||
		(minecraft.flags & jkcraft::protocol::kMinecraftDead))
	{
		gameplaySince = 0;
		return false;
	}
	const std::uint64_t now = GetTickCount64();
	if (!gameplaySince) gameplaySince = now;
	return now >= gameplaySince + 250;
#else
	return false;
#endif
}

bool JKCraft_PlayerPuppeted()
{
#if defined(_WIN32)
	if (!puppet || !puppet->integer || HostCinematic())
	{
		return false;
	}
	jkcraft::protocol::MinecraftState state{};
	return ReadMinecraftState(state) &&
		(state.flags & jkcraft::protocol::kMinecraftInWorld) &&
		!(state.flags & jkcraft::protocol::kMinecraftDead) &&
		TeleportAcknowledged(state.teleportAck);
#else
	return false;
#endif
}

bool JKCraft_CanPickUpItem(int kind)
{
#if defined(_WIN32)
	if (!JKCraft_PlayerPuppeted())
	{
		return false;
	}
	jkcraft::protocol::MinecraftState state{};
	if (!ReadMinecraftState(state))
	{
		return false;
	}
	if (kind == 1)
	{
		return std::isfinite(state.healthFraction) && state.healthFraction < 0.999f;
	}
	if (kind == 2)
	{
		return std::isfinite(state.absorption) && state.absorption < 19.999f;
	}
#else
	(void)kind;
#endif
	return false;
}

bool JKCraft_ForwardPickup(int kind, int quantity)
{
#if defined(_WIN32)
	if ((kind != 1 && kind != 2) || quantity <= 0 || quantity > 1000 ||
		!JKCraft_PlayerPuppeted() || !OpenLink() ||
		header->magic != jkcraft::protocol::kMagic || header->version != jkcraft::protocol::kVersion)
	{
		return false;
	}
	return PushPickup(kind, quantity);
#else
	(void)kind; (void)quantity;
	return false;
#endif
}

void JKCraft_ApplyPlayer(gentity_s* player)
{
#if defined(_WIN32)
	if (!player || !player->inuse || !player->client)
	{
		return;
	}
	if (!puppet)
	{
		puppet = gi.cvar("jkc_puppet", "1", CVAR_ARCHIVE);
		unitsPerBlock = gi.cvar("jkc_units_per_block", "32", CVAR_ARCHIVE);
		cameraSync = gi.cvar("jkc_camera_sync", "1", CVAR_ARCHIVE);
	}
	if (!puppet->integer)
	{
		return;
	}
	if (HostCinematic())
	{
		// Let scripted JA cameras and remote view entities own the native player.
		// Minecraft rejoins its resulting position when ordinary control returns.
		return;
	}

	jkcraft::protocol::MinecraftState state{};
	if (!ReadMinecraftState(state) || !(state.flags & jkcraft::protocol::kMinecraftInWorld) ||
		!TeleportAcknowledged(state.teleportAck))
	{
		ClearMinecraftCells(true);
		return;
	}

	const float scale = unitsPerBlock && unitsPerBlock->value > 0.001f ? unitsPerBlock->value : 32.0f;
	jkcraft::protocol::HostState hostState{};
	if (ReadHostState(hostState)) {
		SyncMinecraftCells(scale, hostState.worldId);
		AffectNpcsByMinecraftFluids(scale);
	}
	DrainGameEvents(player, scale);
	PublishForceState(player);
	if ((state.flags & jkcraft::protocol::kMinecraftDead) || player->health <= 0)
	{
		return;
	}
	PublishActors(player, scale);
	ApplyCameraSettings(state, scale);
	if (std::isfinite(state.eyeHeight))
	{
		const float eyeHeight = std::max(0.1f, std::min(state.eyeHeight, 3.0f));
		player->client->ps.viewheight = static_cast<int>(std::lround(eyeHeight * scale - 24.0f));
	}
	vec3_t origin{
		static_cast<float>(state.x * scale),
		static_cast<float>(-state.z * scale),
		static_cast<float>(state.y * scale + 24.0)};
	G_SetOrigin(player, origin);
	VectorClear(player->client->ps.velocity);
	// BS_FOLLOW_LEADER only steers when its leader has a ground entity. Native
	// pmove does not own this player, so mirror Minecraft's grounded state too.
	const int groundEntity = (state.flags & jkcraft::protocol::kMinecraftOnGround)
		? ENTITYNUM_WORLD : ENTITYNUM_NONE;
	player->client->ps.groundEntityNum = groundEntity;
	player->s.groundEntityNum = groundEntity;

	vec3_t angles{state.pitch, AngleNormalize360(-state.yaw - 90.0f), 0.0f};
	SetClientViewAngle(player, angles);
	const bool exactEye = std::isfinite(state.eyeX) && std::isfinite(state.eyeY) && std::isfinite(state.eyeZ) &&
		std::abs(state.eyeX - state.x) < 4.0 && std::abs(state.eyeY - state.y) < 4.0 &&
		std::abs(state.eyeZ - state.z) < 4.0;
	vec3_t eyePoint{
		static_cast<float>((exactEye ? state.eyeX : state.x) * scale),
		static_cast<float>(-(exactEye ? state.eyeZ : state.z) * scale),
		static_cast<float>((exactEye ? state.eyeY : state.y + 1.62) * scale)};
	VectorCopy(eyePoint, player->client->renderInfo.eyePoint);
	VectorCopy(eyePoint, player->client->renderInfo.headPoint);
	VectorCopy(angles, player->client->renderInfo.eyeAngles);
	VectorCopy(angles, player->client->renderInfo.headAngles);
	gi.linkentity(player);

	// NPC thinking happens after this synchronization. Keep the remembered
	// target point current too, so actors already fighting the player do not
	// continue aiming at the map spawn position.
	for (int i = 1; i < globals.num_entities; ++i)
	{
		gentity_t* actor = &g_entities[i];
		if (actor->inuse && actor->NPC && actor->enemy == player)
		{
			VectorCopy(player->currentOrigin, actor->NPC->enemyLastSeenLocation);
			actor->NPC->enemyLastSeenTime = level.time;
		}
		// Newly visible enemies should react within a few game frames instead
		// of waiting for a long idle/guard scan. Use the original AI visibility
		// test and respect scripts that suspend autonomous enemy searching.
		if (!actor->inuse || !actor->NPC || !actor->client || actor->health <= 0 ||
			actor->enemy || actor->client->enemyTeam != player->client->playerTeam ||
			actor->client->playerTeam == player->client->playerTeam ||
			actor->client->noclip ||
			!(actor->NPC->scriptFlags & SCF_LOOK_FOR_ENEMIES) ||
			(actor->svFlags & (SVF_IGNORE_ENEMIES | SVF_LOCKEDENEMY)) ||
			actor->NPC->confusionTime > level.time ||
			actor->NPC->behaviorState == BS_CINEMATIC ||
			actor->NPC->tempBehavior == BS_CINEMATIC ||
			(level.framenum + i) % 3 != 0)
		{
			continue;
		}
		SaveNPCGlobals();
		SetNPCGlobals(actor);
		const bool visible = NPC_ValidEnemy(player) &&
			NPC_CheckVisibility(player, CHECK_PVS | CHECK_VISRANGE | CHECK_360 | CHECK_FOV) >= VIS_FOV;
		if (visible)
		{
			G_SetEnemy(actor, player);
			if (actor->inuse && actor->NPC && actor->enemy == player)
			{
				VectorCopy(player->currentOrigin, actor->NPC->enemyLastSeenLocation);
				actor->NPC->enemyLastSeenTime = level.time;
				if (!loggedNpcSight)
				{
					loggedNpcSight = true;
					gi.Printf("JKCraft: first visible JA enemy acquired Minecraft player (%d)\n", i);
				}
			}
		}
		RestoreNPCGlobals();
	}
	// Minecraft drives the native player's position directly, so the normal
	// interpolated touch path can miss a func_plat's thin center trigger while
	// the player is standing still. Start only an automatic bottom-position
	// platform that is physically supporting the player's feet.
	// The short downward native trace is authoritative here: Minecraft's
	// on-ground bit can lag by one tick when a mover is underneath the player.
	{
		vec3_t from, to, footMins{-8.0f, -8.0f, 0.0f}, footMaxs{8.0f, 8.0f, 1.0f};
		VectorCopy(player->client->ps.origin, from);
		from[2] += player->mins[2] + 2.0f;
		VectorCopy(from, to);
		to[2] -= 12.0f;
		trace_t support{};
		gi.trace(&support, from, footMins, footMaxs, to, player->s.number,
			MASK_SOLID, (EG2_Collision)0, 0);
		if (!support.startsolid && support.fraction < 1.0f &&
			support.plane.normal[2] >= 0.65f &&
			support.entityNum > 0 && support.entityNum < ENTITYNUM_WORLD)
		{
			gentity_t* platform = &g_entities[support.entityNum];
			if (platform->inuse && platform->classname &&
				!Q_stricmp(platform->classname, "func_plat") &&
				!platform->targetname && platform->moverState == MOVER_POS1)
			{
				Use_BinaryMover(platform, platform, player);
			}
		}
	}
	MaybeSpawnCombatTest(player);
#else
	(void)player;
#endif
}

bool JKCraft_ForwardPlayerDamage(gentity_s* target, gentity_s* attacker,
	int damage, int meansOfDeath, int damageFlags)
{
#if defined(_WIN32)
	(void)damageFlags;
	if (applyingMinecraftEvent || !target || target->s.number != 0 || damage <= 0 ||
		!OpenLink() || header->magic != jkcraft::protocol::kMagic || header->version != jkcraft::protocol::kVersion)
	{
		return false;
	}
	const std::uint64_t now = GetTickCount64();
	const std::uint64_t heartbeat = Read64(&header->minecraftHeartbeatMs);
	if (!heartbeat || now < heartbeat || now - heartbeat >= 3000)
	{
		return false;
	}
	jkcraft::protocol::MinecraftState state{};
	if (!ReadMinecraftState(state) || !(state.flags & jkcraft::protocol::kMinecraftInWorld) ||
		(state.flags & jkcraft::protocol::kMinecraftDead) ||
		!TeleportAcknowledged(state.teleportAck))
	{
		return false;
	}
	int kind = 3;
	if (meansOfDeath == MOD_MELEE || meansOfDeath == MOD_SABER)
	{
		kind = 0;
	}
	else if (meansOfDeath == MOD_FORCE_GRIP || meansOfDeath == MOD_FORCE_LIGHTNING || meansOfDeath == MOD_FORCE_DRAIN)
	{
		kind = 2;
	}
	else if (meansOfDeath >= MOD_BRYAR && meansOfDeath <= MOD_LASERTRIP_ALT)
	{
		kind = 1;
	}
	const int attackerId = attacker && attacker->inuse && attacker->client && attacker->NPC ? attacker->s.number : 0;
	const bool forwarded = PushHurt(kind, damage, attackerId, 0);
	if (forwarded && !loggedPlayerHurt)
	{
		loggedPlayerHurt = true;
		gi.Printf("JKCraft: first JA hit forwarded to Minecraft (%d damage, kind %d, attacker %d)\n",
			damage, kind, attackerId);
	}
	return forwarded;
#else
	(void)target; (void)attacker; (void)damage; (void)meansOfDeath; (void)damageFlags;
	return false;
#endif
}
