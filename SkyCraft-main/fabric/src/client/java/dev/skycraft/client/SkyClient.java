package dev.skycraft.client;

import dev.skycraft.SkyCraft;
import dev.skycraft.client.render.WorldExporter;
import dev.skycraft.link.Proto;
import dev.skycraft.link.SkyLink;
import dev.skycraft.world.SkyCollision;
import net.minecraft.client.Camera;
import net.minecraft.client.CameraType;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.server.level.ServerPlayer;
import net.fabricmc.fabric.api.client.networking.v1.ClientPlayNetworking;
import net.minecraft.world.phys.Vec3;
import org.lwjgl.sdl.SDLVideo;

/**
 * Per-frame glue between the Minecraft client and Skyrim. Everything here runs on the render
 * thread, called from MinecraftMixin.
 */
public final class SkyClient {
	private static final boolean SHOW_WINDOW = Boolean.getBoolean("jkcraft.showWindow");
	// The bundled client starts hidden while OpenJK provides the visible game window.
	// no title-screen music from the first frame, even while Skyrim is paused (Alt-Tabbed) and the
	// two haven't linked up yet. Otherwise the window only goes once Skyrim is there.
	private static final boolean START_HIDDEN = Boolean.getBoolean("jkcraft.startHidden");
	private static boolean startedHidden;

	private static final SkyLink.SkyState sky = new SkyLink.SkyState();
	private static final SkyLink.McState mc = new SkyLink.McState();
	private static volatile boolean linked;
	private static boolean tookOver;
	private static boolean windowHidden;
	private static boolean startupReadyReported;
	private static int appliedViewportW, appliedViewportH;

	// Teleport / hold state: Skyrim decides where the player is after loads, doors and respawns.
	private static int lastTeleportSeq = -1;
	private static int teleportAck;
	private static boolean teleportPending;
	private static LocalPlayer lastPlayer;
	private static Vec3 holdPos;
	private static Vec3 unlinkedHold;
	private static long holdSince;
	private static long qpcFreq;
	private static LocalPlayer eyePlayer;
	private static float eyeSmoothed;
	private static long frameCounter;
	private static int lastPacedSeq;
	private static boolean skyrimStalled;
	private static int exporterErrors;
	private static Vec3 streamedHold;
	private static final SkyLink.ForceState force = new SkyLink.ForceState();
	private static boolean spaceWasDown;
	private static boolean playerWasGrounded;
	private static int forceJumpTicks;
	private static boolean forceJumpActive;
	private static boolean forceJumpPaid;
	private static int forceJumpSequence;
	private static double forceJumpStartY;
	private static int lastMapWorldId;
	private static int resetAfterTeleportWorldId;
	private static long nextPhysicsDiagnostic;
	private static LocalPlayer gravityPlayer;
	private static double previousAirVelocityY;
	private static boolean previousTickAirborne;
	private static boolean loggedGravityRepair;

	private SkyClient() {
	}

	public static boolean linked() {
		return linked;
	}

	/**
	 * True once Skyrim has connected in this session. From then on Minecraft never touches the
	 * real mouse or keyboard again (even if Skyrim closes), since its window is hidden.
	 */
	public static boolean tookOver() {
		return tookOver;
	}

	public static SkyLink.SkyState sky() {
		return sky;
	}

