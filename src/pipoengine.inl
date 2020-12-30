
#include <functional>
#include <optional>
#include <algorithm>
#include <string_view>
#include <vector>
#include <map>
#include <array>
#include <sstream>
#include <fstream>

#include <SDL2/SDL.h>
#include <GL/glew.h>
#include "lua.hpp"

#define SOKOL_GLCORE33
#define SOKOL_IMPL
#define HANDMADE_MATH_IMPLEMENTATION

#include "sokol_time.h"
#include "sokol_gfx.h"
#include "HandmadeMath.h"

#include "shader_default.inl"

namespace pipoengine {

using mat4 = hmm_mat4;

struct Transform
{
	mat4 world;
};

struct BaseVertex
{
	std::array<float, 3> pos;
	std::array<float, 3> normal;
	std::array<float, 2> uv{ 0.5f, 0.5f };
	uint32_t color{ 0xffffffff };
};

struct Pipeline
{
	sg_pipeline pl{};
	std::function<void(const mat4& view, const mat4& proj)> frame{};
	std::function<void(const Transform&)> draw{};
};

struct Texture
{
	sg_image iid{};
};

struct Mesh
{
	sg_buffer vid{};
	sg_buffer iid{};
	int pcount{0};
};

struct Context
{
	SDL_Window* window{ nullptr };
	SDL_GLContext glCtx{};
	mat4 view;
	mat4 proj;
	int frameWidth{0};
	int frameHeight{0};
	lua_State* interp{ nullptr };
	Pipeline plDefault{};
	Texture txWhite{};
	Texture txChecker{};
	sg_pipeline lastPip{};
};

struct InitParams
{
};

struct ReleaseParams
{
};

struct UpdateParams
{
	const double elapsed;
	const uint32_t step;
	const uint32_t updateMs;
};

struct DrawParams
{
	const double elapsed;
};

template <class T>
int attrOffset(T)
{
	return 0;
}

template <typename S>
struct AttrInfo
{
	template <typename T, size_t N>
	static int offset(std::array<T, N> S::*m)
	{
		S* ptr = nullptr;
		std::array<T, N>* mptr = &(ptr->*m);
		return int((uint8_t*)mptr - (uint8_t*)ptr);
	}
	template <typename T>
	static int offset(T S::*m)
	{
		S* ptr = nullptr;
		T* mptr = &(ptr->*m);
		return int((uint8_t*)mptr - (uint8_t*)ptr);
	}
};

inline Pipeline MakePipeline(Context&, const sg_shader_desc* (*fn)())
{
	sg_shader shader = sg_make_shader(fn());

	sg_pipeline_desc pip_desc{
		.shader = shader,
		.index_type = SG_INDEXTYPE_UINT16,
		.rasterizer { .cull_mode = SG_CULLMODE_NONE },
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

inline Texture MakeTextureRGBA(int w, int h, const std::vector<uint32_t>& data)
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

inline Mesh MakeMesh(Context&, const std::vector<BaseVertex>& vertice, const std::vector<uint16_t>& indice)
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

inline std::optional<Mesh> LoadMesh(Context& context, std::string_view path)
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

inline void DrawMesh(Context& ctx, const Mesh& mesh, const Transform& t)
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

inline void SetCamera(Context& ctx, const mat4& proj, const mat4& view)
{
	ctx.proj = proj;
	ctx.view = view;
}

template <class FN>
void RenderMain(Context& ctx, const FN& fn)
{
	const float c = 0.2f;
	sg_begin_default_pass({
			.colors = { { SG_ACTION_CLEAR, { c, c, c, 1.0f } } }
		},
		ctx.frameWidth,
		ctx.frameHeight
	);

	ctx.lastPip = { ~0u };

	fn(ctx.frameWidth, ctx.frameHeight);

	sg_end_pass();
	sg_commit();
}

template <class DATA>
struct Runner {

	struct RunParams
	{
		std::function<bool(Context&, DATA&, const InitParams&)> init {};
		std::function<bool(Context&, DATA&, const UpdateParams&)> update {};
		std::function<bool(Context&, DATA&, const DrawParams&)> draw {};
		std::function<bool(Context&, DATA&, const ReleaseParams&)> release {};
		std::function<bool(Context&, DATA&, const SDL_Event&)> event {};
	};

	static bool init(Context& context)
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

	static bool release(Context& context)
	{
		sg_shutdown();

		SDL_GL_DeleteContext(context.glCtx);
		SDL_DestroyWindow(context.window);

		SDL_Quit();

		return true;
	}

	static int loop(Context& context, DATA& data, const RunParams& params)
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
				//state->handleEvent({ *this, e });
				if (params.event)
					params.event(context, data, e);
			}

			const uint64_t currentTick = stm_now();
			while (updateTick < currentTick) {
				updateTick += updateMs * 1000000;
				++step;
				const uint64_t start = stm_now();
				if (params.update)
					params.update(context, data, { elapsed(), step, updateMs });
				updateDuration = 0.5 * (updateDuration + stm_ms(stm_since(start)));
			}

			{
				SDL_GL_GetDrawableSize(context.window, &context.frameWidth, &context.frameHeight);
				++frame;
				const uint64_t start = stm_now();
				if (params.draw)
					params.draw(context, data, { elapsed() });
				frameDuration = 0.5 * (frameDuration + stm_ms(stm_since(start)));
			}

			SDL_GL_SwapWindow(context.window);

			const unsigned int dt = static_cast<unsigned int>(stm_ms(stm_diff(stm_now(), loop)));
			if (dt < frameMs)
				SDL_Delay(frameMs - dt);
		}

		return 0;
	}

