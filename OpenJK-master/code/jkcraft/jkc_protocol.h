/*
===========================================================================
JKCraft shared-memory protocol bootstrap.

The bridge keeps the original SkyCraft field layout and extends it for JKCraft.
The host/client protocol versions must change together when the layout changes.
===========================================================================
*/

#pragma once

#include <cstdint>

namespace jkcraft
{
	namespace protocol
	{
	static constexpr std::uint32_t kMagic = 0x43594B53; // "SKYC"
	static constexpr std::uint32_t kVersion = 14;
	static constexpr wchar_t kMappingName[] = L"Local\\JKCraft_v1";
	static constexpr std::uint64_t kStateOffset = 0x100;
	static constexpr std::uint64_t kForceStateOffset = 0x180;
	static constexpr std::uint64_t kMinecraftStateOffset = 0x200;
	static constexpr std::uint64_t kHostCursorOffset = 0x40;
	static constexpr std::uint64_t kOverlayControlOffset = 0x300;
	static constexpr std::uint64_t kOverlaySlotHeaderOffset = 0x340;
	static constexpr std::uint64_t kWaterGridOffset = 0x400;
	static constexpr std::uint32_t kWaterGridSize = 16;
	// Water-grid cells above this marker encode lava: decoded surface = value - 8192.
	// JKCraft's supported world height stays below 4096, so the ranges do not overlap.
	static constexpr float kLavaSurfaceOffset = 8192.0f;
	static constexpr std::uint64_t kDirectMoveOffset = 0x900;
	static constexpr std::uint64_t kDirectRayOffset = 0x980;
	static constexpr std::uint64_t kMoverCarryOffset = 0xA00;
	static constexpr std::uint64_t kInputRingOffset = 0x1000;
	static constexpr std::uint64_t kInputRingHeadOffset = 0x00;
	static constexpr std::uint64_t kInputRingTailOffset = 0x40;
	static constexpr std::uint64_t kInputRingDataOffset = 0x80;
	static constexpr std::uint64_t kInputRingEntries = 4096;
	static constexpr std::uint64_t kActorTableOffset = 0x12000;
	static constexpr std::uint32_t kMaxActors = 256;
	static constexpr std::uint64_t kEventRingOffset = 0x17000;
	static constexpr std::uint64_t kMinecraftCellsOffset = 0x1B100;
	static constexpr std::uint32_t kMaxMinecraftCells = 128;
	static constexpr std::uint64_t kEventRingHeadOffset = 0x00;
	static constexpr std::uint64_t kEventRingTailOffset = 0x40;
	static constexpr std::uint64_t kEventRingDataOffset = 0x80;
	static constexpr std::uint64_t kEventRingEntries = 512;
	static constexpr std::uint64_t kCollisionRingOffset = 0x20000;
	static constexpr std::uint64_t kWorldEntitiesOffset = 0x1C000;
	static constexpr std::uint32_t kMaxWorldEntities = 160;
	static constexpr std::uint64_t kCollisionRingHeadOffset = 0x00;
	static constexpr std::uint64_t kCollisionRingTailOffset = 0x40;
	static constexpr std::uint64_t kCollisionRingDataOffset = 0x80;

	static constexpr std::uint64_t kCollisionOffset = 0x20000;
	static constexpr std::uint64_t kCollisionBytes = 32ull << 20;
	static constexpr std::uint64_t kOverlayOffset = kCollisionOffset + kCollisionBytes;
	static constexpr std::uint32_t kOverlayMaxWidth = 3840;
	static constexpr std::uint32_t kOverlayMaxHeight = 2160;
	static constexpr std::uint64_t kOverlaySlotBytes = 3840ull * 2160ull * 4ull;
	static constexpr std::uint64_t kOverlaySlots = 3;
	static constexpr std::uint32_t kOverlayDirty = 1u << 2;
	static constexpr std::uint64_t kRenderOffset = kOverlayOffset + kOverlaySlotBytes * kOverlaySlots;
	static constexpr std::uint64_t kRenderBytes = 64ull << 20;
	static constexpr std::uint64_t kRenderRingHeadOffset = 0x00;
	static constexpr std::uint64_t kRenderRingTailOffset = 0x40;
	static constexpr std::uint64_t kRenderRingDataOffset = 0x80;
	static constexpr std::uint64_t kRenderRingDataBytes = kRenderBytes - kRenderRingDataOffset;
	static constexpr std::uint64_t kMappingBytes = kRenderOffset + kRenderBytes;