	/** Start of Minecraft.runTick: pull state and input from Skyrim before anything else runs. */
	public static void beginFrame() {
		SkyLink.poll();
		quitWithSkyrim(Minecraft.getInstance());
		if (START_HIDDEN && !startedHidden) {
			startedHidden = true;
			Minecraft minecraft = Minecraft.getInstance();
			hideWindowOnce(minecraft);
			minecraft.options.getSoundSourceOptionInstance(net.minecraft.sounds.SoundSource.MUSIC).set(0.0);
			minecraft.getMusicManager().stopPlaying();
		}
		boolean nowLinked = SkyLink.active();
		if (nowLinked) {
			SkyLink.readSkyState(sky); // on a torn read we simply keep last frame's state
			dev.skycraft.world.SkyWater.refresh();
			if (sky.worldId != 0 && sky.worldId != lastMapWorldId) {
				// Also establish the current map on the first link. The mirror-world save can
				// contain blocks from an older run that are not in the in-memory placement list.
				requestMapReset(sky.worldId);
				resetAfterTeleportWorldId = sky.worldId;
				lastMapWorldId = sky.worldId;
			}
		} else {
			dev.skycraft.world.SkyWater.clear();
		}
		if (nowLinked != linked) {
			linked = nowLinked;
			SkyCraft.LOG.info("JKCraft: Jedi Academy link {}", linked ? "up" : "down");
			if (linked) {
				tookOver = true;
				unlinkedHold = null;
				SkyCollision.startConsumer();
				applyLinkedOptions();
			} else {
				InputBridge.releaseAll();
				LocalPlayer player = Minecraft.getInstance().player;
				unlinkedHold = player != null ? player.position() : null;
			}
		}
		if (!linked) {
			return;
		}

		Minecraft minecraft = Minecraft.getInstance();
		hideWindowOnce(minecraft);
		applyViewportSize(minecraft);
		MirrorWorld.openWhenReady(minecraft);

		boolean hostOwnsInput = sky.menuOpen() || sky.loading() || sky.cinematic();
		if (hostOwnsInput) {
			InputBridge.releaseAll();
			InputBridge.discard();
		} else {
			InputBridge.drain(minecraft);
		}
		ProxySync.frame(minecraft);

		LocalPlayer player = minecraft.player;
		if (player == null) {
			lastPlayer = null;
			return;
		}

		// A new player object means we just joined or respawned: put it where Skyrim's player is.
		if (player != lastPlayer) {
			lastPlayer = player;
			teleportPending = true;
		}
		if (sky.teleportSeq != lastTeleportSeq) {
			lastTeleportSeq = sky.teleportSeq;
			teleportPending = true;
		}
		if (teleportPending && sky.inGame() && !sky.loading() && !sky.cinematic()) {
			requestTeleport(minecraft, sky.x, sky.y, sky.z, sky.yaw, sky.pitch);
			teleportAck = sky.teleportSeq;
			teleportPending = false;
			holdPos = new Vec3(sky.x, sky.y, sky.z);
			if (resetAfterTeleportWorldId == sky.worldId) {
				requestMapReset(sky.worldId);
				resetAfterTeleportWorldId = 0;
			}
		}

		// Minecraft owns look direction after the initial teleport. Feeding OpenJK's previous
		// frame back here cancels most relative mouse movement and makes the camera feel locked.
	}

	// Minecraft is started with Skyrim (the SKSE plugin launches it), so it goes when that Skyrim has
	// closed for good: saved and shut down the normal way. -Djkcraft.quitWithJediAcademy=false keeps it
	// running instead (development: restarting Skyrim without restarting Minecraft).
	private static final boolean QUIT_WITH_SKYRIM = Boolean.parseBoolean(System.getProperty("jkcraft.quitWithJediAcademy", "true"));
	private static long skyrimGoneSince;
	private static long nextSkyrimCheck;
	private static int lastSkyrimPid;
	private static ProcessHandle lastSkyrimProcess;
	private static boolean hostShutdownRequested;
	// Started hidden by Skyrim but never connected: nobody can see or use this Minecraft, and it
	// would stop the next Skyrim from starting a fresh one ("already running"). It goes after this.
	private static final long NEVER_CONNECTED_QUIT_MS = 10 * 60 * 1000;
	private static final long STARTED_AT = System.currentTimeMillis();
	private static boolean gaveUpWaiting;

	private static void quitWithSkyrim(Minecraft minecraft) {
		int pid = SkyLink.skyrimPid();
		long now = System.currentTimeMillis();
		if (pid > 0 && pid != lastSkyrimPid) {
			lastSkyrimPid = pid;
			lastSkyrimProcess = ProcessHandle.of(pid).orElse(null);
			skyrimGoneSince = 0;
		}
		if (QUIT_WITH_SKYRIM && START_HIDDEN && pid == 0 && !tookOver && !gaveUpWaiting && now - STARTED_AT > NEVER_CONNECTED_QUIT_MS) {
			gaveUpWaiting = true;
			SkyCraft.LOG.warn("JKCraft: started hidden but Jedi Academy never connected in {} minutes; quitting", NEVER_CONNECTED_QUIT_MS / 60000);
			minecraft.stop();
			return;
		}
		if (!QUIT_WITH_SKYRIM || lastSkyrimPid == 0 || hostShutdownRequested || now < nextSkyrimCheck) {
			return;
		}
		nextSkyrimCheck = now + 1000;
		if (lastSkyrimProcess != null && lastSkyrimProcess.isAlive()) {
			skyrimGoneSince = 0;
			return;
		}
		if (skyrimGoneSince == 0) {
			skyrimGoneSince = now;
		} else if (now - skyrimGoneSince > 2000) {
			hostShutdownRequested = true;
			SkyCraft.LOG.info("JKCraft: Jedi Academy (pid {}) has closed; saving and quitting", lastSkyrimPid);
			minecraft.stop();
		}
	}

