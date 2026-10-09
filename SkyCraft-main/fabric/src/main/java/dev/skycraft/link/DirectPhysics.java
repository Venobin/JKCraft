package dev.skycraft.link;

import static dev.skycraft.link.Proto.*;
import static java.lang.foreign.ValueLayout.*;

import dev.skycraft.SkyCraft;
import java.lang.foreign.MemorySegment;
import java.lang.invoke.VarHandle;
import net.minecraft.world.phys.Vec3;

/**
 * Synchronous collision queries against OpenJK's live BSP collision model.
 *
 * <p>The two mailboxes are intentionally separate: movement and aiming can be issued by different
 * Minecraft paths without copying any part of the Jedi Academy map into the Minecraft world.</p>
 */
public final class DirectPhysics {
	private static final VarHandle INT = JAVA_INT.varHandle();
	private static final Object MOVE_LOCK = new Object();
	private static final Object RAY_LOCK = new Object();
	private static final long TIMEOUT_NS = 80_000_000L;
	private static long nextTimeoutLog;
	private static volatile RayRequest latestRayRequest;
	private static volatile RaySample latestRaySample;
	private static volatile boolean rayWorkerStarted;
	private static volatile MoveDiagnostics lastMoveDiagnostics;

	private DirectPhysics() {
	}

	public record Move(Vec3 delta, Vec3 carry, int flags) {
		public boolean hit() {
			return (flags & DIRECT_HIT) != 0;
		}

		public boolean onGround() {
			return (flags & DIRECT_ON_GROUND) != 0;
		}
	}

	public record MoveDiagnostics(Vec3 requested, Vec3 allowed, double latencyMs, int flags, boolean timedOut) {
	}

	public static MoveDiagnostics moveDiagnostics() {
		return lastMoveDiagnostics;
	}

	public record Ray(Vec3 location, Vec3 normal, double fraction, int material) {
	}

	private record RayRequest(Vec3 from, Vec3 to, long id) {
	}

	private record RaySample(RayRequest request, Ray result, long completedAtNanos) {
	}

	public static boolean active() {
		SkyLink.SkyState state = new SkyLink.SkyState();
		return ready(state);
	}

	public static Move move(Vec3 start, Vec3 delta, float halfWidth, float height, float stepHeight, boolean onGround) {
		synchronized (MOVE_LOCK) {
			long started = System.nanoTime();
			SkyLink.SkyState state = new SkyLink.SkyState();
			if (!ready(state)) {
				return null;
			}
			MemorySegment segment = SkyLink.segment();
			long b = OFF_DIRECT_MOVE;
			int sequence = nextSequence((int) INT.getAcquire(segment, b + DQ_RESPONSE) + 1);
			segment.set(JAVA_INT, b + DQ_WORLD, state.worldId);
			segment.set(JAVA_INT, b + DQ_FLAGS, onGround ? DIRECT_ON_GROUND : 0);
			putVec(segment, b + DM_START_X, start);
			putVec(segment, b + DM_DELTA_X, delta);
			segment.set(JAVA_FLOAT, b + DM_HALF_WIDTH, halfWidth);
			segment.set(JAVA_FLOAT, b + DM_HEIGHT, height);
			segment.set(JAVA_FLOAT, b + DM_STEP, stepHeight);
			VarHandle.storeStoreFence();
			INT.setRelease(segment, b + DQ_REQUEST, sequence);
			if (!await(segment, b, sequence, "movement")) {
				// A missing host answer must never let the player fall through the BSP.
				lastMoveDiagnostics = new MoveDiagnostics(delta, Vec3.ZERO, (System.nanoTime() - started) / 1_000_000.0, DIRECT_HIT, true);
				return new Move(Vec3.ZERO, Vec3.ZERO, DIRECT_HIT);
			}
			Vec3 result = getVec(segment, b + DM_RESULT_X);
			Vec3 carry = getVec(segment, OFF_MOVER_CARRY);
			int flags = segment.get(JAVA_INT, b + DM_RESULT_FLAGS);
			lastMoveDiagnostics = new MoveDiagnostics(delta, result, (System.nanoTime() - started) / 1_000_000.0, flags, false);
			return new Move(result, carry, flags);
		}
	}

	public static Ray ray(Vec3 from, Vec3 to) {
		return ray(from, to, false);
	}

