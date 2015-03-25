#pragma once

#include "types.h"
#include "exec.h"
#include "physics.h"

class Controls : public ExecDynamic<GameTime>
{
public:
	Controls();
	Mat4 view;
	Mat4 projection;

	Vec3 position; 
	float angle_horizontal;
	float angle_vertical;
	float fov_initial;

	float speed;
	float speed_mouse;

	void exec(GameTime);

	btDiscreteDynamicsWorld* world;
};
