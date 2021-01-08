
#include "pipoengine.h"

namespace pe = pipoengine;

class Game
{
public:
	pe::Mesh test;
	pe::Mesh model;
	std::vector<std::pair<pe::vec2, pe::Mesh>> ground;
	pe::vec2 camAngles{ 0, 0 };
};

bool init(pe::Context& ctx, Game& g, const pe::InitParams& params)
{
	g.model = pe::LoadMesh(ctx, "pipo.ply").value_or(g.model);

	const float gsz = 10.0f;
	g.ground.reserve(5 * 5);
	for (int i = -2; i <= 2; ++i) {
		for (int j = -2; j <= 2; ++j) {
			auto msh = pe::MakeHMap(ctx,
				i * 24, j * 24,
				25, 25,
				{ -gsz, -gsz },
				{  gsz,  gsz },
				[&] (int x, int y) {
					const float sc = 0.5f;
					return 0.5f * (sin(sc * x) + sin(sc * y)) - 2.0f;
				}
			);
			msh.diffuse = ctx.txChecker;
			g.ground.push_back({ { i * 2.0f * gsz, j * 2.0f * gsz }, msh });
		}
	}

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
			HMM_Perspective(90.0f, w / float(h), 0.1f, 100.0f),
			camView
		);

		const float lightTime = 0.00008f * params.elapsed;
		pe::SetLight(
			ctx,
			HMM_NormalizeVec3({ 100.0f * cos(lightTime), 100.0f * sin(lightTime), 1 })
		);

		for (const auto& grd : g.ground)
			pe::DrawMesh(ctx, grd.second, { HMM_Translate({ grd.first.X, grd.first.Y, 0.0f }) });

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
	const float maxa = 70.0f;

	const SDL_Event& e = params.evt;
	switch (e.type) {
	case SDL_MOUSEMOTION:
		g.camAngles.X = g.camAngles.X - mSensitivity * e.motion.xrel;
		g.camAngles.Y = std::max(-maxa, std::min(maxa, g.camAngles.Y + mSensitivity * e.motion.yrel));
		break;
	default:
		break;
	}

	return true;
}

bool update(pe::Context& ctx, Game& g, const pe::UpdateParams& params)
{
	return true;
}

int main(int, char**)
{
	Game g;
	return pe::Run(g, {
		.init = init,
		.update = update,
		.draw = draw,
		.event = event,
	});
}

