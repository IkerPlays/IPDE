#pragma once

// IPDE game runtime: plays a project's main scene in its own window.

#include "app/scene.h"

#include <imgui.h>

#include <string>

namespace ipde {

// Draws a scene into p_draw_list. p_origin is the screen position of the world origin.
void draw_scene(ImDrawList *p_draw_list, const Scene &p_scene, ImVec2 p_origin, float p_zoom, ImFont *p_font, double p_time, bool p_animate);

// Opens a window and plays the project in p_project_dir. Returns the process exit code.
// p_start_level: scene file to start from (empty = the first level).
int run_game(const std::string &p_project_dir, const std::string &p_start_level = std::string());

} // namespace ipde
