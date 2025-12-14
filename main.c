#include <raylib.h>
#include "engine.h"
#include <stdlib.h>

const double UPDATE_INTERVAL = 1.0 / 60.0;

void gameLoop(State *state)
{
    double lastUpdateTime = GetTime();
    while (!WindowShouldClose())
    {
        double currentTime = GetTime();
        double deltaTime = currentTime - lastUpdateTime;
        if (deltaTime >= UPDATE_INTERVAL)
        {
            doSimulation(state);
            lastUpdateTime = currentTime;
        }

        draw(state);
    }
}

int main(void)
{
    State *state = createState();
    init(state);
    while (!WindowShouldClose())
    {
        doSimulation(state);
        draw(state);
    }

    free(state);
    return 0;
}