	struct Header
	{
		std::uint32_t magic;
		std::uint32_t version;
		std::uint32_t hostPid;
		std::uint32_t minecraftPid;
		std::uint64_t hostHeartbeatMs;
		std::uint64_t minecraftHeartbeatMs;
	};

	static_assert(sizeof(Header) == 0x20);

	struct HostCursor
	{
		std::uint32_t seq;
		std::int32_t x;
		std::int32_t y;
		std::uint32_t pad;
	};

	static_assert(sizeof(HostCursor) == 0x10);

	enum StateFlags : std::uint32_t
	{
		kInGame = 1u << 0,
		kMenuOpen = 1u << 1,
		kLoading = 1u << 2,
		kCinematic = 1u << 3,
		kDirectCollision = 1u << 4,
	};

	// OpenJK -> Minecraft state. The legacy SkyCraft offsets remain stable while
	// newer JKCraft fields are appended without moving the overlay control block.
	struct HostState
	{
		std::uint32_t seq;
		std::uint32_t flags;
		std::uint32_t worldId;
		std::uint32_t collisionEpoch;
		double posX;
		double posY;
		double posZ;
		float yaw;
		float pitch;
		std::uint32_t teleportSeq;
		std::uint32_t viewportW;
		std::uint32_t viewportH;
		float gameHour;
	};

	static_assert(sizeof(HostState) == 0x40);

	// OpenJK -> Minecraft Force state. The selected power stays in cgame; this
	// snapshot is for cross-engine powers whose movement happens in Minecraft.
	struct ForceState
	{
		std::uint32_t seq;
		std::uint32_t powersKnown;
		std::uint32_t powersActive;
		std::int32_t forcePower;
		std::int32_t forcePowerMax;
		std::int32_t levitationLevel;
		std::uint32_t reserved0;
		std::uint32_t reserved1;
	};

	static_assert(sizeof(ForceState) == 0x20);
	static_assert(kStateOffset + sizeof(HostState) <= kForceStateOffset);
	static_assert(kForceStateOffset + sizeof(ForceState) <= kMinecraftStateOffset);

	struct WaterGrid
	{
		std::uint32_t seq;
		std::int32_t originX, originZ;
		std::uint32_t worldId;
		float surface[kWaterGridSize * kWaterGridSize];
	};

	static_assert(sizeof(WaterGrid) == 0x410);

	enum DirectResultFlags : std::uint32_t
	{
		kDirectHit = 1u << 0,
		kDirectOnGround = 1u << 1,
		kDirectStartSolid = 1u << 2,
		kDirectRayShot = 1u << 3,
	};

	struct DirectMove
	{
		std::uint32_t requestSeq, responseSeq, worldId, flags;
		double startX, startY, startZ;
		double deltaX, deltaY, deltaZ;
		float halfWidth, height, stepHeight, pad4C;
		double resultX, resultY, resultZ;
		float normalX, normalY, normalZ;
		std::uint32_t resultFlags;
	};

	struct DirectRay
	{
		std::uint32_t requestSeq, responseSeq, worldId, flags;
		double fromX, fromY, fromZ;
		double toX, toY, toZ;
		double hitX, hitY, hitZ;
		float normalX, normalY, normalZ, fraction;
		std::uint32_t resultFlags, material, pad70, pad74;
	};

	static_assert(sizeof(DirectMove) == 0x78);
	static_assert(sizeof(DirectRay) == 0x78);
	struct MoverCarry
	{
		double x, y, z;
		std::int32_t entityNum;
		std::uint32_t reserved;
	};
	static_assert(sizeof(MoverCarry) == 0x20);
	static_assert(kWaterGridOffset + sizeof(WaterGrid) <= kDirectMoveOffset);
	static_assert(kDirectMoveOffset + sizeof(DirectMove) <= kDirectRayOffset);
	static_assert(kDirectRayOffset + sizeof(DirectRay) <= kMoverCarryOffset);
	static_assert(kMoverCarryOffset + sizeof(MoverCarry) <= kInputRingOffset);

	enum MinecraftStateFlags : std::uint32_t
	{
		kMinecraftInWorld = 1u << 0,
		kMinecraftScreenOpen = 1u << 1,
		kMinecraftOnGround = 1u << 2,
		kMinecraftSneaking = 1u << 3,
		kMinecraftSprinting = 1u << 4,
		kMinecraftDead = 1u << 5,
		kMinecraftSwimming = 1u << 6,
		kMinecraftFlying = 1u << 7,
	};

