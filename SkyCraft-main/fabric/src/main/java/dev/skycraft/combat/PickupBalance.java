package dev.skycraft.combat;

/** Converts Jedi Academy health and shield pickups into Minecraft hit points. */
public final class PickupBalance {
	public static final float MAX_ABSORPTION = 20.0F;

	private PickupBalance() {
	}

	public static float amount(int jediPoints) {
		return Math.max(0, Math.min(jediPoints, 1000)) / 5.0F;
	}

	public static float shieldAfter(float currentAbsorption, int jediPoints) {
		return Math.min(MAX_ABSORPTION, Math.max(0.0F, currentAbsorption) + amount(jediPoints));
	}
}