	/** Called at the end of every client tick. */
	public static void clientTick(Minecraft minecraft) {
		MirrorWorld.tick(minecraft);
		applyForceJump(minecraft);
		DiscordPresence.tick(minecraft);
		SkyDigClient.tick(minecraft);
		freezeWhileUnlinked(minecraft);
		holdUntilReady(minecraft);
		protectAgainstMissingCollision(minecraft);
		repairResetAirVelocity(minecraft);
		publishTick(minecraft);
		logPhysicsDiagnostic(minecraft);
	}

	/**
	 * Keep vanilla's ballistic velocity continuous while OpenJK supplies the collision geometry.
	 *
	 * <p>The mirror-world client can receive a zero vertical motion between two simulation ticks.
	 * Vanilla then applies only its first gravity tick ({@code -0.0784}) over and over, producing a
	 * long constant-speed hover.  Detect that exact reset signature and restore the value vanilla
	 * would have calculated: {@code (previousY - 0.08) * 0.98}.  Ordinary jumps, ceilings, fluids,
	 * flight and status-effect gravity remain entirely vanilla-owned.</p>
	 */
	private static void repairResetAirVelocity(Minecraft minecraft) {
		LocalPlayer player = minecraft.player;
		if (player != gravityPlayer) {
			gravityPlayer = player;
			previousTickAirborne = false;
		}
		if (!linked || player == null || holdPos != null || !sky.inGame() || sky.loading() || sky.cinematic()
			|| player.onGround() || player.verticalCollision || player.isInWater() || player.isInLava()
			|| player.getAbilities().flying || player.isFallFlying() || player.isNoGravity()
			|| player.hasEffect(net.minecraft.world.effect.MobEffects.LEVITATION)
			|| player.hasEffect(net.minecraft.world.effect.MobEffects.SLOW_FALLING)) {
			previousTickAirborne = false;
			return;
		}

		Vec3 velocity = player.getDeltaMovement();
		if (previousTickAirborne) {
			double expectedY = (previousAirVelocityY - 0.08) * 0.98;
			boolean firstGravityTickAgain = Math.abs(velocity.y + 0.0784) < 0.004
				|| Math.abs(velocity.y + 0.098) < 0.004;
			if (firstGravityTickAgain && Math.abs(velocity.y - expectedY) > 0.02) {
				player.setDeltaMovement(velocity.x, expectedY, velocity.z);
				velocity = player.getDeltaMovement();
				if (!loggedGravityRepair) {
					loggedGravityRepair = true;
					SkyCraft.LOG.info("JKCraft: repaired reset airborne velocity; vanilla gravity is now continuous");
				}
			}
		}
		previousAirVelocityY = velocity.y;
		previousTickAirborne = true;
	}

