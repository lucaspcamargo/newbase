#pragma once

// Flex-style layout utilities for Dear ImGui.
//
// Provides BeginHorizontal / EndHorizontal, BeginVertical / EndVertical, and
// Spring — a stretchy spacer that distributes free space between items — as a
// self-contained layer on top of stock ImGui (no imgui patch required).
//
// Semantics match the layout extension found in the imgui-node-editor's
// bundled imgui fork, so BlueprintNodeBuilder can be ported directly.
//
// Usage:
//   nb::ui::BeginHorizontal("row");
//     nb::ui::BeginVertical("inputs", {0,0}, 0.f);
//       // ... input pins ...
//     nb::ui::EndVertical();
//     nb::ui::Spring();                 // push outputs to the right
//     nb::ui::BeginVertical("outputs", {0,0}, 1.f);
//       // ... output pins ...
//     nb::ui::EndVertical();
//   nb::ui::EndHorizontal();

namespace nb { namespace ui {

void BeginHorizontal(const char* str_id, ImVec2 size = {0.f, 0.f}, float align = -1.f);
void BeginHorizontal(const void* ptr_id, ImVec2 size = {0.f, 0.f}, float align = -1.f);
void BeginHorizontal(int id,             ImVec2 size = {0.f, 0.f}, float align = -1.f);
void EndHorizontal();

void BeginVertical(const char* str_id, ImVec2 size = {0.f, 0.f}, float align = -1.f);
void BeginVertical(const void* ptr_id, ImVec2 size = {0.f, 0.f}, float align = -1.f);
void BeginVertical(int id,             ImVec2 size = {0.f, 0.f}, float align = -1.f);
void EndVertical();

// Inserts a stretchy spacer.  weight <= 0 → always zero size.
// spacing < 0 → use default ItemSpacing; spacing >= 0 → exact pixels.
void Spring(float weight = 1.f, float spacing = -1.f);

}} // namespace nb::ui
