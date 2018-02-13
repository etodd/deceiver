#include "data/import_common.h"
#include "load.h"
#include "data/components.h"
#include "bullet/src/BulletCollision/NarrowPhaseCollision/btRaycastCallback.h"
#include "game/entities.h"
#include "recast/Recast/Include/Recast.h"
#include "recast/Detour/Include/DetourNavMeshBuilder.h"
#include "recast/Detour/Include/DetourCommon.h"
#include "data/priority_queue.h"
#include "mersenne/mersenne-twister.h"
#include "render/ui.h"
#include "asset/shader.h"
#include "game/player.h"
#include "game/game.h"
#include "game/team.h"
#include "game/entities.h"
#include "game/drone.h"
#include "game/minion.h"

#define DEBUG_AUDIO 0

#define ENABLE_RECORD 0

#if DEBUG_AUDIO
#include "render/views.h"
#include "asset/mesh.h"
#endif

namespace VI
{

namespace AI
{

Bitmask<nav_max_obstacles> obstacles;
SyncRingBuffer<SYNC_IN_SIZE> sync_in;
SyncRingBuffer<SYNC_OUT_SIZE> sync_out;
b8 render_meshes_dirty;
u32 callback_in_id = 1;
u32 callback_out_id = 1;
u32 record_id_current = 1;
Revision level_revision;
Revision level_revision_worker;
AssetID drone_render_mesh = AssetNull;
DroneNavMesh drone_nav_mesh;
Worker::DroneNavMeshKey drone_nav_mesh_key;
NavGameState nav_game_state;
Worker::AstarQueue astar_queue(&drone_nav_mesh_key);
Worker::DroneNavContext ctx =
{
	drone_nav_mesh,
	&drone_nav_mesh_key,
	nav_game_state,
	&astar_queue,
	0,
};

void init()
{
	drone_render_mesh = Loader::dynamic_mesh_permanent(1);
	Loader::dynamic_mesh_attrib(RenderDataType::Vec3);
}

void loop()
{
	Worker::loop();
}

void quit()
{
	sync_in.lock();
	sync_in.write(Op::Quit);
	sync_in.unlock();
}

#define RECTIFIER_UPDATE_INTERVAL 0.5f
r32 rectifier_timer = RECTIFIER_UPDATE_INTERVAL;

void update(const Update& u)
{
	rectifier_timer -= u.time.delta;
	if (rectifier_timer < 0.0f)
	{
		rectifier_timer += RECTIFIER_UPDATE_INTERVAL;

		NavGameState state;
		for (auto i = Rectifier::list.iterator(); !i.is_last(); i.next())
			state.rectifiers.add({ i.item()->get<Transform>()->absolute_pos(), i.item()->team });

		for (auto i = ForceField::list.iterator(); !i.is_last(); i.next())
			state.force_fields.add({ i.item()->get<Transform>()->absolute_pos(), i.item()->team });

		sync_in.lock();
		sync_in.write(Op::UpdateState);
		sync_in.write(state.rectifiers.length);
		sync_in.write(state.rectifiers.data, state.rectifiers.length);
		sync_in.write(state.force_fields.length);
		sync_in.write(state.force_fields.data, state.force_fields.length);
		sync_in.unlock();
	}

	sync_out.lock();
	while (sync_out.can_read())
	{
		Callback cb;
		sync_out.read(&cb);
		switch (cb)
		{
			case Callback::Path:
			{
				LinkEntryArg<const Result&> link;
				sync_out.read(&link);
				Result result;
				sync_out.read(&result.path);
				result.id = callback_out_id;
				callback_out_id++;
				if (level_revision == level_revision_worker) // prevent entity ID/revision collisions
					(&link)->fire(result);
				break;
			}
			case Callback::DronePath:
			{
				LinkEntryArg<const DroneResult&> link;
				sync_out.read(&link);
				DroneResult result;
				sync_out.read(&result.path);
				result.id = callback_out_id;
				callback_out_id++;
				if (level_revision == level_revision_worker) // prevent entity ID/revision collisions
					(&link)->fire(result);
				break;
			}
			case Callback::Point:
			{
				LinkEntryArg<const Vec3&> link;
				sync_out.read(&link);
				Vec3 result;
				sync_out.read(&result);
				callback_out_id++;
				if (level_revision == level_revision_worker) // prevent entity ID/revision collisions
					(&link)->fire(result);
				break;
			}
			case Callback::DronePoint:
			{
				LinkEntryArg<const DronePathNode&> link;
				sync_out.read(&link);
				DronePathNode result;
				sync_out.read(&result);
				callback_out_id++;
				if (level_revision == level_revision_worker) // prevent entity ID/revision collisions
					(&link)->fire(result);
				break;
			}
			case Callback::Load:
			{
				sync_out.read(&level_revision_worker);
				break;
			}
			case Callback::AudioPath:
			{
				Ref<AudioEntry> entry;
				s8 listener;
				r32 path_length;
				r32 straight_distance;
				sync_out.read(&entry);
				sync_out.read(&listener);
				sync_out.read(&path_length);
				sync_out.read(&straight_distance);
				callback_out_id++;
				if (level_revision == level_revision_worker) // prevent entity ID/revision collisions
				{
					if (entry.ref())
						entry.ref()->pathfind_result(listener, path_length, straight_distance);
				}
				break;
			}
			default:
			{
				vi_assert(false);
				break;
			}
		}
	}
	sync_out.unlock();
}

u32 record_init(AI::Team team)
{
	u32 id = record_id_current;
	record_id_current++;
	if (record_id_current == 0) // 0 is an invalid record ID
		record_id_current = 1;
#if ENABLE_RECORD
	sync_in.lock();
	sync_in.write(Op::RecordInit);
	sync_in.write(id);
	sync_in.write(team);
	sync_in.unlock();
#endif

	return id;
}

void record_add(u32 id, const AI::RecordedLife::Tag& tag, const AI::RecordedLife::Action& action)
{
	vi_assert(id != 0);
#if ENABLE_RECORD
	sync_in.lock();
	sync_in.write(Op::RecordAdd);
	sync_in.write(id);
	sync_in.write(tag);
	sync_in.write(action);
	sync_in.unlock();
#endif
}

void record_close(u32 id)
{
	if (id != 0)
	{
#if ENABLE_RECORD
		sync_in.lock();
		sync_in.write(Op::RecordClose);
		sync_in.write(id);
		sync_in.unlock();
#endif
	}
}

b8 match(Team t, TeamMask m)
{
	if (m == TeamNone)
		return t == TeamNone;
	else
		return t != TeamNone && (m & (1 << t));
}

u32 obstacle_add(const Vec3& pos, r32 radius, r32 height)
{
	if (obstacles.count() == nav_max_obstacles)
		return u32(nav_max_obstacles); // no room

	u32 id;
	b8 found = false;
	for (s32 i = 0; i < obstacles.count(); i++)
	{
		if (!obstacles.get(i))
		{
			id = u32(i);
			found = true;
			break;
		}
	}

	if (!found)
		id = obstacles.end;
	obstacles.set(id, true);

	sync_in.lock();
	sync_in.write(Op::ObstacleAdd);
	sync_in.write(id);
	sync_in.write(pos);
	sync_in.write(radius);
	sync_in.write(height);
	sync_in.unlock();

	return id;
}

void obstacle_remove(u32 id)
{
	if (id < nav_max_obstacles)
	{
		obstacles.set(id, false);
		sync_in.lock();
		sync_in.write(Op::ObstacleRemove);
		sync_in.write(id);
		sync_in.unlock();
	}
}

void load(AssetID id, const char* filename, const char* record_filename)
{
	sync_in.lock();
	sync_in.write(Op::Load);
	sync_in.write(id);
	s32 length = filename ? s32(strlen(filename)) : 0;
	vi_assert(length <= MAX_PATH_LENGTH);
	sync_in.write(length);
	if (length > 0)
		sync_in.write(filename, length);
	length = record_filename ? s32(strlen(record_filename)) : 0;
	vi_assert(length <= MAX_PATH_LENGTH);
	sync_in.write(length);
	if (length > 0)
		sync_in.write(record_filename, length);
	sync_in.unlock();
	level_revision++;

	drone_nav_mesh.~DroneNavMesh();
	new (&drone_nav_mesh) DroneNavMesh();
	drone_nav_mesh_key.~DroneNavMeshKey();
	new (&drone_nav_mesh_key) Worker::DroneNavMeshKey();

	if (filename)
	{
		FILE* f = fopen(filename, "rb");

		{
			// skip minion nav mesh
			Vec3 min;
			s32 width;
			s32 height;
			fread(&min, sizeof(Vec3), 1, f);
			fread(&width, sizeof(s32), 1, f);
			fread(&height, sizeof(s32), 1, f);
			s32 count = width * height;
			for (s32 i = 0; i < count; i++)
			{
				s32 layer_count;
				fread(&layer_count, sizeof(s32), 1, f);
				for (s32 j = 0; j < layer_count; j++)
				{
					s32 data_size;
					fread(&data_size, sizeof(s32), 1, f);
					fseek(f, data_size, SEEK_CUR);
				}
			}
		}

		drone_nav_mesh.read(f);
		fclose(f);
		drone_nav_mesh_key.resize(drone_nav_mesh);
	}

	{
		Array<Vec3> vertices;
		for (s32 chunk_index = 0; chunk_index < drone_nav_mesh.chunks.length; chunk_index++)
		{
			const DroneNavMeshChunk& chunk = drone_nav_mesh.chunks[chunk_index];
			for (s32 i = 0; i < chunk.vertices.length; i++)
				vertices.add(chunk.vertices[i]);
		}

		Array<s32> indices;
		for (s32 i = 0; i < vertices.length; i++)
			indices.add(i);

		RenderSync* sync = Loader::swapper->get();;

		sync->write(RenderOp::UpdateAttribBuffers);
		sync->write(drone_render_mesh);

		sync->write<s32>(vertices.length);
		sync->write<Vec3>(vertices.data, vertices.length);

		sync->write(RenderOp::UpdateIndexBuffer);
		sync->write(drone_render_mesh);

		sync->write<s32>(indices.length);
		sync->write<s32>(indices.data, indices.length);
	}

	render_meshes_dirty = true;
}

u32 random_path(const Vec3& pos, const Vec3& patrol_point, AI::Team team, r32 range, const LinkEntryArg<const Result&>& callback)
{
	u32 id = callback_in_id;
	callback_in_id++;

	sync_in.lock();
	sync_in.write(Op::RandomPath);
	sync_in.write(pos);
	sync_in.write(patrol_point);
	sync_in.write(team);
	sync_in.write(range);
	sync_in.write(callback);
	sync_in.unlock();

	return id;
}

u32 closest_walk_point(const Vec3& pos, const LinkEntryArg<const Vec3&>& callback)
{
	u32 id = callback_in_id;
	callback_in_id++;

	sync_in.lock();
	sync_in.write(Op::ClosestWalkPoint);
	sync_in.write(pos);
	sync_in.write(callback);
	sync_in.unlock();

	return id;
}

u32 drone_random_path(DroneAllow rule, Team team, const Vec3& pos, const Vec3& normal, const LinkEntryArg<const DroneResult&>& callback)
{
	return drone_pathfind(DronePathfind::Random, rule, team, pos, normal, Vec3::zero, Vec3::zero, callback);
}

u32 pathfind(AI::Team team, const Vec3& a, const Vec3& b, const LinkEntryArg<const Result&>& callback)
{
	u32 id = callback_in_id;
	callback_in_id++;

	sync_in.lock();
	sync_in.write(Op::Pathfind);
	sync_in.write(team);
	sync_in.write(a);
	sync_in.write(b);
	sync_in.write(callback);
	sync_in.unlock();

	return id;
}

u32 drone_pathfind(DronePathfind type, DroneAllow rule, Team team, const Vec3& a, const Vec3& a_normal, const Vec3& b, const Vec3& b_normal, const LinkEntryArg<const DroneResult&>& callback)
{
	u32 id = callback_in_id;
	callback_in_id++;

	sync_in.lock();
	sync_in.write(Op::DronePathfind);
	sync_in.write(type);
	sync_in.write(rule);
	sync_in.write(team);
	sync_in.write(callback);
	sync_in.write(a);
	sync_in.write(a_normal);
	if (type != DronePathfind::Random)
	{
		if (type != DronePathfind::Spawn)
			sync_in.write(b);
		if (type != DronePathfind::Target)
			sync_in.write(b_normal);
	}
	sync_in.unlock();
	
	return id;
}

r32 audio_pathfind(const Vec3& a, const Vec3& b)
{
	return Worker::audio_pathfind(ctx, a, b);
}

u32 audio_pathfind(const Vec3& a, const Vec3& b, AudioEntry* entry, s8 listener, r32 straight_distance)
{
	u32 id = callback_in_id;
	callback_in_id++;

	sync_in.lock();
	sync_in.write(Op::AudioPathfind);
	Ref<AudioEntry> ref = entry;
	sync_in.write(ref);
	sync_in.write(listener);
	sync_in.write(a);
	sync_in.write(b);
	sync_in.write(straight_distance);
	sync_in.unlock();
	
	return id;
}

void audio_reverb_calc(const Vec3& pos, ReverbCell* output)
{
	Worker::audio_reverb_calc(ctx, pos, output);
}

u32 drone_closest_point(const Vec3& pos, AI::Team team, const LinkEntryArg<const DronePathNode&>& callback)
{
	u32 id = callback_in_id;
	callback_in_id++;

	sync_in.lock();
	sync_in.write(Op::DroneClosestPoint);
	sync_in.write(callback);
	sync_in.write(team);
	sync_in.write(pos);
	sync_in.unlock();
	
	return id;
}

void drone_mark_adjacency_bad(DroneNavMeshNode a, DroneNavMeshNode b)
{
	sync_in.lock();
	sync_in.write(Op::DroneMarkAdjacencyBad);
	sync_in.write(a);
	sync_in.write(b);
	sync_in.unlock();
}

void NavGameState::clear()
{
	rectifiers.length = 0;
	force_fields.length = 0;
}

const PathZone* PathZone::get(const Vec3& pos, const Entity* target)
{
	for (s32 i = 0; i < Game::level.path_zones.length; i++)
	{
		const PathZone& zone = Game::level.path_zones[i];
		Vec3 min = zone.pos - zone.radius;
		Vec3 max = zone.pos + zone.radius;
		if (pos.x > min.x && pos.x < max.x
			&& pos.y > min.y && pos.y < max.y
			&& pos.z > min.z && pos.z < max.z)
		{
			for (s32 j = 0; j < zone.targets.length; j++)
			{
				if (zone.targets[j].ref() == target)
					return &zone;
			}
		}
	}
	return nullptr;
}

#if DEBUG
AssetID render_mesh = AssetNull;
void refresh_nav_render_meshes(const RenderParams& params)
{
	if (!render_meshes_dirty)
		return;

	std::this_thread::sleep_for(std::chrono::milliseconds(30));
	sync_in.lock();

	if (render_mesh == AssetNull)
	{
		render_mesh = Loader::dynamic_mesh_permanent(1);
		Loader::dynamic_mesh_attrib(RenderDataType::Vec3);
		Loader::shader_permanent(Asset::Shader::flat);
	}

	Array<Vec3> vertices;
	Array<s32> indices;

	// nav mesh
	{
		if (Worker::nav_mesh)
		{
			for (s32 tile_id = 0; tile_id < Worker::nav_mesh->getMaxTiles(); tile_id++)
			{
				const dtMeshTile* tile = ((const dtNavMesh*)Worker::nav_mesh)->getTile(tile_id);
				if (!tile->header)
					continue;

				for (s32 i = 0; i < tile->header->polyCount; i++)
				{
					const dtPoly* p = &tile->polys[i];
					if (p->getType() == DT_POLYTYPE_OFFMESH_CONNECTION)	// skip off-mesh links.
						continue;

					const dtPolyDetail* pd = &tile->detailMeshes[i];

					for (s32 j = 0; j < pd->triCount; j++)
					{
						const u8* t = &tile->detailTris[(pd->triBase + j) * 4];
						for (s32 k = 0; k < 3; k++)
						{
							if (t[k] < p->vertCount)
								memcpy(vertices.add(), &tile->verts[p->verts[t[k]] * 3], sizeof(Vec3));
							else
								memcpy(vertices.add(), &tile->detailVerts[(pd->vertBase + t[k] - p->vertCount) * 3], sizeof(Vec3));
							indices.add(indices.length);
						}
					}
				}
			}
		}

		params.sync->write(RenderOp::UpdateAttribBuffers);
		params.sync->write(render_mesh);

		params.sync->write<s32>(vertices.length);
		params.sync->write<Vec3>(vertices.data, vertices.length);

		params.sync->write(RenderOp::UpdateIndexBuffer);
		params.sync->write(render_mesh);

		params.sync->write<s32>(indices.length);
		params.sync->write<s32>(indices.data, indices.length);
	}

	sync_in.unlock();

	render_meshes_dirty = false;
}

void render_helper(const RenderParams& params, AssetID m, RenderPrimitiveMode primitive_mode, RenderFillMode fill_mode)
{
	if (m == AssetNull)
		return;

	Loader::shader_permanent(Asset::Shader::flat);

	params.sync->write(RenderOp::Shader);
	params.sync->write(Asset::Shader::flat);
	params.sync->write(params.technique);

	params.sync->write(RenderOp::Uniform);
	params.sync->write(Asset::Uniform::diffuse_color);
	params.sync->write(RenderDataType::Vec4);
	params.sync->write<s32>(1);
	params.sync->write(Vec4(1, 1, 1, 0.75f));

	params.sync->write(RenderOp::Uniform);
	params.sync->write(Asset::Uniform::mvp);
	params.sync->write(RenderDataType::Mat4);
	params.sync->write<s32>(1);
	params.sync->write<Mat4>(params.view_projection);

	params.sync->write(RenderOp::FillMode);
	params.sync->write(fill_mode);
	params.sync->write(RenderOp::LineWidth);
	params.sync->write<r32>(1 * UI::scale);

	params.sync->write(RenderOp::Mesh);
	params.sync->write(primitive_mode);
	params.sync->write(m);

	params.sync->write(RenderOp::FillMode);
	params.sync->write(RenderFillMode::Fill);
}

void debug_draw_nav_mesh(const RenderParams& params)
{
	refresh_nav_render_meshes(params);
	render_helper(params, render_mesh, RenderPrimitiveMode::Triangles, RenderFillMode::Point);
}

#endif

void draw_hollow(const RenderParams& params)
{
	if (!(params.camera->mask & RENDER_MASK_DEFAULT))
		return;

	Loader::shader_permanent(Asset::Shader::nav_dots);

	params.sync->write(RenderOp::Shader);
	params.sync->write(Asset::Shader::nav_dots);
	params.sync->write(params.technique);

	params.sync->write(RenderOp::Uniform);
	params.sync->write(Asset::Uniform::diffuse_color);
	params.sync->write(RenderDataType::Vec4);
	params.sync->write<s32>(1);
	params.sync->write(Vec4(1, 1, 1, 0.4f));

	params.sync->write(RenderOp::Uniform);
	params.sync->write(Asset::Uniform::mv);
	params.sync->write(RenderDataType::Mat4);
	params.sync->write<s32>(1);
	params.sync->write<Mat4>(params.view);

	params.sync->write(RenderOp::Uniform);
	params.sync->write(Asset::Uniform::p);
	params.sync->write(RenderDataType::Mat4);
	params.sync->write<s32>(1);
	params.sync->write<Mat4>(params.camera->projection);

	{
		Vec3 center;
		r32 range;
		if (params.camera->range > 0.0f)
		{
			range = params.camera->range;
			center = params.camera->range_center;
		}
		else
		{
			range = DRONE_MAX_DISTANCE;
			center = Vec3::zero;
		}

		params.sync->write(RenderOp::Uniform);
		params.sync->write(Asset::Uniform::range);
		params.sync->write(RenderDataType::R32);
		params.sync->write<s32>(1);
		params.sync->write<r32>(range * 2.0f);

		params.sync->write(RenderOp::Uniform);
		params.sync->write(Asset::Uniform::range_center);
		params.sync->write(RenderDataType::Vec3);
		params.sync->write<s32>(1);
		params.sync->write(center);
	}

	params.sync->write(RenderOp::FillMode);
	params.sync->write(RenderFillMode::Point);

	params.sync->write(RenderOp::Mesh);
	params.sync->write(RenderPrimitiveMode::Points);
	params.sync->write(drone_render_mesh);

	params.sync->write(RenderOp::FillMode);
	params.sync->write(RenderFillMode::Fill);
}

ComponentMask entity_mask = Rectifier::component_mask
	| Drone::component_mask
	| Minion::component_mask
	| Battery::component_mask
	| ForceField::component_mask
	| Bolt::component_mask
	| Grenade::component_mask
	| SpawnPoint::component_mask
	| Turret::component_mask
	| MinionSpawner::component_mask;

void entity_info(const Entity* e, Team query_team, Team* team, s8* type)
{
	Team _team;
	s8 _type;

	if (e->has<AIAgent>())
	{
		_team = e->get<AIAgent>()->team;
		if (e->has<Drone>())
		{
			s8 shield = e->get<Health>()->shield;
			if (_team == query_team)
				_type = shield <= 1 ? RecordedLife::EntityDroneFriendShield1 : RecordedLife::EntityDroneFriendShield2;
			else
			{
				if (e->get<AIAgent>()->stealth == 1.0f)
				{
					_team = TeamNone;
					_type = RecordedLife::EntityNone;
				}
				else
					_type = shield <= 1 ? RecordedLife::EntityDroneEnemyShield1 : RecordedLife::EntityDroneEnemyShield2;
			}
		}
		else if (e->has<Minion>())
			_type = _team == query_team ? RecordedLife::EntityMinionFriend : RecordedLife::EntityMinionEnemy;
	}
	else if (e->has<Battery>())
	{
		_team = e->get<Battery>()->team;
		if (_team == query_team)
			_type = RecordedLife::EntityBatteryFriend;
		else if (_team == TeamNone)
			_type = RecordedLife::EntityBatteryNeutral;
		else
			_type = RecordedLife::EntityBatteryEnemy;
	}
	else if (e->has<Rectifier>())
	{
		_team = e->get<Rectifier>()->team;
		_type = _team == query_team ? RecordedLife::EntityRectifierFriend : RecordedLife::EntityRectifierEnemy;
	}
	else if (e->has<ForceField>())
	{
		_team = e->get<ForceField>()->team;
		_type = _team == query_team ? RecordedLife::EntityForceFieldFriend : RecordedLife::EntityForceFieldEnemy;
	}
	else if (e->has<Bolt>())
	{
		_team = e->get<Bolt>()->team;
		_type = _team == query_team ? RecordedLife::EntityBoltFriend : RecordedLife::EntityBoltEnemy;
	}
	else if (e->has<Grenade>())
	{
		b8 attached = e->get<Transform>()->parent.ref();
		_team = e->get<Grenade>()->team;
		if (_team == query_team)
			_type = attached ? RecordedLife::EntityGrenadeFriendAttached : RecordedLife::EntityGrenadeFriendDetached;
		else
			_type = attached ? RecordedLife::EntityGrenadeEnemyAttached : RecordedLife::EntityGrenadeEnemyDetached;
	}
	else if (e->has<SpawnPoint>())
	{
		_team = e->get<SpawnPoint>()->team;
		if (_team == query_team)
			_type = RecordedLife::EntitySpawnPointFriend;
		else if (_team == AI::TeamNone)
			_type = RecordedLife::EntitySpawnPointNeutral;
		else
			_type = RecordedLife::EntitySpawnPointEnemy;
	}
	else if (e->has<Turret>())
	{
		_team = e->get<Turret>()->team;
		_type = _team == query_team ? RecordedLife::EntityTurretFriend : RecordedLife::EntityTurretEnemy;
	}
	else if (e->has<MinionSpawner>())
	{
		_team = e->get<MinionSpawner>()->team;
		_type = _team == query_team ? RecordedLife::EntityMinionSpawnerFriend : RecordedLife::EntityMinionSpawnerEnemy;
	}
	else if (e->has<Flag>())
	{
		_team = e->get<Flag>()->team;
		_type = _team == query_team ? RecordedLife::EntityFlagFriend : RecordedLife::EntityFlagEnemy;
	}
	else
	{
		_team = TeamNone;
		_type = RecordedLife::EntityNone;
		vi_assert(false);
	}

	if (team)
		*team = _team;
	if (type)
		*type = _type;
}

b8 record_filter(Entity* e, const Vec3& pos)
{
	return (e->get<Transform>()->absolute_pos() - pos).length_squared() < (DRONE_MAX_DISTANCE * 1.5f * DRONE_MAX_DISTANCE * 1.5f);
}

void RecordedLife::Tag::init(const PlayerManager* manager)
{
	energy = manager->energy;
	upgrades = manager->upgrades;

	AI::Team my_team = manager->team.ref()->team();
	time_remaining = vi_min(255, vi_max(0, s32((Game::session.config.time_limit() - Game::time.total) * 0.5f)));

	enemy_upgrades = 0;
	for (auto i = PlayerManager::list.iterator(); !i.is_last(); i.next())
	{
		if (i.item()->team.ref()->team() != my_team)
			enemy_upgrades |= i.item()->upgrades;
	}

	battery_state = 0;
	{
		vi_assert(Battery::list.count() <= 16);
		for (auto i = Battery::list.iterator(); !i.is_last(); i.next())
		{
			AI::Team t = i.item()->team;
			if (t == AI::TeamNone)
				battery_state |= (1 << (i.index * 2)) | (1 << ((i.index * 2) + 1));
			else if (t == my_team)
				battery_state |= (1 << (i.index * 2));
			else
				battery_state |= (1 << ((i.index * 2) + 1));
		}
	}

	turret_state = 0;
	{
		vi_assert(Turret::list.count() <= 32);
		for (auto i = Turret::list.iterator(); !i.is_last(); i.next())
			turret_state |= 1 << s32(i.index);
	}


	Entity* player = manager->instance.ref();

	if (player)
	{
		shield = player->get<Health>()->shield;
		stealth = player->get<AIAgent>()->stealth == 1.0f;

		{
			Quat rot;
			player->get<Transform>()->absolute(&pos, &rot);
			normal = rot * Vec3(0, 0, 1);
		}

		nearby_entities = 0;
		for (auto i = Entity::iterator(entity_mask); !i.is_last(); i.next())
		{
			if (i.item() != player && record_filter(i.item(), pos))
			{
				AI::Team team;
				s8 entity_type;
				entity_info(i.item(), my_team, &team, &entity_type);
				if (entity_type != EntityNone)
					nearby_entities |= 1 << s32(entity_type);
			}
		}
	}
	else
	{
		shield = 0;
		stealth = false;
		pos = normal = Vec3::zero;
		nearby_entities = 0;
	}
}

s32 RecordedLife::Tag::battery_count(BatteryState s) const
{
	vi_assert(s);
	s32 count = 0;
	for (s32 i = 0; i < 32; i += 2)
	{
		b8 a = battery_state & (1 << i);
		b8 b = battery_state & (1 << (i + 1));
		if (a && b)
		{
			if (s & BatteryStateNeutral)
				count++;
		}
		else if (a)
		{
			if (s & BatteryStateFriendly)
				count++;
		}
		else if (b)
		{
			if (s & BatteryStateEnemy)
				count++;
		}
		else
			break;
	}
	return count;
}

s32 RecordedLife::Tag::turret_count() const
{
	return BitUtility::popcount(turret_state);
}

b8 RecordedLife::Tag::turret(s32 index) const
{
	return turret_state & (1 << index);
}

RecordedLife::Tag::BatteryState RecordedLife::Tag::battery(s32 index) const
{
	b8 a = battery_state & (1 << index);
	b8 b = battery_state & (1 << (index + 1));
	if (a && b)
		return BatteryStateNeutral;
	else if (a)
		return BatteryStateFriendly;
	else if (b)
		return BatteryStateEnemy;
	else
		return BatteryStateNone;
}

void RecordedLife::reset()
{
	shield.length = 0;
	time_remaining.length = 0;
	energy.length = 0;
	pos.length = 0;
	normal.length = 0;
	upgrades.length = 0;
	enemy_upgrades.length = 0;
	battery_state.length = 0;
	turret_state.length = 0;
	nearby_entities.length = 0;
	stealth.length = 0;
	action.length = 0;
}

void RecordedLife::reset(AI::Team t)
{
	team = t;
	reset();
}

void RecordedLife::add(const Tag& tag, const Action& a)
{
	shield.add(tag.shield);
	time_remaining.add(tag.time_remaining);
	energy.add(tag.energy);
	pos.add(tag.pos);
	normal.add(tag.normal);
	upgrades.add(tag.upgrades);
	enemy_upgrades.add(tag.enemy_upgrades);
	battery_state.add(tag.battery_state);
	turret_state.add(tag.turret_state);
	nearby_entities.add(tag.nearby_entities);
	stealth.add(tag.stealth);
	action.add(a);
}

RecordedLife::Action::Action()
{
	memset(this, 0, sizeof(*this));
}

RecordedLife::Action& RecordedLife::Action::operator=(const Action& other)
{
	memcpy(this, &other, sizeof(*this));
	return *this;
}

b8 RecordedLife::Action::fuzzy_equal(const Action& other) const
{
	if (type == other.type)
	{
		switch (type)
		{
			case TypeNone:
				return true;
			case TypeMove:
				return (pos - other.pos).length_squared() < DRONE_MAX_DISTANCE * 0.1f * DRONE_MAX_DISTANCE * 0.1f;
			case TypeAttack:
				return entity_type == other.entity_type;
			case TypeUpgrade:
				return upgrade == other.upgrade;
			case TypeAbility:
				return ability == other.ability;
			case TypeWait:
				return true;
		}
	}
	return false;
}

// these functions get rid of const nonsense so we can pass either one into the serialize function
size_t RecordedLife::custom_fwrite(void* buffer, size_t size, size_t count, FILE* f)
{
	return fwrite(buffer, size, count, f);
}

size_t RecordedLife::custom_fread(void* buffer, size_t size, size_t count, FILE* f)
{
	return fread(buffer, size, count, f);
}

void RecordedLife::serialize(FILE* f, size_t(*func)(void*, size_t, size_t, FILE*))
{
	func(&team, sizeof(AI::Team), 1, f);

	func(&shield.length, sizeof(s32), 1, f);
	shield.resize(shield.length);
	func(shield.data, sizeof(s8), shield.length, f);

	func(&time_remaining.length, sizeof(s32), 1, f);
	time_remaining.resize(time_remaining.length);
	func(time_remaining.data, sizeof(s8), time_remaining.length, f);

	func(&energy.length, sizeof(s32), 1, f);
	energy.resize(energy.length);
	func(energy.data, sizeof(s16), energy.length, f);

	func(&pos.length, sizeof(s32), 1, f);
	pos.resize(pos.length);
	func(pos.data, sizeof(Vec3), pos.length, f);

	func(&normal.length, sizeof(s32), 1, f);
	normal.resize(normal.length);
	func(normal.data, sizeof(Vec3), normal.length, f);

	func(&upgrades.length, sizeof(s32), 1, f);
	upgrades.resize(upgrades.length);
	func(upgrades.data, sizeof(s32), upgrades.length, f);

	func(&enemy_upgrades.length, sizeof(s32), 1, f);
	enemy_upgrades.resize(enemy_upgrades.length);
	func(enemy_upgrades.data, sizeof(s32), enemy_upgrades.length, f);

	func(&battery_state.length, sizeof(s32), 1, f);
	battery_state.resize(battery_state.length);
	func(battery_state.data, sizeof(s32), battery_state.length, f);

	func(&turret_state.length, sizeof(s32), 1, f);
	turret_state.resize(turret_state.length);
	func(turret_state.data, sizeof(s32), turret_state.length, f);

	func(&nearby_entities.length, sizeof(s32), 1, f);
	nearby_entities.resize(nearby_entities.length);
	func(nearby_entities.data, sizeof(s32), nearby_entities.length, f);

	func(&stealth.length, sizeof(s32), 1, f);
	stealth.resize(stealth.length);
	func(stealth.data, sizeof(b8), stealth.length, f);

	func(&action.length, sizeof(s32), 1, f);
	action.resize(action.length);
	func(action.data, sizeof(Action), action.length, f);
}

}


}
