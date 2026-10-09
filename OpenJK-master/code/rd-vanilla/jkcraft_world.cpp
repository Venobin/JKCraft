#include "tr_local.h"
#include "jkcraft_world.h"
#include "../jkcraft/jkc_protocol.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <tuple>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
#if defined(_WIN32)
	constexpr float kUnitsPerBlock = 32.0f;
	constexpr double kDrawDistanceBlocks = 96.0;

	struct Section
	{
		std::int32_t sx = 0, sy = 0, sz = 0;
		GLuint opaque = 0;
		GLuint translucent = 0;
	};

	struct MeshBatch
	{
		GLuint list = 0;
		std::uint32_t texture = 0;
		bool translucent = false;
	};

	struct MinecraftLight
	{
		float origin[3]{};
		float emission = 0.0f;
		std::uint32_t color = 0;
	};

	HANDLE mapping = nullptr;
	std::uint8_t* base = nullptr;
	GLuint atlas = 0;
	GLuint shadowTexture = 0;
	std::uint32_t atlasWidth = 0;
	std::uint32_t atlasHeight = 0;
	std::map<std::uint32_t, GLuint> textures;
	std::map<std::tuple<std::int32_t, std::int32_t, std::int32_t>, Section> sections;
	std::map<std::tuple<std::int32_t, std::int32_t, std::int32_t>, std::vector<MinecraftLight>> lightSections;
	SRWLOCK lightLock = SRWLOCK_INIT;
	std::vector<MeshBatch> avatar;
	std::vector<MeshBatch> scene;
	std::vector<std::uint8_t> pendingAvatar;
	std::vector<std::uint8_t> pendingScene;
	double sceneX = 0.0, sceneY = 0.0, sceneZ = 0.0;
	jkcraft::protocol::WorldEntities worldEntities{};
	bool loggedSection = false;
	bool loggedTexture = false;
	bool loggedAvatar = false;
	bool loggedScene = false;
	bool loggedWorldEntities = false;
	bool loggedLights = false;
	bool loggedDynamicLights = false;

	std::uint32_t Read32(const std::uint32_t* value)
	{
		return static_cast<std::uint32_t>(InterlockedCompareExchange(
			reinterpret_cast<volatile LONG*>(const_cast<std::uint32_t*>(value)), 0, 0));
	}

	std::uint64_t Read64(const std::uint64_t* value)
	{
		return static_cast<std::uint64_t>(InterlockedCompareExchange64(
			reinterpret_cast<volatile LONG64*>(const_cast<std::uint64_t*>(value)), 0, 0));
	}

	void Write64(std::uint64_t* value, std::uint64_t next)
	{
		InterlockedExchange64(reinterpret_cast<volatile LONG64*>(value), static_cast<LONG64>(next));
	}

	bool OpenLink()
	{
		if (base)
		{
			return true;
		}
		mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, jkcraft::protocol::kMappingName);
		if (!mapping)
		{
			return false;
		}
		base = static_cast<std::uint8_t*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));
		if (!base)
		{
			CloseHandle(mapping);
			mapping = nullptr;
			return false;
		}
		return true;
	}

	bool Active()
	{
		// This translation unit also validates the shared protocol version, so it
		// must be rebuilt together with the overlay compositor after layout changes.
		if (!OpenLink())
		{
			return false;
		}
		const auto* header = reinterpret_cast<const jkcraft::protocol::Header*>(base);
		if (header->magic != jkcraft::protocol::kMagic || header->version != jkcraft::protocol::kVersion)
		{
			return false;
		}
		const std::uint64_t now = GetTickCount64();
		const std::uint64_t beat = Read64(&header->minecraftHeartbeatMs);
		const auto* host = reinterpret_cast<const jkcraft::protocol::HostState*>(
			base + jkcraft::protocol::kStateOffset);
		const std::uint32_t flags = Read32(&host->flags);
		return header->minecraftPid && beat && now >= beat && now - beat < 3000 &&
			(flags & jkcraft::protocol::kInGame) &&
			!(flags & (jkcraft::protocol::kMenuOpen | jkcraft::protocol::kLoading |
				jkcraft::protocol::kCinematic));
	}

	void DeleteSection(Section& section)
	{
		if (section.opaque)
		{
			qglDeleteLists(section.opaque, 1);
		}
		if (section.translucent)
		{
			qglDeleteLists(section.translucent, 1);
		}
	}

	void ClearSections()
	{
		for (auto& item : sections)
		{
			DeleteSection(item.second);
		}
		sections.clear();
	}

	void ClearAvatar()
	{
		for (const MeshBatch& batch : avatar)
		{
			if (batch.list)
			{
				qglDeleteLists(batch.list, 1);
			}
		}
		avatar.clear();
	}

	void ClearScene()
	{
		for (const MeshBatch& batch : scene)
		{
			if (batch.list)
			{
				qglDeleteLists(batch.list, 1);
			}
		}
		scene.clear();
	}

	void ClearResources()
	{
		pendingAvatar.clear();
		pendingScene.clear();
		ClearSections();
		ClearAvatar();
		ClearScene();
		for (const auto& item : textures)
		{
			GLuint texture = item.second;
			qglDeleteTextures(1, &texture);
		}
		textures.clear();
		if (atlas)
		{
			qglDeleteTextures(1, &atlas);
			atlas = 0;
		}
		if (shadowTexture)
		{
			qglDeleteTextures(1, &shadowTexture);
			shadowTexture = 0;
		}
		atlasWidth = atlasHeight = 0;
		AcquireSRWLockExclusive(&lightLock);
		lightSections.clear();
		ReleaseSRWLockExclusive(&lightLock);
	}

	void EmitVertex(const jkcraft::protocol::RenderVertex& vertex,
		std::int32_t sx, std::int32_t sy, std::int32_t sz)
	{
		const std::uint32_t c = vertex.color;
		const std::uint32_t blockLight = vertex.light & 0xFFu;
		const std::uint32_t skyLight = (vertex.light >> 8) & 0xFFu;
		const float light = 0.25f + 0.75f * static_cast<float>(std::min(15u, std::max(blockLight, skyLight))) / 15.0f;
		const auto lit = [light](std::uint32_t channel) {
			return static_cast<GLubyte>(std::min(255.0f, static_cast<float>(channel) * light));
		};
		qglColor4ub(lit(c & 0xFFu), lit((c >> 8) & 0xFFu), lit((c >> 16) & 0xFFu),
			static_cast<GLubyte>((c >> 24) & 0xFFu));
		qglTexCoord2f(vertex.u, vertex.v);
		const float mcX = static_cast<float>(sx * 16) + vertex.x;
		const float mcY = static_cast<float>(sy * 16) + vertex.y;
		const float mcZ = static_cast<float>(sz * 16) + vertex.z;
		qglVertex3f(mcX * kUnitsPerBlock, -mcZ * kUnitsPerBlock,
			mcY * kUnitsPerBlock);
	}

	void EmitAvatarVertex(const jkcraft::protocol::RenderVertex& vertex)
	{
		const std::uint32_t c = vertex.color;
		const std::uint32_t blockLight = vertex.light & 0xFFu;
		const std::uint32_t skyLight = (vertex.light >> 8) & 0xFFu;
		const float light = 0.25f + 0.75f * static_cast<float>(std::min(15u, std::max(blockLight, skyLight))) / 15.0f;
		const auto lit = [light](std::uint32_t channel) {
			return static_cast<GLubyte>(std::min(255.0f, static_cast<float>(channel) * light));
		};
		qglColor4ub(lit(c & 0xFFu), lit((c >> 8) & 0xFFu), lit((c >> 16) & 0xFFu),
			static_cast<GLubyte>((c >> 24) & 0xFFu));
		qglTexCoord2f(vertex.u, vertex.v);
		qglVertex3f(vertex.x * kUnitsPerBlock, -vertex.z * kUnitsPerBlock,
			vertex.y * kUnitsPerBlock);
	}

	void EmitWorldVertex(float x, float y, float z, float u, float v, std::uint32_t color)
	{
		qglColor4ub(static_cast<GLubyte>(color & 0xFFu),
			static_cast<GLubyte>((color >> 8) & 0xFFu),
			static_cast<GLubyte>((color >> 16) & 0xFFu),
			static_cast<GLubyte>((color >> 24) & 0xFFu));
		qglTexCoord2f(u, v);
		qglVertex3f(x * kUnitsPerBlock, -z * kUnitsPerBlock, y * kUnitsPerBlock);
	}

	void WorldQuad(const float p[4][3], const float uv[4], std::uint32_t color = 0xFFFFFFFFu)
	{
		const float tc[4][2] = {
			{ uv[0], uv[1] }, { uv[2], uv[1] }, { uv[2], uv[3] }, { uv[0], uv[3] }
		};
		static constexpr int order[6] = { 0, 1, 2, 0, 2, 3 };
		for (int k : order)
		{
			EmitWorldVertex(p[k][0], p[k][1], p[k][2], tc[k][0], tc[k][1], color);
		}
	}

	void WorldBox(const float minimum[3], const float size[3], float yaw,
		const float side[4], const float top[4], const float bottom[4], std::uint32_t topTint)
	{
		const float cx = minimum[0] + size[0] * 0.5f;
		const float cz = minimum[2] + size[2] * 0.5f;
		const float c = std::cos(yaw), s = std::sin(yaw);
		auto corner = [&](int i, float out[3]) {
			const float lx = ((i & 1) ? 0.5f : -0.5f) * size[0];
			const float lz = ((i & 4) ? 0.5f : -0.5f) * size[2];
			out[0] = cx + lx * c - lz * s;
			out[1] = minimum[1] + ((i & 2) ? size[1] : 0.0f);
			out[2] = cz + lx * s + lz * c;
		};
		static constexpr int faces[6][4] = {
			{ 6, 7, 5, 4 }, { 3, 2, 0, 1 }, { 7, 3, 1, 5 },
			{ 2, 6, 4, 0 }, { 2, 3, 7, 6 }, { 4, 5, 1, 0 }
		};
		for (int face = 0; face < 6; ++face)
		{
			float p[4][3];
			for (int k = 0; k < 4; ++k)
			{
				corner(faces[face][k], p[k]);
			}
			const float* faceUv = face == 4 ? top : face == 5 ? bottom : side;
			WorldQuad(p, faceUv, face == 4 && topTint ? topTint : 0xFFFFFFFFu);
		}
	}

	void WorldArrow(const jkcraft::protocol::WorldEntity& entity, bool trident)
	{
		constexpr float kPi = 3.14159265358979323846f;
		const float yaw = entity.yaw * kPi / 180.0f;
		const float pitch = entity.pitch * kPi / 180.0f;
		const float d[3] = {
			std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)
		};
		float side[3] = { d[2], 0.0f, -d[0] };
		float sideLength = std::sqrt(side[0] * side[0] + side[2] * side[2]);
		if (sideLength < 0.001f)
		{
			side[0] = 1.0f;
			side[2] = 0.0f;
			sideLength = 1.0f;
		}
		side[0] /= sideLength;
		side[2] /= sideLength;
		const float up[3] = {
			side[1] * d[2] - side[2] * d[1],
			side[2] * d[0] - side[0] * d[2],
			side[0] * d[1] - side[1] * d[0]
		};
		constexpr float diagonal = 0.70710678f;
		const float fins[2][3] = {
			{ (up[0] + side[0]) * diagonal, (up[1] + side[1]) * diagonal, (up[2] + side[2]) * diagonal },
			{ (up[0] - side[0]) * diagonal, (up[1] - side[1]) * diagonal, (up[2] - side[2]) * diagonal }
		};
		auto at = [&](float along, const float* q, float across, const float* q2, float across2, float out[3]) {
			for (int k = 0; k < 3; ++k)
			{
				const float origin = k == 0 ? entity.x : k == 1 ? entity.y : entity.z;
				out[k] = origin + d[k] * along + q[k] * across + (q2 ? q2[k] * across2 : 0.0f);
			}
		};
		if (!trident)
		{
			constexpr float scale = 0.9f / 16.0f * 0.55f;
			for (const auto& fin : fins)
			{
				float p[4][3];
				at(-12 * scale, fin, -2 * scale, nullptr, 0, p[0]);
				at(4 * scale, fin, -2 * scale, nullptr, 0, p[1]);
				at(4 * scale, fin, 2 * scale, nullptr, 0, p[2]);
				at(-12 * scale, fin, 2 * scale, nullptr, 0, p[3]);
				WorldQuad(p, entity.uv[0]);
			}
			float p[4][3];
			at(-11 * scale, fins[0], -2 * scale, fins[1], -2 * scale, p[0]);
			at(-11 * scale, fins[0], 2 * scale, fins[1], -2 * scale, p[1]);
			at(-11 * scale, fins[0], 2 * scale, fins[1], 2 * scale, p[2]);
			at(-11 * scale, fins[0], -2 * scale, fins[1], 2 * scale, p[3]);
			WorldQuad(p, entity.uv[1]);
		}
		else
		{
			constexpr float half = 0.9f;
			for (const auto& fin : fins)
			{
				float p[4][3];
				at(0, fin, half, nullptr, 0, p[0]);
				at(half, fin, 0, nullptr, 0, p[1]);
				at(0, fin, -half, nullptr, 0, p[2]);
				at(-half, fin, 0, nullptr, 0, p[3]);
				WorldQuad(p, entity.uv[0]);
			}
		}
	}

	GLuint CompilePass(const jkcraft::protocol::RenderVertex* vertices, std::uint32_t count,
		std::int32_t sx, std::int32_t sy, std::int32_t sz, bool translucent)
	{
		bool any = false;
		for (std::uint32_t i = 0; i + 2 < count; i += 3)
		{
			if (((vertices[i].flags & 2u) != 0) == translucent)
			{
				any = true;
				break;
			}
		}
		if (!any)
		{
			return 0;
		}
		const GLuint list = qglGenLists(1);
		if (!list)
		{
			return 0;
		}
		qglNewList(list, GL_COMPILE);
		qglBegin(GL_TRIANGLES);
		for (std::uint32_t i = 0; i + 2 < count; i += 3)
		{
			if (((vertices[i].flags & 2u) != 0) != translucent)
			{
				continue;
			}
			EmitVertex(vertices[i], sx, sy, sz);
			EmitVertex(vertices[i + 1], sx, sy, sz);
			EmitVertex(vertices[i + 2], sx, sy, sz);
		}
		qglEnd();
		qglEndList();
		return list;
	}

	void OnAtlas(const std::uint8_t* data, std::uint32_t bytes)
	{
		if (bytes < sizeof(jkcraft::protocol::RenderAtlas))
		{
			return;
		}
		const auto* header = reinterpret_cast<const jkcraft::protocol::RenderAtlas*>(data);
		const std::uint64_t pixels = static_cast<std::uint64_t>(header->width) * header->height * 4u;
		if (!header->width || !header->height || header->width > 8192 || header->height > 8192 ||
			bytes < sizeof(*header) + pixels)
		{
			return;
		}
		if (!atlas)
		{
			qglGenTextures(1, &atlas);
		}
		qglBindTexture(GL_TEXTURE_2D, atlas);
		qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
		qglPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		qglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, header->width, header->height, 0,
			GL_RGBA, GL_UNSIGNED_BYTE, data + sizeof(*header));
		atlasWidth = header->width;
		atlasHeight = header->height;
		ri.Printf(PRINT_ALL, "JKCraft: Minecraft block atlas %ux%u loaded\n", atlasWidth, atlasHeight);
	}

	void OnAtlasRegion(const std::uint8_t* data, std::uint32_t bytes)
	{
		if (!atlas || bytes < sizeof(jkcraft::protocol::RenderAtlasRegion))
		{
			return;
		}
		const auto* header = reinterpret_cast<const jkcraft::protocol::RenderAtlasRegion*>(data);
		const std::uint64_t pixels = static_cast<std::uint64_t>(header->width) * header->height * 4u;
		if (!header->width || !header->height || header->x + header->width > atlasWidth ||
			header->y + header->height > atlasHeight || bytes < sizeof(*header) + pixels)
		{
			return;
		}
		qglBindTexture(GL_TEXTURE_2D, atlas);
		qglTexSubImage2D(GL_TEXTURE_2D, 0, header->x, header->y, header->width, header->height,
			GL_RGBA, GL_UNSIGNED_BYTE, data + sizeof(*header));
	}

	void OnTexture(const std::uint8_t* data, std::uint32_t bytes)
	{
		if (bytes < sizeof(jkcraft::protocol::RenderTexture))
		{
			return;
		}
		const auto* header = reinterpret_cast<const jkcraft::protocol::RenderTexture*>(data);
		const std::uint64_t pixels = static_cast<std::uint64_t>(header->width) * header->height * 4u;
		if (!header->id || !header->width || !header->height || header->width > 8192 ||
			header->height > 8192 || bytes < sizeof(*header) + pixels)
		{
			return;
		}
		GLuint& texture = textures[header->id];
		if (!texture)
		{
			qglGenTextures(1, &texture);
		}
		qglBindTexture(GL_TEXTURE_2D, texture);
		qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
		qglPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		qglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, header->width, header->height, 0,
			GL_RGBA, GL_UNSIGNED_BYTE, data + sizeof(*header));
		if (!loggedTexture)
		{
			loggedTexture = true;
			ri.Printf(PRINT_ALL, "JKCraft: first Minecraft entity texture %u (%ux%u) loaded\n",
				header->id, header->width, header->height);
		}
	}

	void OnAvatar(const std::uint8_t* data, std::uint32_t bytes)
	{
		if (bytes < sizeof(jkcraft::protocol::RenderMesh))
		{
			return;
		}
		const auto* header = reinterpret_cast<const jkcraft::protocol::RenderMesh*>(data);
		if (header->batchCount > 256u || header->vertexCount > 1000000u)
		{
			return;
		}
		const std::uint64_t batchBytes = static_cast<std::uint64_t>(header->batchCount) * sizeof(jkcraft::protocol::RenderBatch);
		const std::uint64_t vertexBytes = static_cast<std::uint64_t>(header->vertexCount) * sizeof(jkcraft::protocol::RenderVertex);
		if (bytes < sizeof(*header) + batchBytes + vertexBytes)
		{
			return;
		}
		const auto* batches = reinterpret_cast<const jkcraft::protocol::RenderBatch*>(data + sizeof(*header));
		const auto* vertices = reinterpret_cast<const jkcraft::protocol::RenderVertex*>(data + sizeof(*header) + batchBytes);
		for (std::uint32_t i = 0; i < header->batchCount; ++i)
		{
			const std::uint64_t end = static_cast<std::uint64_t>(batches[i].first) + batches[i].count;
			if (batches[i].count % 3u || end > header->vertexCount)
			{
				return;
			}
		}

		ClearAvatar();
		for (std::uint32_t i = 0; i < header->batchCount; ++i)
		{
			const auto& source = batches[i];
			if (!source.count)
			{
				continue;
			}
			const GLuint list = qglGenLists(1);
			if (!list)
			{
				continue;
			}
			qglNewList(list, GL_COMPILE);
			qglBegin(GL_TRIANGLES);
			for (std::uint32_t v = 0; v < source.count; ++v)
			{
				EmitAvatarVertex(vertices[source.first + v]);
			}
			qglEnd();
			qglEndList();
			avatar.push_back({ list, source.texture, (source.flags & 1u) != 0 });
		}
		if (!loggedAvatar && !avatar.empty())
		{
			loggedAvatar = true;
			ri.Printf(PRINT_ALL, "JKCraft: first Minecraft avatar (%u batches, %u vertices) compiled\n",
				header->batchCount, header->vertexCount);
		}
	}

	void OnScene(const std::uint8_t* data, std::uint32_t bytes)
	{
		if (bytes < sizeof(jkcraft::protocol::RenderScene))
		{
			return;
		}
		const auto* header = reinterpret_cast<const jkcraft::protocol::RenderScene*>(data);
		if (header->batchCount > 512u || header->vertexCount > 2000000u)
		{
			return;
		}
		const std::uint64_t batchBytes = static_cast<std::uint64_t>(header->batchCount) * sizeof(jkcraft::protocol::RenderBatch);
		const std::uint64_t vertexBytes = static_cast<std::uint64_t>(header->vertexCount) * sizeof(jkcraft::protocol::RenderVertex);
		if (bytes < sizeof(*header) + batchBytes + vertexBytes)
		{
			return;
		}
		const auto* batches = reinterpret_cast<const jkcraft::protocol::RenderBatch*>(data + sizeof(*header));
		const auto* vertices = reinterpret_cast<const jkcraft::protocol::RenderVertex*>(data + sizeof(*header) + batchBytes);
		for (std::uint32_t i = 0; i < header->batchCount; ++i)
		{
			const std::uint64_t end = static_cast<std::uint64_t>(batches[i].first) + batches[i].count;
			if (batches[i].count % 3u || end > header->vertexCount)
			{
				return;
			}
		}

		ClearScene();
		sceneX = header->originX;
		sceneY = header->originY;
		sceneZ = header->originZ;
		for (std::uint32_t i = 0; i < header->batchCount; ++i)
		{
			const auto& source = batches[i];
			if (!source.count)
			{
				continue;
			}
			const GLuint list = qglGenLists(1);
			if (!list)
			{
				continue;
			}
			qglNewList(list, GL_COMPILE);
			qglBegin(GL_TRIANGLES);
			for (std::uint32_t v = 0; v < source.count; ++v)
			{
				EmitAvatarVertex(vertices[source.first + v]);
			}
			qglEnd();
			qglEndList();
			scene.push_back({ list, source.texture, (source.flags & 1u) != 0 });
		}
		if (!loggedScene && !scene.empty())
		{
			loggedScene = true;
			ri.Printf(PRINT_ALL, "JKCraft: first Minecraft entity scene (%u batches, %u vertices) compiled\n",
				header->batchCount, header->vertexCount);
		}
	}

	void OnSection(const std::uint8_t* data, std::uint32_t bytes)
	{
		if (bytes < sizeof(jkcraft::protocol::RenderSection))
		{
			return;
		}
		const auto* header = reinterpret_cast<const jkcraft::protocol::RenderSection*>(data);
		const auto key = std::make_tuple(header->sx, header->sy, header->sz);
		auto found = sections.find(key);
		if (found != sections.end())
		{
			DeleteSection(found->second);
			sections.erase(found);
		}
		const std::uint32_t count = header->vertexCount - header->vertexCount % 3u;
		if (!count)
		{
			return;
		}
		const std::uint64_t needed = sizeof(*header) + static_cast<std::uint64_t>(count) * sizeof(jkcraft::protocol::RenderVertex);
		if (count > 2000000u || bytes < needed)
		{
			return;
		}
		const auto* vertices = reinterpret_cast<const jkcraft::protocol::RenderVertex*>(data + sizeof(*header));
		Section section{};
		section.sx = header->sx;
		section.sy = header->sy;
		section.sz = header->sz;
		section.opaque = CompilePass(vertices, count, header->sx, header->sy, header->sz, false);
		section.translucent = CompilePass(vertices, count, header->sx, header->sy, header->sz, true);
		if (section.opaque || section.translucent)
		{
			sections.emplace(key, section);
			if (!loggedSection)
			{
				loggedSection = true;
				ri.Printf(PRINT_ALL, "JKCraft: first Minecraft block section %d %d %d (%u vertices) compiled\n",
					header->sx, header->sy, header->sz, count);
			}
		}
	}

	void OnLights(const std::uint8_t* data, std::uint32_t bytes)
	{
		if (bytes < sizeof(jkcraft::protocol::RenderLights))
		{
			return;
		}
		const auto* header = reinterpret_cast<const jkcraft::protocol::RenderLights*>(data);
		const std::uint64_t needed = sizeof(*header) +
			static_cast<std::uint64_t>(header->count) * sizeof(jkcraft::protocol::RenderLight);
		if (header->count > 4096u || bytes < needed)
		{
			return;
		}
		const auto key = std::make_tuple(header->sx, header->sy, header->sz);
		std::vector<MinecraftLight> lights;
		lights.reserve(header->count);
		const auto* records = reinterpret_cast<const jkcraft::protocol::RenderLight*>(data + sizeof(*header));
		for (std::uint32_t i = 0; i < header->count; ++i)
		{
			const auto& source = records[i];
			if (!source.emission || source.x >= 16 || source.y >= 16 || source.z >= 16)
			{
				continue;
			}
			MinecraftLight light{};
			light.origin[0] = (header->sx * 16.0f + source.x + 0.5f) * kUnitsPerBlock;
			light.origin[1] = -(header->sz * 16.0f + source.z + 0.5f) * kUnitsPerBlock;
			light.origin[2] = (header->sy * 16.0f + source.y + 0.5f) * kUnitsPerBlock;
			light.emission = static_cast<float>(source.emission);
			light.color = source.color;
			lights.push_back(light);
		}
		AcquireSRWLockExclusive(&lightLock);
		if (lights.empty())
		{
			lightSections.erase(key);
		}
		else
		{
			lightSections[key] = std::move(lights);
		}
		ReleaseSRWLockExclusive(&lightLock);
		if (!loggedLights && header->count)
		{
			loggedLights = true;
			ri.Printf(PRINT_ALL, "JKCraft: first Minecraft light section %d %d %d (%u lights) received\n",
				header->sx, header->sy, header->sz, header->count);
		}
	}

	void DrainMessages()
	{
		auto* ring = base + jkcraft::protocol::kRenderOffset;
		auto* headPtr = reinterpret_cast<std::uint64_t*>(ring + jkcraft::protocol::kRenderRingHeadOffset);
		auto* tailPtr = reinterpret_cast<std::uint64_t*>(ring + jkcraft::protocol::kRenderRingTailOffset);
		const std::uint64_t head = Read64(headPtr);
		std::uint64_t tail = Read64(tailPtr);
		std::uint64_t drained = 0;
		while (tail < head && drained < (48ull << 20))
		{
			const std::uint64_t pos = tail % jkcraft::protocol::kRenderRingDataBytes;
			if (pos + sizeof(jkcraft::protocol::CollisionMessageHeader) > jkcraft::protocol::kRenderRingDataBytes)
			{
				tail += jkcraft::protocol::kRenderRingDataBytes - pos;
				continue;
			}
			const auto* message = reinterpret_cast<const jkcraft::protocol::CollisionMessageHeader*>(
				ring + jkcraft::protocol::kRenderRingDataOffset + pos);
			if (message->type == jkcraft::protocol::kRenderPad)
			{
				tail += jkcraft::protocol::kRenderRingDataBytes - pos;
				continue;
			}
			const std::uint64_t messageBytes = (sizeof(*message) + static_cast<std::uint64_t>(message->payloadBytes) + 7u) & ~7ull;
			if (messageBytes > jkcraft::protocol::kRenderRingDataBytes || pos + messageBytes > jkcraft::protocol::kRenderRingDataBytes || tail + messageBytes > head)
			{
				break;
			}
			const auto* payload = reinterpret_cast<const std::uint8_t*>(message + 1);
			switch (message->type)
			{
			case jkcraft::protocol::kRenderAtlas: OnAtlas(payload, message->payloadBytes); break;
			case jkcraft::protocol::kRenderSection: OnSection(payload, message->payloadBytes); break;
			case jkcraft::protocol::kRenderClearAll: ClearResources(); break;
			case jkcraft::protocol::kRenderTexture: OnTexture(payload, message->payloadBytes); break;
			case jkcraft::protocol::kRenderAvatar:
				pendingAvatar.assign(payload, payload + message->payloadBytes);
				break;
			case jkcraft::protocol::kRenderScene:
				pendingScene.assign(payload, payload + message->payloadBytes);
				break;
			case jkcraft::protocol::kRenderAtlasRegion: OnAtlasRegion(payload, message->payloadBytes); break;
			case jkcraft::protocol::kRenderLights: OnLights(payload, message->payloadBytes); break;
			default: break;
			}
			tail += messageBytes;
			drained += messageBytes;
		}
		Write64(tailPtr, tail);
		// Avatar and dynamic-scene messages replace the previous pose completely. Compiling
		// every queued intermediate mesh creates a feedback loop when either game has one
		// slow frame; only the newest complete pose can ever be visible.
		if (!pendingAvatar.empty())
		{
			OnAvatar(pendingAvatar.data(), static_cast<std::uint32_t>(pendingAvatar.size()));
			pendingAvatar.clear();
		}
		if (!pendingScene.empty())
		{
			OnScene(pendingScene.data(), static_cast<std::uint32_t>(pendingScene.size()));
			pendingScene.clear();
		}
	}

	bool ReadPlayerPosition(double& x, double& y, double& z)
	{
		const auto* state = reinterpret_cast<const jkcraft::protocol::MinecraftState*>(
			base + jkcraft::protocol::kMinecraftStateOffset);
		for (int attempt = 0; attempt < 4; ++attempt)
		{
			const std::uint32_t before = Read32(&state->seq);
			if (before & 1u) continue;
			x = state->x; y = state->y; z = state->z;
			MemoryBarrier();
			const std::uint32_t after = Read32(&state->seq);
			if (before == after && !(after & 1u)) return true;
		}
		return false;
	}

	bool ReadWorldEntities()
	{
		const auto* source = reinterpret_cast<const jkcraft::protocol::WorldEntities*>(
			base + jkcraft::protocol::kWorldEntitiesOffset);
		for (int attempt = 0; attempt < 4; ++attempt)
		{
			const std::uint32_t before = Read32(&source->seq);
			if (before & 1u)
			{
				continue;
			}
			jkcraft::protocol::WorldEntities snapshot{};
			std::memcpy(&snapshot, source, sizeof(snapshot));
			MemoryBarrier();
			const std::uint32_t after = Read32(&source->seq);
			if (before == after && !(after & 1u) && snapshot.count <= jkcraft::protocol::kMaxWorldEntities)
			{
				worldEntities = snapshot;
				if (!loggedWorldEntities && snapshot.count)
				{
					loggedWorldEntities = true;
					ri.Printf(PRINT_ALL, "JKCraft: first Minecraft world-entity table (%u entries) received\n",
						snapshot.count);
				}
				return true;
			}
		}
		return false;
	}

	bool NearPlayer(const Section& section, double px, double py, double pz)
	{
		const double dx = section.sx * 16.0 + 8.0 - px;
		const double dy = section.sy * 16.0 + 8.0 - py;
		const double dz = section.sz * 16.0 + 8.0 - pz;
		return dx * dx + dz * dz <= kDrawDistanceBlocks * kDrawDistanceBlocks && dy * dy <= 96.0 * 96.0;
	}

	GLuint TextureFor(std::uint32_t id)
	{
		if (!id)
		{
			return atlas;
		}
		const auto found = textures.find(id);
		return found == textures.end() ? 0 : found->second;
	}

	void DrawAvatarPass(bool translucent, double px, double py, double pz)
	{
		qglPushMatrix();
		qglTranslatef(static_cast<float>(px * kUnitsPerBlock),
			static_cast<float>(-pz * kUnitsPerBlock),
			static_cast<float>(py * kUnitsPerBlock));
		for (const MeshBatch& batch : avatar)
		{
			if (batch.translucent != translucent)
			{
				continue;
			}
			const GLuint texture = TextureFor(batch.texture);
			if (!texture)
			{
				continue;
			}
			qglBindTexture(GL_TEXTURE_2D, texture);
			glState.currenttextures[glState.currenttmu] = texture;
			qglCallList(batch.list);
		}
		qglPopMatrix();
	}

	void DrawScenePass(bool translucent)
	{
		qglPushMatrix();
		qglTranslatef(static_cast<float>(sceneX * kUnitsPerBlock),
			static_cast<float>(-sceneZ * kUnitsPerBlock),
			static_cast<float>(sceneY * kUnitsPerBlock));
		for (const MeshBatch& batch : scene)
		{
			if (batch.translucent != translucent)
			{
				continue;
			}
			const GLuint texture = TextureFor(batch.texture);
			if (!texture)
			{
				continue;
			}
			qglBindTexture(GL_TEXTURE_2D, texture);
			glState.currenttextures[glState.currenttmu] = texture;
			qglCallList(batch.list);
		}
		qglPopMatrix();
	}

	void DrawWorldEntityPass(bool translucent)
	{
		if (!atlas)
		{
			return;
		}
		qglBindTexture(GL_TEXTURE_2D, atlas);
		glState.currenttextures[glState.currenttmu] = atlas;
		qglBegin(GL_TRIANGLES);
		for (std::uint32_t i = 0; i < worldEntities.count; ++i)
		{
			const auto& entity = worldEntities.entities[i];
			const bool blended = entity.kind == jkcraft::protocol::kWorldCrack;
			if (blended != translucent)
			{
				continue;
			}
			switch (entity.kind)
			{
			case jkcraft::protocol::kWorldArrow:
				WorldArrow(entity, false);
				break;
			case jkcraft::protocol::kWorldTrident:
				WorldArrow(entity, true);
				break;
			case jkcraft::protocol::kWorldItem:
			{
				constexpr float kPi = 3.14159265358979323846f;
				const float spin = entity.yaw * kPi / 180.0f;
				const float half = entity.scale * 0.5f;
				const float rx = std::cos(spin) * half;
				const float rz = std::sin(spin) * half;
				const float p[4][3] = {
					{ entity.x - rx, entity.y + half, entity.z - rz },
					{ entity.x + rx, entity.y + half, entity.z + rz },
					{ entity.x + rx, entity.y - half, entity.z + rz },
					{ entity.x - rx, entity.y - half, entity.z - rz }
				};
				WorldQuad(p, entity.uv[0]);
				break;
			}
			case jkcraft::protocol::kWorldBlock:
			{
				constexpr float kPi = 3.14159265358979323846f;
				const float minimum[3] = {
					entity.x - entity.scale * 0.5f,
					entity.y - entity.scale * 0.5f,
					entity.z - entity.scale * 0.5f
				};
				const float size[3] = { entity.scale, entity.scale, entity.scale };
				WorldBox(minimum, size, entity.yaw * kPi / 180.0f,
					entity.uv[0], entity.uv[1], entity.uv[2], entity.tint);
				break;
			}
			case jkcraft::protocol::kWorldCrack:
			{
				const float minimum[3] = { entity.x, entity.y, entity.z };
				WorldBox(minimum, entity.ext, 0.0f,
					entity.uv[0], entity.uv[0], entity.uv[0], 0);
				break;
			}
			default:
				break;
			}
		}
		qglEnd();
	}

	void DrawWorldShadows()
	{
		bool any = false;
		for (std::uint32_t i = 0; i < worldEntities.count; ++i)
		{
			if (worldEntities.entities[i].kind == jkcraft::protocol::kWorldShadow)
			{
				any = true;
				break;
			}
		}
		if (!any)
		{
			return;
		}
		if (!shadowTexture)
		{
			constexpr int size = 64;
			std::vector<std::uint8_t> pixels(size * size * 4u, 0);
			for (int y = 0; y < size; ++y)
			{
				for (int x = 0; x < size; ++x)
				{
					const float dx = (x + 0.5f) * (2.0f / size) - 1.0f;
					const float dy = (y + 0.5f) * (2.0f / size) - 1.0f;
					const float fade = std::max(0.0f, 1.0f - dx * dx - dy * dy);
					const std::size_t o = static_cast<std::size_t>(y * size + x) * 4u;
					pixels[o + 3] = static_cast<std::uint8_t>(90.0f * fade * fade);
				}
			}
			qglGenTextures(1, &shadowTexture);
			qglBindTexture(GL_TEXTURE_2D, shadowTexture);
			qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
			qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
			qglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0,
				GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
			ri.Printf(PRINT_ALL, "JKCraft: soft Minecraft contact-shadow texture created\n");
		}
		qglBindTexture(GL_TEXTURE_2D, shadowTexture);
		glState.currenttextures[glState.currenttmu] = shadowTexture;
		qglBegin(GL_TRIANGLES);
		static constexpr float uv[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
		for (std::uint32_t i = 0; i < worldEntities.count; ++i)
		{
			const auto& entity = worldEntities.entities[i];
			if (entity.kind != jkcraft::protocol::kWorldShadow)
			{
				continue;
			}
			const float radius = std::max(0.18f, entity.scale * 0.62f);
			const float y = entity.y + 0.012f;
			const float p[4][3] = {
				{ entity.x - radius, y, entity.z - radius },
				{ entity.x + radius, y, entity.z - radius },
				{ entity.x + radius, y, entity.z + radius },
				{ entity.x - radius, y, entity.z + radius }
			};
			WorldQuad(p, uv);
		}
		qglEnd();
	}

	void DrawWorldSelection()
	{
		if (!worldEntities.hasSelection)
		{
			return;
		}
		constexpr float grow = 0.002f;
		const float low[3] = {
			worldEntities.selMin[0] - grow,
			worldEntities.selMin[1] - grow,
			worldEntities.selMin[2] - grow
		};
		const float high[3] = {
			worldEntities.selMax[0] + grow,
			worldEntities.selMax[1] + grow,
			worldEntities.selMax[2] + grow
		};
		static constexpr int edges[12][2] = {
			{ 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 },
			{ 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 },
			{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
		};
		qglDisable(GL_TEXTURE_2D);
		qglColor4ub(0, 0, 0, 115);
		qglLineWidth(2.0f);
		qglBegin(GL_LINES);
		for (const auto& edge : edges)
		{
			for (int index : edge)
			{
				const float x = (index & 1) ? high[0] : low[0];
				const float y = (index & 2) ? high[1] : low[1];
				const float z = (index & 4) ? high[2] : low[2];
				qglVertex3f(x * kUnitsPerBlock, -z * kUnitsPerBlock, y * kUnitsPerBlock);
			}
		}
		qglEnd();
		qglLineWidth(1.0f);
		qglEnable(GL_TEXTURE_2D);
	}
#endif
}

void JKCraft_AddLightsToScene(const float* viewOrigin, int time)
{
#if defined(_WIN32)
	if (!viewOrigin || !Active())
	{
		return;
	}
	struct RankedLight
	{
		MinecraftLight light;
		float distanceSquared;
	};
	std::vector<RankedLight> nearest;
	constexpr float range = static_cast<float>(kDrawDistanceBlocks * kUnitsPerBlock);
	constexpr float rangeSquared = range * range;
	AcquireSRWLockShared(&lightLock);
	for (const auto& section : lightSections)
	{
		for (const MinecraftLight& light : section.second)
		{
			const float dx = light.origin[0] - viewOrigin[0];
			const float dy = light.origin[1] - viewOrigin[1];
			const float dz = light.origin[2] - viewOrigin[2];
			const float distanceSquared = dx * dx + dy * dy + dz * dz;
			if (distanceSquared <= rangeSquared)
			{
				nearest.push_back({ light, distanceSquared });
			}
		}
	}
	ReleaseSRWLockShared(&lightLock);
	std::sort(nearest.begin(), nearest.end(), [](const RankedLight& a, const RankedLight& b) {
		return a.distanceSquared < b.distanceSquared;
	});
	const std::size_t count = std::min<std::size_t>(nearest.size(), MAX_DLIGHTS);
	for (std::size_t i = 0; i < count; ++i)
	{
		const MinecraftLight& source = nearest[i].light;
		const std::uint32_t packed = source.color;
		const std::uint32_t kind = (packed >> 24) & 0x0Fu;
		float flicker = 1.0f;
		const float phase = source.origin[0] * 0.013f + source.origin[1] * 0.017f + source.origin[2] * 0.019f;
		if (kind == 1u)
		{
			flicker = 0.94f + 0.06f * std::sin(time * 0.021f + phase) * std::sin(time * 0.037f + phase * 1.7f);
		}
		else if (kind == 2u)
		{
			flicker = 0.97f + 0.03f * std::sin(time * 0.009f + phase);
		}
		vec3_t origin = { source.origin[0], source.origin[1], source.origin[2] };
		RE_AddLightToScene(origin, source.emission * kUnitsPerBlock * flicker,
			static_cast<float>(packed & 0xFFu) / 255.0f,
			static_cast<float>((packed >> 8) & 0xFFu) / 255.0f,
			static_cast<float>((packed >> 16) & 0xFFu) / 255.0f);
	}
	if (count && !loggedDynamicLights)
	{
		loggedDynamicLights = true;
		ri.Printf(PRINT_ALL, "JKCraft: Minecraft dynamic lights submitted to OpenJK (%u nearest)\n",
			static_cast<unsigned>(count));
	}
#else
	(void)viewOrigin;
	(void)time;
#endif
}

void JKCraft_DrawWorld()
{
#if defined(_WIN32)
	// Keep the producer moving even on a transition frame where OpenJK briefly
	// reports a menu/loading flag.  Rendering still requires Active(), but atlas
	// and section messages must not fill the ring while that flag is transient.
	if (!OpenLink())
	{
		return;
	}
	DrainMessages();
	if (!Active())
	{
		return;
	}
	ReadWorldEntities();
	if ((!atlas || sections.empty()) && avatar.empty() && scene.empty() &&
		worldEntities.count == 0 && !worldEntities.hasSelection)
	{
		return;
	}
	double px = 0.0, py = 0.0, pz = 0.0;
	ReadPlayerPosition(px, py, pz);

	qglViewport(backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
		backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight);
	qglScissor(backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
		backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight);
	qglMatrixMode(GL_PROJECTION);
	qglLoadMatrixf(backEnd.viewParms.projectionMatrix);
	qglMatrixMode(GL_MODELVIEW);
	qglLoadMatrixf(backEnd.viewParms.world.modelMatrix);
	GL_SelectTexture(0);
	qglEnable(GL_TEXTURE_2D);
	GL_Cull(CT_TWO_SIDED);
	qglBindTexture(GL_TEXTURE_2D, atlas);
	glState.currenttextures[glState.currenttmu] = atlas;
	GL_TexEnv(GL_MODULATE);

	GL_State(GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO | GLS_ATEST_GT_0);
	for (const auto& item : sections)
	{
		const Section& section = item.second;
		if (section.opaque && NearPlayer(section, px, py, pz))
		{
			qglCallList(section.opaque);
		}
	}
	DrawWorldEntityPass(false);
	DrawScenePass(false);
	DrawAvatarPass(false, px, py, pz);

	GL_State(GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_ATEST_GT_0);
	for (const auto& item : sections)
	{
		const Section& section = item.second;
		if (section.translucent && NearPlayer(section, px, py, pz))
		{
			qglCallList(section.translucent);
		}
	}
	DrawScenePass(true);
	DrawAvatarPass(true, px, py, pz);
	DrawWorldShadows();
	DrawWorldEntityPass(true);
	GL_State(GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
	DrawWorldSelection();
	qglColor4f(1.0f, 1.0f, 1.0f, 1.0f);
#endif
}
