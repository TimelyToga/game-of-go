#include "engine.h"
#include "raylib.h"
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include "renderer.h"

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

    // Initialize some random stones
    for (int i = 0; i < 10; i++)
    {
        int x = rand() % BOARD_SIZE;
        int y = rand() % BOARD_SIZE;
        BOARD_SET(state, x, y, CELL_BLACK);
    }

    for (int i = 0; i < 10; i++)
    {
        int x = rand() % BOARD_SIZE;
        int y = rand() % BOARD_SIZE;
        BOARD_SET(state, x, y, CELL_WHITE);
    }

    SetTargetFPS(60);
}

void doSimulation(State *state)
{
    state->simulationStep++;
}

static void drawBoard(State *state)
{
    // Get current window dimensions (handles resize)
    int windowWidth = GetScreenWidth();
    int windowHeight = GetScreenHeight();

    // Calculate the biggest square that fits with padding on all sides
    int availableWidth = windowWidth - 2 * BOARD_PADDING;
    int availableHeight = windowHeight - 2 * BOARD_PADDING;
    int squareSize = fmax(0, fmin(availableWidth, availableHeight));

    int x = (windowWidth - squareSize) / 2;
    int y = (windowHeight - squareSize) / 2;
    DrawRectangle(x, y, squareSize, squareSize, GoBoardColor);

    // Calculate grid area with padding inside the board
    int gridX = x + GRID_PADDING;
    int gridY = y + GRID_PADDING;
    int gridSize = squareSize - 2 * GRID_PADDING;

    // Draw grid lines
    DrawLineEx((Vector2){gridX, gridY}, (Vector2){gridX + gridSize, gridY}, LINE_THICKNESS, GridLineOverlayColor);
    DrawLineEx((Vector2){gridX, gridY}, (Vector2){gridX, gridY + gridSize}, LINE_THICKNESS, GridLineOverlayColor);
    DrawLineEx((Vector2){gridX + gridSize, gridY}, (Vector2){gridX + gridSize, gridY + gridSize}, LINE_THICKNESS, GridLineOverlayColor);
    DrawLineEx((Vector2){gridX, gridY + gridSize}, (Vector2){gridX + gridSize, gridY + gridSize}, LINE_THICKNESS, GridLineOverlayColor);

    // Draw vertical lines
    for (int i = 0; i < BOARD_SIZE; i++)
    {
        DrawLineEx((Vector2){gridX + i * gridSize / (BOARD_SIZE - 1), gridY}, (Vector2){gridX + i * gridSize / (BOARD_SIZE - 1), gridY + gridSize}, LINE_THICKNESS, GridLineOverlayColor);
    }

    // Draw horizontal lines
    for (int i = 0; i < BOARD_SIZE; i++)
    {
        DrawLineEx((Vector2){gridX, gridY + i * gridSize / (BOARD_SIZE - 1)}, (Vector2){gridX + gridSize, gridY + i * gridSize / (BOARD_SIZE - 1)}, LINE_THICKNESS, GridLineOverlayColor);
    }

    // Draw Go stones
    for (int x = 0; x < BOARD_SIZE; x++)
    {
        for (int y = 0; y < BOARD_SIZE; y++)
        {
            Color *color = NULL;
            if (BOARD_GET(state, x, y) == CELL_BLACK)
            {
                color = &BLACK;
            }
            else if (BOARD_GET(state, x, y) == CELL_WHITE)
            {
                color = &WHITE;
            }

            if (color != NULL)
            {
                DrawCircle(gridX + x * gridSize / (BOARD_SIZE - 1), gridY + y * gridSize / (BOARD_SIZE - 1), STONE_RADIUS, *color);
            }
        }
    }
}

void draw(State *state)
{
    BeginDrawing();
    ClearBackground(RAYWHITE);
    drawBoard(state);
    DrawText(TextFormat("Simulation Step: %llu", (unsigned long long)state->simulationStep), 10, 10, 20, BLACK);
    EndDrawing();
}