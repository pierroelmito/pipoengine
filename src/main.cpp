
#include "pipoengine.h"

namespace pe = pipoengine;

struct HeightMap
{
	int _width{};
	int _height{};
	std::vector<float> _data;
	pe::vec4 _aabb{};
};

class Game
{
public:
	bool init(pe::Context& ctx, const pe::InitParams& params);
	bool draw(pe::Context& ctx, const pe::DrawParams& params);
	bool event(pe::Context& ctx, const pe::EventParams& params);
	bool update(pe::Context& ctx, const pe::UpdateParams& params);
	float getHeightAt(hmm_vec2 pos) const;
protected:
	HeightMap _hmap;
	pe::Mesh _test;
	pe::Mesh _model;
	std::vector<std::pair<pe::vec2, pe::Mesh>> _ground;
	pe::vec2 _camAngles{ 0, 0 };
	pe::vec3 _camPos{ 0, 0, 0 };
};

bool Game::init(pe::Context& ctx, const pe::InitParams& params)
{
	_model = pe::LoadMesh(ctx, "pipo.ply").value_or(_model);

	const float hmpixsz = 0.7f;
	const int tilesz = 24;
	const float gsz = hmpixsz * tilesz;

	pe::LoadPPM(
		"hmap.ppm",
		[&] (int w, int h) {
			_hmap._width = w;
			_hmap._height = h;
			_hmap._data.reserve(w * h);
			float hmw = w * hmpixsz;
			float hmh = h * hmpixsz;
			_hmap._aabb = { -0.5f * hmw, -0.5f * hmh, 0.5f * hmw, 0.5f * hmh };
		},
		[&] (std::array<float, 3> c) {
			_hmap._data.push_back(100.0f * c[0] - 100.0f);
		}
	);

	std::optional<pe::Texture> t00 = pe::LoadDDS({ "terrain00.dds" });

	const int tx = 1 + _hmap._width / tilesz;
	const int ty = 1 + _hmap._height / tilesz;
	const float hgsz = 0.5f * gsz;
	const float dbgc = 0.99f;
	_ground.reserve(tx * ty);
	for (int i = 0; i < tx; ++i) {
		for (int j = 0; j < ty; ++j) {
			const int startx = i * tilesz;
			const int starty = j * tilesz;
			auto msh = pe::MakeHMap(ctx,
				startx, starty,
				tilesz + 1, tilesz + 1,
				{ -dbgc * hgsz, -dbgc * hgsz },
				{  dbgc * hgsz,  dbgc * hgsz },
				[&] (int x, int y) {
					x = std::max(0, std::min(_hmap._width - 1, x));
					y = std::max(0, std::min(_hmap._height - 1, y));
					const int o = (y * _hmap._width + x);
					const float h = _hmap._data[o];
					return h;
				}
			);
			//msh.diffuse = ctx.txChecker;
			msh.diffuse = *t00;
			//_ground.push_back({ { _hmap._aabb.X + startx * hmpixsz, _hmap._aabb.Y + starty * hmpixsz }, msh });
			_ground.push_back({ { _hmap._aabb.X + startx * hmpixsz + hgsz, _hmap._aabb.Y + starty * hmpixsz + gsz }, msh });
			//_ground.push_back({ { _hmap._aabb.X + startx * hmpixsz - hgsz, _hmap._aabb.Y + starty * hmpixsz - gsz }, msh });
		}
	}

	const float wsz = 500.0f;
	const float wh = -95.0f;
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
	_test = pe::MakeMesh(ctx, vertice, indice);

	return true;
}

float Game::getHeightAt(hmm_vec2 pos) const
{
	const float ncrx = (pos.X - _hmap._aabb.X) / (_hmap._aabb.Z - _hmap._aabb.X);
	const float ncry = (pos.Y - _hmap._aabb.Y) / (_hmap._aabb.W - _hmap._aabb.Y);
	const float rx = std::max(0.0f, std::min(1.0f, ncrx));
	const float ry = std::max(0.0f, std::min(1.0f, ncry));
	const int ix = int((rx * 0.999999f) * _hmap._width);
	const int iy = int((ry * 0.999999f) * _hmap._height);
	return _hmap._data[iy * _hmap._width + ix];
}

