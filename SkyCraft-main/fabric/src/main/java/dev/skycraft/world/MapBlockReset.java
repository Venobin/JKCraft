package dev.skycraft.world;

import dev.skycraft.SkyCraft;
import dev.skycraft.link.SkyLink;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import net.minecraft.core.BlockPos;
import net.minecraft.tags.FluidTags;
import net.minecraft.world.level.block.Block;
import net.fabricmc.fabric.api.event.player.PlayerBlockBreakEvents;
import net.fabricmc.fabric.api.event.player.UseBlockCallback;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.minecraft.core.BlockPos;
import net.minecraft.resources.ResourceKey;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.InteractionResult;
import net.minecraft.world.level.Level;

/** Remembers actual player placements so map changes never need a destructive world-wide sweep. */
public final class MapBlockReset {
	private record BuiltBlock(ResourceKey<Level> level, long pos) {
	}
	private record PlacementCheck(ServerLevel level, ArrayList<Long> airBefore) {
	}

	private static final Set<BuiltBlock> BUILT = new HashSet<>();
	private static final ArrayDeque<PlacementCheck> PLACEMENT_CHECKS = new ArrayDeque<>();

	private MapBlockReset() {
	}

	public static void init() {
		UseBlockCallback.EVENT.register((player, level, hand, hit) -> {
			if (!(level instanceof ServerLevel serverLevel) || !(player instanceof ServerPlayer)) {
				return InteractionResult.PASS;
			}
			// Doors, beds and similar items may place more than the one cell next to the clicked face.
			BlockPos centre = hit.getBlockPos().relative(hit.getDirection());
			ArrayList<Long> airBefore = new ArrayList<>(125);
			for (BlockPos pos : BlockPos.betweenClosed(centre.offset(-2, -2, -2), centre.offset(2, 2, 2))) {
				if (serverLevel.getBlockState(pos).isAir()) {
					airBefore.add(pos.asLong());
				}
			}
			PLACEMENT_CHECKS.addLast(new PlacementCheck(serverLevel, airBefore));
			return InteractionResult.PASS;
		});
		// UseBlockCallback runs before the item places its block. At the end of that server tick,
		// compare the captured air cells and remember only those that actually became blocks.
		ServerTickEvents.END_SERVER_TICK.register(server -> {
			while (!PLACEMENT_CHECKS.isEmpty()) {
				PlacementCheck check = PLACEMENT_CHECKS.removeFirst();
				for (long packed : check.airBefore()) {
					BlockPos pos = BlockPos.of(packed);
					if (!check.level().getBlockState(pos).isAir()) {
						BUILT.add(new BuiltBlock(check.level().dimension(), packed));
					}
				}
			}
			if (server.getTickCount() % 4 == 0) publishNearbyCells(server.getPlayerList().getPlayers());
		});
		PlayerBlockBreakEvents.AFTER.register((level, player, pos, state, blockEntity) ->
			BUILT.remove(new BuiltBlock(level.dimension(), pos.asLong())));
	}

	private static void publishNearbyCells(List<ServerPlayer> players) {
		if (players.isEmpty() || !SkyLink.active()) return;
		ServerPlayer player = players.getFirst();
		ServerLevel level = player.level();
		SkyLink.SkyState host = new SkyLink.SkyState();
		if (!SkyLink.readSkyState(host)) return;
		BlockPos center = player.blockPosition();
		ArrayList<BuiltBlock> nearby = new ArrayList<>();
		for (BuiltBlock built : BUILT) {
			if (!built.level().equals(level.dimension())) continue;
			BlockPos pos = BlockPos.of(built.pos());
			if (Math.abs(pos.getX() - center.getX()) <= 12 && Math.abs(pos.getY() - center.getY()) <= 8 &&
				Math.abs(pos.getZ() - center.getZ()) <= 12) nearby.add(built);
		}
		nearby.sort(Comparator.comparingDouble(b -> BlockPos.of(b.pos()).distSqr(center)));
		ArrayList<SkyLink.MinecraftCell> cells = new ArrayList<>(128);
		for (BuiltBlock built : nearby) {
			if (cells.size() >= 96) break;
			BlockPos pos = BlockPos.of(built.pos());
			if (!level.hasChunkAt(pos)) continue;
			var state = level.getBlockState(pos);
			if (Block.isShapeFullBlock(state.getCollisionShape(level, pos))) {
				cells.add(new SkyLink.MinecraftCell(pos.getX(), pos.getY(), pos.getZ(), 1));
			}
		}
		// Reserve wall slots, then choose the closest liquid cells instead of filling
		// the table with the lowest layer of a large pool.
		ArrayList<SkyLink.MinecraftCell> liquids = new ArrayList<>();
		for (int y = center.getY() - 3; y <= center.getY() + 3; y++) {
			for (int z = center.getZ() - 4; z <= center.getZ() + 4; z++) {
				for (int x = center.getX() - 4; x <= center.getX() + 4; x++) {
					BlockPos pos = new BlockPos(x, y, z);
					if (!level.hasChunkAt(pos)) continue;
					var state = level.getBlockState(pos);
					if (!state.getCollisionShape(level, pos).isEmpty()) continue;
					var fluid = state.getFluidState();
					int kind = fluid.is(FluidTags.LAVA) ? 3 : fluid.is(FluidTags.WATER) ? 2 : 0;
					if (kind != 0) liquids.add(new SkyLink.MinecraftCell(x, y, z, kind));
				}
			}
		}
		liquids.sort(Comparator.comparingDouble(c -> center.distSqr(new BlockPos(c.x(), c.y(), c.z()))));
		for (SkyLink.MinecraftCell liquid : liquids) {
			if (cells.size() >= 128) break;
			cells.add(liquid);
		}
		cells.sort(Comparator.comparingInt(SkyLink.MinecraftCell::kind)
			.thenComparingInt(SkyLink.MinecraftCell::x).thenComparingInt(SkyLink.MinecraftCell::y)
			.thenComparingInt(SkyLink.MinecraftCell::z));
		SkyLink.writeMinecraftCells(cells, host.worldId);
	}

	public static void reset(ServerPlayer player, int world) {
		ServerLevel level = player.level();
		ResourceKey<Level> dimension = level.dimension();
		ArrayList<BuiltBlock> remove = new ArrayList<>();
		int removed = 0;
		for (BuiltBlock built : BUILT) {
			if (!built.level().equals(dimension)) {
				continue;
			}
			BlockPos pos = BlockPos.of(built.pos());
			if (!level.hasChunkAt(pos)) {
				// Keep it remembered: the second request after teleport, or a later map change,
				// will remove it when that chunk is present without force-loading distant chunks.
				continue;
			}
			boolean wasAir = level.getBlockState(pos).isAir();
			if (wasAir || level.removeBlock(pos, false)) {
				if (!wasAir) removed++;
				remove.add(built);
			}
		}
		BUILT.removeAll(remove);
		SkyCraft.LOG.info("JKCraft: OpenJK map {} removed {} player-built blocks", Integer.toUnsignedString(world), removed);
	}
}
