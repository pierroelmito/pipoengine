
#include "pipoengine.h"

namespace pe = pipoengine;

class Game
{
public:
	pe::Mesh test;
	pe::Mesh model;
	pe::Mesh ground;
};

int fnYolo(lua_State* l)
{
	return 0;
}

template <typename FN>
pe::Mesh makeHMap(pe::Context& ctx, int w, int h, hmm_vec2 min, hmm_vec2 max, const FN& f)
{
	std::vector<pe::BaseVertex> vertice;
	std::vector<uint16_t> indice;

	vertice.reserve(w * h);
	indice.reserve(6 * (w - 1) * (h - 1));

	const std::array<float, 2> uv{ 0.5f, 0.5f };
	const std::array<float, 3> n{ 0, 0, 1 };

	for (int y = 0; y < h; ++y) {
		const float fy = min.Y + (max.Y - min.Y) * (float(y) / float(h - 1));
		for (int x = 0; x < w; ++x) {
			const float fx = min.X + (max.X - min.X) * (float(x) / float(w - 1));
			vertice.push_back({
				{ fx, fy, f(x, y) },
				n,
				uv,
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

	return pe::MakeMesh(ctx, vertice, indice);
}

bool init(pe::Context& ctx, Game& g, const pe::InitParams& params)
{
	lua_register(ctx.interp, "yolo", fnYolo);

	g.model = pe::LoadMesh(ctx, "pipo.ply").value_or(g.model);

	const float gsz = 3.0f;
	g.ground = makeHMap(ctx,
		32, 32,
		{ -gsz, -gsz },
		{  gsz,  gsz },
		[&] (int x, int y) {
			return 0.3f - ((rand() % 1000) / 1000.0f);
		}
	);

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
		const float camTime = 0.00008f * params.elapsed;
		const float camRadius = 1.0f;
		const hmm_vec3 camPos{ camRadius * cos(camTime), camRadius * sin(camTime), 2.0f };
		pe::SetCamera(
			ctx,
			HMM_Perspective(90.0f, w / float(h), 0.5f, 100.0f),
			HMM_LookAt(camPos, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f })
		);
		pe::DrawMesh(ctx, g.ground, { HMM_Translate({ 0.0f, 0.0f, 0.0f }) });
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

