package dev.skycraft.world;

import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;
import java.util.UUID;
import net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.damagesource.DamageTypes;

/** Prevents fall damage only for the airborne/landing cycle started by a held Force Jump. */
public final class ForceJumpProtection {
	private static final int NEVER_LEFT_GROUND_TIMEOUT = 10;
	private static final int MAX_PROTECTION_TICKS = 400;
	private static final Map<UUID, State> ACTIVE = new HashMap<>();

	private ForceJumpProtection() {
	}

	public static void init() {
		ServerLivingEntityEvents.ALLOW_DAMAGE.register((entity, source, amount) ->
			!(entity instanceof ServerPlayer player
				&& source.is(DamageTypes.FALL)
				&& ACTIVE.containsKey(player.getUUID()))
		);
		ServerTickEvents.END_SERVER_TICK.register(ForceJumpProtection::tick);
	}

	public static void start(ServerPlayer player) {
		ACTIVE.put(player.getUUID(), new State());
		player.resetFallDistance();
	}

	private static void tick(MinecraftServer server) {
		for (Iterator<Map.Entry<UUID, State>> it = ACTIVE.entrySet().iterator(); it.hasNext();) {
			Map.Entry<UUID, State> entry = it.next();
			ServerPlayer player = server.getPlayerList().getPlayer(entry.getKey());
			if (player == null) {
				it.remove();
				continue;
			}
			State state = entry.getValue();
			state.ticks++;
			player.resetFallDistance();
			if (!player.onGround()) {
				state.airborne = true;
			} else if ((state.airborne && state.ticks > 1)
				|| (!state.airborne && state.ticks > NEVER_LEFT_GROUND_TIMEOUT)
				|| state.ticks > MAX_PROTECTION_TICKS) {
				it.remove();
			}
		}
	}

	private static final class State {
		private int ticks;
		private boolean airborne;
	}
}
