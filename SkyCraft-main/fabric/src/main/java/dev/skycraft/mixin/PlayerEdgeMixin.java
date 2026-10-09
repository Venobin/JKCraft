package dev.skycraft.mixin;

import dev.skycraft.link.SkyLink;
import net.minecraft.world.entity.MoverType;
import net.minecraft.world.entity.player.Player;
import net.minecraft.core.BlockPos;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * Crouching doesn't stop at edges. Minecraft looks for block collision under the player to decide
 * where an edge is; the Skyrim ground the player walks on isn't blocks (the player collides with
 * its exact triangles), so every direction looked like a drop and crouching froze the player.
 */
@Mixin(Player.class)
public abstract class PlayerEdgeMixin {
	@Inject(method = "maybeBackOffFromEdge", at = @At("HEAD"), cancellable = true)
	private void skycraft$crouchWalkAnywhere(Vec3 delta, MoverType moverType, CallbackInfoReturnable<Vec3> cir) {
		if (SkyLink.active()) {
			Player player = (Player) (Object) this;
			AABB feet = player.getBoundingBox();
			int y = (int) Math.floor(feet.minY - 0.05);
			// Let vanilla edge-sneaking work when the player is standing on a real
			// Minecraft block. Only bypass it on bare Jedi Academy geometry, which
			// has no block support and otherwise freezes crouch movement entirely.
			for (int x = (int) Math.floor(feet.minX + 1.0e-4); x <= (int) Math.floor(feet.maxX - 1.0e-4); x++) {
				for (int z = (int) Math.floor(feet.minZ + 1.0e-4); z <= (int) Math.floor(feet.maxZ - 1.0e-4); z++) {
					BlockPos pos = new BlockPos(x, y, z);
					if (!player.level().getBlockState(pos).getCollisionShape(player.level(), pos).isEmpty()) {
						return;
					}
				}
			}
			cir.setReturnValue(delta);
		}
	}
}