	private static void logPhysicsDiagnostic(Minecraft minecraft) {
		long now = System.currentTimeMillis();
		LocalPlayer player = minecraft.player;
		if (now < nextPhysicsDiagnostic || player == null || !sky.directCollision()) {
			return;
		}
		nextPhysicsDiagnostic = now + 3000;
		var diagnostic = dev.skycraft.link.DirectPhysics.moveDiagnostics();
		Vec3 velocity = player.getDeltaMovement();
		if (diagnostic == null) {
			SkyCraft.LOG.info("JKCraft physics: velocity=({},{},{}) ground={} water={} swimming={} sprint={} sneak={} use={} fly={} speed={} keys[W,C,S]={},{},{} (no direct move yet)",
				fmt(velocity.x), fmt(velocity.y), fmt(velocity.z), player.onGround(), player.isInWater(), player.isSwimming(),
				player.isSprinting(), player.isShiftKeyDown(), player.isUsingItem(), player.getAbilities().flying,
				fmt(player.getAttributeValue(net.minecraft.world.entity.ai.attributes.Attributes.MOVEMENT_SPEED)),
				InputBridge.isKeyDown(26), InputBridge.isKeyDown(224), InputBridge.isKeyDown(225));
			return;
		}
		SkyCraft.LOG.info("JKCraft physics: velocity=({},{},{}) ground={} water={} swimming={} sprint={} sneak={} use={} fly={} speed={} keys[W,C,S]={},{},{} request=({},{},{}) allowed=({},{},{}) OpenJK={}ms flags=0x{} timeout={}",
			fmt(velocity.x), fmt(velocity.y), fmt(velocity.z), player.onGround(), player.isInWater(), player.isSwimming(),
			player.isSprinting(), player.isShiftKeyDown(), player.isUsingItem(), player.getAbilities().flying,
			fmt(player.getAttributeValue(net.minecraft.world.entity.ai.attributes.Attributes.MOVEMENT_SPEED)),
			InputBridge.isKeyDown(26), InputBridge.isKeyDown(224), InputBridge.isKeyDown(225),
			fmt(diagnostic.requested().x), fmt(diagnostic.requested().y), fmt(diagnostic.requested().z),
			fmt(diagnostic.allowed().x), fmt(diagnostic.allowed().y), fmt(diagnostic.allowed().z),
			fmt(diagnostic.latencyMs()), Integer.toHexString(diagnostic.flags()), diagnostic.timedOut());
	}

	private static String fmt(double value) {
		return String.format(java.util.Locale.ROOT, "%.3f", value);
	}

	/** A tap stays low; holding Space adds Force Jump lift up to the current JA ability level. */
	private static void applyForceJump(Minecraft minecraft) {
		LocalPlayer player = minecraft.player;
		boolean spaceDown = InputBridge.isKeyDown(44); // SDL_SCANCODE_SPACE
		if (!linked || player == null || minecraft.gui.screen() != null || holdPos != null || sky.cinematic()) {
			spaceWasDown = spaceDown;
			playerWasGrounded = player != null && player.onGround();
			forceJumpActive = false;
			forceJumpTicks = 0;
			forceJumpPaid = false;
			return;
		}
		boolean jumpEdge = spaceDown && !spaceWasDown;
		boolean canStart = player.onGround() || playerWasGrounded;
		if (jumpEdge && canStart && SkyLink.readForceState(force) && force.knowsLevitation()
			&& force.power >= Proto.FORCE_JUMP_COST) {
			forceJumpActive = true;
			forceJumpTicks = 0;
			forceJumpPaid = false;
			forceJumpStartY = player.getY();
		}
		if (forceJumpActive) {
			// verticalCollision is also true while standing on the launch surface. It is a
			// ceiling only after leaving the ground, so do not cancel the jump at take-off.
			boolean hitCeiling = forceJumpTicks > 1 && player.verticalCollision && !player.onGround();
			if (!spaceDown || hitCeiling || player.isInWater() || player.isInLava()) {
				forceJumpActive = false;
			} else {
				int level = Math.max(1, Math.min(force.levitationLevel, 3));
				// Jedi Academy's forceJumpHeight table is 96/192/384 engine units,
				// plus the character step/crouch allowance. At 32 units per Minecraft
				// block that corresponds closely to visible rises of 4/7/13 blocks.
				double maxHeight = switch (level) {
					case 1 -> 4.0;
					case 2 -> 7.0;
					default -> 13.0;
				};
				double extraVelocity = switch (level) {
					case 1 -> 0.10;
					case 2 -> 0.14;
					default -> 0.20;
				};
				forceJumpTicks++;
				// A one-tick tap remains the normal Minecraft jump and spends no Force.
				if (forceJumpTicks >= 2) {
					if (!forceJumpPaid) {
						forceJumpPaid = true;
						SkyLink.pushEvent(Proto.EV_FORCE_JUMP, 0, 0, 0, 0, 0, 0);
						if (ClientPlayNetworking.canSend(dev.skycraft.net.SkyNet.ForceJumpStarted.TYPE)) {
							ClientPlayNetworking.send(new dev.skycraft.net.SkyNet.ForceJumpStarted(++forceJumpSequence));
						}
					}
					double risen = Math.max(0.0, player.getY() - forceJumpStartY);
					Vec3 velocity = player.getDeltaMovement();
					if (risen < maxHeight) {
						// Like JA, keep supplying upward velocity while Jump is held, tapering
						// as the level-specific height limit approaches.
						double remaining = Math.max(0.0, 1.0 - risen / maxHeight);
						double target = 0.36 + remaining * extraVelocity;
						player.setDeltaMovement(velocity.x, Math.max(velocity.y, target), velocity.z);
					} else {
						player.setDeltaMovement(velocity.x, Math.min(velocity.y, 0.0), velocity.z);
						forceJumpActive = false;
					}
				}
			}
		}
		spaceWasDown = spaceDown;
		playerWasGrounded = player.onGround();
	}

