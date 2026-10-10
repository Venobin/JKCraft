package dev.skycraft.world;

import java.util.ArrayList;
import java.util.List;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.Mob;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.Vec3;

/** Exact Jedi Academy triangle collision for ordinary Minecraft mobs. */
public final class MobCollider {
	private MobCollider() {
	}

	public static Vec3 collide(Mob mob, Vec3 movement) {
		AABB box = mob.getBoundingBox();
		double step = mob.maxUpStep();
		List<SkyTri> tris = new ArrayList<>();
		SkyCollision.trianglesNear(box.expandTowards(movement).inflate(1.0, 1.0 + step, 1.0), tris);
		if (tris.isEmpty()) {
			return movement;
		}
		double[] result = TriCollider.resolve(
			tris,
			(box.minX + box.maxX) * 0.5, box.minY, (box.minZ + box.maxZ) * 0.5,
			Math.max(0.1, Math.min(box.getXsize(), box.getZsize()) * 0.5), box.getYsize(),
			step, mob.onGround(), movement.x, movement.y, movement.z
		);
		Vec3 exact = new Vec3(result[0], result[1], result[2]);
		if (exact.equals(movement)) {
			return movement;
		}
		// Triangle correction can slide a mob into a player-built block. Let vanilla blocks have
		// the final word; JA voxels are omitted for mobs by BlockCollisionsMixin.
		return Entity.collideBoundingBox(mob, exact, box, mob.level(), List.of());
	}
}
