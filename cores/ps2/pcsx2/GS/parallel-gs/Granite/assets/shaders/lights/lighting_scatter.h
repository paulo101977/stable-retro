#ifndef LIGHTING_SCATTER_H_
#define LIGHTING_SCATTER_H_

#include "lighting_resources.h"

mediump float directional_scatter_phase_function(mediump float VoL)
{
	// Very crude :)
	return 0.55 - 0.45 * VoL;
}

mediump vec3 compute_scatter_lighting(
		vec3 light_world_pos,
		mediump vec3 light_camera_pos,
		mediump vec3 light_camera_front,
		mediump vec3 light_direction,
		mediump vec3 light_color)
{
#ifdef SHADOWS
	mediump float shadow_term = get_directional_shadow_term(
		light_world_pos, light_camera_pos,
		light_camera_front, light_direction);
#else
	mediump const float shadow_term = 1.0;
#endif

	float VoL = dot(normalize(light_camera_pos - light_world_pos), light_direction);
	mediump vec3 in_scatter = light_color * (directional_scatter_phase_function(VoL) * shadow_term);

#ifdef VOLUMETRIC_DIFFUSE
	in_scatter += compute_volumetric_diffuse(light_world_pos, normalize(light_world_pos - light_camera_pos), true);
#endif

#ifdef POSITIONAL_LIGHTS
	in_scatter += compute_cluster_scatter_light(light_world_pos, light_camera_pos);
#endif

	return in_scatter;
}

#endif
