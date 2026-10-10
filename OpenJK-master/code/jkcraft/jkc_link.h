/*
===========================================================================
JKCraft engine-side link lifecycle.
===========================================================================
*/

#pragma once

#include <cstdint>

namespace jkcraft
{
	void Init();
	void Frame();
	// Publishes engine screen-routing diagnostics into the shared header. This
	// remains available even when cgame itself is no longer being scheduled.
	void PublishScreenDebug(std::uint32_t bits);
	void Shutdown();
	bool MinecraftScreenOpen();
	bool MinecraftOwnsInput();
	void PushInput(std::uint16_t type, std::uint16_t code, std::int32_t a = 0, std::int32_t b = 0, std::int32_t c = 0);
}
