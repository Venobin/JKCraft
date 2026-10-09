package dev.skycraft.client;

import dev.skycraft.link.Proto;
import dev.skycraft.link.SkyLink;
import net.minecraft.client.Minecraft;
import net.minecraft.tags.ItemTags;
import net.minecraft.world.entity.ai.attributes.Attributes;
import net.minecraft.world.item.Items;
import net.minecraft.world.phys.EntityHitResult;
import net.minecraft.world.phys.HitResult;

/** Sends a Minecraft melee swing to native Jedi Academy breakable entities under the crosshair. */
public final class SkyPropAttack {
	private static int lastAttackTick = Integer.MIN_VALUE;

	private SkyPropAttack() {
	}

	/** Returns true when a possible JA prop swing was queued. */
	public static boolean attack(Minecraft minecraft, boolean held) {
		var player = minecraft.player;
		if (!SkyClient.linked() || player == null || minecraft.level == null || SkyClient.sky().cinematic()
			|| SkyClient.sky().loading() || minecraft.gui.screen() != null) {
			return false;
		}
		// Minecraft entities (including JA actor proxies) and real placed blocks keep their
		// normal attack/mining paths. A SkyrimHitResult is synthetic map geometry and is valid.
		if (minecraft.hitResult instanceof EntityHitResult) {
			return false;
		}
		if (minecraft.hitResult != null && minecraft.hitResult.getType() == HitResult.Type.BLOCK
			&& !(minecraft.hitResult instanceof dev.skycraft.world.SkyClip.SkyrimHitResult)) {
			return false;
		}
		float strength = player.getAttackStrengthScale(0.5F);
		if (held && strength < 0.9F) {
			return false;
		}
		int tick = player.tickCount;
		if (tick == lastAttackTick) {
			return false;
		}
		lastAttackTick = tick;
		float base = (float) player.getAttributeValue(Attributes.ATTACK_DAMAGE);
		float damage = base * (0.2F + strength * strength * 0.8F);
		int weapon = weaponClass(player.getMainHandItem());
		SkyLink.pushEvent(Proto.EV_HIT_PROP, 0, Math.max(1.0F, damage), 0.0F, 0.0F, 5.0F, 0, weapon);
		player.resetAttackStrengthTicker();
		return true;
	}

	private static int weaponClass(net.minecraft.world.item.ItemStack weapon) {
		if (weapon.isEmpty()) return Proto.WEAPON_UNARMED;
		if (weapon.is(ItemTags.SWORDS)) return Proto.WEAPON_BLADE;
		if (weapon.is(ItemTags.AXES)) return Proto.WEAPON_AXE;
		if (weapon.is(Items.TRIDENT)) return Proto.WEAPON_PIERCE;
		return Proto.WEAPON_BLUNT;
	}
}
