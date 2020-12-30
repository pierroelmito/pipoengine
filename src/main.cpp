
#include "pipoengine.inl"

namespace pe = pipoengine;

class Game
{
public:
	pe::Mesh test;
	pe::Mesh model;
};

int fnYolo(lua_State* l)
{
	return 0;
}

bool init(pe::Context& ctx, Game& g, const pe::InitParams& params)
{
	lua_register(ctx.interp, "yolo", fnYolo);

	g.model = pe::LoadMesh(ctx, "pipo.ply").value_or(g.model);

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
	const float camTime = 0.0003f * params.elapsed;
	const float camRadius = 1.0f;
	const hmm_vec3 camPos{ camRadius * cos(camTime), camRadius * sin(camTime), 2.0f };

	pe::RenderMain(ctx, [&] (int w, int h) {
		pe::SetCamera(
			ctx,
			HMM_Perspective(90.0f, w / float(h), 0.5f, 100.0f),
			HMM_LookAt(
				camPos,
				{ 0.0f, 0.0f, 0.0f },
				{ 0.0f, 0.0f, 1.0f }
			)
		);
		pe::DrawMesh(ctx, g.model, { HMM_Translate({ -1.0f, -1.0f, 0.0f }) });
		pe::DrawMesh(ctx, g.model, { HMM_Translate({  1.0f, -1.0f, 0.0f }) });
		pe::DrawMesh(ctx, g.model, { HMM_Translate({ -1.0f,  1.0f, 0.0f }) });
		pe::DrawMesh(ctx, g.model, { HMM_Translate({  1.0f,  1.0f, 0.0f }) });
		pe::DrawMesh(ctx, g.test, { HMM_Translate({ 0.0f, 0.0f, 0.0f }) });
	});

	return true;
}

int main(int, char**)
{
	Game g;
	return pe::Run(g, {
		.init = init,
		.draw = draw,
	});
}

