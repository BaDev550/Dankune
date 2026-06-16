#version 450
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoords;

layout(location = 0) out vec2 vTexCoords;
layout(location = 1) out flat uint vTexID;

#include "core.glslh"

layout(push_constant) uniform PcData {
	mat4 transform;
	vec2 UVscale;
	vec2 UVoffset;
	uint textureID;
} uPc;

void main() {
	CameraData camData = GetCamera();
	vTexID = uPc.textureID;
	vTexCoords = (aTexCoords * uPc.UVscale) + uPc.UVoffset;
	gl_Position = camData.view * camData.proj * uPc.transform * vec4(aPos, 0.0f, 1.0f);
}