	struct MinecraftState
	{
		std::uint32_t seq;
		std::uint32_t flags;
		double x, y, z;
		float yaw, pitch;
		float eyeHeight;
		float sensitivity;
		std::uint32_t teleportAck;
		std::uint32_t guiScale;
		std::uint64_t frameCounter;
		float fovDeg;
		float bobPhase;
		float bobAmount;
		float healthFraction;
		double eyeX, eyeY, eyeZ;
		std::int64_t tickQpc;
		double prevX, prevY, prevZ;
		double curX, curY, curZ;
		float tickEyeO, tickEye;
		float walkDistO, walkDist;
		float bobO, bob;
		float tickMs;
		float absorption;
		std::uint32_t cameraMode;
		float cameraDistance;
		double cameraX, cameraY, cameraZ;
		float cameraYaw, cameraPitch;
	};

	static_assert(sizeof(MinecraftState) == 0xE8);
	static_assert(kMinecraftStateOffset + sizeof(MinecraftState) <= kOverlayControlOffset);

	enum WorldEntityKind : std::uint32_t
	{
		kWorldArrow = 1,
		kWorldItem = 2,
		kWorldTrident = 3,
		kWorldBlock = 4,
		kWorldCrack = 5,
		kWorldShadow = 6,
	};

	struct WorldEntity
	{
		std::uint32_t kind, id;
		float x, y, z;
		float yaw, pitch, scale;
		float ext[3];
		float uv[3][4];
		std::uint32_t tint;
	};

	struct WorldEntities
	{
		std::uint32_t seq, count, hasSelection;
		float selMin[3], selMax[3];
		std::uint8_t pad[0x40 - 36];
		WorldEntity entities[kMaxWorldEntities];
	};

	static_assert(sizeof(WorldEntity) == 96);
	static_assert(sizeof(WorldEntities) == 0x40 + sizeof(WorldEntity) * kMaxWorldEntities);
	static_assert(kWorldEntitiesOffset + sizeof(WorldEntities) <= kCollisionRingOffset);

	enum ActorFlags : std::uint32_t
	{
		kActorHostile = 1u << 0,
		kActorDead = 1u << 1,
		kActorEssential = 1u << 2,
		kActorInCombat = 1u << 3,
	};

	struct ActorRecord
	{
		std::uint32_t formId, flags;
		float x, y, z, yaw, width, height, healthFrac;
		std::uint16_t level, pad;
		char name[24];
	};

	struct ActorTable
	{
		std::uint32_t seq, count;
		std::uint8_t reserved[0x40 - 8];
		ActorRecord actors[kMaxActors];
	};

	enum GameEventType : std::uint32_t
	{
		kEventHitActor = 1,
		kEventPlayerDied = 2,
		kEventExplosion = 3,
		kEventArrowStuck = 4,
		kEventSkillUse = 5,
		kEventForceJump = 6,
		kEventHitProp = 7,
	};

	enum HitFlags : std::uint32_t
	{
		kHitCritical = 1u << 0,
		kHitProjectile = 1u << 1,
		kHitSweep = 1u << 2,
		kHitFire = 1u << 3,
	};

	enum WeaponClass : std::uint32_t
	{
		kWeaponUnarmed = 0,
		kWeaponBlade = 1,
		kWeaponAxe = 2,
		kWeaponBlunt = 3,
		kWeaponPierce = 4,
		kWeaponArrow = 5,
	};

	// Combat uses asymmetric scales: Minecraft armor is applied after JA damage
	// reaches Minecraft, so using the same factor in both directions makes NPC
	// attacks far too weak against an armored player.
	constexpr float kMinecraftToJediDamage = 4.0f;
	constexpr float kMaxMinecraftDamagePerTick = 100.0f;

	struct GameEvent
	{
		std::uint32_t type, formId;
		float a, b, c, d;
		std::uint32_t flags, weapon;
	};

	static_assert(sizeof(ActorRecord) == 64);
	static_assert(sizeof(ActorTable) == 0x40 + sizeof(ActorRecord) * kMaxActors);
	static_assert(kActorTableOffset + sizeof(ActorTable) <= kEventRingOffset);
	static_assert(sizeof(GameEvent) == 32);
	struct MinecraftCell { std::int32_t x, y, z, kind; };
	struct MinecraftCells {
		std::uint32_t seq, count, worldId, reserved;
		MinecraftCell cells[kMaxMinecraftCells];
	};
	static_assert(sizeof(MinecraftCell) == 16);
	static_assert(kEventRingOffset + kEventRingDataOffset + sizeof(GameEvent) * kEventRingEntries <= kMinecraftCellsOffset);
	static_assert(kMinecraftCellsOffset + sizeof(MinecraftCells) <= kWorldEntitiesOffset);