	private static void requestMapReset(int worldId) {
		if (ClientPlayNetworking.canSend(dev.skycraft.net.SkyNet.ResetMap.TYPE)) {
			ClientPlayNetworking.send(new dev.skycraft.net.SkyNet.ResetMap(worldId));
			SkyCraft.LOG.info("JKCraft: requested player-build cleanup for OpenJK map {}", Integer.toUnsignedString(worldId));
		}
	}

	/**
	 * Never let a slow collision stream turn into a fall through the map.  Walking up to an
	 * unpublished 8-block region briefly holds the previous valid position; normal movement
	 * resumes as soon as OpenJK publishes both the feet and ground regions.
	 */
	private static void protectAgainstMissingCollision(Minecraft minecraft) {
		LocalPlayer player = minecraft.player;
		if (sky.directCollision()) {
			streamedHold = null;
			return;
		}
		if (!linked || player == null || holdPos != null || !sky.inGame() || sky.loading() || sky.cinematic() || !SkyCollision.active()
			|| player.getAbilities().flying) {
			streamedHold = null;
			return;
		}
		int x = player.getBlockX(), y = player.getBlockY(), z = player.getBlockZ();
		boolean ready = SkyCollision.isKnown(x, y, z)
			&& SkyCollision.isKnown(x, y - 1, z)
			&& SkyCollision.isKnown(x, y - SkyCollision.REGION_SIZE, z);
		if (ready) {
			streamedHold = null;
			return;
		}
		if (streamedHold == null) {
			streamedHold = new Vec3(player.xo, player.yo, player.zo);
		}
		player.setDeltaMovement(Vec3.ZERO);
		player.setPos(streamedHold.x, streamedHold.y, streamedHold.z);
		player.xo = streamedHold.x;
		player.yo = streamedHold.y;
		player.zo = streamedHold.z;
		player.resetFallDistance();
	}

	/**
	 * Skyrim went quiet (a long loading screen, a stall, or it closed). Its collision around the
	 * player may be about to change (interior doors), so keep the player exactly where they were
	 * instead of letting them fall; Skyrim puts them where they belong when it's back.
	 */
	private static void freezeWhileUnlinked(Minecraft minecraft) {
		LocalPlayer player = minecraft.player;
		if (linked || !tookOver || player == null) {
			return;
		}
		if (unlinkedHold == null) {
			unlinkedHold = player.position();
		}
		player.setDeltaMovement(Vec3.ZERO);
		player.setPos(unlinkedHold.x, unlinkedHold.y, unlinkedHold.z);
		player.xo = unlinkedHold.x;
		player.yo = unlinkedHold.y;
		player.zo = unlinkedHold.z;
		player.resetFallDistance();
	}

