#pragma once

// Draws the newest Minecraft hand/HUD/GUI frame over the completed OpenJK frame.
// Safe to call every frame; it is a no-op while JKCraft or Minecraft is absent.
void JKCraft_DrawOverlay();
