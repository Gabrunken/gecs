#include <stdio.h>
#include <gecs.h>

int main()
{
	GECS_RegisterComponent("Transform", 16);

	GECS_CleanUp();

	printf("All done.\n");
    return 0;
}