	/**
	 * Hands Skyrim the raw physics tick (previous + latest feet, smoothed eye height, walk bob) with a
	 * QueryPerformanceCounter timestamp. Skyrim interpolates between them on its own frame clock,
	 * exactly like Minecraft's renderer does with partial ticks.
	 */
	private static void publishTick(Minecraft minecraft) {
		LocalPlayer player = minecraft.player;
		if (!linked || player == null) {
			return;
		}
		if (qpcFreq == 0) {
			qpcFreq = SkyLink.qpcFrequency();
		}
		float tickMs = minecraft.level != null ? minecraft.level.tickRateManager().millisecondsPerTick() : 50.0F;
		// The tick really "happened" partial ticks ago (DeltaTracker keeps the remainder).
		float remainder = minecraft.getDeltaTracker().getGameTimeDeltaPartialTick(false);
		mc.tickQpc = SkyLink.qpc() - (long) (remainder * tickMs * qpcFreq / 1000.0);
		mc.tickMs = tickMs;
		mc.prevX = player.xo;
		mc.prevY = player.yo;
		mc.prevZ = player.zo;
		mc.curX = player.getX();
		mc.curY = player.getY();
		mc.curZ = player.getZ();
		mc.healthFraction = player.getMaxHealth() > 0.0F
			? Math.max(0.0F, Math.min(1.0F, player.getHealth() / player.getMaxHealth())) : 0.0F;
		mc.absorption = Math.max(0.0F, player.getAbsorptionAmount());
		// Same smoothing as Camera.tick(): eye height eases halfway toward the target each tick.
		if (player != eyePlayer) {
			eyePlayer = player;
			eyeSmoothed = player.getEyeHeight();
		}
		mc.eyeHeightO = eyeSmoothed;
		eyeSmoothed += (player.getEyeHeight() - eyeSmoothed) * 0.5F;
		mc.eyeHeightT = eyeSmoothed;
		boolean bob = minecraft.options.bobView().get();
		var avatar = player.avatarState();
		mc.walkDistO = bob ? avatar.getInterpolatedWalkDistance(0.0F) : 0.0F;
		mc.walkDist = bob ? avatar.getInterpolatedWalkDistance(1.0F) : 0.0F;
		mc.bobO = bob ? avatar.getInterpolatedBob(0.0F) : 0.0F;
		mc.bob = bob ? avatar.getInterpolatedBob(1.0F) : 0.0F;
		SkyLink.writeMcState(mc);
	}

	/** Freeze the player until Skyrim's collision around them has arrived. */
	private static void holdUntilReady(Minecraft minecraft) {
		LocalPlayer player = minecraft.player;
		if (!linked || player == null) {
			return;
		}
		if (!sky.inGame() || sky.loading() || sky.cinematic()) {
			// Skyrim is on its main menu or a loading screen: park the player where they are.
			if (holdPos == null) {
				holdPos = player.position();
			}
			teleportPending = true;
		}
		if (holdPos == null) {
			holdSince = 0;
			return;
		}
		if (holdSince == 0) {
			holdSince = System.currentTimeMillis();
		}
		if (sky.directCollision() && sky.inGame() && !sky.loading() && !sky.cinematic()) {
			// OpenJK's live BSP is immediately queryable; there are no streamed regions to await.
			holdPos = null;
			holdSince = 0;
			return;
		}
		int bx = (int) Math.floor(holdPos.x), by = (int) Math.floor(holdPos.y), bz = (int) Math.floor(holdPos.z);
		boolean known = SkyCollision.isKnown(bx, by - 1, bz) && SkyCollision.isKnown(bx, by, bz)
			&& SkyCollision.isKnown(bx, by - SkyCollision.REGION_SIZE, bz);
		// Release once there is actual ground below (or after a timeout, e.g. when mid-air on purpose).
		boolean ready = known && (SkyCollision.hasSolidBelow(bx, by, bz, 12) || System.currentTimeMillis() - holdSince > 6000);
		if (ready && sky.inGame() && !sky.loading() && !sky.cinematic()) {
			// Skyrim's feet can sit a fraction of a voxel inside our ground layer. Minecraft's
			// collision never pushes you out of a shape, so you'd drop through: lift out first.
			Vec3 safe = liftOutOfGeometry(player, holdPos);
			if (safe.y != holdPos.y) {
				player.setPos(safe.x, safe.y, safe.z);
				player.yo = safe.y;
				SkyCraft.LOG.info("JKCraft: lifted player {} blocks out of the ground", String.format("%.3f", safe.y - holdPos.y));
			}
			holdPos = null;
			return;
		}
		player.setDeltaMovement(Vec3.ZERO);
		player.setPos(holdPos.x, holdPos.y, holdPos.z);
		player.xo = holdPos.x;
		player.yo = holdPos.y;
		player.zo = holdPos.z;
		player.resetFallDistance();
	}

	private static Vec3 liftOutOfGeometry(LocalPlayer player, Vec3 pos) {
		if (sky.directCollision()) {
			return pos;
		}
		// Stand on the exact Skyrim ground if it is slightly above the feet (up to 2.5 blocks).
		double ground = SkyCollider.groundAt(pos.x, pos.y, pos.z, 2.5);
		return !Double.isNaN(ground) && ground > pos.y ? new Vec3(pos.x, ground, pos.z) : pos;
	}

