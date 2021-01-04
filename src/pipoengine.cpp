
#define SOKOL_GLCORE33
#define SOKOL_IMPL
#define HANDMADE_MATH_IMPLEMENTATION

#include "pipoengine.h"

#include "shader_default.inl"

#include <map>
#include <sstream>
#include <fstream>

namespace pipoengine {

void SetCamera(Context& ctx, const mat4& proj, const mat4& view)
{
	ctx.proj = proj;
	ctx.view = view;
}

void SetLight(Context& ctx, const vec3& lightdir)
{
	ctx.lightdir = lightdir;
}

Mesh MakeMesh(Context& context, const std::vector<BaseVertex>& vertice, const std::vector<uint16_t>& indice)
{
	auto vid = sg_make_buffer({
		.size = int(vertice.size() * sizeof(BaseVertex)),
		.type = SG_BUFFERTYPE_VERTEXBUFFER,
		.content = &vertice[0],
	});
	auto iid = sg_make_buffer({
		.size = int(indice.size() * sizeof(uint16_t)),
		.type = SG_BUFFERTYPE_INDEXBUFFER,
		.content = &indice[0],
	});
	return { &context.plDefault, context.txWhite, vid, iid, int(indice.size()) };
}

Mesh MakeHMap(Context& context, int w, int h, vec2 min, vec2 max, const std::function<float(int, int)>& f)
{
	std::vector<BaseVertex> vertice;
	std::vector<uint16_t> indice;

	vertice.reserve(w * h);
	indice.reserve(6 * (w - 1) * (h - 1));

	for (int y = 0; y < h; ++y) {
		const float ry = (float(y) / float(h - 1));
		const float fy = min.Y + (max.Y - min.Y) * ry;
		for (int x = 0; x < w; ++x) {
			const float rx = (float(x) / float(w - 1));
			const float fx = min.X + (max.X - min.X) * rx;
			const float tz = f(x, y);
			const float zl = f(x - 1, y);
			const float zr = f(x + 1, y);
			const float zu = f(x, y - 1);
			const float zd = f(x, y + 1);
			const float dx = zl - zr;
			const float dy = zu - zd;
			const vec3 n{ dx, dy, 2 };
			vertice.push_back({
				{ fx, fy, tz },
				{ n.X, n.Y, n.Z },
				{ rx, ry },
				0xffff00ff
			});
		}
	}

	for (int y = 0; y < h - 1; ++y) {
		for (int x = 0; x < w - 1; ++x) {
			const uint16_t i0 = y * w + x;
			const uint16_t i1 = i0 + 1;
			const uint16_t i2 = i0 + w;
			const uint16_t i3 = i1 + w;
			const std::array<uint16_t, 6> face{ i0, i1, i2, i1, i3, i2 };
			indice.insert(indice.end(), face.begin(), face.end());
		}
	}

	return MakeMesh(context, vertice, indice);
}

std::optional<Mesh> LoadMesh(Context& context, std::string_view path)
{
	std::ifstream in(path.data());
	if (!in)
		return {};

	enum Cmp {
		px, py, pz, nx, ny, nz, u, v, r, g, b, a, count,
	};

	const std::map<std::string, Cmp> idToCmp = {
		{ "x", px },
		{ "y", py },
		{ "z", pz },
		{ "nx", nx },
		{ "ny", ny },
		{ "nz", nz },
		{ "u", u },
		{ "u", v },
		{ "red", r },
		{ "green", g },
		{ "blue", b },
		{ "alpha", a },
	};

	std::vector<std::pair<int, std::vector<Cmp>>> format;
	char buffer[512];

	while (in.getline(buffer, sizeof(buffer))) {
		if (strncmp(buffer, "element", 7) == 0) {
			std::istringstream str(buffer + 8);
			std::string type, count;
			str >> type >> count;
			format.push_back({ std::stoi(count), {} });
		} else if (strncmp(buffer, "property", 8) == 0) {
			std::istringstream str(buffer + 9);
			std::string fmt, cmp;
			str >> fmt >> cmp;
			auto& cmps = format.back().second;
			auto itf = idToCmp.find(cmp);
			if (itf != idToCmp.end())
				cmps.push_back(itf->second);
		} else if (strncmp(buffer, "end_header", 10) == 0) {
			break;
		}
	}

	if (format.size() != 2)
		return {};

	std::vector<BaseVertex> vertice;
	std::vector<uint16_t> indice;

	{
		const auto& part = format[0];
		vertice.reserve(part.first);
		std::array<float, count> data;
		std::string item;
		for (int i = 0; i < part.first; ++i) {
			if (!in.getline(buffer, sizeof(buffer)))
				return {};
			std::istringstream str(buffer);
			std::fill(data.begin(), data.end(), 0.5f);
			for (Cmp cmp : part.second) {
				str >> item;
				const float v = stof(item);
				data[cmp] = v;
			}
			vertice.push_back({
				{ data[px], data[py], data[pz] },
				{ data[nx], data[ny], data[nz] },
				{ data[u], data[v] },
				{ 0xff00ff00 },
			});
		}
	}

	{
		const auto& part = format[1];
		indice.reserve(3 * part.first);
		std::string c, v0, v1, v2;
		for (int i = 0; i < part.first; ++i) {
			if (!in.getline(buffer, sizeof(buffer)))
				return {};
			std::istringstream str(buffer);
			str >> c >> v0 >> v1 >> v2;
			const int i0 = std::stoi(v0);
			const int i1 = std::stoi(v1);
			const int i2 = std::stoi(v2);
			indice.push_back(i0);
			indice.push_back(i1);
			indice.push_back(i2);
		}
	}

	return MakeMesh(context, vertice, indice);
}

void DrawMesh(Context& ctx, const Mesh& mesh, const Transform& t)
{
	if (mesh.pcount <= 0)
		return;

	if (ctx.lastPip.id != mesh.pip->pl.id) {
		ctx.lastPip = mesh.pip->pl;
		sg_apply_pipeline(mesh.pip->pl);
		mesh.pip->frame(ctx);
	}

	const sg_bindings bd = {
		.vertex_buffers = { mesh.vid },
		.index_buffer = mesh.iid,
		.fs_images = { mesh.diffuse.iid },
	};

	sg_apply_bindings(&bd);

	mesh.pip->draw(t);

	sg_draw(0, mesh.pcount, 1);
}

Texture MakeTextureRGBA(int w, int h, const std::vector<uint32_t>& data)
{
	auto iid = sg_make_image({
		.width = w,
		.height = h,
		.content = {
			.subimage = {
				{ { &data[0], w * h * 4 } }
			}
		}
	});
	return { iid };
}

Pipeline MakePipeline(Context&, const sg_shader_desc* (*fn)())
{
	sg_shader shader = sg_make_shader(fn());

	sg_pipeline_desc pip_desc{
		.shader = shader,
		.index_type = SG_INDEXTYPE_UINT16,
		.depth_stencil = {
			.depth_compare_func = SG_COMPAREFUNC_LESS,
			.depth_write_enabled = true,
		},
		.rasterizer {
			.cull_mode = SG_CULLMODE_NONE
		},
	};

	using VtxInfo = AttrInfo<BaseVertex>;
	pip_desc.layout = {
		.attrs = {
			[ATTR_vs_default_vposition] = { 0, VtxInfo::offset(&BaseVertex::pos), SG_VERTEXFORMAT_FLOAT3 },
			[ATTR_vs_default_vnormal] = { 0, VtxInfo::offset(&BaseVertex::normal), SG_VERTEXFORMAT_FLOAT3 },
			[ATTR_vs_default_vtextcoord] = { 0, VtxInfo::offset(&BaseVertex::uv), SG_VERTEXFORMAT_FLOAT2 },
			[ATTR_vs_default_vcolor] = { 0, VtxInfo::offset(&BaseVertex::color), SG_VERTEXFORMAT_UBYTE4N },
		},
	};

	return { sg_make_pipeline(&pip_desc) };
}

bool Init(Context& context)
{
	lua_State*& interp = context.interp;
	interp = luaL_newstate();

	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER);

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

