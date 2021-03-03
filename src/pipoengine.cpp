
#define SOKOL_GLCORE33
#define SOKOL_IMPL
#define HANDMADE_MATH_IMPLEMENTATION
#define TINYDDSLOADER_IMPLEMENTATION

#include "pipoengine.h"

#include "shader_default.inl"

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

Mesh MakeMesh(
	Context& context,
	const std::variant<sg_buffer, std::vector<BaseVertex>>& vertice,
	const std::variant<std::pair<sg_buffer, int>, std::vector<uint16_t>>& indice)
{
	std::optional<sg_buffer> vid, iid;
	int sz;

	if (const sg_buffer* pvid = std::get_if<sg_buffer>(&vertice); pvid) {
		vid = *pvid;
	} else if (const std::vector<BaseVertex>* vdata = std::get_if<std::vector<BaseVertex>>(&vertice); vdata) {
		vid = sg_make_buffer({
			//.size = int(vdata->size() * sizeof(BaseVertex)),
			.type = SG_BUFFERTYPE_VERTEXBUFFER,
			.data = { &((*vdata)[0]), vdata->size() * sizeof(BaseVertex) },
		});
	}

	if (const std::pair<sg_buffer, int>* piid = std::get_if<std::pair<sg_buffer, int>>(&indice); piid) {
		iid = piid->first;
		sz = piid->second;
	} else if (const std::vector<uint16_t>* idata = std::get_if<std::vector<uint16_t>>(&indice); idata) {
		iid = sg_make_buffer({
			//.size = int(idata->size() * sizeof(uint16_t)),
			.type = SG_BUFFERTYPE_INDEXBUFFER,
			.data = { &((*idata)[0]), idata->size() * sizeof(uint16_t) },
		});
		sz = idata->size();
	}

	return { &context.plDefault, context.txWhite, *vid, *iid, sz };
}

Mesh MakeHMap(Context& context, const int ox, const int oy, const int w, const int h, const vec2 min, const vec2 max, const std::function<float(int, int)>& f)
{
	std::vector<BaseVertex> vertice;
	std::vector<vec3> normals;
	std::vector<uint16_t> indice;

	const float xsz = (max.X - min.X) / w;
	const float ysz = (max.Y - min.Y) / h;
	const float mweight = 4.0f;

	vertice.reserve(w * h);
	normals.reserve(w * h);
	for (int y = 0; y < h; ++y) {
		const float ry = (float(y) / float(h - 1));
		const float fy = min.Y + (max.Y - min.Y) * ry;
		for (int x = 0; x < w; ++x) {
			const float rx = (float(x) / float(w - 1));
			const float fx = min.X + (max.X - min.X) * rx;

			const float tz = f(ox + x, oy + y);
			const float zl = f(ox + x - 1, oy + y);
			const float zr = f(ox + x + 1, oy + y);
			const float zu = f(ox + x, oy + y - 1);
			const float zd = f(ox + x, oy + y + 1);
			const float zul = f(ox + x - 1, oy + y - 1);
			const float zur = f(ox + x + 1, oy + y - 1);
			const float zdl = f(ox + x - 1, oy + y + 1);
			const float zdr = f(ox + x + 1, oy + y + 1);

			const float dx = (mweight * (zl - zr) + (zul - zur) + (zdr - zdl)) / (2.0f + mweight);
			const float dy = (mweight * (zu - zd) + (zul - zdl) + (zur - zdr)) / (2.0f + mweight);

			const vec3 v0{ -2.0f * xsz, 0.0f, dx };
			const vec3 v1{ 0.0f, -2.0f * ysz, dy };
			const vec3 n = HMM_Normalize(HMM_Cross(v0, v1));

			normals.push_back(n);
			vertice.push_back({
				{ fx, fy, tz },
				{ n.X, n.Y, n.Z },
				{ rx, ry },
				0xffff00ff
			});
		}
	}

	auto& hmapIid = context.hmapIndexBuffer[{ w, h }];
	if (hmapIid.second == 0) {
		indice.reserve(6 * (w - 1) * (h - 1));
		for (int y = 0; y < h - 1; ++y) {
			for (int x = 0; x < w - 1; ++x) {
				const uint16_t i0 = y * w + x;
				const uint16_t i1 = i0 + 1;
				const uint16_t i2 = i0 + w;
				const uint16_t i3 = i1 + w;
				if ((x ^ y) & 1) {
					const std::array<uint16_t, 6> face{ i0, i3, i2, i1, i3, i0 };
					indice.insert(indice.end(), face.begin(), face.end());
				} else {
					const std::array<uint16_t, 6> face{ i0, i1, i2, i1, i3, i2 };
					indice.insert(indice.end(), face.begin(), face.end());
				}
			}
		}
		auto iid = sg_make_buffer({
			.type = SG_BUFFERTYPE_INDEXBUFFER,
			.data= { &indice[0], indice.size() * sizeof(uint16_t) },
		});
		hmapIid = { iid, indice.size() };
	}

	return MakeMesh(context, vertice, hmapIid);
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
		.data = {
			.subimage = {
				{ { &data[0], w * h * 4u } }
			}
		}
	});
	return { iid };
}

std::optional<Texture> LoadDDS(const std::vector<std::string>& arrayItems)
{
	tinyddsloader::DDSFile dds;

	/*auto loadStatus =*/ dds.Load(arrayItems[0].c_str());

	const int w = dds.GetWidth();
	const int h = dds.GetHeight();
	const int mips = dds.GetMipCount();

	sg_image_desc img_desc {
		.width = w,
		.height = h,
		.num_mipmaps = mips,
		.max_anisotropy = 4,
	};

	for (int i = 0; i < mips; ++i) {
		const tinyddsloader::DDSFile::ImageData* data = dds.GetImageData(i);
		img_desc.data.subimage[0][i] = {
			.ptr = data->m_mem,
			.size = data->m_memSlicePitch,
		};
	}

	Texture tex{ sg_make_image(&img_desc) };
	return tex;
}

