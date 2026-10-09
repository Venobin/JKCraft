#pragma once

// Consumes Minecraft's render ring and draws placed blocks into OpenJK's 3D frame.
void JKCraft_DrawWorld();

// Adds the nearest cached Minecraft block lights to OpenJK's ordinary dynamic-light list.
// Called by the renderer front end before it traverses the BSP for the main view.
void JKCraft_AddLightsToScene(const float* viewOrigin, int time);
