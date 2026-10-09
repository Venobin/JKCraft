package dev.skycraft.world;

import dev.skycraft.link.SkyLink;
import dev.skycraft.link.Proto;
import net.minecraft.core.BlockPos;
import net.minecraft.world.level.BlockGetter;
import net.minecraft.world.level.material.FluidState;
import net.minecraft.world.level.material.Fluids;
import org.jspecify.annotations.Nullable;

/**
 * Jedi Academy water and lava as Minecraft fluids. OpenJK sends the surface and fluid kind for
 * nearby columns; entity physics uses the matching vanilla fluid without changing world blocks.
 */
public final class SkyWater {
	private record Grid(int originX, int originZ, int size, float[] surface) {
	}

	private static volatile @Nullable Grid grid;

	private SkyWater() {
	}

	/** Once a frame on the client: pick up Skyrim's latest grid. */
	public static void refresh() {
		SkyLink.WaterGrid read = SkyLink.readWaterGrid();
		if (read != null) {
			grid = new Grid(read.originX, read.originZ, read.size, read.surface);
		}
	}

	public static void clear() {
		grid = null;
	}

	public static boolean active() {
		return grid != null;
	}

	private static float encodedAt(int x, int z) {
		Grid g = grid;
		if (g == null) {
			return Float.NaN;
		}
		int dx = x - g.originX(), dz = z - g.originZ();
		if (dx < 0 || dz < 0 || dx >= g.size() || dz >= g.size()) {
			return Float.NaN;
		}
		float s = g.surface()[dz * g.size() + dx];
		return s < -1.0e20F ? Float.NaN : s;
	}

	/** Minecraft y of the Jedi Academy fluid surface, or NaN where there is none. */
	public static double surfaceAt(int x, int z) {
		float raw = encodedAt(x, z);
		return raw >= 4096.0F ? raw - Proto.LAVA_SURFACE_OFFSET : raw;
	}

	/** How much of this block (0..1) is under Skyrim's water; 0 above the surface. */
	public static float depthIn(BlockPos pos) {
		double s = surfaceAt(pos.getX(), pos.getZ());
		if (Double.isNaN(s)) {
			return 0.0F;
		}
		double h = s - pos.getY();
		return h < 0.02 ? 0.0F : (float) Math.min(1.0, h);
	}

	/** True if Skyrim water reaches up into the box of block cells (inclusive). */
	public static boolean anyIn(int x0, int y0, int z0, int x1, int y1, int z1) {
		if (grid == null) {
			return false;
		}
		for (int x = x0; x <= x1; x++) {
			for (int z = z0; z <= z1; z++) {
				double s = surfaceAt(x, z);
				if (!Double.isNaN(s) && s > y0) {
					return true;
				}
			}
		}
		return false;
	}

	/** Jedi Academy water or lava in an otherwise empty Minecraft cell. */
	public static @Nullable FluidState fluidAt(BlockGetter level, BlockPos pos) {
		float raw = encodedAt(pos.getX(), pos.getZ());
		double surface = raw >= 4096.0F ? raw - Proto.LAVA_SURFACE_OFFSET : raw;
		if (Float.isNaN(raw) || surface - pos.getY() < 0.02 || !level.getBlockState(pos).isAir()) {
			return null;
		}
		return raw >= 4096.0F ? Fluids.LAVA.getSource(false) : Fluids.WATER.getSource(false);
	}

	/** The exact water height in a cell only Skyrim fills (so floating matches its surface); -1 otherwise. */
	public static float substitutedHeight(BlockGetter level, BlockPos pos) {
		float depth = depthIn(pos);
		if (depth <= 0.0F || !level.getFluidState(pos).isEmpty() || !level.getBlockState(pos).isAir()) {
			return -1.0F;
		}
		return depth;
	}
}