bool Game::draw(pe::Context& ctx, const pe::DrawParams& params)
{
	float camZ = getHeightAt({ _camPos.X, _camPos.Y });
	_camPos.Z = camZ + 1.0f;

	pe::RenderMain(ctx, [&] (int w, int h) {
		const hmm_mat4 r0{
			.Elements = {
				{ 1, 0, 0, 0 },
				{ 0, 0, 1, 0 },
				{ 0, 1, 0, 0 },
				{ 0, 0, 0, 1 },
			}
		};
		const hmm_mat4 r1 = HMM_Rotate(_camAngles.X, { 0, 0, 1 });
		const hmm_mat4 r2 = HMM_Rotate(_camAngles.Y, { 1, 0, 0 });
		const hmm_mat4 t = HMM_Translate(-1.0f * _camPos);
		const pe::mat4 camView = r2 * r0 * r1 * t;

		pe::SetCamera(
			ctx,
			HMM_Perspective(90.0f, w / float(h), 0.1f, 2000.0f),
			camView
		);

		const float lightTime = 0.00008f * params.elapsed;
		pe::SetLight(
			ctx,
			HMM_NormalizeVec3({ 100.0f * cos(lightTime), 100.0f * sin(lightTime), 30.0f })
		);

		for (const auto& grd : _ground)
			pe::DrawMesh(ctx, grd.second, { HMM_Translate({ grd.first.X, grd.first.Y, 0.0f }) });

		pe::DrawMesh(ctx, _model, { HMM_Translate({ -1.0f, -1.0f, 0.0f }) });
		pe::DrawMesh(ctx, _model, { HMM_Translate({  1.0f, -1.0f, 0.0f }) });
		pe::DrawMesh(ctx, _model, { HMM_Translate({ -1.0f,  1.0f, 0.0f }) });
		pe::DrawMesh(ctx, _model, { HMM_Translate({  1.0f,  1.0f, 0.0f }) });

		//pe::DrawMesh(ctx, _test, { HMM_Translate({ 0.0f, 0.0f, 2.0f }) });
	});

	return true;
}

bool Game::event(pe::Context& ctx, const pe::EventParams& params)
{
	const float mSensitivity = 0.2f;
	const float maxa = 70.0f;

	const SDL_Event& e = params.evt;
	switch (e.type) {
	case SDL_MOUSEMOTION:
		_camAngles.X = _camAngles.X - mSensitivity * e.motion.xrel;
		_camAngles.Y = std::max(-maxa, std::min(maxa, _camAngles.Y + mSensitivity * e.motion.yrel));
		break;
	default:
		break;
	}

	return true;
}

bool Game::update(pe::Context& ctx, const pe::UpdateParams& params)
{
	const float speed = 0.01f;
	const Uint8 *state = SDL_GetKeyboardState(NULL);
	if (state[SDL_SCANCODE_RIGHT]) {
		_camPos.X += speed * params.updateMs;
	}
	if (state[SDL_SCANCODE_LEFT]) {
		_camPos.X -= speed * params.updateMs;
	}
	if (state[SDL_SCANCODE_UP]) {
		_camPos.Y += speed * params.updateMs;
	}
	if (state[SDL_SCANCODE_DOWN]) {
		_camPos.Y -= speed * params.updateMs;
	}
	return true;
}

int main(int, char**)
{
	using namespace std::placeholders;
	Game g;
	return pe::Exec({
		.init = std::bind(&Game::init, &g, _1, _2),
		.update = std::bind(&Game::update, &g, _1, _2),
		.draw = std::bind(&Game::draw, &g, _1, _2),
		.event = std::bind(&Game::event, &g, _1, _2),
	});
}

