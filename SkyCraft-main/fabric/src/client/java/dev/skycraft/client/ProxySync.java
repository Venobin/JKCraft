package dev.skycraft.client;

import dev.skycraft.combat.SkyrimActorEntity;
import dev.skycraft.combat.StuckArrowStore;
import dev.skycraft.SkyCraft;
import dev.skycraft.link.SkyLink;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.HashSet;
import java.util.Set;
import net.minecraft.client.Minecraft;
import net.minecraft.core.particles.ParticleTypes;
import net.minecraft.world.entity.Entity;

/**
 * Puts the client's copies of the Skyrim actor stand-ins exactly where Skyrim has the actors this
 * frame, so the crosshair and melee reach line up with what's on screen (the server copy only
 * moves once per tick and reaches the client a tick or two later).
 */
final class ProxySync {
	private static final List<SkyLink.Actor> ACTORS = new ArrayList<>();
	private static final Map<Integer, SkyLink.Actor> BY_ID = new HashMap<>();
	private static final Set<Integer> DEAD = new HashSet<>();

	private ProxySync() {
	}

	static void frame(Minecraft minecraft) {
		if (minecraft.level == null || !SkyLink.readActors(ACTORS)) {
			return;
		}
		BY_ID.clear();
		for (SkyLink.Actor a : ACTORS) {
			BY_ID.put(a.formId(), a);
			if (a.dead()) {
				if (DEAD.add(a.formId())) {
					StuckArrowStore.markActorDead(a.formId(), a.x(), a.y(), a.z());
					deathBurst(minecraft, a);
					SkyCraft.LOG.info("JKCraft: {} died in Jedi Academy; proxy hitbox removed", a.name());
				}
			} else if (DEAD.remove(a.formId())) {
				StuckArrowStore.markActorAlive(a.formId());
			}
		}
		for (Entity entity : minecraft.level.entitiesForRendering()) {
			if (entity instanceof SkyrimActorEntity proxy) {
				SkyLink.Actor a = BY_ID.get(proxy.formId());
				if (a == null) {
					continue;
				}
				proxy.setSize(a.width(), a.height());
				proxy.setPos(a.x(), a.y(), a.z());
				proxy.xo = a.x();
				proxy.yo = a.y();
				proxy.zo = a.z();
				proxy.setYRot(a.yaw());
				proxy.yRotO = a.yaw();
			}
		}
	}

	private static void deathBurst(Minecraft minecraft, SkyLink.Actor actor) {
		for (int i = 0; i < 12; i++) {
			double angle = i * Math.PI * 2.0 / 12.0;
			double y = actor.y() + actor.height() * (0.2 + 0.6 * ((i % 4) / 3.0));
			minecraft.level.addParticle(ParticleTypes.POOF,
				actor.x() + Math.cos(angle) * actor.width() * 0.35, y,
				actor.z() + Math.sin(angle) * actor.width() * 0.35,
				Math.cos(angle) * 0.035, 0.025, Math.sin(angle) * 0.035);
		}
	}
}
