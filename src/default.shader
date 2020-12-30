
@ctype mat4 hmm_mat4
@ctype vec4 hmm_vec4
@ctype vec3 hmm_vec3

@vs vs_default

uniform params_default_pass {
	mat4 view;
	mat4 proj;
};

uniform params_default_instance {
	mat4 world;
};

in vec3 vposition;
in vec3 vnormal;
in vec2 vtextcoord;
in vec4 vcolor;

out vec4 pcolor;
out vec3 pnormal;
out vec2 ptextcoord;

void main() {
	mat4 wvp = proj * view * world;
	pcolor = vcolor;
	pnormal = normalize((vec4(vnormal, 0) * world).xyz);
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
in vec2 ptextcoord;

out vec4 fragcolor;

void main() {
	float l = max(0, dot(lightdir, pnormal));
	fragcolor = vec4(l.xxx, 1) * pcolor * texture(texDiffuse, ptextcoord.xy);
}

@end

@program default vs_default fs_default

