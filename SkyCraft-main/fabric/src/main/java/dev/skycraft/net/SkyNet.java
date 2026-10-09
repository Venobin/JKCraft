package dev.skycraft.net;

import dev.skycraft.SkyCraft;
import dev.skycraft.combat.SkyCombat;
import dev.skycraft.world.SkyDig;
import java.util.List;
import net.minecraft.core.BlockPos;
import net.fabricmc.fabric.api.networking.v1.PayloadTypeRegistry;
import net.fabricmc.fabric.api.networking.v1.ServerPlayNetworking;
import net.minecraft.network.RegistryFriendlyByteBuf;
import net.minecraft.network.codec.ByteBufCodecs;
import net.minecraft.network.codec.StreamCodec;
import net.minecraft.network.protocol.common.custom.CustomPacketPayload;
import net.minecraft.resources.Identifier;
import net.minecraft.server.level.ServerPlayer;

/**
 * Multiplayer: every player has their own Skyrim, talking to their own Minecraft client. The host's
 * Skyrim reaches the host's integrated server through shared memory; a guest's Skyrim reaches the
 * host's server through these packets instead.
 */
public final class SkyNet {
	private SkyNet() {
	}

	/** Guest -> server: the guest's Skyrim hit them (as proto::InputEvent kInHurt). */
	public record Hurt(int kind, float skyrimDamage, int attackerFormId, int flags) implements CustomPacketPayload {
		public static final Type<Hurt> TYPE = new Type<>(Identifier.fromNamespaceAndPath(SkyCraft.MOD_ID, "hurt"));
		public static final StreamCodec<RegistryFriendlyByteBuf, Hurt> CODEC = StreamCodec.composite(
			ByteBufCodecs.VAR_INT, Hurt::kind,
			ByteBufCodecs.FLOAT, Hurt::skyrimDamage,
			ByteBufCodecs.INT, Hurt::attackerFormId,
			ByteBufCodecs.VAR_INT, Hurt::flags,
			Hurt::new
		);

		@Override
		public Type<? extends CustomPacketPayload> type() {
			return TYPE;
		}
	}

	/** Guest -> server: their Jedi Academy player collected a medpak or shield pickup. */
	public record Pickup(int kind, int jediPoints) implements CustomPacketPayload {
		public static final Type<Pickup> TYPE = new Type<>(Identifier.fromNamespaceAndPath(SkyCraft.MOD_ID, "pickup"));
		public static final StreamCodec<RegistryFriendlyByteBuf, Pickup> CODEC = StreamCodec.composite(
			ByteBufCodecs.VAR_INT, Pickup::kind,
			ByteBufCodecs.VAR_INT, Pickup::jediPoints,
			Pickup::new
		);

		@Override
		public Type<? extends CustomPacketPayload> type() {
			return TYPE;
		}
	}

	/** Server -> guest: the guest died in Minecraft, so their Skyrim player dies too. */
	public record Died(int attackerFormId) implements CustomPacketPayload {
		public static final Type<Died> TYPE = new Type<>(Identifier.fromNamespaceAndPath(SkyCraft.MOD_ID, "died"));
		public static final StreamCodec<RegistryFriendlyByteBuf, Died> CODEC = StreamCodec.composite(ByteBufCodecs.INT, Died::attackerFormId, Died::new);

		@Override
		public Type<? extends CustomPacketPayload> type() {
			return TYPE;
		}
	}

	/** Client -> server: the player hit Skyrim's geometry in this cell (SkyDig.open). */
	public record DigOpen(int world, BlockPos pos, int material) implements CustomPacketPayload {
		public static final Type<DigOpen> TYPE = new Type<>(Identifier.fromNamespaceAndPath(SkyCraft.MOD_ID, "dig_open"));
		public static final StreamCodec<RegistryFriendlyByteBuf, DigOpen> CODEC = StreamCodec.composite(
			ByteBufCodecs.INT, DigOpen::world,
			BlockPos.STREAM_CODEC, DigOpen::pos,
			ByteBufCodecs.VAR_INT, DigOpen::material,
			DigOpen::new
		);

		@Override
		public Type<? extends CustomPacketPayload> type() {
			return TYPE;
		}
	}

	/** Client -> server: cells around a broken dug block that are inside Skyrim's geometry (SkyDig.reveal). */
	public record DigReveal(int world, List<BlockPos> cells, List<Integer> materials) implements CustomPacketPayload {
		public static final Type<DigReveal> TYPE = new Type<>(Identifier.fromNamespaceAndPath(SkyCraft.MOD_ID, "dig_reveal"));
		public static final StreamCodec<RegistryFriendlyByteBuf, DigReveal> CODEC = StreamCodec.composite(
			ByteBufCodecs.INT, DigReveal::world,
			BlockPos.STREAM_CODEC.apply(ByteBufCodecs.list(64)), DigReveal::cells,
			ByteBufCodecs.VAR_INT.apply(ByteBufCodecs.list(64)), DigReveal::materials,
			DigReveal::new
		);

