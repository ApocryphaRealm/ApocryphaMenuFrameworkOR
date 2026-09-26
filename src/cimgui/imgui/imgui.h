// Forwarding header. The vendored cimgui.cpp is kept UNMODIFIED, and it includes Dear ImGui as
// "./imgui/imgui.h" because upstream builds it with imgui as a subdirectory. Here imgui comes from
// vcpkg and is on the include path as <imgui.h>, so this file bridges the two rather than editing
// the generated source - which is what keeps re-fetching a newer cimgui tag a copy-in, not a merge.
#pragma once
#include <imgui.h>
