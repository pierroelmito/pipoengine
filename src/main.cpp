
#include "pipoengine.h"

namespace pe = pipoengine;

class Game
{
public:
	pe::Mesh test;
	pe::Mesh model;
	pe::Mesh ground;
	hmm_vec2 camAngles{ 0, 0 };
};

int fnYolo(lua_State* l)
{
	return 0;
}

bool init(pe::Context& ctx, Game& g, const pe::InitParams& params)
{
	lua_register(ctx.interp, "yolo", fnYolo);

	g.model = pe::LoadMesh(ctx, "pipo.ply").value_or(g.model);

	const int hmsz = 34;
	std::vector<float> heightData;
	heightData.resize(hmsz * hmsz, 0.0f);
	for (int y = 0; y < hmsz; ++y) {
		for (int x = 0; x < hmsz; ++x) {
			const float sc = 0.5f;
			heightData[y * hmsz + x] = 0.6f * (sin(sc * x) + sin(sc * y)) - 2.0f;
		}
	}

	const float gsz = 10.0f;
	g.ground = pe::MakeHMap(ctx,
		hmsz - 2, hmsz - 2,
		{ -gsz, -gsz },
		{  gsz,  gsz },
		[&] (int x, int y) {
			const int ofs = (y + 1) * hmsz + x + 1;
			return heightData[ofs];
		}
	);
	g.ground.diffuse = ctx.txChecker;

	const std::array<float, 2> uv{ 0.5f, 0.5f };
	const std::array<float, 3> n { 0, 0, 1 };
	g.test = pe::MakeMesh(ctx,
		{
			{ { -0.5f, -0.5f, 0.0f }, n, uv, 0xffff0000 },
			{ {  0.5f, -0.5f, 0.0f }, n, uv, 0xff00ff00 },
			{ { -0.5f,  0.5f, 0.0f }, n, uv, 0xff0000ff },
			{ {  0.5f,  0.5f, 0.0f }, n, uv, 0xffffffff },
		},
		{
			0, 1, 2,
			1, 3, 2,
		}
	);

	return true;
}

bool draw(pe::Context& ctx, Game& g, const pe::DrawParams& params)
{
	pe::RenderMain(ctx, [&] (int w, int h) {
		const hmm_mat4 r0{
			.Elements = {
				{ 1, 0, 0, 0 },
				{ 0, 0, 1, 0 },
				{ 0, 1, 0, 0 },
				{ 0, 0, 0, 1 },
			}
		};
		const hmm_mat4 r1 = HMM_Rotate(g.camAngles.X, { 0, 0, 1 });
		const hmm_mat4 r2 = HMM_Rotate(g.camAngles.Y, { 1, 0, 0 });
		const pe::mat4 camView = r2 * r0 * r1;

		pe::SetCamera(
			ctx,
			HMM_Perspective(90.0f, w / float(h), 0.5f, 100.0f),
			camView
		);

		const float lightTime = 0.00008f * params.elapsed;
		pe::SetLight(
			ctx,
			HMM_NormalizeVec3({ cos(lightTime), 0, sin(lightTime) })
		);

		pe::DrawMesh(ctx, g.ground, { HMM_Translate({ 0.0f, 0.0f, 0.0f }) });
		pe::DrawMesh(ctx, g.model, { HMM_Translate({ -1.0f, -1.0f, 0.0f }) });
		pe::DrawMesh(ctx, g.model, { HMM_Translate({  1.0f, -1.0f, 0.0f }) });
		pe::DrawMesh(ctx, g.model, { HMM_Translate({ -1.0f,  1.0f, 0.0f }) });
		pe::DrawMesh(ctx, g.model, { HMM_Translate({  1.0f,  1.0f, 0.0f }) });
		pe::DrawMesh(ctx, g.test, { HMM_Translate({ 0.0f, 0.0f, 2.0f }) });
	});

	return true;
}

bool event(pe::Context& ctx, Game& g, const pe::EventParams& params)
{
	const float mSensitivity = 0.2f;
	SDL_Event e = params.evt;
	switch (e.type) {
	case SDL_MOUSEMOTION:
		g.camAngles.X -= mSensitivity * e.motion.xrel;
		g.camAngles.Y += mSensitivity * e.motion.yrel;
		break;
	default:
		break;
	}
	return true;
}

int main(int, char**)
{
	Game g;
	return pe::Run(g, {
		.init = init,
		.draw = draw,
		.event = event,
	});
}

