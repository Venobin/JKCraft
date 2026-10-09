#include "tr_local.h"
#include "jkcraft_overlay.h"
#include "../jkcraft/jkc_protocol.h"

#include <cstdint>

extern void RB_SetGL2D(void);

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
	HANDLE mapping = nullptr;
	std::uint8_t* base = nullptr;
	GLuint texture = 0;
	std::uint32_t textureWidth = 0;
	std::uint32_t textureHeight = 0;
	std::uint32_t frontSlot = 2;
	bool haveFrame = false;
	bool flipY = true;
	bool loggedFrame = false;

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

	bool MinecraftReady()
	{
		// Keep this renderer rebuilt with the shared camera-aware protocol: an old
		// renderer would otherwise silently reject every HUD/hand overlay frame.
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
		return header->minecraftPid != 0 && beat != 0 && now >= beat && now - beat < 3000;
	}

	bool HostAllowsOverlay()
	{
		const auto* host = reinterpret_cast<const jkcraft::protocol::HostState*>(
			base + jkcraft::protocol::kStateOffset);
		const std::uint32_t flags = Read32(&host->flags);
		return (flags & jkcraft::protocol::kInGame) != 0 &&
			(flags & (jkcraft::protocol::kMenuOpen | jkcraft::protocol::kLoading |
				jkcraft::protocol::kCinematic)) == 0;
	}

	bool MinecraftScreenOpen()
	{
		const auto* state = reinterpret_cast<const jkcraft::protocol::MinecraftState*>(
			base + jkcraft::protocol::kMinecraftStateOffset);
		return (Read32(&state->flags) & jkcraft::protocol::kMinecraftScreenOpen) != 0;
	}

	bool ReadCursor(std::int32_t& x, std::int32_t& y)
	{
		const auto* cursor = reinterpret_cast<const jkcraft::protocol::HostCursor*>(
			base + jkcraft::protocol::kHostCursorOffset);
		for (int attempt = 0; attempt < 4; ++attempt)
		{
			const std::uint32_t before = Read32(&cursor->seq);
			if (before & 1u)
			{
				continue;
			}
			x = cursor->x;
			y = cursor->y;
			MemoryBarrier();
			const std::uint32_t after = Read32(&cursor->seq);
			if (before == after && !(after & 1u))
			{
				return true;
			}
		}
		return false;
	}

	void DrawCursor()
	{
		if (!MinecraftScreenOpen())
		{
			return;
		}
		std::int32_t px = 0;
		std::int32_t py = 0;
		if (!ReadCursor(px, py) || glConfig.vidWidth <= 0 || glConfig.vidHeight <= 0)
		{
			return;
		}
		const float x = static_cast<float>(px) * 640.0f / static_cast<float>(glConfig.vidWidth);
		const float y = static_cast<float>(py) * 480.0f / static_cast<float>(glConfig.vidHeight);
		qglDisable(GL_TEXTURE_2D);
		qglColor4f(0.0f, 0.0f, 0.0f, 1.0f);
		qglBegin(GL_TRIANGLES);
		qglVertex2f(x, y); qglVertex2f(x + 11.0f, y + 18.0f); qglVertex2f(x + 4.0f, y + 15.0f);
		qglEnd();
		qglColor4f(1.0f, 1.0f, 1.0f, 1.0f);
		qglBegin(GL_TRIANGLES);
		qglVertex2f(x + 1.5f, y + 2.0f); qglVertex2f(x + 8.0f, y + 14.5f); qglVertex2f(x + 3.5f, y + 12.5f);
		qglEnd();
		qglEnable(GL_TEXTURE_2D);
	}

	bool UploadLatestFrame()
	{
		auto* control = reinterpret_cast<jkcraft::protocol::OverlayControl*>(
			base + jkcraft::protocol::kOverlayControlOffset);
		const LONG observed = InterlockedCompareExchange(
			reinterpret_cast<volatile LONG*>(&control->state), 0, 0);
		if (!(static_cast<std::uint32_t>(observed) & jkcraft::protocol::kOverlayDirty))
		{
			return haveFrame;
		}

		const LONG old = InterlockedExchange(
			reinterpret_cast<volatile LONG*>(&control->state), static_cast<LONG>(frontSlot));
		frontSlot = static_cast<std::uint32_t>(old) & 3u;
		if (frontSlot >= jkcraft::protocol::kOverlaySlots)
		{
			return haveFrame;
		}

		const auto* slot = reinterpret_cast<const jkcraft::protocol::OverlaySlotHeader*>(
			base + jkcraft::protocol::kOverlaySlotHeaderOffset +
			frontSlot * sizeof(jkcraft::protocol::OverlaySlotHeader));
		if (!slot->width || !slot->height ||
			slot->width > jkcraft::protocol::kOverlayMaxWidth ||
			slot->height > jkcraft::protocol::kOverlayMaxHeight)
		{
			return haveFrame;
		}

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

		const void* pixels = base + jkcraft::protocol::kOverlayOffset +
			frontSlot * jkcraft::protocol::kOverlaySlotBytes;
		if (textureWidth != slot->width || textureHeight != slot->height)
		{
			qglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, slot->width, slot->height, 0,
				GL_RGBA, GL_UNSIGNED_BYTE, pixels);
			textureWidth = slot->width;
			textureHeight = slot->height;
		}
		else
		{
			qglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, slot->width, slot->height,
				GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		}
		flipY = (slot->flags & 1u) != 0;
		haveFrame = true;
		if (!loggedFrame)
		{
			loggedFrame = true;
			ri.Printf(PRINT_ALL, "JKCraft: overlay compositor received %ux%u frame %llu\n",
				slot->width, slot->height, static_cast<unsigned long long>(slot->frameId));
		}
		return true;
	}
#endif
}

void JKCraft_DrawOverlay()
{
#if defined(_WIN32)
	if (!MinecraftReady() || !HostAllowsOverlay() || !UploadLatestFrame() || !texture)
	{
		return;
	}

	RB_SetGL2D();
	GL_State(GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
	qglDisable(GL_CULL_FACE);
	qglEnable(GL_TEXTURE_2D);
	qglBindTexture(GL_TEXTURE_2D, texture);
	glState.currenttextures[glState.currenttmu] = texture;
	qglColor4f(1.0f, 1.0f, 1.0f, 1.0f);

	const float topV = flipY ? 1.0f : 0.0f;
	const float bottomV = flipY ? 0.0f : 1.0f;
	qglBegin(GL_QUADS);
	qglTexCoord2f(0.0f, topV);    qglVertex2f(0.0f, 0.0f);
	qglTexCoord2f(1.0f, topV);    qglVertex2f(640.0f, 0.0f);
	qglTexCoord2f(1.0f, bottomV); qglVertex2f(640.0f, 480.0f);
	qglTexCoord2f(0.0f, bottomV); qglVertex2f(0.0f, 480.0f);
	qglEnd();
#endif
}