	static int exec(DATA& data, const RunParams& params)
	{
		Context context;
		init(context);
		if (params.init)
			params.init(context, data, {});
		loop(context, data, params);
		if (params.release)
			params.release(context, data, {});
		release(context);
		return 0;
	}
};

template <class T>
int Run(T& data, const typename Runner<T>::RunParams& params)
{
	return Runner<T>::exec(data, params);
}

}

/*

int StateInterface::axisMove(std::map<Sint32, float>& values, const SDL_Event& e)
{
	int r = 0;
	float& lastY = values[e.caxis.which];
	float newY = e.caxis.value / 32768.0f;
	if (newY < -0.5f && !(lastY < -0.5f))
		r = -1;
	if (newY > 0.5f && !(lastY > 0.5f))
		r = 1;
	lastY = newY;
	return r;
}

void Game::init()
{
	const bool mainOk = luaL_dofile(interp, "scripts/main.lua") == 0;
	(void)mainOk;

	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER);

	SDL_AudioSpec wantedSpecs;
	wantedSpecs.freq = 48000;
	wantedSpecs.format = AUDIO_F32;
	wantedSpecs.channels = 2;
	wantedSpecs.samples = 4096;
	wantedSpecs.callback = Game_AudioCallback;
	wantedSpecs.userdata = this;

	audioId = SDL_OpenAudioDevice(nullptr, 0, &wantedSpecs, &audioSpecs, 1);

	Mesh* bg = render.getMesh("proc:quad/bg");
	bg->first.fs_images[SLOT_bgTex] = render.getImage("file:textures/dirt00.png");
	bg->first.fs_images[SLOT_bgNoise] = render.getImage("file:textures/bgnoise.png");

	Mesh* text = render.getMesh("proc:quad/text");
	con = { text, 112, 52 };
	text->first.fs_images[SLOT_textFont] = render.getImage("file:textures/font00.png");
	text->first.fs_images[SLOT_textData] = setupTextBuffer(con);

	SDL_PauseAudioDevice(audioId, 0);

	sprite = render.getMesh("proc:quad/sprite");
}

void Game::main()
{
	while(!SDL_QuitRequested() && !states.empty()) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			switch (e.type) {
			case SDL_CONTROLLERDEVICEADDED:
				{
					const int idx = e.cdevice.which;
					auto ctrl = SDL_GameControllerOpen(idx);
					controllers[idx] = std::make_pair(ctrl, SDL_JoystickGetDeviceInstanceID(idx));
				}
				break;
			case SDL_CONTROLLERDEVICEREMOVED:
				{
					const int idx = IdxfromID(e.cdevice.which);
					SDL_GameControllerClose(controllers[idx].first);
					controllers[idx].first = nullptr;
				}
				break;
			case SDL_CONTROLLERDEVICEREMAPPED:
				{
					//const int idx = IdxfromID(e.cdevice.which);
					//controllers[idx] = nullptr;
				}
				break;
			case SDL_KEYDOWN:
				if (e.key.keysym.sym == SDLK_F1)
					dbgInfo = !dbgInfo;
				break;
			}
			state->handleEvent({ *this, e });
		}
	}
}

void Game::cleanup()
{
	SDL_CloseAudioDevice(audioId);
}

int Game::IdxfromID(int which)
{
	for (int i = 0; i < 4; ++i) {
		//if (SDL_JoystickGetDeviceInstanceID(i) == which)
		if (controllers[i].second == which)
			return i;
	}
	return -1;
}

void Game::drawRect(const hmm_vec2 screen, const hmm_vec4& rect, const hmm_vec4& col)
{
	params_sprite_t cst;
	cst.screen = { screen.X, screen.Y, 0.0f, 0.0f };
	cst.rect = rect;
	cst.col = col;

	render.draw(Pipeline::Sprite, sprite, [&] () {
		sg_apply_uniforms(SG_SHADERSTAGE_VS, SLOT_params_sprite, &cst, sizeof(cst));
	});
}
*/