	public static Ray ray(Vec3 from, Vec3 to, boolean shot) {
		synchronized (RAY_LOCK) {
			SkyLink.SkyState state = new SkyLink.SkyState();
			if (!ready(state)) {
				return null;
			}
			MemorySegment segment = SkyLink.segment();
			long b = OFF_DIRECT_RAY;
			int sequence = nextSequence((int) INT.getAcquire(segment, b + DQ_RESPONSE) + 1);
			segment.set(JAVA_INT, b + DQ_WORLD, state.worldId);
			segment.set(JAVA_INT, b + DQ_FLAGS, shot ? DIRECT_RAY_SHOT : 0);
			putVec(segment, b + DR_FROM_X, from);
			putVec(segment, b + DR_TO_X, to);
			VarHandle.storeStoreFence();
			INT.setRelease(segment, b + DQ_REQUEST, sequence);
			if (!await(segment, b, sequence, "ray")) {
				return null;
			}
			int flags = segment.get(JAVA_INT, b + DR_RESULT_FLAGS);
			if ((flags & DIRECT_HIT) == 0) {
				return null;
			}
			return new Ray(
				getVec(segment, b + DR_HIT_X),
				getFloatVec(segment, b + DR_NORMAL_X),
				segment.get(JAVA_FLOAT, b + DR_FRACTION),
				segment.get(JAVA_INT, b + DR_MATERIAL)
			);
		}
	}

	/**
	 * Non-blocking ray for the render-thread crosshair. A daemon performs the OpenJK query and the
	 * render thread uses a very recent geometrically compatible answer; projectile queries remain
	 * synchronous through {@link #ray(Vec3, Vec3)}.
	 */
	public static Ray rayLatest(Vec3 from, Vec3 to) {
		if (!active()) {
			latestRayRequest = null;
			latestRaySample = null;
			return null;
		}
		RayRequest request = new RayRequest(from, to, System.nanoTime());
		latestRayRequest = request;
		startRayWorker();
		RaySample sample = latestRaySample;
		if (sample == null || System.nanoTime() - sample.completedAtNanos > 120_000_000L || !compatible(sample.request, request)) {
			return null;
		}
		return sample.result;
	}

	private static synchronized void startRayWorker() {
		if (rayWorkerStarted) {
			return;
		}
		rayWorkerStarted = true;
		Thread worker = new Thread(() -> {
			long processed = Long.MIN_VALUE;
			while (true) {
				RayRequest request = latestRayRequest;
				if (request == null || request.id == processed) {
					java.util.concurrent.locks.LockSupport.parkNanos(500_000L);
					continue;
				}
				processed = request.id;
				Ray result = ray(request.from, request.to);
				latestRaySample = new RaySample(request, result, System.nanoTime());
			}
		}, "JKCraft OpenJK ray");
		worker.setDaemon(true);
		worker.start();
	}

	private static boolean compatible(RayRequest a, RayRequest b) {
		if (a.from.distanceToSqr(b.from) > 0.0625) {
			return false;
		}
		Vec3 ad = a.to.subtract(a.from).normalize();
		Vec3 bd = b.to.subtract(b.from).normalize();
		return ad.dot(bd) > 0.995;
	}

	private static boolean ready(SkyLink.SkyState state) {
		return SkyLink.active() && SkyLink.readSkyState(state) && state.directCollision()
			&& state.inGame() && !state.loading() && !state.cinematic() && state.worldId != 0;
	}

	private static boolean await(MemorySegment segment, long base, int sequence, String kind) {
		long deadline = System.nanoTime() + TIMEOUT_NS;
		int spins = 0;
		while ((int) INT.getAcquire(segment, base + DQ_RESPONSE) != sequence) {
			if (!SkyLink.active() || System.nanoTime() >= deadline) {
				long now = System.currentTimeMillis();
				if (now >= nextTimeoutLog) {
					nextTimeoutLog = now + 5000;
					SkyCraft.LOG.warn("JKCraft: timed out waiting for OpenJK {} collision query", kind);
				}
				return false;
			}
			if (++spins < 200) {
				Thread.onSpinWait();
			} else {
				Thread.yield();
			}
		}
		VarHandle.loadLoadFence();
		return true;
	}

	private static int nextSequence(int sequence) {
		return sequence == 0 ? 1 : sequence;
	}

	private static void putVec(MemorySegment segment, long offset, Vec3 value) {
		segment.set(JAVA_DOUBLE, offset, value.x);
		segment.set(JAVA_DOUBLE, offset + 8, value.y);
		segment.set(JAVA_DOUBLE, offset + 16, value.z);
	}

	private static Vec3 getVec(MemorySegment segment, long offset) {
		return new Vec3(segment.get(JAVA_DOUBLE, offset), segment.get(JAVA_DOUBLE, offset + 8), segment.get(JAVA_DOUBLE, offset + 16));
	}

	private static Vec3 getFloatVec(MemorySegment segment, long offset) {
		return new Vec3(segment.get(JAVA_FLOAT, offset), segment.get(JAVA_FLOAT, offset + 4), segment.get(JAVA_FLOAT, offset + 8));
	}
}