void LoadPPM(
	std::string_view path,
	const std::function<void (int, int)>& init,
	const std::function<void (std::array<float, 3>)>& pix
)
{
	FILE* f = fopen(path.data(), "rb");

	char buffer[256];
	fgets(buffer, sizeof(buffer), f);
	std::cout << buffer;
	fgets(buffer, sizeof(buffer), f);
	std::cout << buffer;
	fgets(buffer, sizeof(buffer), f);
	std::cout << buffer;
	fgets(buffer, sizeof(buffer), f);
	std::cout << buffer;

	init(512, 512);
	uint8_t rgb[6];
	const int s0 = 8;
	const int s1 = 0;
	for (int y = 0; y < 512; ++y) {
		for (int x = 0; x < 512; ++x) {
			/*size_t ct =*/ fread(&rgb, 1, 6, f);
			int r = (rgb[0] << s0) + (rgb[1] << s1);
			int g = (rgb[2] << s0) + (rgb[3] << s1);
			int b = (rgb[4] << s0) + (rgb[5] << s1);
			pix({ float(r) / 65535.0f, float(g) / 65535.0f, float(b) / 65535.0f });
		}
	}
}

Pipeline MakePipeline(Context&, const sg_shader_desc* (*fn)(sg_backend), std::function<void(const Context& ctx)> frame, std::function<void(const Transform&)> draw)
{
	sg_shader shader = sg_make_shader(fn(sg_query_backend()));

	sg_pipeline_desc pip_desc{
		.shader = shader,
		.depth = {
			.compare = SG_COMPAREFUNC_LESS,
			.write_enabled = true,
		},
		.index_type = SG_INDEXTYPE_UINT16,
		.cull_mode = SG_CULLMODE_BACK,
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

	return {
		sg_make_pipeline(&pip_desc),
		frame,
		draw,
	};
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
	//SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
	//SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 1);

	constexpr auto flags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE;
	context.window = SDL_CreateWindow("Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720, flags);

	SDL_SetRelativeMouseMode(SDL_TRUE);

	context.glCtx = SDL_GL_CreateContext(context.window);
	SDL_GL_SetSwapInterval(1);

	glewInit();

	sg_desc desc_sg {
		.buffer_pool_size = 2048,
	};

	stm_setup();
	sg_setup(&desc_sg);

	//SDL_

	context.plDefault = MakePipeline(
		context,
		&default_shader_desc,
		[] (const Context& ctx) {
			params_default_pass_t ubPass {
				.view = ctx.view,
				.proj = ctx.proj,
			};
			sg_apply_uniforms(SG_SHADERSTAGE_VS, SLOT_params_default_pass, { &ubPass, sizeof(ubPass) });
			params_default_lighting_t ubLighting {
				.lightdir = ctx.lightdir,
			};
			sg_apply_uniforms(SG_SHADERSTAGE_FS, SLOT_params_default_lighting, { &ubLighting, sizeof(ubLighting) });
		},
		[] (const Transform& transform) {
			params_default_instance_t ubInstance {
				.world = transform.world
			};
			sg_apply_uniforms(SG_SHADERSTAGE_VS, SLOT_params_default_instance, { &ubInstance, sizeof(ubInstance) });
		}
	);

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

int Loop(Context& context, const RunParams& params)
{
	unsigned int step = 0;
	unsigned int frame = 0;

	const unsigned int updateMs = 8;
	const unsigned int frameMs = 1000 / 100;

	double updateDuration {};
	double frameDuration {};
	double loopDuration {};

	const uint64_t startTick = stm_now();
	uint64_t loop = startTick;
	uint64_t updateTick = startTick;
	auto elapsed = [&] () -> double { return double(updateTick - startTick) / 100000.0; };

	while(!SDL_QuitRequested()) {
		loopDuration = 0.5 * (loopDuration + stm_ms(stm_laptime(&loop)));

		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			//switch (e.type) {
			//case SDL_CONTROLLERDEVICEADDED:
			//	break;
			//case SDL_CONTROLLERDEVICEREMOVED:
			//	break;
			//case SDL_CONTROLLERDEVICEREMAPPED:
			//	break;
			//case SDL_KEYDOWN:
			//	break;
			//}
			if (params.event)
				params.event(context, { e });
		}

		const uint64_t currentTick = stm_now();
		while (updateTick < currentTick) {
			updateTick += updateMs * 1000000;
			++step;
			const uint64_t start = stm_now();
			if (params.update)
				params.update(context, { elapsed(), step, updateMs });
			updateDuration = 0.5 * (updateDuration + stm_ms(stm_since(start)));
		}

		{
			SDL_GL_GetDrawableSize(context.window, &context.frameWidth, &context.frameHeight);
			++frame;
			const uint64_t start = stm_now();
			if (params.draw)
				params.draw(context, { elapsed() });
			frameDuration = 0.5 * (frameDuration + stm_ms(stm_since(start)));
		}

		SDL_GL_SwapWindow(context.window);

		const unsigned int dt = static_cast<unsigned int>(stm_ms(stm_diff(stm_now(), loop)));
		if (dt < frameMs)
			SDL_Delay(frameMs - dt);
	}

	return 0;
}

int Exec(const RunParams& params)
{
	Context context;
	Init(context);
	if (params.init)
		params.init(context, {});
	Loop(context, params);
	if (params.release)
		params.release(context, {});
	Release(context);
	return 0;
}

}

