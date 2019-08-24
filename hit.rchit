#version 460
#extension GL_NV_ray_tracing : require

layout(location = 0) rayPayloadNV vec4 hitColor;

void main() {
	hitColor = vec4(1);
}

