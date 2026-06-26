#include <stdio.h>
#include <gecs.h>
#include <stdint.h>

struct Position
{
	float x, y;
};

int main()
{
	GECS_Init();

	GECS_RegisterComponent("Transform", sizeof(struct Position));

	ID player = GECS_CreateEntity("Player");

	struct Position pos = {1.0f, 1.0f};
	GECS_CreateComponent(player, "Transform", &pos);

	GECS_DeleteEntity(player);
	GECS_CleanUp();

	printf("All done.\n");
    return 0;
}
