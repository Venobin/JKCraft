package dev.skycraft.combat;

import java.util.ArrayList;
import java.util.Iterator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Cross-thread collection of arrows embedded in Jedi Academy actors. The integrated server records
 * the hit relative to the actor proxy; the render thread then exports that arrow with the actor's
 * latest position. Entries are intentionally transient, like Minecraft's own embedded arrows.
 */
public final class StuckArrowStore {
	private static final int MAX_TOTAL = 32;
	private static final int MAX_PER_ACTOR = 8;
	private static final long LIFETIME_NANOS = 60_000_000_000L;
	private static final long DEAD_LIFETIME_NANOS = 10_000_000_000L;
	private static final List<StuckArrow> ARROWS = new ArrayList<>();
	private static final Map<Integer, FrozenActor> DEAD_ACTORS = new HashMap<>();
	private static int nextId = 1;

	private StuckArrowStore() {
	}

	public record StuckArrow(int id, int formId, float offsetX, float offsetY, float offsetZ,
		float yaw, float pitch, int texture, long expiresAt) {
	}

	public record PlacedArrow(int id, float x, float y, float z, float yaw, float pitch, int texture) {
	}

	private record FrozenActor(float x, float y, float z, long expiresAt) {
	}

	public static synchronized void add(int formId, float offsetX, float offsetY, float offsetZ,
		float yaw, float pitch, int texture) {
		long now = System.nanoTime();
		prune(now);
		int forActor = 0;
		for (StuckArrow arrow : ARROWS) {
			if (arrow.formId == formId) {
				forActor++;
			}
		}
		if (forActor >= MAX_PER_ACTOR) {
			for (Iterator<StuckArrow> it = ARROWS.iterator(); it.hasNext();) {
				if (it.next().formId == formId) {
					it.remove();
					break;
				}
			}
		}
		while (ARROWS.size() >= MAX_TOTAL) {
			ARROWS.remove(0);
		}
		ARROWS.add(new StuckArrow(nextId++, formId, offsetX, offsetY, offsetZ,
			yaw, pitch, texture, now + LIFETIME_NANOS));
	}

	public static synchronized List<StuckArrow> forActor(int formId) {
		prune(System.nanoTime());
		List<StuckArrow> result = new ArrayList<>();
		if (DEAD_ACTORS.containsKey(formId)) {
			return result;
		}
		for (StuckArrow arrow : ARROWS) {
			if (arrow.formId == formId) {
				result.add(arrow);
			}
		}
		return result;
	}

	/** Freeze this actor's embedded arrows over its Jedi Academy death animation. */
	public static synchronized void markActorDead(int formId, float x, float y, float z) {
		long now = System.nanoTime();
		prune(now);
		DEAD_ACTORS.put(formId, new FrozenActor(x, y, z, now + DEAD_LIFETIME_NANOS));
	}

	/** A reused JA entity slot is a new living actor, not the old corpse. */
	public static synchronized void markActorAlive(int formId) {
		if (DEAD_ACTORS.remove(formId) != null) {
			ARROWS.removeIf(arrow -> arrow.formId == formId);
		}
	}

	public static synchronized List<PlacedArrow> frozenArrows() {
		prune(System.nanoTime());
		List<PlacedArrow> result = new ArrayList<>();
		for (StuckArrow arrow : ARROWS) {
			FrozenActor actor = DEAD_ACTORS.get(arrow.formId);
			if (actor != null) {
				result.add(new PlacedArrow(arrow.id, actor.x + arrow.offsetX, actor.y + arrow.offsetY,
					actor.z + arrow.offsetZ, arrow.yaw, arrow.pitch, arrow.texture));
			}
		}
		return result;
	}

	public static synchronized void clear() {
		ARROWS.clear();
		DEAD_ACTORS.clear();
	}

	private static void prune(long now) {
		ARROWS.removeIf(arrow -> arrow.expiresAt <= now);
		for (Iterator<Map.Entry<Integer, FrozenActor>> it = DEAD_ACTORS.entrySet().iterator(); it.hasNext();) {
			Map.Entry<Integer, FrozenActor> entry = it.next();
			if (entry.getValue().expiresAt <= now) {
				int formId = entry.getKey();
				it.remove();
				ARROWS.removeIf(arrow -> arrow.formId == formId);
			}
		}
	}
}