	private static void requestTeleport(Minecraft minecraft, double x, double y, double z, float yaw, float pitch) {
		LocalPlayer player = minecraft.player;
		player.setPos(x, y, z);
		player.setYRot(yaw);
		player.setXRot(pitch);
		player.yRotO = yaw;
		player.xRotO = pitch;
		player.setDeltaMovement(Vec3.ZERO);
		player.resetFallDistance();
		var server = minecraft.getSingleplayerServer();
		if (server != null) {
			var uuid = player.getUUID();
			server.execute(() -> {
				ServerPlayer sp = server.getPlayerList().getPlayer(uuid);
				if (sp != null) {
					sp.teleportTo(x, y, z);
					sp.setYRot(yaw);
					sp.setXRot(pitch);
					sp.resetFallDistance();
				}
			});
		}
		SkyCraft.LOG.info("JKCraft: teleported to {} {} {}", x, y, z);
	}

	/** After GameRenderer.render(): report the player to Skyrim and ship the overlay frame. */
	public static void afterRender() {
		if (!linked) {
			return;
		}
		Minecraft minecraft = Minecraft.getInstance();
		LocalPlayer player = minecraft.player;
		int flags = 0;
		if (player != null && minecraft.level != null) {
			mc.healthFraction = player.getMaxHealth() > 0.0F
				? Math.max(0.0F, Math.min(1.0F, player.getHealth() / player.getMaxHealth())) : 0.0F;
			mc.absorption = Math.max(0.0F, player.getAbsorptionAmount());
			float partial = minecraft.getDeltaTracker().getGameTimeDeltaPartialTick(false);
			Vec3 feet = player.getPosition(partial);
			Camera camera = minecraft.gameRenderer.mainCamera();
			flags |= Proto.MC_IN_WORLD;
			if (player.onGround()) {
				flags |= Proto.MC_ON_GROUND;
			}
			if (player.isShiftKeyDown()) {
				flags |= Proto.MC_SNEAKING;
			}
			if (player.isSprinting()) {
				flags |= Proto.MC_SPRINTING;
			}
			if (player.isDeadOrDying()) {
				flags |= Proto.MC_DEAD;
			}
			if (player.isSwimming()) {
				flags |= Proto.MC_SWIMMING;
			}
			if (player.getAbilities().flying) {
				flags |= Proto.MC_FLYING;
			}
			mc.x = feet.x;
			mc.y = feet.y;
			mc.z = feet.z;
			mc.yaw = player.getYRot();
			mc.pitch = player.getXRot();
			// The eye, not the camera: in third person Minecraft's camera sits behind or in front.
			Vec3 eye = camera.isDetached() ? player.getEyePosition(partial) : camera.position();
			mc.eyeHeight = (float) (eye.y - feet.y);
			mc.eyeX = eye.x;
			mc.eyeY = eye.y;
			mc.eyeZ = eye.z;
			mc.fov = camera.getFov();
			// Minecraft's F5 camera: Skyrim puts its camera where Minecraft's would be.
			mc.cameraMode = minecraft.options.getCameraType().ordinal();
			mc.cameraDistance = camera.isDetached() ? (float) camera.position().distanceTo(player.getEyePosition(partial)) : 0.0F;
			mc.cameraX = camera.position().x;
			mc.cameraY = camera.position().y;
			mc.cameraZ = camera.position().z;
			mc.cameraYaw = camera.yRot();
			mc.cameraPitch = camera.xRot();
			// Walk bob, exactly what GameRenderer.bobView() uses this frame.
			var entityState = minecraft.gameRenderer.gameRenderState().levelRenderState.cameraRenderState.entityRenderState;
			boolean bob = minecraft.options.bobView().get() && entityState.isPlayer;
			mc.bobPhase = bob ? entityState.backwardsInterpolatedWalkDistance : 0.0F;
			mc.bobAmount = bob ? entityState.bob : 0.0F;
		}
		if (minecraft.gui.screen() != null) {
			flags |= Proto.MC_SCREEN_OPEN;
		}
		mc.flags = flags;
		mc.sensitivity = minecraft.options.sensitivity().get().floatValue();
		mc.teleportAck = holdPos == null ? teleportAck : teleportAck - 1; // not "arrived" until we are released
		mc.guiScale = minecraft.getWindow().getGuiScale();
		mc.frameCounter = ++frameCounter;
		SkyLink.writeMcState(mc);

		if ((flags & Proto.MC_IN_WORLD) != 0 && !sky.menuOpen() && !sky.loading() && !sky.cinematic()) {
			try {
				WorldExporter.frame(minecraft, minecraft.getDeltaTracker().getGameTimeDeltaPartialTick(false));
			} catch (RuntimeException e) {
				if (exporterErrors++ < 5) {
					SkyCraft.LOG.error("JKCraft: world export failed", e);
				}
			}
			FrameExporter.capture(minecraft);
		}
	}