		@Override
		public Type<? extends CustomPacketPayload> type() {
			return TYPE;
		}
	}

	/** Client -> server: OpenJK changed maps, so remove blocks built on the previous map. */
	public record ResetMap(int world) implements CustomPacketPayload {
		public static final Type<ResetMap> TYPE = new Type<>(Identifier.fromNamespaceAndPath(SkyCraft.MOD_ID, "reset_map"));
		public static final StreamCodec<RegistryFriendlyByteBuf, ResetMap> CODEC = StreamCodec.composite(
			ByteBufCodecs.INT, ResetMap::world,
			ResetMap::new
		);

		@Override
		public Type<? extends CustomPacketPayload> type() {
			return TYPE;
		}
	}

	/** Client -> server: a held Jedi Force Jump has started and its landing is protected. */
	public record ForceJumpStarted(int sequence) implements CustomPacketPayload {
		public static final Type<ForceJumpStarted> TYPE = new Type<>(Identifier.fromNamespaceAndPath(SkyCraft.MOD_ID, "force_jump_started"));
		public static final StreamCodec<RegistryFriendlyByteBuf, ForceJumpStarted> CODEC = StreamCodec.composite(
			ByteBufCodecs.VAR_INT, ForceJumpStarted::sequence,
			ForceJumpStarted::new
		);

		@Override
		public Type<? extends CustomPacketPayload> type() {
			return TYPE;
		}
	}

	public static void init() {
		PayloadTypeRegistry.serverboundPlay().register(Hurt.TYPE, Hurt.CODEC);
		PayloadTypeRegistry.serverboundPlay().register(Pickup.TYPE, Pickup.CODEC);
		PayloadTypeRegistry.serverboundPlay().register(DigOpen.TYPE, DigOpen.CODEC);
		PayloadTypeRegistry.serverboundPlay().register(DigReveal.TYPE, DigReveal.CODEC);
		PayloadTypeRegistry.serverboundPlay().register(ResetMap.TYPE, ResetMap.CODEC);
		PayloadTypeRegistry.serverboundPlay().register(ForceJumpStarted.TYPE, ForceJumpStarted.CODEC);
		ServerPlayNetworking.registerGlobalReceiver(DigOpen.TYPE, (payload, context) -> {
			ServerPlayer player = context.player();
			context.server().execute(() -> SkyDig.open(player, payload.world(), payload.pos(), payload.material()));
		});
		ServerPlayNetworking.registerGlobalReceiver(DigReveal.TYPE, (payload, context) -> {
			ServerPlayer player = context.player();
			int[] materials = payload.materials().stream().mapToInt(Integer::intValue).toArray();
			context.server().execute(() -> SkyDig.reveal(player, payload.world(), payload.cells(), materials));
		});
		ServerPlayNetworking.registerGlobalReceiver(ResetMap.TYPE, (payload, context) -> {
			ServerPlayer player = context.player();
			context.server().execute(() -> dev.skycraft.world.MapBlockReset.reset(player, payload.world()));
		});
		ServerPlayNetworking.registerGlobalReceiver(ForceJumpStarted.TYPE, (payload, context) -> {
			ServerPlayer player = context.player();
			context.server().execute(() -> dev.skycraft.world.ForceJumpProtection.start(player));
		});
		PayloadTypeRegistry.clientboundPlay().register(Died.TYPE, Died.CODEC);
		ServerPlayNetworking.registerGlobalReceiver(Hurt.TYPE, (payload, context) -> {
			ServerPlayer player = context.player();
			// A hit's worth of damage, whatever the guest's client claims (friends only, but still).
			float damage = Math.max(0.0F, Math.min(payload.skyrimDamage(), 10000.0F));
			context.server().execute(() -> SkyCombat.hurtPlayer(player, payload.kind(), damage, payload.attackerFormId(), payload.flags()));
		});
		ServerPlayNetworking.registerGlobalReceiver(Pickup.TYPE, (payload, context) -> {
			ServerPlayer player = context.player();
			context.server().execute(() -> SkyCombat.pickupPlayer(player, payload.kind(), payload.jediPoints()));
		});
	}

	/** True if this player plays on this machine (their Skyrim is on the shared-memory link). */
	public static boolean isHost(ServerPlayer player) {
		var server = player.level().getServer();
		return server != null && server.isSingleplayerOwner(player.nameAndId());
	}
}
