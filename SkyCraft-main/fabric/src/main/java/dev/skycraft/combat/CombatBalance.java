package dev.skycraft.combat;

import dev.skycraft.link.Proto;

/** Shared, deterministic damage policy at the Jedi Academy/Minecraft boundary. */
public final class CombatBalance {
	private CombatBalance() {
	}

	/** Combines all Minecraft damage received by one actor in a tick and caps malformed bursts. */
	public static float combineMinecraftDamage(float accumulated, float next) {
		if (!Float.isFinite(accumulated) || accumulated < 0.0F) {
			accumulated = 0.0F;
		}
		if (!Float.isFinite(next) || next <= 0.0F) {
			return Math.min(accumulated, Proto.MAX_MC_DAMAGE_PER_TICK);
		}
		return Math.min(accumulated + next, Proto.MAX_MC_DAMAGE_PER_TICK);
	}

	/** Expected JA damage for UI, tests and diagnostics; OpenJK performs the authoritative conversion. */
	public static float minecraftToJedi(float minecraftDamage) {
		return combineMinecraftDamage(0.0F, minecraftDamage) * Proto.MC_TO_JEDI_DAMAGE;
	}

	/** Converts one JA hit before Minecraft applies shields, armor and enchantments. */
	public static float jediToMinecraft(float jediDamage) {
		if (!Float.isFinite(jediDamage) || jediDamage <= 0.0F) {
			return 0.0F;
		}
		return Math.min(jediDamage, Proto.MAX_JEDI_DAMAGE_PER_HIT) / Proto.JEDI_TO_MC_DAMAGE_DIVISOR;
	}
}
