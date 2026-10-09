package dev.skycraft.combat;

import static org.junit.jupiter.api.Assertions.assertEquals;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

class StuckArrowStoreTest {
	@AfterEach
	void clear() {
		StuckArrowStore.clear();
	}

	@Test
	void keepsOnlyEightNewestArrowsPerActor() {
		for (int i = 0; i < 9; i++) {
			StuckArrowStore.add(364, i, 0, 0, 90, 0, 0);
		}
		var arrows = StuckArrowStore.forActor(364);
		assertEquals(8, arrows.size());
		assertEquals(1.0F, arrows.get(0).offsetX());
		assertEquals(8.0F, arrows.get(7).offsetX());
	}

	@Test
	void capsTheGlobalCollection() {
		for (int actor = 0; actor < 40; actor++) {
			StuckArrowStore.add(actor, 0, 0, 0, 0, 0, actor % 3);
		}
		int total = 0;
		for (int actor = 0; actor < 40; actor++) {
			total += StuckArrowStore.forActor(actor).size();
		}
		assertEquals(32, total);
	}

	@Test
	void freezesArrowsAtTheActorsDeathPosition() {
		StuckArrowStore.add(146, 0.5F, 1.0F, -0.25F, 90, 5, 2);
		StuckArrowStore.markActorDead(146, 10, 20, 30);
		assertEquals(0, StuckArrowStore.forActor(146).size());
		var arrow = StuckArrowStore.frozenArrows().getFirst();
		assertEquals(10.5F, arrow.x());
		assertEquals(21.0F, arrow.y());
		assertEquals(29.75F, arrow.z());
		assertEquals(2, arrow.texture());
	}
}
