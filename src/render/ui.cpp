#include "ui.h"
#include "asset/shader.h"
#include "load.h"
#include "render/render.h"

namespace VI
{

Array<UIText*> UIText::instances = Array<UIText*>();

UIText::UIText()
	: indices(),
	vertices(),
	color(Vec4(1, 1, 1, 1)),
	font(),
	pos(Vec2::zero),
	rot(),
	size(16),
	string()
{
	instances.add(this);
}

UIText::~UIText()
{
	for (int i = 0; i < instances.length; i++)
	{
		if (instances[i] == this)
		{
			instances.remove(i);
			break;
		}
	}
}

void UIText::reeval()
{
	if (string)
		text(string);
}

void UIText::reeval_all()
{
	for (int i = 0; i < instances.length; i++)
		instances[i]->reeval();
}

void UIText::text(const char* s)
{
	if (string != s)
	{
		if (string)
			free(string);

		int length = strlen(s);

		string = (char*)malloc(sizeof(char) * (length + 1));
		memcpy(string, s, sizeof(char) * (length + 1));
	}

	vertices.length = 0;
	indices.length = 0;

	int char_index = 0;
	int vertex_index = 0;
	int index_index = 0;
	Vec3 pos(0, 0, 0);
	while (true)
	{
		char c = string[char_index];
		if (!c)
			break;

		if (c == '\n')
		{
			pos.x = 0.0f;
			pos.y -= 1.0f;
		}
		else
		{
			Font* f = Loader::font(font);
			Font::Character* character = &f->characters[c];
			if (character->code == c)
			{
				vertices.resize(vertex_index + character->vertex_count);
				for (int i = 0; i < character->vertex_count; i++)
					vertices[vertex_index + i] = f->vertices[character->vertex_start + i] + pos;

				pos.x += 0.075f + character->max.x;

				indices.resize(index_index + character->index_count);
				for (int i = 0; i < character->index_count; i++)
					indices[index_index + i] = vertex_index + f->indices[character->index_start + i] - character->vertex_start;
			}
			else
			{
				// Font is missing character
			}
			vertex_index = vertices.length;
			index_index = indices.length;
		}
		char_index++;
	}
}

void UIText::draw(const RenderParams& params)
{
	int vertex_start = UI::vertices.length;
	Vec2 screen = Vec2(params.camera->viewport.width * 0.5f, params.camera->viewport.height * 0.5f);
	Vec2 offset = pos - screen;
	Vec2 scale = Vec2(1.0f / screen.x, 1.0f / screen.y);
	float cs = cosf(rot), sn = sinf(rot);
	for (int i = 0; i < vertices.length; i++)
	{
		Vec3 vertex;
		vertex.x = (offset.x + size * UI::scale * (vertices[i].x * cs - vertices[i].y * sn)) * scale.x;
		vertex.y = (offset.y + size * UI::scale * (vertices[i].x * sn + vertices[i].y * cs)) * scale.y;
		UI::vertices.add(vertex);
	}

	for (int i = 0; i < vertices.length; i++)
		UI::colors.add(color);

	UI::indices.reserve(UI::indices.length + indices.length);
	for (int i = 0; i < indices.length; i++)
		UI::indices.add(indices[i] + vertex_start);
}

float UI::scale = 1.0f;
int UI::mesh = AssetNull;
int UI::texture_mesh = AssetNull;
Array<Vec3> UI::vertices = Array<Vec3>();
Array<Vec4> UI::colors = Array<Vec4>();
Array<int> UI::indices = Array<int>();

void UI::box(const RenderParams& params, const Vec2& pos, const Vec2& size, const Vec4& color)
{
	if (size.x > 0 && size.y > 0 && color.w > 0)
	{
		int vertex_start = UI::vertices.length;
		Vec2 screen = Vec2(params.camera->viewport.width * 0.5f, params.camera->viewport.height * 0.5f);
		Vec2 scale = Vec2(1.0f / screen.x, 1.0f / screen.y);
		Vec2 scaled_pos = (pos - screen) * scale;
		Vec2 scaled_size = size * scale;

		UI::vertices.add(Vec3(scaled_pos.x, scaled_pos.y, 0));
		UI::vertices.add(Vec3(scaled_pos.x + scaled_size.x, scaled_pos.y, 0));
		UI::vertices.add(Vec3(scaled_pos.x, scaled_pos.y + scaled_size.y, 0));
		UI::vertices.add(Vec3(scaled_pos.x + scaled_size.x, scaled_pos.y + scaled_size.y, 0));
		UI::colors.add(color);
		UI::colors.add(color);
		UI::colors.add(color);
		UI::colors.add(color);
		UI::indices.add(vertex_start + 0);
		UI::indices.add(vertex_start + 1);
		UI::indices.add(vertex_start + 2);
		UI::indices.add(vertex_start + 1);
		UI::indices.add(vertex_start + 3);
		UI::indices.add(vertex_start + 2);
	}
}

void UI::centered_box(const RenderParams& params, const Vec2& pos, const Vec2& size, const Vec4& color, float rot)
{
	if (size.x > 0 && size.y > 0 && color.w > 0)
	{
		int vertex_start = UI::vertices.length;
		Vec2 screen = Vec2(params.camera->viewport.width * 0.5f, params.camera->viewport.height * 0.5f);
		Vec2 scale = Vec2(1.0f / screen.x, 1.0f / screen.y);
		Vec2 scaled_pos = (pos - screen) * scale;

		Vec2 corners[4] =
		{
			Vec2(size.x * -0.5f, size.y * -0.5f),
			Vec2(size.x * 0.5f, size.y * -0.5f),
			Vec2(size.x * -0.5f, size.y * 0.5f),
			Vec2(size.x * 0.5f, size.y * 0.5f),
		};

		float cs = cosf(rot), sn = sinf(rot);
		UI::vertices.add(Vec3(scaled_pos.x + (corners[0].x * cs - corners[0].y * sn) * scale.x, scaled_pos.y + (corners[0].x * sn + corners[0].y * cs) * scale.y, 0));
		UI::vertices.add(Vec3(scaled_pos.x + (corners[1].x * cs - corners[1].y * sn) * scale.x, scaled_pos.y + (corners[1].x * sn + corners[1].y * cs) * scale.y, 0));
		UI::vertices.add(Vec3(scaled_pos.x + (corners[2].x * cs - corners[2].y * sn) * scale.x, scaled_pos.y + (corners[2].x * sn + corners[2].y * cs) * scale.y, 0));
		UI::vertices.add(Vec3(scaled_pos.x + (corners[3].x * cs - corners[3].y * sn) * scale.x, scaled_pos.y + (corners[3].x * sn + corners[3].y * cs) * scale.y, 0));
		UI::colors.add(color);
		UI::colors.add(color);
		UI::colors.add(color);
		UI::colors.add(color);
		UI::indices.add(vertex_start + 0);
		UI::indices.add(vertex_start + 1);
		UI::indices.add(vertex_start + 2);
		UI::indices.add(vertex_start + 1);
		UI::indices.add(vertex_start + 3);
		UI::indices.add(vertex_start + 2);
	}
}

void UI::border(const RenderParams& params, const Vec2& pos, const Vec2& size, const Vec4& color, float thickness)
{
	if (size.x > 0 && size.y > 0 && color.w > 0)
	{
		int vertex_start = UI::vertices.length;
		Vec2 screen = Vec2(params.camera->viewport.width * 0.5f, params.camera->viewport.height * 0.5f);
		Vec2 scale = Vec2(1.0f / screen.x, 1.0f / screen.y);
		Vec2 scaled_pos = (pos - screen) * scale;
		Vec2 scaled_size = size * scale;
		Vec2 scaled_thickness = Vec2(thickness, thickness) * scale;

		UI::vertices.add(Vec3(scaled_pos.x, scaled_pos.y, 0));
		UI::vertices.add(Vec3(scaled_pos.x + scaled_size.x, scaled_pos.y, 0));
		UI::vertices.add(Vec3(scaled_pos.x, scaled_pos.y + scaled_size.y, 0));
		UI::vertices.add(Vec3(scaled_pos.x + scaled_size.x, scaled_pos.y + scaled_size.y, 0));
		UI::vertices.add(Vec3(scaled_pos.x - scaled_thickness.x, scaled_pos.y - scaled_thickness.y, 0));
		UI::vertices.add(Vec3(scaled_pos.x + scaled_size.x + scaled_thickness.x, scaled_pos.y - scaled_thickness.y, 0));
		UI::vertices.add(Vec3(scaled_pos.x - scaled_thickness.x, scaled_pos.y + scaled_size.y + scaled_thickness.y, 0));
		UI::vertices.add(Vec3(scaled_pos.x + scaled_size.x + scaled_thickness.x, scaled_pos.y + scaled_size.y + scaled_thickness.y, 0));
		UI::colors.add(color);
		UI::colors.add(color);
		UI::colors.add(color);
		UI::colors.add(color);
		UI::colors.add(color);
		UI::colors.add(color);
		UI::colors.add(color);
		UI::colors.add(color);
		UI::indices.add(vertex_start + 0);
		UI::indices.add(vertex_start + 4);
		UI::indices.add(vertex_start + 5);
		UI::indices.add(vertex_start + 0);
		UI::indices.add(vertex_start + 5);
		UI::indices.add(vertex_start + 1);
		UI::indices.add(vertex_start + 1);
		UI::indices.add(vertex_start + 5);
		UI::indices.add(vertex_start + 3);
		UI::indices.add(vertex_start + 5);
		UI::indices.add(vertex_start + 7);
		UI::indices.add(vertex_start + 3);
		UI::indices.add(vertex_start + 3);
		UI::indices.add(vertex_start + 7);
		UI::indices.add(vertex_start + 2);
		UI::indices.add(vertex_start + 7);
		UI::indices.add(vertex_start + 6);
		UI::indices.add(vertex_start + 2);
		UI::indices.add(vertex_start + 6);
		UI::indices.add(vertex_start + 0);
		UI::indices.add(vertex_start + 2);
		UI::indices.add(vertex_start + 6);
		UI::indices.add(vertex_start + 4);
		UI::indices.add(vertex_start + 0);
	}
}

void UI::centered_border(const RenderParams& params, const Vec2& pos, const Vec2& size, const Vec4& color, float thickness, float rot)
{
	if (size.x > 0 && size.y > 0 && color.w > 0)
	{
		int vertex_start = UI::vertices.length;
		Vec2 screen = Vec2(params.camera->viewport.width * 0.5f, params.camera->viewport.height * 0.5f);
		Vec2 scale = Vec2(1.0f / screen.x, 1.0f / screen.y);
		Vec2 scaled_pos = (pos - screen) * scale;

		Vec2 corners[8] =
		{
			Vec2(size.x * -0.5f, size.y * -0.5f),
			Vec2(size.x * 0.5f, size.y * -0.5f),
			Vec2(size.x * -0.5f, size.y * 0.5f),
			Vec2(size.x * 0.5f, size.y * 0.5f),
			Vec2(size.x * -0.5f - thickness, size.y * -0.5f - thickness),
			Vec2(size.x * 0.5f + thickness, size.y * -0.5f - thickness),
			Vec2(size.x * -0.5f - thickness, size.y * 0.5f + thickness),
			Vec2(size.x * 0.5f + thickness, size.y * 0.5f + thickness),
		};

		float cs = cosf(rot), sn = sinf(rot);
		UI::vertices.add(Vec3(scaled_pos.x + (corners[0].x * cs - corners[0].y * sn) * scale.x, scaled_pos.y + (corners[0].x * sn + corners[0].y * cs) * scale.y, 0));
		UI::vertices.add(Vec3(scaled_pos.x + (corners[1].x * cs - corners[1].y * sn) * scale.x, scaled_pos.y + (corners[1].x * sn + corners[1].y * cs) * scale.y, 0));
		UI::vertices.add(Vec3(scaled_pos.x + (corners[2].x * cs - corners[2].y * sn) * scale.x, scaled_pos.y + (corners[2].x * sn + corners[2].y * cs) * scale.y, 0));
		UI::vertices.add(Vec3(scaled_pos.x + (corners[3].x * cs - corners[3].y * sn) * scale.x, scaled_pos.y + (corners[3].x * sn + corners[3].y * cs) * scale.y, 0));
		UI::vertices.add(Vec3(scaled_pos.x + (corners[4].x * cs - corners[4].y * sn) * scale.x, scaled_pos.y + (corners[4].x * sn + corners[4].y * cs) * scale.y, 0));
		UI::vertices.add(Vec3(scaled_pos.x + (corners[5].x * cs - corners[5].y * sn) * scale.x, scaled_pos.y + (corners[5].x * sn + corners[5].y * cs) * scale.y, 0));
		UI::vertices.add(Vec3(scaled_pos.x + (corners[6].x * cs - corners[6].y * sn) * scale.x, scaled_pos.y + (corners[6].x * sn + corners[6].y * cs) * scale.y, 0));
		UI::vertices.add(Vec3(scaled_pos.x + (corners[7].x * cs - corners[7].y * sn) * scale.x, scaled_pos.y + (corners[7].x * sn + corners[7].y * cs) * scale.y, 0));

		UI::colors.add(color);
		UI::colors.add(color);
		UI::colors.add(color);
		UI::colors.add(color);
		UI::colors.add(color);
		UI::colors.add(color);
		UI::colors.add(color);
		UI::colors.add(color);
		UI::indices.add(vertex_start + 0);
		UI::indices.add(vertex_start + 4);
		UI::indices.add(vertex_start + 5);
		UI::indices.add(vertex_start + 0);
		UI::indices.add(vertex_start + 5);
		UI::indices.add(vertex_start + 1);
		UI::indices.add(vertex_start + 1);
		UI::indices.add(vertex_start + 5);
		UI::indices.add(vertex_start + 3);
		UI::indices.add(vertex_start + 5);
		UI::indices.add(vertex_start + 7);
		UI::indices.add(vertex_start + 3);
		UI::indices.add(vertex_start + 3);
		UI::indices.add(vertex_start + 7);
		UI::indices.add(vertex_start + 2);
		UI::indices.add(vertex_start + 7);
		UI::indices.add(vertex_start + 6);
		UI::indices.add(vertex_start + 2);
		UI::indices.add(vertex_start + 6);
		UI::indices.add(vertex_start + 0);
		UI::indices.add(vertex_start + 2);
		UI::indices.add(vertex_start + 6);
		UI::indices.add(vertex_start + 4);
		UI::indices.add(vertex_start + 0);
	}
}

void UI::triangle(const RenderParams& params, const Vec2& pos, const Vec2& size, const Vec4& color, float rot)
{
	if (size.x > 0 && size.y > 0 && color.w > 0)
	{
		int vertex_start = UI::vertices.length;
		Vec2 screen = Vec2(params.camera->viewport.width * 0.5f, params.camera->viewport.height * 0.5f);
		Vec2 scale = Vec2(1.0f / screen.x, 1.0f / screen.y);
		Vec2 scaled_pos = (pos - screen) * scale;

		Vec2 corners[3] =
		{
			Vec2(size.x * 0.5f, size.y * -0.5f),
			Vec2(0, size.y * 0.5f * 0.8660254037844386f),
			Vec2(size.x * -0.5f, size.y * -0.5f),
		};

		float cs = cosf(rot), sn = sinf(rot);
		UI::vertices.add(Vec3(scaled_pos.x + (corners[0].x * cs - corners[0].y * sn) * scale.x, scaled_pos.y + (corners[0].x * sn + corners[0].y * cs) * scale.y, 0));
		UI::vertices.add(Vec3(scaled_pos.x + (corners[1].x * cs - corners[1].y * sn) * scale.x, scaled_pos.y + (corners[1].x * sn + corners[1].y * cs) * scale.y, 0));
		UI::vertices.add(Vec3(scaled_pos.x + (corners[2].x * cs - corners[2].y * sn) * scale.x, scaled_pos.y + (corners[2].x * sn + corners[2].y * cs) * scale.y, 0));
		UI::colors.add(color);
		UI::colors.add(color);
		UI::colors.add(color);
		UI::indices.add(vertex_start + 0);
		UI::indices.add(vertex_start + 1);
		UI::indices.add(vertex_start + 2);
	}
}

bool UI::project(const RenderParams& p, const Vec3& v, Vec2& out)
{
	Vec4 projected = p.view_projection * Vec4(v.x, v.y, v.z, 1);
	Vec2 screen = Vec2(p.camera->viewport.width * 0.5f, p.camera->viewport.height * 0.5f);
	out = Vec2((projected.x / projected.w + 1.0f) * screen.x, (projected.y / projected.w + 1.0f) * screen.y);
	return projected.z > -projected.w && projected.z < projected.w;
}

void UI::init(RenderSync* sync)
{
	mesh = Loader::dynamic_mesh_permanent(2);
	Loader::dynamic_mesh_attrib(RenderDataType_Vec3);
	Loader::dynamic_mesh_attrib(RenderDataType_Vec4);
	Loader::shader_permanent(Asset::Shader::ui);

	texture_mesh = Loader::dynamic_mesh_permanent(3);
	Loader::dynamic_mesh_attrib(RenderDataType_Vec3);
	Loader::dynamic_mesh_attrib(RenderDataType_Vec4);
	Loader::dynamic_mesh_attrib(RenderDataType_Vec2);
	Loader::shader_permanent(Asset::Shader::ui_texture);

	int indices[] =
	{
		0,
		1,
		2,
		1,
		3,
		2
	};

	sync->write(RenderOp_UpdateIndexBuffer);
	sync->write(texture_mesh);
	sync->write<int>(6);
	sync->write(indices, 6);

	scale = get_scale(sync->input.width, sync->input.height);
}

float UI::get_scale(const int width, const int height)
{
	int area = width * height;
	if (area >= 1680 * 1050)
		return 1.5f;
	else
		return 1.0f;
}

void UI::update(const RenderParams& p)
{
	scale = get_scale(p.camera->viewport.width, p.camera->viewport.height);
}

void UI::draw(const RenderParams& p)
{
	if (indices.length > 0)
	{
		p.sync->write(RenderOp_UpdateAttribBuffers);
		p.sync->write(mesh);
		p.sync->write<int>(vertices.length);
		p.sync->write(vertices.data, vertices.length);
		p.sync->write(colors.data, colors.length);

		p.sync->write(RenderOp_UpdateIndexBuffer);
		p.sync->write(mesh);
		p.sync->write<int>(indices.length);
		p.sync->write(indices.data, indices.length);

		p.sync->write(RenderOp_Shader);
		p.sync->write(Asset::Shader::ui);
		p.sync->write(p.technique);

		p.sync->write(RenderOp_Mesh);
		p.sync->write(mesh);

		vertices.length = 0;
		colors.length = 0;
		indices.length = 0;
	}
}

void UI::texture(const RenderParams& p, const int texture, const Vec2& pos, const Vec2& size, const Vec4& color, const Vec2& uva, const Vec2& uvb)
{
	Vec2 screen = Vec2(p.camera->viewport.width * 0.5f, p.camera->viewport.height * 0.5f);
	Vec2 scale = Vec2(1.0f / screen.x, 1.0f / screen.y);
	Vec2 scaled_pos = (pos - screen) * scale;
	Vec2 scaled_size = size * scale;

	Vec3 vertices[] =
	{
		Vec3(scaled_pos.x, scaled_pos.y, 0),
		Vec3(scaled_pos.x + scaled_size.x, scaled_pos.y, 0),
		Vec3(scaled_pos.x, scaled_pos.y + scaled_size.y, 0),
		Vec3(scaled_pos.x + scaled_size.x, scaled_pos.y + scaled_size.y, 0),
	};

	Vec4 colors[] =
	{
		color,
		color,
		color,
		color,
	};

	Vec2 uvs[] =
	{
		Vec2(uva.x, uva.y),
		Vec2(uvb.x, uva.y),
		Vec2(uva.x, uvb.y),
		Vec2(uvb.x, uvb.y),
	};

	p.sync->write(RenderOp_UpdateAttribBuffers);
	p.sync->write(texture_mesh);
	p.sync->write<int>(4);
	p.sync->write(vertices, 4);
	p.sync->write(colors, 4);
	p.sync->write(uvs, 4);

	p.sync->write(RenderOp_Shader);
	p.sync->write(Asset::Shader::ui_texture);
	p.sync->write(p.technique);

	p.sync->write(RenderOp_Uniform);
	p.sync->write(Asset::Uniform::color_buffer);
	p.sync->write(RenderDataType_Texture);
	p.sync->write<int>(1);
	p.sync->write<RenderTextureType>(RenderTexture2D);
	p.sync->write<AssetID>(texture);

	p.sync->write(RenderOp_Mesh);
	p.sync->write(texture_mesh);
}

}