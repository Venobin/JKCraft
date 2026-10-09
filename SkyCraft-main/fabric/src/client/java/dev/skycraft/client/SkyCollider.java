package dev.skycraft.client;

import dev.skycraft.link.DirectPhysics;
import dev.skycraft.world.SkyCollision;
import dev.skycraft.world.SkyTri;
import dev.skycraft.world.TriCollider;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.Vec3;

/** Feeds the local player's movement through {@link TriCollider} against nearby Skyrim triangles. */
public final class SkyCollider {
	private static Vec3 lastCarry = Vec3.ZERO;

	private SkyCollider() {
	}

	public static Vec3 lastCarry() {
		return lastCarry;
	}

	public static Vec3 collide(LocalPlayer player, Vec3 move) {
		lastCarry = Vec3.ZERO;
		AABB box = player.getBoundingBox();
		double step = player.maxUpStep();
		if (DirectPhysics.active()) {
			// Keep a half-JA-unit skin around the hull. Without it, a foot exactly on a
			// BSP plane can be reported start-solid and cancel the first upward jump tick.
			final double skin = 0.015625;
			float halfWidth = (float) Math.max(0.05, Math.max(box.getXsize(), box.getZsize()) * 0.5 - skin);
			float height = (float) Math.max(0.1, Math.min(box.getYsize() - skin * 2.0, 1.72));
			DirectPhysics.Move direct = DirectPhysics.move(
				new Vec3((box.minX + box.maxX) * 0.5, box.minY + skin, (box.minZ + box.maxZ) * 0.5),
				move, halfWidth, height, (float) step, player.onGround()
			);
			if (direct == null) {
				return move;
			}
			lastCarry = direct.carry();
			// OpenJK owns the JA map, while Minecraft still owns blocks placed by the player.
			return Entity.collideBoundingBox(player, direct.delta(), box, player.level(), List.of());
		}
		List<SkyTri> tris = new ArrayList<>();
		SkyCollision.trianglesNear(box.expandTowards(move).inflate(1.0, 1.0 + step, 1.0), tris);
		if (tris.isEmpty()) {
			return move;
		}
		double[] r = TriCollider.resolve(
			tris, (box.minX + box.maxX) * 0.5, box.minY, (box.minZ + box.maxZ) * 0.5, box.getXsize() * 0.5, box.getYsize(), step, player.onGround(),
			move.x, move.y, move.z
		);
		if (r[0] == move.x && r[1] == move.y && r[2] == move.z) {
			return move;
		}
		// The triangle pass (snapping down a slope, pushing out of a wall) can move the player into a
		// Minecraft block placed on the terrain; collide that result with Minecraft blocks again.
		return Entity.collideBoundingBox(player, new Vec3(r[0], r[1], r[2]), box, player.level(), List.of());
	}

	/** Highest Skyrim surface at or below {@code maxAbove} over the feet at (x, y, z), or NaN. */
	public static double groundAt(double x, double y, double z, double maxAbove) {
		List<SkyTri> tris = new ArrayList<>();
		SkyCollision.trianglesNear(new AABB(x - 1, y - 4, z - 1, x + 1, y + maxAbove + 1, z + 1), tris);
		return TriCollider.groundAt(tris, x, y, z, maxAbove);
	}
}
