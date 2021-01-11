
#include "pipoengine.h"

namespace pe = pipoengine;

class Game
{
public:
	pe::Mesh test;
	pe::Mesh model;
	std::vector<std::pair<pe::vec2, pe::Mesh>> ground;
	pe::vec2 camAngles{ 0, 0 };
	pe::vec3 camPos{ 0, 0, 0 };
};

bool init(pe::Context& ctx, Game& g, const pe::InitParams& params)
{
	g.model = pe::LoadMesh(ctx, "pipo.ply").value_or(g.model);

	tinyddsloader::DDSFile hmap;
	auto loadStatus = hmap.Load("hmap.dds");
	const int hmapWidth = hmap.GetWidth();
	const int hmapHeight = hmap.GetHeight();
	std::cout << "Hmap " << hmapWidth << "x" << hmapHeight << " " << hmap.GetMipCount() << " mipmaps\n";
	std::cout << "\t-> " << loadStatus << "\n";
	const tinyddsloader::DDSFile::ImageData* hmapData = hmap.GetImageData();
	const uint8_t* hmapPtr = (const uint8_t*)hmapData->m_mem;

	const float gsz = 16.0f;
	const int tx = hmapWidth / 24;
	const int ty = hmapHeight / 24;
	g.ground.reserve(tx * ty);
	for (int i = 0; i < tx; ++i) {
		for (int j = 0; j < ty; ++j) {
			auto msh = pe::MakeHMap(ctx,
				1 + i * 24, 1 + j * 24,
				25, 25,
				{ -gsz, -gsz },
				{  gsz,  gsz },
				[&] (int x, int y) {
					x = std::max(0, std::min(hmapWidth, x));
					y = std::max(0, std::min(hmapHeight, y));
					const int o = (y * hmapWidth + x) * 4;
					const int iz0 = hmapPtr[o + 1];
					const int iz1 = hmapPtr[o + 2];
					const int iz2 = hmapPtr[o + 0];
					const float fz0 = (iz0 * 50.0f) / 255.0f;
					const float fz1 = (iz1 * 100.0f) / 255.0f;
					const float fz2 = (iz2 * 5.0f) / 255.0f;
					return fz0 + fz1 + fz2 - 100.0f;
					//const float sc = 0.5f;
					//return 0.5f * (sin(sc * x) + sin(sc * y)) - 2.0f;
				}
			);
			msh.diffuse = ctx.txChecker;
			g.ground.push_back({ { (i - tx / 2) * 2.0f * gsz, (j - ty / 2) * 2.0f * gsz }, msh });
		}
	}

	const float wsz = 500.0f;
	const float wh = -55.0f;
	const std::array<float, 2> uv{ 0.5f, 0.5f };
	const std::array<float, 3> n { 0, 0, 1 };
	const std::vector<pe::BaseVertex> vertice =  {
		{ { -wsz, -wsz, wh }, n, uv, 0xffff0000 },
		{ {  wsz, -wsz, wh }, n, uv, 0xffff0000 },
		{ { -wsz,  wsz, wh }, n, uv, 0xffff0000 },
		{ {  wsz,  wsz, wh }, n, uv, 0xffff0000 },
	};
	const std::vector<uint16_t> indice = {
		0, 1, 2,
		1, 3, 2,
	};
	g.test = pe::MakeMesh(ctx, vertice, indice);

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
		const hmm_mat4 t = HMM_Translate(g.camPos);
		const pe::mat4 camView = r2 * r0 * r1 * t;

		pe::SetCamera(
			ctx,
			HMM_Perspective(90.0f, w / float(h), 0.1f, 2000.0f),
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

