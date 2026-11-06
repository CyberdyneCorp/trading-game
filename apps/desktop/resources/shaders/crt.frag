// TradeLab CRT post-process (design "Retro look"): a Raven effect (raven_shader.h) on a full-window
// overlay Border that reads what is drawn behind it (R_SHADER_BACKDROP). Static: nothing depends on
// time, so it never flickers. Like the web's CrtOverlay: scanlines (every 3rd pixel row 18% darker)
// and a vignette; plus a soft bloom of the bright phosphor and a fixed grain.
//   params.x  scanlines 0/1    params.y  bloom 0/1    params.z  noise 0/1
// Rebuild crt.spv after editing (scripts/build_shaders.sh); a test checks crt.spv matches this file.
#version 450
#extension GL_GOOGLE_include_directive : require
#include "raven_effect.glsl"

vec4 raven_effect(RavenEffect fx) {
	vec3 color = raven_backdrop(fx.screen).rgb;
	if (fx.params.y > 0.5) {
		// Bloom: the blurred backdrop added back where it is bright (the green phosphor glows).
		vec3 blur = raven_backdrop_blur(fx.screen).rgb;
		float glow = smoothstep(0.35, 0.9, max(blur.r, max(blur.g, blur.b)));
		color += blur * glow * 0.35;
	}
	if (fx.params.x > 0.5 && mod(floor(fx.pixel.y), 3.0) >= 2.0) {
		color *= 0.82;
	}
	if (fx.params.z > 0.5) {
		// Fixed grain: a hash of the pixel, never of the time.
		color += (raven_hash(floor(fx.pixel)) - 0.5) * 0.035;
	}
	// Vignette: clear to 60% of the radius, 45% darker at the corners.
	vec2 d = fx.uv - 0.5;
	float r = length(d) / length(vec2(0.5));
	color *= 1.0 - 0.45 * smoothstep(0.6, 1.0, r);
	return vec4(clamp(color, 0.0, 1.0), 1.0);
}
