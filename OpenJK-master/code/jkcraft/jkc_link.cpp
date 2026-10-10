/*
===========================================================================
JKCraft engine-side shared-memory bootstrap.
===========================================================================
*/

#include "jkc_link.h"
#include "jkc_protocol.h"

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "qcommon/cm_public.h"
#include "qcommon/cm_local.h"
#include "client/client.h"
#include "game/surfaceflags.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <tuple>
#include <vector>

extern void CM_GetWorldBounds(vec3_t mins, vec3_t maxs);

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace jkcraft
{
	namespace
	{
		constexpr std::uint64_t kMinecraftTimeoutMs = 3000;
		cvar_t* enabled = nullptr;
		cvar_t* unitsPerBlock = nullptr;
		cvar_t* cinematicState = nullptr;
		cvar_t* fullPreload = nullptr;
		cvar_t* preloadMaxRegions = nullptr;
		cvar_t* preloadBudgetMs = nullptr;
		cvar_t* directCollision = nullptr;
		std::uint32_t lastWorldId = 0;
		std::uint32_t collisionEpoch = 0;
		std::uint32_t teleportSeq = 0;
		std::uint32_t collisionWorldId = 0;
		std::set<std::tuple<int, int, int>> publishedCollisionRegions;
		std::set<std::tuple<int, int, int>> publishedTriangleRegions;
		std::set<std::tuple<int, int, int>> publishedDynamicRegions;
		std::uint64_t lastDynamicSignature = 0;
		std::uint64_t lastDynamicUpdateMs = 0;
		int lastWaterOriginX = 0x7fffffff;
		int lastWaterOriginZ = 0x7fffffff;
		std::uint32_t lastWaterWorldId = 0;
		bool lastPlayerInWater = false;
		bool lastPlayerInLava = false;
		std::uint32_t lastMinecraftPid = 0;
		bool lastCinematic = false;
		std::uint32_t cameraReleaseSeq = 0;
		bool lastDirectCollision = true;
		bool preloadActive = false;
		bool preloadAwaitingDrain = false;
		std::vector<std::tuple<int, int, int>> preloadRegions;
		std::size_t preloadCursor = 0;
		int preloadLastPercent = -1;
		std::uint64_t preloadStartedMs = 0;
		std::uint64_t lastDirectClipLogMs = 0;
		struct MoverPose
		{
			vec3_t origin, angles;
			int modelindex;
		};
		std::map<int, MoverPose> previousMoverPoses;
		std::uint32_t moverWorldId = 0;
		bool MinecraftAlive(std::uint64_t now);

#if defined(_WIN32)
		HANDLE mapping = nullptr;
		protocol::Header* header = nullptr;

		std::uint64_t Read64(const std::uint64_t* value)
		{
			return static_cast<std::uint64_t>(InterlockedCompareExchange64(
				reinterpret_cast<volatile LONG64*>(const_cast<std::uint64_t*>(value)), 0, 0));
		}

		void Write64(std::uint64_t* value, std::uint64_t next)
		{
			InterlockedExchange64(reinterpret_cast<volatile LONG64*>(value), static_cast<LONG64>(next));
		}

		std::uint32_t Read32(const std::uint32_t* value)
		{
			return static_cast<std::uint32_t>(InterlockedCompareExchange(
				reinterpret_cast<volatile LONG*>(const_cast<std::uint32_t*>(value)), 0, 0));
		}

		bool ReadMinecraftState(protocol::MinecraftState& state)
		{
			if (!header)
			{
				return false;
			}
			const auto* source = reinterpret_cast<const protocol::MinecraftState*>(
				reinterpret_cast<const std::uint8_t*>(header) + protocol::kMinecraftStateOffset);
			for (int attempt = 0; attempt < 5; ++attempt)
			{
				const std::uint32_t before = Read32(&source->seq);
				if (before & 1u)
				{
					continue;
				}
				std::memcpy(&state, source, sizeof(state));
				MemoryBarrier();
				const std::uint32_t after = Read32(&source->seq);
				if (before == after && !(after & 1u))
				{
					return true;
				}
			}
			return false;
		}

		std::uint8_t* MappingBase()
		{
			return reinterpret_cast<std::uint8_t*>(header);
		}

		bool WriteCollisionMessage(std::uint32_t type, const void* payload, std::uint32_t payloadBytes)
		{
			const std::uint64_t dataBytes = protocol::kCollisionBytes - protocol::kCollisionRingDataOffset;
			auto* ring = MappingBase() + protocol::kCollisionRingOffset;
			auto* headPtr = reinterpret_cast<std::uint64_t*>(ring + protocol::kCollisionRingHeadOffset);
			auto* tailPtr = reinterpret_cast<std::uint64_t*>(ring + protocol::kCollisionRingTailOffset);
			std::uint64_t head = Read64(headPtr);
			const std::uint64_t tail = Read64(tailPtr);
			const std::uint64_t messageBytes = (sizeof(protocol::CollisionMessageHeader) + payloadBytes + 7u) & ~7ull;
			std::uint64_t position = head % dataBytes;
			const std::uint64_t wrapBytes = position + messageBytes > dataBytes ? dataBytes - position : 0;
			if (head - tail + wrapBytes + messageBytes > dataBytes)
			{
				Com_Printf(S_COLOR_YELLOW "JKCraft: collision ring full; initial region was not sent\n");
				return false;
			}

			if (wrapBytes)
			{
				auto* pad = reinterpret_cast<protocol::CollisionMessageHeader*>(
					ring + protocol::kCollisionRingDataOffset + position);
				pad->type = protocol::kCollisionPad;
				pad->payloadBytes = 0;
				head += wrapBytes;
				position = 0;
			}

			auto* destination = ring + protocol::kCollisionRingDataOffset + position;
			protocol::CollisionMessageHeader messageHeader{type, payloadBytes};
			std::memcpy(destination, &messageHeader, sizeof(messageHeader));
			if (payloadBytes)
			{
				std::memcpy(destination + sizeof(messageHeader), payload, payloadBytes);
			}
			MemoryBarrier();
			Write64(headPtr, head + messageBytes);
			return true;
		}

		int RegionFloor(double coordinate)
		{
			return static_cast<int>(std::floor(coordinate / 8.0)) * 8;
		}

		void ToMinecraftPoint(const vec3_t point, double scale, float out[3])
		{
			out[0] = static_cast<float>(point[0] / scale);
			out[1] = static_cast<float>(point[2] / scale);
			out[2] = static_cast<float>(-point[1] / scale);
		}

		void AddTriangle(std::vector<protocol::CollisionTriangle>& triangles,
			const vec3_t a, const vec3_t b, const vec3_t c, double scale, std::uint32_t flags)
		{
			protocol::CollisionTriangle triangle{};
			ToMinecraftPoint(a, scale, triangle.xyz + 0);
			ToMinecraftPoint(b, scale, triangle.xyz + 3);
			ToMinecraftPoint(c, scale, triangle.xyz + 6);
			triangle.flags = flags;
			triangles.push_back(triangle);
		}

		void PublishSurfaceTriangles(int minX, int minY, int minZ, double scale)
		{
			const auto key = std::make_tuple(minX, minY, minZ);
			if (publishedTriangleRegions.count(key))
			{
				return;
			}
			// Trace exact BSP hit points on a quarter-block grid. The triangles retain the
			// actual plane heights/normals; the old 1/8 voxels remain the wall/failure fallback.
			// Half-block surface samples are accurate enough for picking/building and
			// cost a quarter as many BSP traces as the old quarter-block grid.
			constexpr int subdivisions = 2;
			constexpr int samples = 8 * subdivisions + 1;
			struct Hit { vec3_t point; bool valid; };
			std::vector<Hit> hits(samples * samples);
			const vec3_t zero = {0.0f, 0.0f, 0.0f};
			const int mask = CONTENTS_SOLID | CONTENTS_PLAYERCLIP | CONTENTS_TERRAIN;
			for (int z = 0; z < samples; ++z)
			{
				for (int x = 0; x < samples; ++x)
				{
					vec3_t start, end;
					start[0] = end[0] = static_cast<float>((minX + x / static_cast<double>(subdivisions)) * scale);
					start[1] = end[1] = static_cast<float>(-(minZ + z / static_cast<double>(subdivisions)) * scale);
					start[2] = static_cast<float>((minY + 16.0) * scale);
					end[2] = static_cast<float>((minY - 1.0) * scale);
					trace_t trace{};
					CM_BoxTrace(&trace, start, end, zero, zero, 0, mask);
					Hit& hit = hits[z * samples + x];
					hit.valid = trace.fraction < 1.0f && !trace.startsolid;
					if (hit.valid) VectorCopy(trace.endpos, hit.point);
				}
			}

			std::vector<protocol::CollisionTriangle> triangles;
			triangles.reserve((samples - 1) * (samples - 1) * 2);
			const std::uint32_t flags = protocol::kTriangleDiggable | (3u << 8); // stone default
			auto acceptable = [scale](const Hit& a, const Hit& b, const Hit& c)
			{
				if (!a.valid || !b.valid || !c.valid) return false;
				const float low = std::min(a.point[2], std::min(b.point[2], c.point[2]));
				const float high = std::max(a.point[2], std::max(b.point[2], c.point[2]));
				return high - low <= static_cast<float>(scale * 1.5);
			};
			for (int z = 0; z < samples - 1; ++z)
			{
				for (int x = 0; x < samples - 1; ++x)
				{
					const Hit& a = hits[z * samples + x];
					const Hit& b = hits[z * samples + x + 1];
					const Hit& c = hits[(z + 1) * samples + x + 1];
					const Hit& d = hits[(z + 1) * samples + x];
					if (acceptable(a, b, c)) AddTriangle(triangles, a.point, b.point, c.point, scale, flags);
					if (acceptable(a, c, d)) AddTriangle(triangles, a.point, c.point, d.point, scale, flags);
				}
			}
			protocol::CollisionRegion region{minX, minY, minZ, minX + 7, minY + 7, minZ + 7,
				collisionEpoch, static_cast<std::uint32_t>(triangles.size())};
			std::vector<std::uint8_t> payload(sizeof(region) + triangles.size() * sizeof(protocol::CollisionTriangle));
			std::memcpy(payload.data(), &region, sizeof(region));
			if (!triangles.empty()) std::memcpy(payload.data() + sizeof(region), triangles.data(),
				triangles.size() * sizeof(protocol::CollisionTriangle));
			if (WriteCollisionMessage(protocol::kCollisionTriangles, payload.data(), static_cast<std::uint32_t>(payload.size())))
			{
				publishedTriangleRegions.insert(key);
				Com_DPrintf("JKCraft: exact surface region [%d %d %d], %u triangles\n",
					minX, minY, minZ, static_cast<unsigned>(triangles.size()));
			}
		}

		void PublishCollisionRegion(double playerX, double playerY, double playerZ, double scale, bool clear)
		{
			const int minX = RegionFloor(playerX);
			const int minY = RegionFloor(playerY - 8.0);
			const int minZ = RegionFloor(playerZ);
			const int regionY = RegionFloor(playerY);
			const auto regionKey = std::make_tuple(minX, regionY, minZ);
			if (clear)
			{
				publishedCollisionRegions.clear();
				publishedTriangleRegions.clear();
				publishedDynamicRegions.clear();
				lastDynamicSignature = 0;
			}
			else if (publishedCollisionRegions.count(regionKey))
			{
				// The shared ring may have filled after the voxel region but before one of
				// its exact-surface messages. Retry those independently without resampling.
				PublishSurfaceTriangles(minX, minY, minZ, scale);
				PublishSurfaceTriangles(minX, minY + 8, minZ, scale);
				return;
			}
			const int maxX = minX + 7;
			const int maxY = RegionFloor(playerY) + 7;
			const int maxZ = minZ + 7;

			if (clear)
			{
				WriteCollisionMessage(protocol::kCollisionClear, &collisionEpoch, sizeof(collisionEpoch));
			}
			const int sizeX = maxX - minX + 1;
			const int sizeZ = maxZ - minZ + 1;
			std::vector<protocol::CollisionBlock> sampled(
				static_cast<std::size_t>(sizeX) * (maxY - minY + 1) * sizeZ);
			auto blockAt = [&](int x, int y, int z) -> protocol::CollisionBlock&
			{
				return sampled[(static_cast<std::size_t>(y - minY) * sizeZ + (z - minZ)) * sizeX + (x - minX)];
			};
			for (int y = minY; y <= maxY; ++y)
			{
				for (int z = minZ; z <= maxZ; ++z)
				{
					for (int x = minX; x <= maxX; ++x)
					{
						protocol::CollisionBlock block{};
						block.x = x;
						block.y = y;
						block.z = z;
						for (int sy = 0; sy < 8; sy += 2)
						{
							for (int sz = 0; sz < 8; sz += 2)
							{
								for (int sx = 0; sx < 8; sx += 2)
								{
									vec3_t point;
									point[0] = static_cast<float>((x + (sx + 1.0) / 8.0) * scale);
									point[1] = static_cast<float>(-(z + (sz + 1.0) / 8.0) * scale);
									point[2] = static_cast<float>((y + (sy + 1.0) / 8.0) * scale);
									if (CM_PointContents(point, 0) & (CONTENTS_SOLID | CONTENTS_PLAYERCLIP))
									{
										for (int oy = 0; oy < 2; ++oy)
											for (int oz = 0; oz < 2; ++oz)
												for (int ox = 0; ox < 2; ++ox)
													block.bits[sy + oy] |= std::uint64_t{1} << ((sz + oz) * 8 + sx + ox);
									}
								}
							}
						}
						blockAt(x, y, z) = block;
					}
				}
			}

			// Point contents captures brush volume.  A point trace also captures thin
			// BSP/patch surfaces (terrain, ramps and curved floors) that have no solid
			// volume to sample.  Mark the 1/8-block cell immediately below each hit.
			const int collisionMask = CONTENTS_SOLID | CONTENTS_PLAYERCLIP | CONTENTS_TERRAIN;
			const vec3_t zero = {0.0f, 0.0f, 0.0f};
			for (int subZ = 0; subZ < sizeZ * 8; subZ += 2)
			{
				for (int subX = 0; subX < sizeX * 8; subX += 2)
				{
					vec3_t start, end;
					start[0] = end[0] = static_cast<float>((minX + (subX + 1.0) / 8.0) * scale);
					start[1] = end[1] = static_cast<float>(-(minZ + (subZ + 1.0) / 8.0) * scale);
					start[2] = static_cast<float>((playerY + 2.5) * scale);
					end[2] = static_cast<float>((playerY - 12.0) * scale);
					trace_t trace{};
					CM_BoxTrace(&trace, start, end, zero, zero, 0, collisionMask);
					if (trace.fraction >= 1.0f || trace.startsolid)
					{
						continue;
					}
					const double hitY = static_cast<double>(trace.endpos[2]) / scale;
					const int voxelY = static_cast<int>(std::floor(hitY * 8.0 - 0.01));
					const int blockY = static_cast<int>(std::floor(voxelY / 8.0));
					if (blockY < minY || blockY > maxY)
					{
						continue;
					}
					const int sy = voxelY - blockY * 8;
					const int blockX = minX + subX / 8;
					const int blockZ = minZ + subZ / 8;
					const int sx = subX & 7;
					const int sz = subZ & 7;
					for (int oz = 0; oz < 2; ++oz)
						for (int ox = 0; ox < 2; ++ox)
							blockAt(blockX, blockY, blockZ).bits[sy] |= std::uint64_t{1} << ((sz + oz) * 8 + sx + ox);
				}
			}

			std::vector<protocol::CollisionBlock> blocks;
			blocks.reserve(sampled.size());
			for (const auto& block : sampled)
			{
				bool occupied = false;
				for (const std::uint64_t bits : block.bits)
				{
					occupied |= bits != 0;
				}
				if (occupied)
				{
					blocks.push_back(block);
				}
			}

			protocol::CollisionRegion region{
				minX, minY, minZ, maxX, maxY, maxZ, collisionEpoch,
				static_cast<std::uint32_t>(blocks.size())};
			std::vector<std::uint8_t> payload(sizeof(region) + blocks.size() * sizeof(protocol::CollisionBlock));
			std::memcpy(payload.data(), &region, sizeof(region));
			if (!blocks.empty())
			{
				std::memcpy(payload.data() + sizeof(region), blocks.data(), blocks.size() * sizeof(protocol::CollisionBlock));
			}
			if (WriteCollisionMessage(protocol::kCollisionRegion, payload.data(), static_cast<std::uint32_t>(payload.size())))
			{
				publishedCollisionRegions.insert(regionKey);
				Com_DPrintf("JKCraft: collision region [%d %d %d]-[%d %d %d], %u occupied blocks\n",
					minX, minY, minZ, maxX, maxY, maxZ, static_cast<unsigned>(blocks.size()));
			}
			PublishSurfaceTriangles(minX, minY, minZ, scale);
			PublishSurfaceTriangles(minX, minY + 8, minZ, scale);
		}

		bool RegionHasStaticCollision(int regionX, int regionY, int regionZ, double scale)
		{
			// PublishCollisionRegion covers 8x16x8 blocks: the region containing the
			// player plus the eight blocks below it. A stationary box trace cheaply
			// rejects the large empty part of a BSP before expensive sub-voxel sampling.
			vec3_t center{
				static_cast<float>((regionX + 4.0) * scale),
				static_cast<float>(-(regionZ + 4.0) * scale),
				static_cast<float>((regionY - 0.5) * scale)};
			vec3_t mins{
				static_cast<float>(-4.05 * scale),
				static_cast<float>(-4.05 * scale),
				static_cast<float>(-8.05 * scale)};
			vec3_t maxs{
				static_cast<float>(4.05 * scale),
				static_cast<float>(4.05 * scale),
				static_cast<float>(8.05 * scale)};
			trace_t trace{};
			CM_BoxTrace(&trace, center, center, mins, maxs, 0,
				CONTENTS_SOLID | CONTENTS_PLAYERCLIP | CONTENTS_TERRAIN);
			return trace.startsolid || trace.allsolid || trace.fraction < 1.0f;
		}

		bool PublishEmptyCollisionRegion(int regionX, int regionY, int regionZ)
		{
			const auto key = std::make_tuple(regionX, regionY, regionZ);
			protocol::CollisionRegion region{
				regionX, regionY - 8, regionZ, regionX + 7, regionY + 7, regionZ + 7,
				collisionEpoch, 0u};
			if (!WriteCollisionMessage(protocol::kCollisionRegion, &region, sizeof(region)))
			{
				return false;
			}
			publishedCollisionRegions.insert(key);
			return true;
		}

		void StopFullPreload(bool completed)
		{
			if (!preloadActive && !preloadAwaitingDrain)
			{
				return;
			}
			preloadActive = false;
			preloadAwaitingDrain = false;
			Cvar_Set("jkc_preloading", "0");
			if (completed)
			{
				++teleportSeq;
				const std::uint64_t elapsed = GetTickCount64() - preloadStartedMs;
				Com_Printf("JKCraft: full map collision preload complete: %u regions in %.1f seconds\n",
					static_cast<unsigned>(preloadRegions.size()), elapsed / 1000.0);
			}
			preloadRegions.clear();
			preloadCursor = 0;
		}

		bool BeginFullPreload(double playerX, double playerY, double playerZ, double scale)
		{
			if (!fullPreload || !fullPreload->integer)
			{
				return false;
			}
			vec3_t mins, maxs;
			CM_GetWorldBounds(mins, maxs);
			int minX = RegionFloor(mins[0] / scale) - 8;
			int maxX = RegionFloor(maxs[0] / scale) + 8;
			int minY = RegionFloor(mins[2] / scale) - 8;
			int maxY = RegionFloor(maxs[2] / scale) + 8;
			int minZ = RegionFloor(-maxs[1] / scale) - 8;
			int maxZ = RegionFloor(-mins[1] / scale) + 8;
			// Match JKCraft's configured Minecraft dimension and reject corrupt BSP bounds.
			minY = std::max(minY, -2024);
			maxY = std::min(maxY, 2024);
			const std::uint64_t nx = maxX >= minX ? static_cast<std::uint64_t>((maxX - minX) / 8 + 1) : 0;
			const std::uint64_t ny = maxY >= minY ? static_cast<std::uint64_t>((maxY - minY) / 8 + 1) : 0;
			const std::uint64_t nz = maxZ >= minZ ? static_cast<std::uint64_t>((maxZ - minZ) / 8 + 1) : 0;
			const std::uint64_t count = nx * ny * nz;
			const std::uint64_t limit = preloadMaxRegions && preloadMaxRegions->integer > 0
				? static_cast<std::uint64_t>(preloadMaxRegions->integer) : 60000u;
			if (!count || count > limit)
			{
				Com_Printf(S_COLOR_YELLOW "JKCraft: full preload skipped: BSP bounds need %llu regions (limit %llu); using streaming\n",
					static_cast<unsigned long long>(count), static_cast<unsigned long long>(limit));
				return false;
			}

			publishedCollisionRegions.clear();
			publishedTriangleRegions.clear();
			publishedDynamicRegions.clear();
			lastDynamicSignature = 0;
			if (!WriteCollisionMessage(protocol::kCollisionClear, &collisionEpoch, sizeof(collisionEpoch)))
			{
				return false;
			}
			preloadRegions.clear();
			preloadRegions.reserve(static_cast<std::size_t>(count));
			for (int y = minY; y <= maxY; y += 8)
				for (int z = minZ; z <= maxZ; z += 8)
					for (int x = minX; x <= maxX; x += 8)
						preloadRegions.emplace_back(x, y, z);
			std::sort(preloadRegions.begin(), preloadRegions.end(), [=](const auto& a, const auto& b)
			{
				auto distance = [=](const auto& region)
				{
					const double dx = std::get<0>(region) + 4.0 - playerX;
					const double dy = std::get<1>(region) - playerY;
					const double dz = std::get<2>(region) + 4.0 - playerZ;
					return dx * dx + dy * dy + dz * dz;
				};
				return distance(a) < distance(b);
			});
			preloadCursor = 0;
			preloadLastPercent = -1;
			preloadStartedMs = GetTickCount64();
			preloadActive = true;
			preloadAwaitingDrain = false;
			Cvar_Set("jkc_preloading", "1");
			Com_Printf("JKCraft: full map collision preload started: %llu regions, bounds [%d %d %d]-[%d %d %d]\n",
				static_cast<unsigned long long>(count), minX, minY, minZ, maxX, maxY, maxZ);
			return true;
		}

		void StepFullPreload(double scale)
		{
			if (!preloadActive)
			{
				return;
			}
			if (preloadAwaitingDrain)
			{
				auto* ring = MappingBase() + protocol::kCollisionRingOffset;
				const std::uint64_t head = Read64(reinterpret_cast<std::uint64_t*>(ring + protocol::kCollisionRingHeadOffset));
				const std::uint64_t tail = Read64(reinterpret_cast<std::uint64_t*>(ring + protocol::kCollisionRingTailOffset));
				if (tail >= head)
				{
					StopFullPreload(true);
				}
				return;
			}
			const std::uint64_t started = GetTickCount64();
			const std::uint64_t budget = preloadBudgetMs && preloadBudgetMs->integer > 0
				? static_cast<std::uint64_t>(preloadBudgetMs->integer) : 12u;
			do
			{
				const auto region = preloadRegions[preloadCursor];
				const int x = std::get<0>(region), y = std::get<1>(region), z = std::get<2>(region);
				const auto key = std::make_tuple(x, y, z);
				bool needsTriangles = false;
				if (!publishedCollisionRegions.count(key) ||
					!publishedTriangleRegions.count(std::make_tuple(x, y - 8, z)) ||
					!publishedTriangleRegions.count(std::make_tuple(x, y, z)))
				{
					needsTriangles = RegionHasStaticCollision(x, y, z, scale);
					if (needsTriangles)
					{
						PublishCollisionRegion(x + 0.5, y + 0.5, z + 0.5, scale, false);
					}
					else
					{
						PublishEmptyCollisionRegion(x, y, z);
					}
					if (!publishedCollisionRegions.count(key) ||
						(needsTriangles &&
							(!publishedTriangleRegions.count(std::make_tuple(x, y - 8, z)) ||
							 !publishedTriangleRegions.count(std::make_tuple(x, y, z)))))
					{
						break; // shared ring is full; let Minecraft catch up
					}
				}
				++preloadCursor;
				const int percent = static_cast<int>(preloadCursor * 100 / preloadRegions.size());
				if (percent / 10 != preloadLastPercent / 10)
				{
					preloadLastPercent = percent;
					Com_Printf("JKCraft: full map collision preload %d%% (%u/%u)\n", percent,
						static_cast<unsigned>(preloadCursor), static_cast<unsigned>(preloadRegions.size()));
				}
				if (preloadCursor >= preloadRegions.size())
				{
					preloadAwaitingDrain = true;
					Com_Printf("JKCraft: full map collision preload 100%%; waiting for Minecraft\n");
					break;
				}
			} while (GetTickCount64() - started < budget);
		}

		void DirectTrajectory(const trajectory_t& tr, int atTime, vec3_t out)
		{
			VectorCopy(tr.trBase, out);
			if (tr.trType == TR_STATIONARY || tr.trType == TR_INTERPOLATE) return;
			if (tr.trType == TR_SINE && tr.trDuration > 0)
			{
				const float phase = std::sin((atTime - tr.trTime) * (2.0f * static_cast<float>(M_PI) / tr.trDuration));
				VectorMA(out, phase, tr.trDelta, out);
				return;
			}
			const int elapsed = tr.trType == TR_LINEAR_STOP || tr.trType == TR_NONLINEAR_STOP
				? std::max(0, std::min(atTime - tr.trTime, tr.trDuration)) : atTime - tr.trTime;
			float seconds = elapsed * 0.001f;
			if (tr.trType == TR_NONLINEAR_STOP && tr.trDuration > 0)
			{
				seconds = tr.trDuration * 0.001f *
					std::sin((static_cast<float>(elapsed) / tr.trDuration) * (static_cast<float>(M_PI) * 0.5f));
			}
			VectorMA(out, seconds, tr.trDelta, out);
		}

		MoverPose CurrentMoverPose(const entityState_t& entity)
		{
			MoverPose pose{};
			DirectTrajectory(entity.pos, cl.serverTime, pose.origin);
			DirectTrajectory(entity.apos, cl.serverTime, pose.angles);
			pose.modelindex = entity.modelindex;
			return pose;
		}

		void DirectTrace(trace_t& result, const vec3_t start, const vec3_t end,
			const vec3_t mins, const vec3_t maxs, int mask, int ignoredEntity = -1)
		{
			CM_BoxTrace(&result, start, end, mins, maxs, 0, mask);
			for (int i = 0; i < cl.frame.numEntities; ++i)
			{
				const entityState_t& entity = cl.parseEntities[(cl.frame.parseEntitiesNum + i) & (MAX_PARSE_ENTITIES - 1)];
				if (entity.number == ignoredEntity || entity.solid != SOLID_BMODEL ||
					entity.modelindex <= 0 || entity.modelindex >= CM_NumInlineModels())
				{
					continue;
				}
				const clipHandle_t model = CM_InlineModel(entity.modelindex);
				if (!(CM_ModelContents(model, -1) & mask))
				{
					continue;
				}
				trace_t candidate{};
				const MoverPose pose = CurrentMoverPose(entity);
				CM_TransformedBoxTrace(&candidate, start, end, mins, maxs, model, mask,
					pose.origin, pose.angles);
				if ((candidate.startsolid && !result.startsolid) || candidate.allsolid || candidate.fraction < result.fraction)
				{
					result = candidate;
				}
			}
		}

		int CarryByMover(const vec3_t start, const vec3_t mins, const vec3_t maxs,
			vec3_t carry)
		{
			VectorClear(carry);
			std::map<int, MoverPose> current;
			int supportingEntity = -1;
			float nearestGround = 1.0f;
			for (int i = 0; i < cl.frame.numEntities; ++i)
			{
				const entityState_t& entity = cl.parseEntities[(cl.frame.parseEntitiesNum + i) & (MAX_PARSE_ENTITIES - 1)];
				if (entity.solid != SOLID_BMODEL || entity.modelindex <= 0 ||
					entity.modelindex >= CM_NumInlineModels()) continue;
				const clipHandle_t model = CM_InlineModel(entity.modelindex);
				if (!(CM_ModelContents(model, -1) & (CONTENTS_SOLID | CONTENTS_PLAYERCLIP))) continue;
				const MoverPose now = CurrentMoverPose(entity);
				current[entity.number] = now;
				const auto previous = previousMoverPoses.find(entity.number);
				if (previous == previousMoverPoses.end() || previous->second.modelindex != now.modelindex) continue;
				const MoverPose& old = previous->second;
				vec3_t displacement;
				VectorSubtract(now.origin, old.origin, displacement);
				if (VectorLengthSquared(displacement) < 0.0001f || VectorLengthSquared(displacement) > 4096.0f ||
					std::abs(now.angles[0] - old.angles[0]) > 1.0f ||
					std::abs(now.angles[1] - old.angles[1]) > 1.0f ||
					std::abs(now.angles[2] - old.angles[2]) > 1.0f) continue;
				vec3_t below;
				VectorCopy(start, below);
				below[2] -= 8.0f;
				trace_t ground{};
				CM_TransformedBoxTrace(&ground, start, below, mins, maxs, model,
					CONTENTS_SOLID | CONTENTS_PLAYERCLIP, old.origin, old.angles);
				if (!ground.startsolid && !ground.allsolid && ground.fraction < nearestGround &&
					ground.plane.normal[2] >= 0.65f)
				{
					nearestGround = ground.fraction;
					supportingEntity = entity.number;
					VectorCopy(displacement, carry);
				}
			}
			previousMoverPoses.swap(current);
			if (supportingEntity < 0) return -1;
			vec3_t target;
			VectorAdd(start, carry, target);
			trace_t obstruction{};
			DirectTrace(obstruction, start, target, mins, maxs,
				CONTENTS_SOLID | CONTENTS_PLAYERCLIP | CONTENTS_TERRAIN, supportingEntity);
			if (obstruction.allsolid)
			{
				VectorClear(carry);
			}
			else if (obstruction.fraction < 1.0f)
			{
				VectorSubtract(obstruction.endpos, start, carry);
			}
			return supportingEntity;
		}

		struct DirectMoveResult
		{
			vec3_t end;
			vec3_t normal;
			bool hit = false;
			bool onGround = false;
			bool startSolid = false;
		};

		void DirectMoveAxis(DirectMoveResult& out, float amount, int axis,
			const vec3_t mins, const vec3_t maxs)
		{
			if (std::abs(amount) <= 0.000001f)
			{
				return;
			}
			vec3_t target;
			VectorCopy(out.end, target);
			target[axis] += amount;
			trace_t trace{};
			const int mask = CONTENTS_SOLID | CONTENTS_PLAYERCLIP | CONTENTS_TERRAIN;
			DirectTrace(trace, out.end, target, mins, maxs, mask);
			out.startSolid |= trace.startsolid || trace.allsolid;
			if (!trace.allsolid && trace.fraction > 0.0f)
			{
				VectorCopy(trace.endpos, out.end);
			}
			if (trace.allsolid || trace.fraction < 1.0f)
			{
				out.hit = true;
				VectorCopy(trace.plane.normal, out.normal);
				if (axis == 2 && amount < 0.0f && trace.plane.normal[2] >= 0.65f)
				{
					out.onGround = true;
				}
			}
		}

		DirectMoveResult DirectSlideHorizontal(const vec3_t start, float dx, float dy,
			const vec3_t mins, const vec3_t maxs)
		{
			DirectMoveResult out{};
			VectorCopy(start, out.end);
			VectorClear(out.normal);
			vec3_t target{start[0] + dx, start[1] + dy, start[2]};
			trace_t first{};
			const int mask = CONTENTS_SOLID | CONTENTS_PLAYERCLIP | CONTENTS_TERRAIN;
			DirectTrace(first, start, target, mins, maxs, mask);
			out.startSolid = first.startsolid || first.allsolid;
			if (!first.allsolid && first.fraction > 0.0f)
			{
				VectorCopy(first.endpos, out.end);
			}
			if (!first.allsolid && first.fraction >= 1.0f)
			{
				return out;
			}
			out.hit = true;
			VectorCopy(first.plane.normal, out.normal);
			const float nx = first.plane.normal[0];
			const float ny = first.plane.normal[1];
			const float horizontalNormal = nx * nx + ny * ny;
			// Only a wall may steer horizontal movement.  Floors and walkable slopes can have a
			// small horizontal component at BSP seams; projecting onto that component produces
			// the apparently random sideways pull seen on t1_sour's otherwise flat floor.
			if (first.allsolid || horizontalNormal < 0.01f || std::abs(first.plane.normal[2]) >= 0.65f)
			{
				return out;
			}
			// Preserve the untravelled part of the requested step, remove only its component
			// into the BSP plane, and trace the tangent. This is the normal Quake/Jedi wall
			// slide and avoids catching the rectangular Minecraft hull on diagonal details.
			const float remainX = dx * (1.0f - first.fraction);
			const float remainY = dy * (1.0f - first.fraction);
			const float into = (remainX * nx + remainY * ny) / horizontalNormal;
			vec3_t slideTarget{
				out.end[0] + remainX - nx * into,
				out.end[1] + remainY - ny * into,
				out.end[2]};
			trace_t second{};
			DirectTrace(second, out.end, slideTarget, mins, maxs, mask);
			out.startSolid |= second.startsolid || second.allsolid;
			if (!second.allsolid && second.fraction > 0.0f)
			{
				VectorCopy(second.endpos, out.end);
			}
			return out;
		}

		DirectMoveResult DirectMinecraftAxes(const vec3_t start, const vec3_t delta,
			const vec3_t mins, const vec3_t maxs)
		{
			DirectMoveResult out{};
			VectorCopy(start, out.end);
			VectorClear(out.normal);
			// This is Direction.axisStepOrder from Minecraft 26.3: Y first, then the
			// smaller horizontal component, then the larger one.
			DirectMoveAxis(out, delta[2], 2, mins, maxs);
			vec3_t horizontalMins;
			VectorCopy(mins, horizontalMins);
			// A box resting exactly on a Quake BSP plane can catch a sub-unit bevel or a
			// neighbouring brush seam during a purely horizontal trace.  Ignore only the
			// lowest JA unit for horizontal sweeps (1/32 of a Minecraft block).  Vertical
			// traces still use the complete hull, so floors, falls and real steps stay solid.
			horizontalMins[2] = std::min(maxs[2] - 0.01f, mins[2] + 1.0f);
			if (std::abs(delta[0]) < std::abs(delta[1]))
			{
				DirectMoveAxis(out, delta[1], 1, horizontalMins, maxs);
				DirectMoveAxis(out, delta[0], 0, horizontalMins, maxs);
			}
			else
			{
				DirectMoveAxis(out, delta[0], 0, horizontalMins, maxs);
				DirectMoveAxis(out, delta[1], 1, horizontalMins, maxs);
			}
			vec3_t horizontalStart{start[0], start[1], out.end[2]};
			DirectMoveResult slide = DirectSlideHorizontal(horizontalStart, delta[0], delta[1], horizontalMins, maxs);
			const float axesDx = out.end[0] - horizontalStart[0];
			const float axesDy = out.end[1] - horizontalStart[1];
			const float slideDx = slide.end[0] - horizontalStart[0];
			const float slideDy = slide.end[1] - horizontalStart[1];
			if (slideDx * slideDx + slideDy * slideDy > axesDx * axesDx + axesDy * axesDy + 0.0001f)
			{
				slide.end[2] = out.end[2];
				slide.onGround = out.onGround;
				slide.startSolid |= out.startSolid;
				out = slide;
			}
			return out;
		}

		DirectMoveResult DirectMinecraftMove(const vec3_t start, const vec3_t delta,
			const vec3_t mins, const vec3_t maxs, float stepHeight, bool wasGrounded)
		{
			DirectMoveResult flat = DirectMinecraftAxes(start, delta, mins, maxs);
			const float flatDx = flat.end[0] - start[0];
			const float flatDy = flat.end[1] - start[1];
			const bool horizontalClipped = std::abs(flatDx - delta[0]) > 0.0001f ||
				std::abs(flatDy - delta[1]) > 0.0001f;
			const bool landed = delta[2] < 0.0f && std::abs((flat.end[2] - start[2]) - delta[2]) > 0.0001f;
			if (stepHeight <= 0.0f || !(wasGrounded || landed) || !horizontalClipped)
			{
				return flat;
			}

			// Minecraft tests candidate step heights after its ordinary axis pass. BSP
			// surfaces do not expose a finite list of voxel Y coordinates, so try the
			// configured maximum, move horizontally, then settle back onto the exact BSP.
			DirectMoveResult stepped{};
			VectorCopy(start, stepped.end);
			VectorClear(stepped.normal);
			if (landed)
			{
				stepped.end[2] = flat.end[2];
			}
			const float stepBaseZ = stepped.end[2];
			DirectMoveAxis(stepped, stepHeight, 2, mins, maxs);
			const float climbed = stepped.end[2] - stepBaseZ;
			if (climbed <= 0.0001f)
			{
				return flat;
			}
			if (std::abs(delta[0]) < std::abs(delta[1]))
			{
				DirectMoveAxis(stepped, delta[1], 1, mins, maxs);
				DirectMoveAxis(stepped, delta[0], 0, mins, maxs);
			}
			else
			{
				DirectMoveAxis(stepped, delta[0], 0, mins, maxs);
				DirectMoveAxis(stepped, delta[1], 1, mins, maxs);
			}
			DirectMoveAxis(stepped, -climbed, 2, mins, maxs);

			const float flatX = flat.end[0] - start[0];
			const float flatY2 = flat.end[1] - start[1];
			const float stepX = stepped.end[0] - start[0];
			const float stepY = stepped.end[1] - start[1];
			const float flatDistance = flatX * flatX + flatY2 * flatY2;
			const float stepDistance = stepX * stepX + stepY * stepY;
			return stepDistance > flatDistance + 0.01f ? stepped : flat;
		}

		void ProcessDirectQueries(std::uint32_t worldId, double scale)
		{
			if (!directCollision || !directCollision->integer || !header || !worldId)
			{
				return;
			}
			if (moverWorldId != worldId)
			{
				previousMoverPoses.clear();
				moverWorldId = worldId;
			}
			auto* move = reinterpret_cast<protocol::DirectMove*>(MappingBase() + protocol::kDirectMoveOffset);
			auto* moverCarry = reinterpret_cast<protocol::MoverCarry*>(MappingBase() + protocol::kMoverCarryOffset);
			const std::uint32_t moveRequest = Read32(&move->requestSeq);
			if (moveRequest && moveRequest != Read32(&move->responseSeq))
			{
				MemoryBarrier();
				vec3_t start{
					static_cast<float>(move->startX * scale),
					static_cast<float>(-move->startZ * scale),
					static_cast<float>(move->startY * scale)};
				vec3_t delta{
					static_cast<float>(move->deltaX * scale),
					static_cast<float>(-move->deltaZ * scale),
					static_cast<float>(move->deltaY * scale)};
				const float half = std::max(0.05f, std::min(move->halfWidth, 4.0f)) * static_cast<float>(scale);
				const float height = std::max(0.1f, std::min(move->height, 8.0f)) * static_cast<float>(scale);
				vec3_t mins{-half, -half, 0.0f};
				vec3_t maxs{half, half, height};
				vec3_t carry{0.0f, 0.0f, 0.0f};
				vec3_t movedStart;
				VectorCopy(start, movedStart);
				int supportingEntity = -1;
				DirectMoveResult result{};
				if (move->worldId == worldId)
				{
					supportingEntity = CarryByMover(start, mins, maxs, carry);
					VectorAdd(start, carry, movedStart);
					result = DirectMinecraftMove(movedStart, delta, mins, maxs,
						std::max(0.0f, std::min(move->stepHeight, 2.0f)) * static_cast<float>(scale),
						(move->flags & protocol::kDirectOnGround) != 0);
				}
				else
				{
					VectorCopy(start, result.end);
				}
				const float resultDx = result.end[0] - start[0];
				const float resultDy = result.end[1] - start[1];
				const float resultDz = result.end[2] - start[2];
				// Minecraft decides horizontalCollision with a 1e-5-block tolerance.  OpenJK's
				// collision model is float-based, so at ordinary JA world coordinates an entirely
				// unobstructed double -> float -> double round trip can exceed that tolerance.  It
				// then intermittently cancels sprint and velocity despite a clear trace.  Echo the
				// original double exactly whenever an axis differs only by sub-unit CM precision;
				// real clipping remains many times larger and keeps the traced result.
				constexpr float kRoundTripEpsilon = 0.03125f;
				moverCarry->x = carry[0] / scale;
				moverCarry->y = carry[2] / scale;
				moverCarry->z = -carry[1] / scale;
				moverCarry->entityNum = supportingEntity;
				moverCarry->reserved = 0;
				move->resultX = std::abs(resultDx - carry[0] - delta[0]) < kRoundTripEpsilon
					? move->deltaX + moverCarry->x : resultDx / scale;
				move->resultY = std::abs(resultDz - carry[2] - delta[2]) < kRoundTripEpsilon
					? move->deltaY + moverCarry->y : resultDz / scale;
				move->resultZ = std::abs(resultDy - carry[1] - delta[1]) < kRoundTripEpsilon
					? move->deltaZ + moverCarry->z : -resultDy / scale;
				move->normalX = result.normal[0];
				move->normalY = result.normal[2];
				move->normalZ = -result.normal[1];
				move->resultFlags = (result.hit ? protocol::kDirectHit : 0u)
					| (result.onGround ? protocol::kDirectOnGround : 0u)
					| (result.startSolid ? protocol::kDirectStartSolid : 0u);
				const float requestedHorizontal = delta[0] * delta[0] + delta[1] * delta[1];
				const float resultHorizontal = (result.end[0] - movedStart[0]) * (result.end[0] - movedStart[0])
					+ (result.end[1] - movedStart[1]) * (result.end[1] - movedStart[1]);
				const std::uint64_t now = GetTickCount64();
				if (requestedHorizontal > 0.01f && resultHorizontal + 0.01f < requestedHorizontal * 0.81f
					&& now - lastDirectClipLogMs >= 250u)
				{
					lastDirectClipLogMs = now;
					Com_Printf("JKCraft: direct horizontal clip at %.1f %.1f %.1f request %.1f %.1f result %.1f %.1f normal %.2f %.2f %.2f flags 0x%X\n",
						start[0], start[1], start[2], delta[0], delta[1],
						result.end[0] - start[0], result.end[1] - start[1],
						result.normal[0], result.normal[1], result.normal[2], move->resultFlags);
				}
				MemoryBarrier();
				InterlockedExchange(reinterpret_cast<volatile LONG*>(&move->responseSeq), static_cast<LONG>(moveRequest));
			}

			auto* ray = reinterpret_cast<protocol::DirectRay*>(MappingBase() + protocol::kDirectRayOffset);
			auto* mobGround = reinterpret_cast<protocol::MobGroundBatch*>(MappingBase() + protocol::kMobGroundOffset);
			const std::uint32_t mobRequest = Read32(&mobGround->requestSeq);
			if (mobRequest && mobRequest != Read32(&mobGround->responseSeq))
			{
				MemoryBarrier();
				const std::uint32_t count = std::min(mobGround->count, protocol::kMaxMobGround);
				for (std::uint32_t i = 0; i < count; ++i)
				{
					auto& record = mobGround->records[i];
					// Keep the Minecraft-side classification bits while replacing only
					// the low response bits with this frame's native trace result.
					record.resultFlags &= protocol::kMobRequestMask;
					record.groundY = record.y;
					if (mobGround->worldId != worldId) continue;
					const float probeUp = std::max(0.0f, std::min(record.probeUp, 2.0f));
					vec3_t start{
						record.x * static_cast<float>(scale),
						-record.z * static_cast<float>(scale),
						(record.y + probeUp) * static_cast<float>(scale)};
					vec3_t end;
					VectorCopy(start, end);
					end[2] -= std::max(0.25f, std::min(record.probeDown, 8.0f)) * static_cast<float>(scale);
					const float half = std::max(0.05f, std::min(record.halfWidth, 4.0f)) * static_cast<float>(scale);
					const float height = std::max(0.1f, std::min(record.height, 8.0f)) * static_cast<float>(scale);
					vec3_t mins{-half, -half, 0.0f};
					vec3_t maxs{half, half, height};
					trace_t trace{};
					DirectTrace(trace, start, end, mins, maxs,
						CONTENTS_SOLID | CONTENTS_PLAYERCLIP | CONTENTS_TERRAIN);
					if (trace.startsolid || trace.allsolid)
					{
						record.resultFlags |= protocol::kMobGroundStartSolid;
					}
					if (!trace.allsolid && trace.fraction < 1.0f && trace.plane.normal[2] >= 0.45f)
					{
						record.groundY = trace.endpos[2] / static_cast<float>(scale);
						record.resultFlags |= protocol::kMobGroundHit;
					}
				}
				MemoryBarrier();
				InterlockedExchange(reinterpret_cast<volatile LONG*>(&mobGround->responseSeq), static_cast<LONG>(mobRequest));
			}

			const std::uint32_t rayRequest = Read32(&ray->requestSeq);
			if (rayRequest && rayRequest != Read32(&ray->responseSeq))
			{
				MemoryBarrier();
				vec3_t from{
					static_cast<float>(ray->fromX * scale),
					static_cast<float>(-ray->fromZ * scale),
					static_cast<float>(ray->fromY * scale)};
				vec3_t to{
					static_cast<float>(ray->toX * scale),
					static_cast<float>(-ray->toZ * scale),
					static_cast<float>(ray->toY * scale)};
				trace_t trace{};
				const vec3_t zero{0.0f, 0.0f, 0.0f};
				if (ray->worldId == worldId)
				{
					// Playerclip blocks walking, not shots. Match Jedi Academy's
					// projectile mask so arrows pass its invisible traversal barriers.
					// Mirror MASK_SHOT from game/bg_public.h; the engine executable
					// does not include that game-only convenience macro.
					const int shotMask = CONTENTS_SOLID | CONTENTS_BODY | CONTENTS_CORPSE |
						CONTENTS_SHOTCLIP | CONTENTS_TERRAIN;
					const int mask = (ray->flags & protocol::kDirectRayShot)
						? shotMask : CONTENTS_SOLID | CONTENTS_PLAYERCLIP | CONTENTS_TERRAIN;
					DirectTrace(trace, from, to, zero, zero,
						mask);
				}
				else
				{
					trace.fraction = 1.0f;
					VectorCopy(to, trace.endpos);
				}
				ray->hitX = trace.endpos[0] / scale;
				ray->hitY = trace.endpos[2] / scale;
				ray->hitZ = -trace.endpos[1] / scale;
				ray->normalX = trace.plane.normal[0];
				ray->normalY = trace.plane.normal[2];
				ray->normalZ = -trace.plane.normal[1];
				ray->fraction = trace.fraction;
				ray->resultFlags = trace.fraction < 1.0f ? protocol::kDirectHit : 0u;
				ray->material = 3u; // stone fallback; map destruction is disabled in JKCraft
				MemoryBarrier();
				InterlockedExchange(reinterpret_cast<volatile LONG*>(&ray->responseSeq), static_cast<LONG>(rayRequest));
			}
		}

		void BoxTriangles(const vec3_t mins, const vec3_t maxs, const vec3_t origin,
			const vec3_t angles, double scale, std::vector<protocol::CollisionTriangle>& out)
		{
			vec3_t axis[3];
			AnglesToAxis(angles, axis);
			vec3_t corner[8];
			for (int i = 0; i < 8; ++i)
			{
				VectorCopy(origin, corner[i]);
				VectorMA(corner[i], (i & 1) ? maxs[0] : mins[0], axis[0], corner[i]);
				VectorMA(corner[i], (i & 2) ? maxs[1] : mins[1], axis[1], corner[i]);
				VectorMA(corner[i], (i & 4) ? maxs[2] : mins[2], axis[2], corner[i]);
			}
			static const int faces[6][4] = {
				{0, 4, 6, 2}, {1, 3, 7, 5}, {0, 1, 5, 4},
				{2, 6, 7, 3}, {0, 2, 3, 1}, {4, 5, 7, 6}};
			for (const auto& face : faces)
			{
				AddTriangle(out, corner[face[0]], corner[face[1]], corner[face[2]], scale, 0);
				AddTriangle(out, corner[face[0]], corner[face[2]], corner[face[3]], scale, 0);
			}
		}

		void PublishDynamicBrushes(double scale)
		{
			const std::uint64_t now = GetTickCount64();
			if (now - lastDynamicUpdateMs < 50) return;
			lastDynamicUpdateMs = now;
			std::uint64_t signature = 1469598103934665603ull;
			struct Brush { vec3_t mins, maxs, origin, angles; };
			std::vector<Brush> brushes;
			for (int i = 0; i < cl.frame.numEntities; ++i)
			{
				const entityState_t& entity = cl.parseEntities[(cl.frame.parseEntitiesNum + i) & (MAX_PARSE_ENTITIES - 1)];
				if (entity.solid != SOLID_BMODEL || entity.modelindex <= 0 ||
					entity.modelindex >= CM_NumInlineModels())
				{
					continue;
				}
				const clipHandle_t handle = CM_InlineModel(entity.modelindex);
				if (!(CM_ModelContents(handle, -1) & (CONTENTS_SOLID | CONTENTS_PLAYERCLIP))) continue;
				clipMap_t* local = nullptr;
				cmodel_t* model = CM_ClipHandleToModel(handle, &local);
				if (!model) continue;
				Brush brush{};
				VectorCopy(model->mins, brush.mins);
				VectorCopy(model->maxs, brush.maxs);
				const MoverPose pose = CurrentMoverPose(entity);
				VectorCopy(pose.origin, brush.origin);
				VectorCopy(pose.angles, brush.angles);
				brushes.push_back(brush);
				const int values[] = {entity.number, entity.modelindex,
					static_cast<int>(std::lround(brush.origin[0] * 8.0f)),
					static_cast<int>(std::lround(brush.origin[1] * 8.0f)),
					static_cast<int>(std::lround(brush.origin[2] * 8.0f)),
					static_cast<int>(std::lround(brush.angles[0] * 4.0f)),
					static_cast<int>(std::lround(brush.angles[1] * 4.0f)),
					static_cast<int>(std::lround(brush.angles[2] * 4.0f))};
				for (int value : values) signature = (signature ^ static_cast<std::uint32_t>(value)) * 1099511628211ull;
			}
			if (signature == lastDynamicSignature) return;
			lastDynamicSignature = signature;

			using Key = std::tuple<int, int, int>;
			std::map<Key, std::vector<protocol::CollisionTriangle>> regions;
			for (const Brush& brush : brushes)
			{
				std::vector<protocol::CollisionTriangle> triangles;
				BoxTriangles(brush.mins, brush.maxs, brush.origin, brush.angles, scale, triangles);
				float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
				for (const auto& triangle : triangles) for (int v = 0; v < 9; v += 3) for (int a = 0; a < 3; ++a)
				{
					lo[a] = std::min(lo[a], triangle.xyz[v + a]); hi[a] = std::max(hi[a], triangle.xyz[v + a]);
				}
				int emitted = 0;
				for (int rx = RegionFloor(lo[0]); rx <= RegionFloor(hi[0]) && emitted < 64; rx += 8)
					for (int ry = RegionFloor(lo[1]); ry <= RegionFloor(hi[1]) && emitted < 64; ry += 8)
						for (int rz = RegionFloor(lo[2]); rz <= RegionFloor(hi[2]) && emitted < 64; rz += 8, ++emitted)
							regions[Key(rx, ry, rz)].insert(regions[Key(rx, ry, rz)].end(), triangles.begin(), triangles.end());
			}

			std::set<Key> all = publishedDynamicRegions;
			for (const auto& item : regions) all.insert(item.first);
			for (const Key& key : all)
			{
				const auto found = regions.find(key);
				const std::vector<protocol::CollisionTriangle>* triangles = found == regions.end() ? nullptr : &found->second;
				protocol::CollisionRegion region{std::get<0>(key), std::get<1>(key), std::get<2>(key),
					std::get<0>(key) + 7, std::get<1>(key) + 7, std::get<2>(key) + 7, collisionEpoch,
					triangles ? static_cast<std::uint32_t>(triangles->size()) : 0u};
				const std::size_t bytes = triangles ? triangles->size() * sizeof(protocol::CollisionTriangle) : 0;
				std::vector<std::uint8_t> payload(sizeof(region) + bytes);
				std::memcpy(payload.data(), &region, sizeof(region));
				if (bytes) std::memcpy(payload.data() + sizeof(region), triangles->data(), bytes);
				WriteCollisionMessage(protocol::kCollisionDynamicTriangles, payload.data(), static_cast<std::uint32_t>(payload.size()));
			}
			publishedDynamicRegions.clear();
			for (const auto& item : regions) publishedDynamicRegions.insert(item.first);
			if (!brushes.empty()) Com_DPrintf("JKCraft: %u dynamic brush models published in %u regions\n",
				static_cast<unsigned>(brushes.size()), static_cast<unsigned>(regions.size()));
		}

		void EnsureCollisionAround(double playerX, double playerY, double playerZ, double scale)
		{
			PublishCollisionRegion(playerX, playerY, playerZ, scale, false);
			const int regionX = RegionFloor(playerX);
			const int regionY = RegionFloor(playerY);
			const int regionZ = RegionFloor(playerZ);
			// Generate adjacent collision regions well before the player reaches an
			// edge. This moves the expensive JA collision sampling away from the
			// exact crossing where a visible hitch could also make the player fall.
			constexpr double kPrefetchMargin = 3.0;
			const bool west = playerX - regionX < kPrefetchMargin;
			const bool east = regionX + 8.0 - playerX < kPrefetchMargin;
			const bool north = playerZ - regionZ < kPrefetchMargin;
			const bool south = regionZ + 8.0 - playerZ < kPrefetchMargin;
			// The Minecraft safety hold refuses to enter an unpublished collision region.
			// Without vertical prefetch that creates a deadlock at y=8,16,...: OpenJK
			// waits for Minecraft to cross the boundary while Minecraft waits for OpenJK
			// to publish it. Prepare the region above before a jump/lift reaches it.
			const bool up = regionY + 8.0 - playerY < 4.0;
			if (up) PublishCollisionRegion(playerX, playerY + 8.0, playerZ, scale, false);
			if (west) PublishCollisionRegion(playerX - 8.0, playerY, playerZ, scale, false);
			if (east) PublishCollisionRegion(playerX + 8.0, playerY, playerZ, scale, false);
			if (north) PublishCollisionRegion(playerX, playerY, playerZ - 8.0, scale, false);
			if (south) PublishCollisionRegion(playerX, playerY, playerZ + 8.0, scale, false);
			if (west && north) PublishCollisionRegion(playerX - 8.0, playerY, playerZ - 8.0, scale, false);
			if (west && south) PublishCollisionRegion(playerX - 8.0, playerY, playerZ + 8.0, scale, false);
			if (east && north) PublishCollisionRegion(playerX + 8.0, playerY, playerZ - 8.0, scale, false);
			if (east && south) PublishCollisionRegion(playerX + 8.0, playerY, playerZ + 8.0, scale, false);
		}

		void ClearWaterGrid();

		void PublishWaterGrid(double playerX, double playerY, double playerZ, double scale, std::uint32_t worldId)
		{
			vec3_t feetPoint{
				static_cast<float>(playerX * scale),
				static_cast<float>(-playerZ * scale),
				static_cast<float>((playerY + 0.10) * scale)};
			vec3_t bodyPoint;
			VectorCopy(feetPoint, bodyPoint);
			bodyPoint[2] = static_cast<float>((playerY + 0.90) * scale);
			vec3_t headPoint;
			VectorCopy(feetPoint, headPoint);
			headPoint[2] = static_cast<float>((playerY + 1.55) * scale);
			// A single wet point at the feet is frequently a thin BSP water/clip overlap and
			// must not put Minecraft into swimming physics while the player is on dry ground.
			// Requiring both torso and head keeps real submersion, but rejects those false
			// positives that caused 0.005-block/tick "levitating" descents.
			const bool playerInWater = (CM_PointContents(bodyPoint, 0) & CONTENTS_WATER) != 0 &&
				(CM_PointContents(headPoint, 0) & CONTENTS_WATER) != 0;
			const bool playerInLava = (CM_PointContents(feetPoint, 0) & CONTENTS_LAVA) != 0 ||
				(CM_PointContents(bodyPoint, 0) & CONTENTS_LAVA) != 0;
			if (!playerInWater && !playerInLava)
			{
				if (lastPlayerInWater || lastWaterWorldId != 0)
				{
					ClearWaterGrid();
					Com_DPrintf("JKCraft: player left Jedi Academy fluid\n");
				}
				lastPlayerInWater = false;
				lastPlayerInLava = false;
				return;
			}
			if (!lastPlayerInWater)
			{
				Com_DPrintf("JKCraft: player entered Jedi Academy %s\n", playerInLava ? "lava" : "water");
			}
			lastPlayerInWater = true;
			// The 16x16 cache already covers a large area. Move it in four-block steps
			// instead of rebuilding 256 water traces for every single block walked.
			constexpr int kWaterGridStep = 4;
			const int cellX = static_cast<int>(std::floor(playerX / kWaterGridStep)) * kWaterGridStep;
			const int cellZ = static_cast<int>(std::floor(playerZ / kWaterGridStep)) * kWaterGridStep;
			const int originX = cellX - static_cast<int>(protocol::kWaterGridSize / 2);
			const int originZ = cellZ - static_cast<int>(protocol::kWaterGridSize / 2);
			if (originX == lastWaterOriginX && originZ == lastWaterOriginZ &&
				worldId == lastWaterWorldId && playerInLava == lastPlayerInLava) return;
			lastWaterOriginX = originX;
			lastWaterOriginZ = originZ;
			lastWaterWorldId = worldId;
			lastPlayerInLava = playerInLava;
			auto* grid = reinterpret_cast<protocol::WaterGrid*>(MappingBase() + protocol::kWaterGridOffset);
			const std::uint32_t nextSeq = (Read32(&grid->seq) + 2u) & ~1u;
			InterlockedExchange(reinterpret_cast<volatile LONG*>(&grid->seq), static_cast<LONG>(nextSeq - 1u));
			grid->originX = originX;
			grid->originZ = originZ;
			grid->worldId = worldId;
			const vec3_t zero = {0.0f, 0.0f, 0.0f};
			unsigned found = 0;
			for (unsigned z = 0; z < protocol::kWaterGridSize; ++z)
			{
				for (unsigned x = 0; x < protocol::kWaterGridSize; ++x)
				{
					vec3_t start, end;
					start[0] = end[0] = static_cast<float>((originX + x + 0.5) * scale);
					start[1] = end[1] = static_cast<float>(-(originZ + z + 0.5) * scale);
					start[2] = static_cast<float>((playerY + 32.0) * scale);
					end[2] = static_cast<float>((playerY - 32.0) * scale);
					trace_t trace{};
					CM_BoxTrace(&trace, start, end, zero, zero, 0, CONTENTS_WATER | CONTENTS_LAVA);
					float surface = -3.4e38f;
					if (trace.fraction < 1.0f && !trace.startsolid)
					{
						surface = trace.endpos[2] / static_cast<float>(scale);
						if (trace.contents & CONTENTS_LAVA) surface += protocol::kLavaSurfaceOffset;
						++found;
					}
					grid->surface[z * protocol::kWaterGridSize + x] = surface;
				}
			}
			MemoryBarrier();
			InterlockedExchange(reinterpret_cast<volatile LONG*>(&grid->seq), static_cast<LONG>(nextSeq));
			if (found) Com_DPrintf("JKCraft: water grid %d %d contains %u wet columns\n", originX, originZ, found);
		}

		void ClearWaterGrid()
		{
			auto* grid = reinterpret_cast<protocol::WaterGrid*>(MappingBase() + protocol::kWaterGridOffset);
			const std::uint32_t nextSeq = (Read32(&grid->seq) + 2u) & ~1u;
			InterlockedExchange(reinterpret_cast<volatile LONG*>(&grid->seq), static_cast<LONG>(nextSeq - 1u));
			grid->originX = 0;
			grid->originZ = 0;
			grid->worldId = 0;
			for (float& surface : grid->surface)
			{
				surface = -3.4e38f;
			}
			MemoryBarrier();
			InterlockedExchange(reinterpret_cast<volatile LONG*>(&grid->seq), static_cast<LONG>(nextSeq));
			lastWaterWorldId = 0;
			lastPlayerInWater = false;
			lastPlayerInLava = false;
		}

		std::uint32_t HashMapName(const char* name)
		{
			std::uint32_t hash = 2166136261u;
			for (const unsigned char* c = reinterpret_cast<const unsigned char*>(name); c && *c; ++c)
			{
				hash = (hash ^ *c) * 16777619u;
			}
			return hash ? hash : 1u;
		}

		void WriteHostState()
		{
			if (!header)
			{
				return;
			}

			auto* state = reinterpret_cast<protocol::HostState*>(
				reinterpret_cast<std::uint8_t*>(header) + protocol::kStateOffset);
			std::uint32_t nextSeq = state->seq + 1u;
			if ((nextSeq & 1u) == 0)
			{
				++nextSeq;
			}
			InterlockedExchange(reinterpret_cast<volatile LONG*>(&state->seq), static_cast<LONG>(nextSeq));

			const bool active = cls.state == CA_ACTIVE && cl.frame.valid;
			const bool direct = directCollision && directCollision->integer != 0;
			const bool loading = cls.state == CA_LOADING || cls.state == CA_PRIMED || (!direct && preloadActive);
			const bool menu = (Key_GetCatcher() & (KEYCATCH_UI | KEYCATCH_CONSOLE)) != 0;
			// Surveillance consoles use ps.viewEntity rather than the scripted-camera
			// cvar. Treat them as a temporary JA-owned view for input, overlay and
			// the teleport back to Minecraft when the console closes.
			const bool remoteView = active && cl.frame.ps.viewEntity > 0 &&
				cl.frame.ps.viewEntity < ENTITYNUM_WORLD;
			const bool cinematic = active &&
				((cinematicState && cinematicState->integer != 0) || remoteView ||
					CL_IsRunningInGameCinematic() || CL_InGameCinematicOnStandBy());
			const std::uint32_t worldId = active ? HashMapName(cl.mapname) : 0u;
			if (worldId != 0 && worldId != lastWorldId)
			{
				lastWorldId = worldId;
				++collisionEpoch;
				++teleportSeq;
			}
			const std::uint32_t minecraftPid = header->minecraftPid;
			if (direct != lastDirectCollision)
			{
				StopFullPreload(false);
				lastDirectCollision = direct;
				++collisionEpoch;
				++teleportSeq;
				collisionWorldId = 0;
				publishedCollisionRegions.clear();
				publishedTriangleRegions.clear();
				publishedDynamicRegions.clear();
				lastDynamicSignature = 0;
				Com_Printf("JKCraft: direct OpenJK collision %s\n", direct ? "enabled" : "disabled");
			}
			if (active && minecraftPid && minecraftPid != lastMinecraftPid)
			{
				StopFullPreload(false);
				lastMinecraftPid = minecraftPid;
				++collisionEpoch;
				++teleportSeq;
				collisionWorldId = 0;
				publishedCollisionRegions.clear();
				publishedTriangleRegions.clear();
				publishedDynamicRegions.clear();
				lastDynamicSignature = 0;
				lastWaterWorldId = 0;
				Com_Printf("JKCraft: Minecraft process changed to %u; republishing world data\n", minecraftPid);
			}
			if (lastCinematic && !cinematic && active)
			{
				// A scripted camera may also have moved the native JA player. Make Minecraft
				// rejoin that authoritative position when control is returned to the player.
				++teleportSeq;
				Key_ClearStates();
				Com_DPrintf("JKCraft: cinematic ended; returning input to Minecraft\n");
			}
			lastCinematic = cinematic;
			auto* cameraHandoff = reinterpret_cast<protocol::CameraHandoff*>(
				reinterpret_cast<std::uint8_t*>(header) + protocol::kCameraHandoffOffset);
			InterlockedExchange(reinterpret_cast<volatile LONG*>(&cameraHandoff->releaseSeq),
				static_cast<LONG>(cameraReleaseSeq));
			InterlockedExchange(reinterpret_cast<volatile LONG*>(&cameraHandoff->cinematicActive),
				cinematic ? 1 : 0);
			InterlockedExchange(reinterpret_cast<volatile LONG*>(&cameraHandoff->worldId),
				static_cast<LONG>(worldId));

			const double scale = unitsPerBlock && unitsPerBlock->value > 0.001f
				? static_cast<double>(unitsPerBlock->value) : 32.0;
			state->flags = (active ? protocol::kInGame : 0u)
				| (menu ? protocol::kMenuOpen : 0u)
				| (loading ? protocol::kLoading : 0u)
				| (cinematic ? protocol::kCinematic : 0u)
				| (direct ? protocol::kDirectCollision : 0u);
			state->worldId = worldId;
			state->collisionEpoch = collisionEpoch;
			state->posX = active ? static_cast<double>(cl.frame.ps.origin[0]) / scale : 0.0;
			state->posY = active ? static_cast<double>(cl.frame.ps.origin[2] - 24.0f) / scale : 0.0;
			state->posZ = active ? -static_cast<double>(cl.frame.ps.origin[1]) / scale : 0.0;
			// Minecraft and OpenJK use opposite handedness for horizontal rotation:
			// MC yaw 0 (+Z) is OpenJK yaw -90 (-Y), hence -(yaw) - 90 both ways.
			state->yaw = active ? AngleNormalize360(-cl.viewangles[YAW] - 90.0f) : 0.0f;
			state->pitch = active ? cl.viewangles[PITCH] : 0.0f;
			state->teleportSeq = teleportSeq;
			state->viewportW = cls.glconfig.vidWidth > 0 ? static_cast<std::uint32_t>(cls.glconfig.vidWidth) : 0u;
			state->viewportH = cls.glconfig.vidHeight > 0 ? static_cast<std::uint32_t>(cls.glconfig.vidHeight) : 0u;
			state->gameHour = 12.0f;

			MemoryBarrier();
			InterlockedExchange(reinterpret_cast<volatile LONG*>(&state->seq), static_cast<LONG>(nextSeq + 1u));

			if (active && minecraftPid && MinecraftAlive(GetTickCount64()) && worldId != collisionWorldId)
			{
				collisionWorldId = worldId;
				if (direct)
				{
					// Direct mode never mirrors BSP collision into Minecraft. Clear anything
					// left by a previous streamed/preloaded session exactly once per map.
					WriteCollisionMessage(protocol::kCollisionClear, &collisionEpoch, sizeof(collisionEpoch));
					ClearWaterGrid();
					publishedCollisionRegions.clear();
					publishedTriangleRegions.clear();
					publishedDynamicRegions.clear();
				}
				else if (!BeginFullPreload(state->posX, state->posY, state->posZ, scale))
				{
					PublishCollisionRegion(state->posX, state->posY, state->posZ, scale, true);
				}
			}
			else if (!active)
			{
				StopFullPreload(false);
				collisionWorldId = 0;
				publishedCollisionRegions.clear();
				publishedTriangleRegions.clear();
				publishedDynamicRegions.clear();
				lastDynamicSignature = 0;
				lastWaterWorldId = 0;
			}

			protocol::MinecraftState minecraft{};
			if (active && ReadMinecraftState(minecraft) && (minecraft.flags & protocol::kMinecraftInWorld))
			{
				if (direct)
				{
					ProcessDirectQueries(worldId, scale);
					// Fluid semantics need a tiny local height cache because Minecraft asks
					// about many block cells per entity tick. This is not BSP collision data.
					PublishWaterGrid(minecraft.x, minecraft.y, minecraft.z, scale, worldId);
				}
				else if (preloadActive)
				{
					StepFullPreload(scale);
				}
				else
				{
					EnsureCollisionAround(minecraft.x, minecraft.y, minecraft.z, scale);
					PublishDynamicBrushes(scale);
					PublishWaterGrid(minecraft.x, minecraft.y, minecraft.z, scale, worldId);
				}
			}
		}

		bool MinecraftAlive(std::uint64_t now)
		{
			if (!header)
			{
				return false;
			}
			const std::uint64_t last = Read64(&header->minecraftHeartbeatMs);
			return last != 0 && now >= last && now - last < kMinecraftTimeoutMs;
		}

		bool CreateLink()
		{
			if (header)
			{
				return true;
			}

			const std::uint64_t size = protocol::kMappingBytes;
			mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
				static_cast<DWORD>(size >> 32), static_cast<DWORD>(size), protocol::kMappingName);
			const DWORD result = GetLastError();
			if (!mapping)
			{
				Com_Printf(S_COLOR_RED "JKCraft: CreateFileMapping failed (%lu)\n", GetLastError());
				return false;
			}

			header = static_cast<protocol::Header*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));
			if (!header)
			{
				Com_Printf(S_COLOR_RED "JKCraft: MapViewOfFile failed (%lu)\n", GetLastError());
				CloseHandle(mapping);
				mapping = nullptr;
				return false;
			}

			// Publish the camera-aware protocol version before magic: the Fabric side
			// treats magic as the ready flag and rejects mismatched versions.
			header->magic = 0;
			header->version = protocol::kVersion;
			header->hostPid = GetCurrentProcessId();
			header->minecraftPid = 0;
			Write64(&header->minecraftHeartbeatMs, 0);
			Write64(&header->hostHeartbeatMs, GetTickCount64());
			auto* ring = reinterpret_cast<std::uint8_t*>(header) + protocol::kCollisionRingOffset;
			Write64(reinterpret_cast<std::uint64_t*>(ring + protocol::kCollisionRingHeadOffset), 0);
			Write64(reinterpret_cast<std::uint64_t*>(ring + protocol::kCollisionRingTailOffset), 0);
			auto* inputRing = reinterpret_cast<std::uint8_t*>(header) + protocol::kInputRingOffset;
			Write64(reinterpret_cast<std::uint64_t*>(inputRing + protocol::kInputRingHeadOffset), 0);
			Write64(reinterpret_cast<std::uint64_t*>(inputRing + protocol::kInputRingTailOffset), 0);
			auto* renderRing = reinterpret_cast<std::uint8_t*>(header) + protocol::kRenderOffset;
			Write64(reinterpret_cast<std::uint64_t*>(renderRing + protocol::kRenderRingHeadOffset), 0);
			Write64(reinterpret_cast<std::uint64_t*>(renderRing + protocol::kRenderRingTailOffset), 0);
			auto* overlay = reinterpret_cast<protocol::OverlayControl*>(
				reinterpret_cast<std::uint8_t*>(header) + protocol::kOverlayControlOffset);
			overlay->state = 0;
			overlay->framesPublished = 0;
			std::memset(MappingBase() + protocol::kDirectMoveOffset, 0, sizeof(protocol::DirectMove));
			std::memset(MappingBase() + protocol::kDirectRayOffset, 0, sizeof(protocol::DirectRay));
			std::memset(MappingBase() + protocol::kMobGroundOffset, 0, sizeof(protocol::MobGroundBatch));
			MemoryBarrier();
			InterlockedExchange(reinterpret_cast<volatile LONG*>(&header->magic), static_cast<LONG>(protocol::kMagic));

			Com_Printf("JKCraft: shared link ready (%llu MiB, %s)\n",
				static_cast<unsigned long long>(size >> 20), result == ERROR_ALREADY_EXISTS ? "reused" : "created");
			return true;
		}
