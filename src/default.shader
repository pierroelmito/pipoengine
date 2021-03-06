
@include common.shader

@vs vs_default

uniform params_default_pass {
	mat4 view;
	mat4 proj;
};

uniform params_default_instance {
	mat4 world;
};

@include_block base_vertex

out vec4 pcolor;
out vec3 pnormal;
out vec3 pcampos;
out vec3 pworldpos;
out vec2 ptextcoord;

void main() {
	mat4 wvp = proj * view * world;
	vec4 worldpos = world * vec4(vposition.xyz, 1);
	pcampos = (view * vec4(0, 0, 0, 1)).xyz;
	pcolor = vcolor;
	pnormal = normalize((vec4(vnormal, 0) * world).xyz);
	pworldpos = worldpos.xyz / worldpos.w;
	ptextcoord = vtextcoord;
	gl_Position = wvp * vec4(vposition.xyz, 1);
}

@end

@fs fs_default

uniform params_default_lighting {
	vec3 lightdir;
};

uniform sampler2D texDiffuse;

in vec4 pcolor;
in vec3 pnormal;
in vec3 pcampos;
in vec3 pworldpos;
in vec2 ptextcoord;

out vec4 fragcolor;

void main() {
	vec3 n = normalize(pnormal);
	vec3 viewdir = normalize(pworldpos - pcampos);
	vec3 rdir = reflect(viewdir, n);
	//vec3 rdir = -normalize(lightdir + viewdir);
	float depth = length(pworldpos - pcampos);
	float fog = 1.0f - exp(0.001f * -depth);

	float ndotl = dot(lightdir, n);
	float ndotr = dot(lightdir, rdir);
	//float ndotr = max(0, dot(rdir, n));
	float diff = pow(0.5 * (1 +  ndotl), 2);
	float spec = pow(ndotr, 32);
	float fresnel = pow(1 - abs(dot(n, -viewdir)), 16);

	fragcolor = vec4(diff.xxx, 1) * pcolor * texture(texDiffuse, ptextcoord.xy);
	fragcolor.xyz = fragcolor.xyz + (diff * 0.5f * fresnel).xxx;
	fragcolor.xyz = fragcolor.xyz + 0.3f * spec.xxx;
	fragcolor.xyz = mix(fragcolor.xyz, vec3(0.3, 0.3, 0.6), fog);
}

@end

@program default vs_default fs_default

