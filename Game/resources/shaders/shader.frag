#version 450
#include "core.glslh"

layout(location = 0) out vec4 vFragColor;

layout(location = 0) in vec2 vTexCoords;
layout(location = 1) in flat uint vTexID;

void main() {
	vec4 texColor = texture(GetBindlessTextureFromID(vTexID), vTexCoords);
	vFragColor = texColor;
}