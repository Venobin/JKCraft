#pragma once

struct gentity_s;

// Registers the effects emitted by JKCraft while the level is still loading.
void JKCraft_Precache();

// Replaces OpenJK's eye with Minecraft's live first- or third-person camera.
// Returns true when the view was overridden.
bool JKCraft_OverrideFirstPersonCamera(float viewOrigin[3], float viewAngles[3]);

// True while the linked Minecraft player requests first-person camera mode.
bool JKCraft_ForceFirstPerson();
// Hide the native Jedi model whenever the linked Minecraft avatar owns the view.
bool JKCraft_HideNativePlayer();

// Detect a scripted camera left active after OpenJK has already handed input
// and the view back to Minecraft (usually after skipping a level cutscene).
bool JKCraft_ShouldReleaseStaleCamera();

// True while Minecraft owns the live player. Native saber animations can remain frozen in this
// mode and must not indefinitely suppress Jedi Academy's Force regeneration.
bool JKCraft_PlayerPuppeted();

// Applies the Minecraft-owned player transform to Jedi Academy's real player.
// Safe no-op while JKCraft is disabled or Minecraft is disconnected.
void JKCraft_ApplyPlayer(struct gentity_s* player);

// Returns true when damage to the JA player was handed to Minecraft and must not
// also be applied to the puppet's duplicate JA health pool.
bool JKCraft_ForwardPlayerDamage(struct gentity_s* target, struct gentity_s* attacker,
	int damage, int meansOfDeath, int damageFlags);

// JA health/shield pickups are applied to Minecraft's real health pool.
bool JKCraft_CanPickUpItem(int kind);
bool JKCraft_ForwardPickup(int kind, int quantity);
