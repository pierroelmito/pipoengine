
#include <functional>
#include <optional>
#include <algorithm>
#include <string_view>
#include <vector>
#include <array>

#include <SDL2/SDL.h>
#include <GL/glew.h>
#include "lua.hpp"

#include "sokol_time.h"
#include "sokol_gfx.h"
#include "HandmadeMath.h"

namespace pipoengine {

using mat4 = hmm_mat4;
using vec4 = hmm_vec4;
using vec3 = hmm_vec3;
using vec2 = hmm_vec2;

struct Context;

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
	std::function<void(const Context& ctx)> frame{};
	std::function<void(const Transform&)> draw{};
};

struct Texture
{
	sg_image iid{};
};

struct Mesh
{
	Pipeline* pip{nullptr};

	Texture diffuse{};

	sg_buffer vid{};
	sg_buffer iid{};
	int pcount{0};
};

struct Context
{
	int frameWidth{ 0 };
	int frameHeight{ 0 };
	SDL_Window* window{ nullptr };
	SDL_GLContext glCtx{};

	mat4 view;
	mat4 proj;
	vec3 lightdir;

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

struct EventParams
{
	const SDL_Event& evt;
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

Pipeline MakePipeline(Context&, const sg_shader_desc* (*fn)(), std::function<void(const Context& ctx)> frame, std::function<void(const Transform&)> draw);

Texture MakeTextureRGBA(int w, int h, const std::vector<uint32_t>& data);

Mesh MakeMesh(Context&, const std::vector<BaseVertex>& vertice, const std::vector<uint16_t>& indice);
Mesh MakeHMap(Context& ctx, int ox, int oy, int w, int h, vec2 min, vec2 max, const std::function<float(int, int)>& f);
std::optional<Mesh> LoadMesh(Context& context, std::string_view path);

void DrawMesh(Context& ctx, const Mesh& mesh, const Transform& t);

void SetCamera(Context& ctx, const mat4& proj, const mat4& view);
void SetLight(Context& ctx, const vec3& lightdir);

bool Init(Context& context);
bool Release(Context& context);

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
		std::function<bool(Context&, DATA&, const EventParams&)> event {};
	};

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
				if (params.event)
					params.event(context, data, { e });
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
		Init(context);
		if (params.init)
			params.init(context, data, {});
		loop(context, data, params);
		if (params.release)
			params.release(context, data, {});
		Release(context);
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

	SDL_PauseAudioDevice(audioId, 0);
}

void Game::cleanup()
{
	SDL_CloseAudioDevice(audioId);
}

*/