#endif

		void StatusCommand()
		{
			if (!enabled || !enabled->integer)
			{
				Com_Printf("JKCraft: disabled (set jkc_enabled 1 and restart)\n");
				return;
			}
#if defined(_WIN32)
			if (!header)
			{
				Com_Printf("JKCraft: shared link is not available\n");
				return;
			}
			const std::uint64_t now = GetTickCount64();
			const std::uint64_t beat = Read64(&header->minecraftHeartbeatMs);
			if (MinecraftAlive(now))
			{
				Com_Printf("JKCraft: Minecraft connected (pid %u, heartbeat %llu ms ago)\n",
					header->minecraftPid, static_cast<unsigned long long>(now - beat));
				protocol::MinecraftState state{};
				if (ReadMinecraftState(state) && (state.flags & protocol::kMinecraftInWorld))
				{
					Com_Printf("JKCraft: Minecraft player %.3f %.3f %.3f, yaw %.1f, flags 0x%X\n",
						state.x, state.y, state.z, state.yaw, state.flags);
				}
			}
			else
			{
				Com_Printf("JKCraft: waiting for Minecraft\n");
			}
#else
			Com_Printf("JKCraft: shared link is not implemented on this platform yet\n");
#endif
		}
	}

	void Init()
	{
		enabled = Cvar_Get("jkc_enabled", "1", CVAR_ARCHIVE_ND);
		unitsPerBlock = Cvar_Get("jkc_units_per_block", "32", CVAR_ARCHIVE_ND);
		cinematicState = Cvar_Get("jkc_cinematic", "0", 0);
		fullPreload = Cvar_Get("jkc_full_preload", "1", CVAR_ARCHIVE_ND);
		directCollision = Cvar_Get("jkc_direct_collision", "1", CVAR_ARCHIVE_ND);
		lastDirectCollision = directCollision->integer != 0;
		preloadMaxRegions = Cvar_Get("jkc_preload_max_regions", "60000", CVAR_ARCHIVE_ND);
		preloadBudgetMs = Cvar_Get("jkc_preload_budget_ms", "12", CVAR_ARCHIVE_ND);
		Cvar_Get("jkc_preloading", "0", 0);
		Cvar_Set("jkc_preloading", "0");
		Cmd_AddCommand("jkc_status", StatusCommand);
		if (!enabled->integer)
		{
			Com_Printf("JKCraft: disabled\n");
			return;
		}
#if defined(_WIN32)
		CreateLink();
#else
		Com_Printf("JKCraft: shared link is currently Windows-only\n");
#endif
	}

