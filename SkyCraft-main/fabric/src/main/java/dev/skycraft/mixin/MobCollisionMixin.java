package dev.skycraft.mixin;

import dev.skycraft.world.MobCollider;
import dev.skycraft.world.SkyCollision;
import net.minecraft.util.Mth;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.Mob;
import net.minecraft.world.entity.MoverType;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** Gives Minecraft mobs the same smooth JA map geometry used by the player fallback collider. */
@Mixin(Entity.class)
public abstract class MobCollisionMixin {
	@Unique private Vec3 skycraft$mobRequested;
	@Unique private Vec3 skycraft$mobResolved;
	@Unique private boolean skycraft$mobMove;

	@Inject(method = "move", at = @At("HEAD"))
	private void skycraft$beginMobMove(MoverType type, Vec3 movement, CallbackInfo ci) {
		skycraft$mobMove = (Object) this instanceof Mob mob && !mob.noPhysics && SkyCollision.active();
		skycraft$mobRequested = null;
		skycraft$mobResolved = null;
	}

	@Inject(method = "collide", at = @At("RETURN"), cancellable = true)
	private void skycraft$collideMobWithJa(Vec3 movement, CallbackInfoReturnable<Vec3> cir) {
		if (!skycraft$mobMove || !((Object) this instanceof Mob mob)) {
			return;
		}
		Vec3 result = MobCollider.collide(mob, cir.getReturnValue());
		cir.setReturnValue(result);
		skycraft$mobRequested = movement;
		skycraft$mobResolved = result;
	}

	@Inject(method = "move", at = @At("RETURN"))
	private void skycraft$finishMobMove(MoverType type, Vec3 movement, CallbackInfo ci) {
		if (!skycraft$mobMove || skycraft$mobRequested == null || skycraft$mobResolved == null
			|| !((Object) this instanceof Mob mob)) {
			return;
		}
		boolean vertical = !Mth.equal(skycraft$mobRequested.y, skycraft$mobResolved.y);
		boolean grounded = vertical && skycraft$mobRequested.y < 0.0;
		boolean horizontal = !Mth.equal(skycraft$mobRequested.x, skycraft$mobResolved.x)
			|| !Mth.equal(skycraft$mobRequested.z, skycraft$mobResolved.z);
		mob.verticalCollision = vertical;
		mob.verticalCollisionBelow = grounded;
		mob.horizontalCollision = horizontal;
		mob.setOnGroundWithMovement(grounded, horizontal, skycraft$mobResolved);
		if (grounded && mob.getDeltaMovement().y < 0.0) {
			mob.setDeltaMovement(mob.getDeltaMovement().multiply(1.0, 0.0, 1.0));
			mob.resetFallDistance();
		}
		skycraft$mobMove = false;
	}
}
