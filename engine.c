#include "engine.h"
#include "raylib.h"
#include <stdint.h>
#include <stdlib.h>

State *createState(void)
{
    State *state = malloc(sizeof(State));
    state->windowWidth = 800;
    state->windowHeight = 600;

    return state;
}
void init(State *state)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(state->windowWidth, state->windowHeight, "Game of Go");

    SetTargetFPS(60);
}

void doSimulation(State *state)
{
    state->simulationStep++;
}

void draw(State *state)
{
    BeginDrawing();
    ClearBackground(RAYWHITE);
    DrawText(TextFormat("Simulation Step: %llu", (unsigned long long)state->simulationStep), 10, 10, 20, BLACK);
    EndDrawing();
}