	/** End of the frame: render at most once per Skyrim frame instead of spinning freely. */
	public static void paceFrame() {
		if (!startupReadyReported) {
			Minecraft minecraft = Minecraft.getInstance();
			if (minecraft.gui.overlay() == null &&
				(minecraft.gui.screen() != null || minecraft.level != null)) {
				startupReadyReported = true;
				SkyCraft.LOG.info("JKCRAFT_MINECRAFT_READY pid={}", ProcessHandle.current().pid());
			}
		}
		if (!linked) {
			return;
		}
		if (skyrimStalled && (SkyLink.skyStateSeq() >>> 1) == lastPacedSeq) {
			return; // Skyrim is paused (menu / alt-tab): don't block every frame waiting for it
		}
		skyrimStalled = false;
		long deadline = System.nanoTime() + 25_000_000L;
		// SkyState.seq advances by 2 per Skyrim frame (odd while writing).
		while ((SkyLink.skyStateSeq() >>> 1) == lastPacedSeq && System.nanoTime() < deadline) {
			// Do not burn a CPU core while waiting: that starves OpenJK, delays collision
			// publishing and produces exactly the hitch/fall-through cycle this pacing avoids.
			java.util.concurrent.locks.LockSupport.parkNanos(250_000L);
		}
		int seqNow = SkyLink.skyStateSeq() >>> 1;
		skyrimStalled = seqNow == lastPacedSeq;
		lastPacedSeq = seqNow;
	}

	private static void applyLinkedOptions() {
		Minecraft minecraft = Minecraft.getInstance();
		var options = minecraft.options;
		// Rendering, the crosshair ray and interaction must share one camera.  Do not inherit
		// an F5 perspective saved by an earlier test run.
		options.setCameraType(CameraType.FIRST_PERSON);
		options.pauseOnLostFocus = false;
		options.vignette().set(false);
		options.enableVsync().set(false);
		options.framerateLimit().set(120);
		options.smoothCamera = false;
		// Minecraft doesn't draw the world itself; these only decide how far out placed blocks,
		// arrows and Skyrim NPC stand-ins stay loaded and simulated.
		options.renderDistance().set(8);
		options.simulationDistance().set(8);
		options.autoJump().set(false);
		options.onboardAccessibility = false;
		if (options.tutorialStep != net.minecraft.client.tutorial.TutorialSteps.NONE) {
			minecraft.getTutorial().setStep(net.minecraft.client.tutorial.TutorialSteps.NONE);
		}
		options.getSoundSourceOptionInstance(net.minecraft.sounds.SoundSource.MUSIC).set(0.0);
		options.save();
	}

	private static void hideWindowOnce(Minecraft minecraft) {
		if (windowHidden || SHOW_WINDOW) {
			return;
		}
		windowHidden = true;
		SDLVideo.SDL_HideWindow(minecraft.getWindow().handle());
		SkyCraft.LOG.info("JKCraft: game window hidden (run with -Djkcraft.showWindow=true to keep it)");
	}

	private static void applyViewportSize(Minecraft minecraft) {
		int w = Math.min(sky.viewportW, Proto.MAX_OVERLAY_W);
		int h = Math.min(sky.viewportH, Proto.MAX_OVERLAY_H);
		if (w <= 0 || h <= 0 || (w == appliedViewportW && h == appliedViewportH)) {
			return;
		}
		appliedViewportW = w;
		appliedViewportH = h;
		minecraft.getWindow().setWindowed(w, h);
		SkyCraft.LOG.info("JKCraft: sizing overlay to Jedi Academy viewport {}x{}", w, h);
	}
}