	struct OverlayControl
	{
		std::uint32_t state;
		std::uint32_t pad;
		std::uint64_t framesPublished;
	};

	struct OverlaySlotHeader
	{
		std::uint32_t width;
		std::uint32_t height;
		std::uint32_t flags;
		std::uint32_t pad;
		std::uint64_t frameId;
		std::uint8_t reserved[0x40 - 0x18];
	};

	static_assert(sizeof(OverlayControl) == 0x10);
	static_assert(sizeof(OverlaySlotHeader) == 0x40);

	enum RenderMessageType : std::uint32_t
	{
		kRenderPad = 0,
		kRenderAtlas = 1,
		kRenderSection = 2,
		kRenderClearAll = 3,
		kRenderTexture = 4,
		kRenderAvatar = 5,
		kRenderScene = 6,
		kRenderAtlasRegion = 7,
		kRenderLights = 8,
		kRenderRagdoll = 9,
		kRenderSolids = 10,
		kRenderDug = 11,
	};

	struct RenderAtlas
	{
		std::uint32_t width, height;
	};

	struct RenderAtlasRegion
	{
		std::uint32_t x, y, width, height;
	};

	struct RenderSection
	{
		std::int32_t sx, sy, sz;
		std::uint32_t vertexCount;
	};

	struct RenderLights
	{
		std::int32_t sx, sy, sz;
		std::uint32_t count;
	};

	struct RenderLight
	{
		std::uint8_t x, y, z, emission;
		std::uint32_t color;
	};

	struct RenderTexture
	{
		std::uint32_t id, width, height, flags;
	};

	struct RenderMesh
	{
		std::uint32_t batchCount, vertexCount;
	};

	struct RenderScene
	{
		double originX, originY, originZ;
		std::uint32_t batchCount, vertexCount;
	};

	struct RenderBatch
	{
		std::uint32_t texture, first, count, flags;
	};

	struct RenderVertex
	{
		float x, y, z;
		float u, v;
		std::uint32_t color;
		std::uint32_t light;
		std::uint32_t flags;
	};

	static_assert(sizeof(RenderAtlas) == 8);
	static_assert(sizeof(RenderAtlasRegion) == 16);
	static_assert(sizeof(RenderSection) == 16);
	static_assert(sizeof(RenderLights) == 16);
	static_assert(sizeof(RenderLight) == 8);
	static_assert(sizeof(RenderTexture) == 16);
	static_assert(sizeof(RenderMesh) == 8);
	static_assert(sizeof(RenderScene) == 32);
	static_assert(sizeof(RenderBatch) == 16);
	static_assert(sizeof(RenderVertex) == 32);

	enum InputType : std::uint16_t
	{
		kInputKey = 1,
		kInputMouseButton = 2,
		kInputScroll = 3,
		kInputCursor = 4,
		kInputText = 5,
		kInputReleaseAll = 6,
		kInputHurt = 7,
		kInputOpenMenu = 8,
		kInputPickup = 9,
	};

	struct InputEvent
	{
		std::uint16_t type;
		std::uint16_t code;
		std::int32_t a;
		std::int32_t b;
		std::int32_t c;
	};

	static_assert(sizeof(InputEvent) == 16);

	enum CollisionMessageType : std::uint32_t
	{
		kCollisionPad = 0,
		kCollisionClear = 1,
		kCollisionRegion = 2,
		kCollisionTriangles = 3,
		kCollisionDynamicTriangles = 4,
	};

	enum CollisionTriangleFlags : std::uint32_t
	{
		kTriangleStairHelper = 1u << 0,
		kTriangleDiggable = 1u << 1,
		kTriangleGhost = 1u << 2,
		kTriangleTerrain = 1u << 3,
	};

	struct CollisionMessageHeader
	{
		std::uint32_t type;
		std::uint32_t payloadBytes;
	};

	struct CollisionRegion
	{
		std::int32_t minX, minY, minZ;
		std::int32_t maxX, maxY, maxZ;
		std::uint32_t epoch;
		std::uint32_t count;
	};

	struct CollisionBlock
	{
		std::int32_t x, y, z;
		std::uint32_t pad;
		std::uint64_t bits[8];
	};

	struct CollisionTriangle
	{
		float xyz[9];
		std::uint32_t flags;
	};

	static_assert(sizeof(CollisionRegion) == 32);
	static_assert(sizeof(CollisionBlock) == 80);
	static_assert(sizeof(CollisionTriangle) == 40);
	}
}
