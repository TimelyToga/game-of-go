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


    // Get current window dimensions (handles resize)
    int windowWidth = GetScreenWidth();
    int windowHeight = GetScreenHeight();

    // Calculate the biggest square that fits with padding on all sides
    int availableWidth = windowWidth - 2 * BOARD_PADDING;
    int availableHeight = windowHeight - 2 * BOARD_PADDING;
    int squareSize = fmax(0, fmin(availableWidth, availableHeight));

    // Calculate grid area with padding inside the board
    int x = (windowWidth - squareSize) / 2;
    int y = (windowHeight - squareSize) / 2;
    int gridX = x + GRID_PADDING;
    int gridY = y + GRID_PADDING;
    int gridSize = squareSize - 2 * GRID_PADDING;

    state->boardLayout.gridX = gridX;
    state->boardLayout.gridY = gridY;
    state->boardLayout.gridSize = gridSize;
    state->boardLayout.cellSpacing = gridSize / (BOARD_SIZE - 1);
    state->boardLayout.boardSize = squareSize;
}

static void screenToBoardCoordinates(State *state, int screenX, int screenY, int *boardX, int *boardY)
{
    // Calculate which intersection is closest to the click
    int closestX = (screenX - state->boardLayout.gridX + state->boardLayout.cellSpacing / 2) / state->boardLayout.cellSpacing;
    int closestY = (screenY - state->boardLayout.gridY + state->boardLayout.cellSpacing / 2) / state->boardLayout.cellSpacing;
    
    // Check if the click is within the stone hitbox around the intersection
    int intersectionScreenX = state->boardLayout.gridX + closestX * state->boardLayout.cellSpacing;
    int intersectionScreenY = state->boardLayout.gridY + closestY * state->boardLayout.cellSpacing;
    
    int stoneRadius = state->boardLayout.cellSpacing / 2 - 2; // Slightly smaller than half cell spacing
    int dx = screenX - intersectionScreenX;
    int dy = screenY - intersectionScreenY;
    
    if (dx * dx + dy * dy <= stoneRadius * stoneRadius) {
        *boardX = closestX;
        *boardY = closestY;
    } else {
        *boardX = -1;
        *boardY = -1;
    }

    if (*boardX < 0 || *boardX >= BOARD_SIZE || *boardY < 0 || *boardY >= BOARD_SIZE)
    {
        *boardX = -1;
        *boardY = -1;
    }
}

void doSimulation(State *state)
{
    state->simulationStep++;

    // Handle input 
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        int x = GetMouseX();
        int y = GetMouseY();

        int boardX, boardY;
        screenToBoardCoordinates(state, x, y, &boardX, &boardY);
        if (boardX != -1 && boardY != -1)
        {
            BOARD_SET(state, boardX, boardY, CELL_BLACK);
        }
    }

    // Handle game logic 
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

    // Draw grid lines
    DrawLineEx((Vector2){state->boardLayout.gridX, state->boardLayout.gridY}, (Vector2){state->boardLayout.gridX + state->boardLayout.gridSize, state->boardLayout.gridY}, LINE_THICKNESS, GridLineOverlayColor);
    DrawLineEx((Vector2){state->boardLayout.gridX, state->boardLayout.gridY}, (Vector2){state->boardLayout.gridX, state->boardLayout.gridY + state->boardLayout.gridSize}, LINE_THICKNESS, GridLineOverlayColor);
    DrawLineEx((Vector2){state->boardLayout.gridX + state->boardLayout.gridSize, state->boardLayout.gridY}, (Vector2){state->boardLayout.gridX + state->boardLayout.gridSize, state->boardLayout.gridY + state->boardLayout.gridSize}, LINE_THICKNESS, GridLineOverlayColor);
    DrawLineEx((Vector2){state->boardLayout.gridX, state->boardLayout.gridY + state->boardLayout.gridSize}, (Vector2){state->boardLayout.gridX + state->boardLayout.gridSize, state->boardLayout.gridY + state->boardLayout.gridSize}, LINE_THICKNESS, GridLineOverlayColor);

    // Draw vertical lines
    for (int i = 0; i < BOARD_SIZE; i++)
    {
        DrawLineEx((Vector2){state->boardLayout.gridX + i * state->boardLayout.cellSpacing, state->boardLayout.gridY}, (Vector2){state->boardLayout.gridX + i * state->boardLayout.cellSpacing, state->boardLayout.gridY + state->boardLayout.gridSize}, LINE_THICKNESS, GridLineOverlayColor);
    }

    // Draw horizontal lines
    for (int i = 0; i < BOARD_SIZE; i++)
    {
        DrawLineEx((Vector2){state->boardLayout.gridX, state->boardLayout.gridY + i * state->boardLayout.cellSpacing}, (Vector2){state->boardLayout.gridX + state->boardLayout.gridSize, state->boardLayout.gridY + i * state->boardLayout.cellSpacing}, LINE_THICKNESS, GridLineOverlayColor);
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
                DrawCircle(state->boardLayout.gridX + x * state->boardLayout.cellSpacing, state->boardLayout.gridY + y * state->boardLayout.cellSpacing, STONE_RADIUS, *color);
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