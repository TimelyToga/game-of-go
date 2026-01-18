#include "engine.h"
#include "raylib.h"
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "renderer.h"

static unsigned int countLiberties(State *state, int x, int y);
static unsigned int floodFill(State *state, CellState scratchpad[NUM_CELLS], int x, int y);

State *createState(void)
{
    State *state = calloc(1, sizeof(State));
    state->windowWidth = 800;
    state->windowHeight = 600;

    return state;
}

static bool isInBounds(int x, int y)
{
    return x >= 0 && x < BOARD_SIZE && y >= 0 && y < BOARD_SIZE;
}

static void addAction(State *state, int x, int y, CellState cellState)
{
    BOARD_SET(state, x, y, cellState);
    state->actions[state->actionCount].x = x;
    state->actions[state->actionCount].y = y;
    state->actions[state->actionCount].cellState = cellState;
    state->actionCount++;
}

// Returns true if there is a last action, false otherwise
static bool getLastAction(State *state, Action *action)
{
    if (state->actionCount > 0)
    {
        *action = state->actions[state->actionCount - 1];
        return true;
    }

    return false;
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
        addAction(state, x, y, CELL_BLACK);
    }

    for (int i = 0; i < 10; i++)
    {
        int x = rand() % BOARD_SIZE;
        int y = rand() % BOARD_SIZE;
        addAction(state, x, y, CELL_WHITE);
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
    int boardX = -1, boardY = -1;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        int x = GetMouseX();
        int y = GetMouseY();

        screenToBoardCoordinates(state, x, y, &boardX, &boardY);
    }
    
    // Handle game logic 
    if (boardX != -1 && boardY != -1)
    {
        CellState nextCellState = CELL_BLACK;
        Action lastAction = {0};
        if (getLastAction(state, &lastAction))
        {
            nextCellState = lastAction.cellState == CELL_BLACK ? CELL_WHITE : CELL_BLACK;
        }

        if (BOARD_GET(state, boardX, boardY) != CELL_EMPTY)
        {
            return;
        }

        CellState boardBeforeMove[NUM_CELLS];
        memcpy(boardBeforeMove, state->board, sizeof(state->board));

        BOARD_SET(state, boardX, boardY, nextCellState);

        CellState scratchpad[NUM_CELLS];
        memset(scratchpad, 0, sizeof(scratchpad));
        bool shouldRemove = shouldRemoveStones(state, scratchpad, boardX, boardY);
        if (shouldRemove)
        {
            removeStones(state, scratchpad);
        }

        if (countLiberties(state, boardX, boardY) == 0)
        {
            memcpy(state->board, boardBeforeMove, sizeof(state->board));
            return;
        }

        if (state->hasKoBoard &&
            memcmp(state->board, state->koBoard, sizeof(state->board)) == 0)
        {
            memcpy(state->board, boardBeforeMove, sizeof(state->board));
            return;
        }

        memcpy(state->koBoard, boardBeforeMove, sizeof(state->board));
        state->hasKoBoard = true;
        addAction(state, boardX, boardY, nextCellState);
    }
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

static unsigned int countLiberties(State *state, int x, int y) {
    if (!isInBounds(x, y))
    {
        return 0;
    }

    CellState color = BOARD_GET(state, x, y);
    if (color == CELL_EMPTY)
    {
        return 0;
    }

    bool visited[NUM_CELLS] = {0};
    bool libertySeen[NUM_CELLS] = {0};
    int stackX[NUM_CELLS];
    int stackY[NUM_CELLS];
    int stackSize = 0;

    int startIndex = y * BOARD_SIZE + x;
    visited[startIndex] = true;
    stackX[stackSize] = x;
    stackY[stackSize] = y;
    stackSize++;

    unsigned int liberties = 0;
    while (stackSize > 0)
    {
        stackSize--;
        int cx = stackX[stackSize];
        int cy = stackY[stackSize];

        const int offsets[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (int i = 0; i < 4; i++)
        {
            int nx = cx + offsets[i][0];
            int ny = cy + offsets[i][1];
            if (!isInBounds(nx, ny))
            {
                continue;
            }

            int nIndex = ny * BOARD_SIZE + nx;
            CellState neighbor = BOARD_GET(state, nx, ny);
            if (neighbor == CELL_EMPTY)
            {
                if (!libertySeen[nIndex])
                {
                    libertySeen[nIndex] = true;
                    liberties++;
                }
                continue;
            }

            if (neighbor != color || visited[nIndex])
            {
                continue;
            }

            visited[nIndex] = true;
            stackX[stackSize] = nx;
            stackY[stackSize] = ny;
            stackSize++;
        }
    }

    return liberties;
}

static unsigned int floodFill(State *state, CellState scratchpad[NUM_CELLS], int x, int y)
{ 
    if (!isInBounds(x, y))
    {
        return 0;
    }

    CellState color = BOARD_GET(state, x, y);
    if (color == CELL_EMPTY)
    {
        return 0;
    }

    int startIndex = y * BOARD_SIZE + x;
    if (scratchpad[startIndex] == color)
    {
        return 0;
    }

    bool visited[NUM_CELLS] = {0};
    int stackX[NUM_CELLS];
    int stackY[NUM_CELLS];
    int stackSize = 0;

    visited[startIndex] = true;
    stackX[stackSize] = x;
    stackY[stackSize] = y;
    stackSize++;

    unsigned int filled = 0;
    while (stackSize > 0)
    {
        stackSize--;
        int cx = stackX[stackSize];
        int cy = stackY[stackSize];
        int cIndex = cy * BOARD_SIZE + cx;

        scratchpad[cIndex] = color;
        filled++;

        const int offsets[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (int i = 0; i < 4; i++)
        {
            int nx = cx + offsets[i][0];
            int ny = cy + offsets[i][1];
            if (!isInBounds(nx, ny))
            {
                continue;
            }

            int nIndex = ny * BOARD_SIZE + nx;
            if (visited[nIndex])
            {
                continue;
            }

            if (BOARD_GET(state, nx, ny) != color)
            {
                continue;
            }

            visited[nIndex] = true;
            stackX[stackSize] = nx;
            stackY[stackSize] = ny;
            stackSize++;
        }
    }

    return filled;
}

bool shouldRemoveStones(State *state,
                        CellState scratchpad[NUM_CELLS], int targetX,
                        int targetY)
{
    if (!isInBounds(targetX, targetY))
    {
        return false;
    }

    CellState actingColor = BOARD_GET(state, targetX, targetY);
    if (actingColor == CELL_EMPTY)
    {
        return false;
    }

    CellState opponentColor = actingColor == CELL_BLACK ? CELL_WHITE : CELL_BLACK;
    bool removed = false;
    const int offsets[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

    for (int i = 0; i < 4; i++)
    {
        int nx = targetX + offsets[i][0];
        int ny = targetY + offsets[i][1];
        if (!isInBounds(nx, ny))
        {
            continue;
        }

        if (BOARD_GET(state, nx, ny) != opponentColor)
        {
            continue;
        }

        if (countLiberties(state, nx, ny) == 0)
        {
            if (floodFill(state, scratchpad, nx, ny) > 0)
            {
                removed = true;
            }
        }
    }

    return removed;
}

// removeStones just applies the removal of stones from scratpad to board if
// necessary
void removeStones(State *state, CellState scratpad[NUM_CELLS])
{
    for (int i = 0; i < NUM_CELLS; i++)
    {
        if (scratpad[i] != CELL_EMPTY)
        {
            state->board[i] = CELL_EMPTY;
        }
    }
}
