package dev.skycraft.combat;

import static org.junit.jupiter.api.Assertions.assertEquals;

import org.junit.jupiter.api.Test;

class PickupBalanceTest {
	@Test void convertsJediPickupToMinecraftHearts() {
		assertEquals(5.0F, PickupBalance.amount(25));
		assertEquals(20.0F, PickupBalance.amount(100));
	}

	@Test void capsGoldenHearts() {
		assertEquals(20.0F, PickupBalance.shieldAfter(18.0F, 25));
		assertEquals(5.0F, PickupBalance.shieldAfter(0.0F, 25));
	}

	@Test void rejectsNegativeAndHugeAmounts() {
		assertEquals(0.0F, PickupBalance.amount(-5));
		assertEquals(200.0F, PickupBalance.amount(2000));
	}
}