	constexpr auto flags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE;
	context.window = SDL_CreateWindow("Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720, flags);

	SDL_SetRelativeMouseMode(SDL_TRUE);

	context.glCtx = SDL_GL_CreateContext(context.window);
	SDL_GL_SetSwapInterval(1);

	glewInit();

	sg_desc desc_sg {};

	stm_setup();
	sg_setup(&desc_sg);

	//SDL_

	context.plDefault = MakePipeline(context, &default_shader_desc);
	context.plDefault.frame = [] (const Context& ctx) {
		params_default_pass_t ubPass {
			.view = ctx.view,
			.proj = ctx.proj,
		};
		sg_apply_uniforms(SG_SHADERSTAGE_VS, SLOT_params_default_pass, &ubPass, sizeof(ubPass));
		params_default_lighting_t ubLighting {
			.lightdir = ctx.lightdir,
		};
		sg_apply_uniforms(SG_SHADERSTAGE_FS, SLOT_params_default_lighting, &ubLighting, sizeof(ubLighting));
	};
	context.plDefault.draw = [] (const Transform& transform) {
		params_default_instance_t ubInstance {
			.world = transform.world
		};
		sg_apply_uniforms(SG_SHADERSTAGE_VS, SLOT_params_default_instance, &ubInstance, sizeof(ubInstance));
	};

	context.txWhite = MakeTextureRGBA(2, 2, { 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff });
	context.txChecker = MakeTextureRGBA(2, 2, { 0xffffffff, 0x000000ff, 0x000000ff, 0xffffffff });

	return true;
}

bool Release(Context& context)
{
	sg_shutdown();

	SDL_GL_DeleteContext(context.glCtx);
	SDL_DestroyWindow(context.window);

	SDL_Quit();

	return true;
}

}

