package dev.skycraft.client.mixin;

import dev.skycraft.client.SkyDigClient;
import dev.skycraft.client.SkyPropAttack;
import net.minecraft.client.Minecraft;
import net.minecraft.world.InteractionHand;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** Attacking Skyrim's geometry digs into it (SkyDigClient). */
@Mixin(Minecraft.class)
public abstract class MinecraftDigMixin {
	@Inject(method = "startAttack", at = @At("HEAD"), cancellable = true)
	private void skycraft$digStart(CallbackInfoReturnable<Boolean> cir) {
		Minecraft minecraft = (Minecraft) (Object) this;
		boolean propAttack = SkyPropAttack.attack(minecraft, false);
		if (SkyDigClient.attack(minecraft) || propAttack) {
			// A swing, not a miss: no miss cooldown before mining the block that appears.
			var held = minecraft.player.getItemInHand(InteractionHand.MAIN_HAND);
			minecraft.player.swing(InteractionHand.MAIN_HAND, held.getAttackAnimation(), false);
			cir.setReturnValue(true);
		}
	}

	@Inject(method = "continueAttack", at = @At("HEAD"))
	private void skycraft$digHold(boolean down, CallbackInfo ci) {
		if (down) {
			Minecraft minecraft = (Minecraft) (Object) this;
			if (SkyPropAttack.attack(minecraft, true) && minecraft.player != null) {
				var held = minecraft.player.getItemInHand(InteractionHand.MAIN_HAND);
				minecraft.player.swing(InteractionHand.MAIN_HAND, held.getAttackAnimation(), false);
			}
			SkyDigClient.attack(minecraft);
		}
	}
}
