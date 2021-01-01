
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

Mesh MakeMesh(Context&, const std::vector<BaseVertex>& vertice, const std::vector<uint16_t>& indice)
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
	return { vid, iid, int(indice.size()) };
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

	if (ctx.lastPip.id != ctx.plDefault.pl.id) {
		ctx.lastPip = ctx.plDefault.pl;
		sg_apply_pipeline(ctx.plDefault.pl);
		ctx.plDefault.frame(ctx.view, ctx.proj);
	}

	const sg_bindings bd = {
		.vertex_buffers = { mesh.vid },
		.index_buffer = mesh.iid,
		.fs_images = { ctx.txWhite.iid },
	};

	sg_apply_bindings(&bd);

	ctx.plDefault.draw(t);

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

	context.glCtx = SDL_GL_CreateContext(context.window);
	SDL_GL_SetSwapInterval(1);

	glewInit();

	sg_desc desc_sg {};

	stm_setup();
	sg_setup(&desc_sg);

	context.plDefault = MakePipeline(context, &default_shader_desc);
	context.plDefault.frame = [] (const mat4& view, const mat4& proj) {
		params_default_pass_t ubPass {
			.view = view,
			.proj = proj,
		};
		sg_apply_uniforms(SG_SHADERSTAGE_VS, SLOT_params_default_pass, &ubPass, sizeof(ubPass));
		params_default_lighting_t ubLighting {
			.lightdir = { 0, 0, 1 },
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

