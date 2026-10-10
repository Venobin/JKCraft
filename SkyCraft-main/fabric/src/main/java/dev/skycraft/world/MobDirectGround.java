package dev.skycraft.world;

import static dev.skycraft.link.Proto.*;
import static java.lang.foreign.ValueLayout.*;

import dev.skycraft.SkyCraft;
import dev.skycraft.link.SkyLink;
import java.lang.foreign.MemorySegment;
import java.lang.invoke.VarHandle;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.Mob;
import net.minecraft.world.entity.monster.Enemy;

/**
 * Batched, non-blocking ground probes against OpenJK's live BSP for Minecraft mobs.
 *
 * <p>The direct-physics mode deliberately does not copy the JA map into Minecraft, so ordinary
 * voxel collision cannot hold mobs up. One shared-memory request contains up to 32 native traces;
 * OpenJK answers the whole batch in one frame and the server applies it on the following tick.</p>
 */
public final class MobDirectGround {
	private static final VarHandle INT = JAVA_INT.varHandle();
	private static int appliedResponse;
	private static boolean logged;

	private MobDirectGround() {
	}

	public static void init() {
		ServerTickEvents.END_SERVER_TICK.register(MobDirectGround::tick);
	}

	private static void tick(MinecraftServer server) {
		MemorySegment memory = SkyLink.segment();
		if (memory == null || !SkyLink.active()) {
			appliedResponse = 0;
			return;
		}
		long base = OFF_MOB_GROUND;
		int response = (int) INT.getAcquire(memory, base + MG_RESPONSE);
		if (response != 0 && response != appliedResponse) {
			apply(server, memory, base);
			appliedResponse = response;
		}
		int request = (int) INT.getAcquire(memory, base + MG_REQUEST);
		if (request != response) {
			return; // OpenJK is still answering the preceding frame; never block the server
		}

		List<Candidate> candidates = collect(server);
		int count = Math.min(candidates.size(), MAX_MOB_GROUND);
		for (int i = 0; i < count; i++) {
			Mob mob = candidates.get(i).mob;
			long record = base + MG_RECORDS + i * MOB_GROUND_BYTES;
			memory.set(JAVA_INT, record, mob.getId());
			memory.set(JAVA_FLOAT, record + 4, (float) mob.getX());
			memory.set(JAVA_FLOAT, record + 8, (float) mob.getY());
			memory.set(JAVA_FLOAT, record + 12, (float) mob.getZ());
			memory.set(JAVA_FLOAT, record + 16, (float) Math.max(0.1, Math.min(mob.getBbWidth() * 0.5, 4.0)));
			memory.set(JAVA_FLOAT, record + 20, (float) Math.max(0.1, Math.min(mob.getBbHeight(), 8.0)));
			memory.set(JAVA_FLOAT, record + 24, 3.0F);
			// Begin above the current feet so a mob that crossed a paper-thin BSP surface during
			// the preceding tick can still trace back onto it instead of reporting start-solid.
			memory.set(JAVA_FLOAT, record + 28, 0.75F);
			memory.set(JAVA_FLOAT, record + 32, Float.NaN);
			memory.set(JAVA_INT, record + 36, mob instanceof Enemy ? MOB_REQUEST_HOSTILE : 0);
		}
		SkyLink.SkyState state = new SkyLink.SkyState();
		if (!SkyLink.readSkyState(state) || !state.directCollision() || !state.inGame() || state.loading() || state.cinematic()) {
			return;
		}
		memory.set(JAVA_INT, base + MG_WORLD, state.worldId);
		memory.set(JAVA_INT, base + MG_COUNT, count);
		VarHandle.storeStoreFence();
		int next = request + 1;
		INT.setRelease(memory, base + MG_REQUEST, next == 0 ? 1 : next);
	}

	private static List<Candidate> collect(MinecraftServer server) {
		List<ServerPlayer> players = server.getPlayerList().getPlayers();
		List<Candidate> result = new ArrayList<>();
		if (players.isEmpty()) return result;
		for (ServerLevel level : server.getAllLevels()) {
			for (Entity entity : level.getAllEntities()) {
				if (!(entity instanceof Mob mob) || !mob.isAlive() || mob.isNoGravity() || mob.isPassenger()) continue;
				double nearest = Double.POSITIVE_INFINITY;
				for (ServerPlayer player : players) {
					if (player.level() == level) nearest = Math.min(nearest, mob.distanceToSqr(player));
				}
				if (nearest <= 96.0 * 96.0) result.add(new Candidate(mob, nearest));
			}
		}
		result.sort(Comparator.comparingDouble(Candidate::distance));
		return result;
	}

	private static void apply(MinecraftServer server, MemorySegment memory, long base) {
		int count = Math.min(memory.get(JAVA_INT, base + MG_COUNT), MAX_MOB_GROUND);
		for (int i = 0; i < count; i++) {
			long record = base + MG_RECORDS + i * MOB_GROUND_BYTES;
			int flags = memory.get(JAVA_INT, record + 36);
			if ((flags & MOB_GROUND_HIT) == 0) continue;
			Entity entity = find(server, memory.get(JAVA_INT, record));
			if (!(entity instanceof Mob mob) || !mob.isAlive() || mob.isNoGravity()) continue;
			double queryX = memory.get(JAVA_FLOAT, record + 4);
			double queryZ = memory.get(JAVA_FLOAT, record + 12);
			if (mob.distanceToSqr(queryX, mob.getY(), queryZ) > 2.25) continue;
			double ground = memory.get(JAVA_FLOAT, record + 32);
			if (!Double.isFinite(ground)) continue;
			double difference = ground - mob.getY();
			// Snap only while descending and close to the probed floor. A mob genuinely jumping or
			// falling from a ledge remains airborne; a mob that crossed a thin BSP is lifted back.
			if (mob.getDeltaMovement().y <= 0.0 && difference >= -0.35 && difference <= 1.5) {
				mob.setPos(mob.getX(), ground, mob.getZ());
				mob.setDeltaMovement(mob.getDeltaMovement().multiply(1.0, 0.0, 1.0));
				mob.setOnGround(true);
				mob.resetFallDistance();
				if (!logged) {
					logged = true;
					SkyCraft.LOG.info("JKCraft: OpenJK direct ground support active for Minecraft mobs");
				}
			}
		}
	}

	private static Entity find(MinecraftServer server, int id) {
		for (ServerLevel level : server.getAllLevels()) {
			Entity entity = level.getEntity(id);
			if (entity != null) return entity;
		}
		return null;
	}

	private record Candidate(Mob mob, double distance) {
	}
}
