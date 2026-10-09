package dev.skycraft.combat;

import static org.junit.jupiter.api.Assertions.assertEquals;

import org.junit.jupiter.api.Test;

class CombatBalanceTest {
	@Test
	void convertsMinecraftDamageToJediScale() {
		assertEquals(20.0F, CombatBalance.minecraftToJedi(5.0F));
		assertEquals(42.0F, CombatBalance.minecraftToJedi(10.5F));
	}

	@Test
	void convertsJediDamageBeforeMinecraftArmor() {
		assertEquals(3.0F, CombatBalance.jediToMinecraft(6.0F));
		assertEquals(7.5F, CombatBalance.jediToMinecraft(15.0F));
	}

	@Test
	void capsCombinedMinecraftDamagePerTick() {
		assertEquals(100.0F, CombatBalance.combineMinecraftDamage(75.0F, 50.0F));
		assertEquals(400.0F, CombatBalance.minecraftToJedi(Float.MAX_VALUE));
	}

	@Test
	void rejectsNonFiniteAndNegativeDamage() {
		assertEquals(0.0F, CombatBalance.minecraftToJedi(Float.NaN));
		assertEquals(0.0F, CombatBalance.minecraftToJedi(-5.0F));
		assertEquals(0.0F, CombatBalance.jediToMinecraft(Float.POSITIVE_INFINITY));
		assertEquals(0.0F, CombatBalance.jediToMinecraft(-1.0F));
	}

	@Test
	void capsJediBossHitsWithoutOverflow() {
		assertEquals(100.0F, CombatBalance.jediToMinecraft(1000.0F));
	}
}