void Frame()
{
	// Keep the legacy OpenJK command buttons from remaining latched when
	// keyboard ownership moves to Minecraft (for example after a skipped
	// cinematic).  The matching key-up event is routed to Minecraft, so
	// OpenJK would otherwise keep running with the last held direction.
	static bool minecraftOwnedInputLastFrame = false;
	const bool minecraftOwnsInputNow = MinecraftOwnsInput();
	if (minecraftOwnsInputNow && !minecraftOwnedInputLastFrame)
	{
		Key_ClearStates();
	}
	minecraftOwnedInputLastFrame = minecraftOwnsInputNow;
#if defined(_WIN32)
		if (enabled && enabled->integer && header)
		{
			Write64(&header->hostHeartbeatMs, GetTickCount64());
			WriteHostState();
		}
#endif
	}

	void PublishScreenDebug(std::uint32_t bits)
	{
#if defined(_WIN32)
		if (!header)
		{
			return;
		}
		auto* handoff = reinterpret_cast<protocol::CameraHandoff*>(
			reinterpret_cast<std::uint8_t*>(header) + protocol::kCameraHandoffOffset);
		InterlockedExchange(reinterpret_cast<volatile LONG*>(&handoff->reserved),
			static_cast<LONG>(bits));
#else
		(void)bits;
#endif
	}

	bool MinecraftScreenOpen()
	{
#if defined(_WIN32)
		protocol::MinecraftState state{};
		return enabled && enabled->integer && header && MinecraftAlive(GetTickCount64()) &&
			ReadMinecraftState(state) && (state.flags & protocol::kMinecraftInWorld) &&
			(state.flags & protocol::kMinecraftScreenOpen);
#else
		return false;
#endif
	}

	bool MinecraftOwnsInput()
	{
#if defined(_WIN32)
		if (!enabled || !enabled->integer || !header || cls.state != CA_ACTIVE ||
			(cl.frame.valid && cl.frame.ps.viewEntity > 0 &&
				cl.frame.ps.viewEntity < ENTITYNUM_WORLD) ||
			(cinematicState && cinematicState->integer != 0) ||
			CL_IsRunningInGameCinematic() || CL_InGameCinematicOnStandBy() ||
			(Key_GetCatcher() & (KEYCATCH_UI | KEYCATCH_CONSOLE)) != 0 ||
			!MinecraftAlive(GetTickCount64()))
		{
			return false;
		}
		protocol::MinecraftState state{};
		return ReadMinecraftState(state) && (state.flags & protocol::kMinecraftInWorld);
#else
		return false;
#endif
	}

	void PushInput(std::uint16_t type, std::uint16_t code, std::int32_t a, std::int32_t b, std::int32_t c)
	{
#if defined(_WIN32)
		if (!enabled || !enabled->integer || !header || !MinecraftAlive(GetTickCount64()))
		{
			return;
		}
		if (type == protocol::kInputCursor)
		{
			auto* cursor = reinterpret_cast<protocol::HostCursor*>(MappingBase() + protocol::kHostCursorOffset);
			const std::uint32_t nextSeq = (Read32(&cursor->seq) + 2u) & ~1u;
			InterlockedExchange(reinterpret_cast<volatile LONG*>(&cursor->seq), static_cast<LONG>(nextSeq - 1u));
			cursor->x = a;
			cursor->y = b;
			MemoryBarrier();
			InterlockedExchange(reinterpret_cast<volatile LONG*>(&cursor->seq), static_cast<LONG>(nextSeq));
		}
		auto* ring = MappingBase() + protocol::kInputRingOffset;
		auto* headPtr = reinterpret_cast<std::uint64_t*>(ring + protocol::kInputRingHeadOffset);
		auto* tailPtr = reinterpret_cast<std::uint64_t*>(ring + protocol::kInputRingTailOffset);
		const std::uint64_t head = Read64(headPtr);
		const std::uint64_t tail = Read64(tailPtr);
		if (head - tail >= protocol::kInputRingEntries)
		{
			return;
		}
		auto* event = reinterpret_cast<protocol::InputEvent*>(ring + protocol::kInputRingDataOffset)
			+ (head & (protocol::kInputRingEntries - 1));
		event->type = type;
		event->code = code;
		event->a = a;
		event->b = b;
		event->c = c;
		MemoryBarrier();
		Write64(headPtr, head + 1);
#else
		(void)type; (void)code; (void)a; (void)b; (void)c;
#endif
	}

	void Shutdown()
	{
		Cmd_RemoveCommand("jkc_status");
#if defined(_WIN32)
		if (header)
		{
			header->magic = 0;
			UnmapViewOfFile(header);
			header = nullptr;
		}
		if (mapping)
		{
			CloseHandle(mapping);
			mapping = nullptr;
		}
#endif
	}
}
