package dev.skycraft.client.mixin;

import dev.skycraft.client.SkyClient;
import dev.skycraft.client.SkyCollider;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.MoverType;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * After vanilla has collided the local player's movement with Minecraft blocks, collide it with
 * Skyrim's exact triangles (smooth slopes instead of voxel stair-steps).
 */
@Mixin(Entity.class)
public abstract class EntityCollideMixin {
	@Unique private Vec3 skycraft$collisionRequest;
	@Unique private Vec3 skycraft$collisionResult;
	@Unique private Vec3 skycraft$platformCarry;
	@Unique private Vec3 skycraft$preMoveVelocity;
	@Unique private boolean skycraft$fixGroundState;

	@Inject(method = "move", at = @At("HEAD"))
	private void skycraft$beginMove(MoverType type, Vec3 movement, CallbackInfo ci) {
		skycraft$fixGroundState = (Object) this instanceof LocalPlayer player
			&& SkyClient.linked() && !player.noPhysics;
		skycraft$collisionRequest = null;
		skycraft$collisionResult = null;
		skycraft$platformCarry = Vec3.ZERO;
		skycraft$preMoveVelocity = (Object) this instanceof LocalPlayer player ? player.getDeltaMovement() : Vec3.ZERO;
	}

	@Inject(method = "collide", at = @At("RETURN"), cancellable = true)
	private void skycraft$smoothSkyrimCollision(Vec3 movement, CallbackInfoReturnable<Vec3> cir) {
		if ((Object) this instanceof LocalPlayer player && SkyClient.linked() && !player.noPhysics) {
			Vec3 result = SkyCollider.collide(player, cir.getReturnValue());
			cir.setReturnValue(result);
			if (skycraft$fixGroundState) {
				skycraft$collisionRequest = movement;
				skycraft$collisionResult = result;
				skycraft$platformCarry = SkyCollider.lastCarry();
			}
		}
	}

	/**
	 * Entity.move normally derives onGround by comparing its requested and resolved Y movement.
	 * A second collision provider can leave LocalPlayer's previous ground bit alive, however, and
	 * that turns falling into a series of first gravity ticks and permits jumping in mid-air.
	 * Make the result authoritative after both vanilla blocks and the OpenJK BSP have resolved it.
	 */
	@Inject(method = "move", at = @At("RETURN"))
	private void skycraft$finishMove(MoverType type, Vec3 movement, CallbackInfo ci) {
		if (!skycraft$fixGroundState || skycraft$collisionRequest == null || skycraft$collisionResult == null
			|| !((Object) this instanceof LocalPlayer player)) {
			return;
		}
		Vec3 requested = skycraft$collisionRequest;
		Vec3 resolved = skycraft$collisionResult;
		Vec3 ownMovement = resolved.subtract(skycraft$platformCarry);
		boolean verticalCollision = !net.minecraft.util.Mth.equal(requested.y, ownMovement.y);
		boolean groundContact = verticalCollision && requested.y < 0.0;
		boolean horizontalCollision = !net.minecraft.util.Mth.equal(requested.x, ownMovement.x)
			|| !net.minecraft.util.Mth.equal(requested.z, ownMovement.z);
		player.verticalCollision = verticalCollision;
		player.verticalCollisionBelow = groundContact;
		if (!horizontalCollision && player.horizontalCollision) {
			Vec3 velocity = player.getDeltaMovement();
			player.setDeltaMovement(skycraft$preMoveVelocity.x, velocity.y, skycraft$preMoveVelocity.z);
		}
		player.horizontalCollision = horizontalCollision;
		player.setOnGroundWithMovement(groundContact, horizontalCollision, resolved);
		skycraft$fixGroundState = false;
	}
}
