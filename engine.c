#include "engine.h"
#include "raylib.h"
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "renderer.h"

static const char *DEFAULT_SAVE_PATH = "game.sgf";

static unsigned int countLiberties(State *state, int x, int y);
static unsigned int floodFill(State *state, CellState scratchpad[NUM_CELLS], int x, int y);
static void updateBoardLayout(State *state);
static CellState getNextPlayer(State *state);
static bool applyMove(State *state, int x, int y, CellState player, bool isPass);
static void recordHistory(State *state);
static void resetGame(State *state);
static void undoMove(State *state);
static void drawHud(State *state);
static Font loadUIFont(bool *hasCustomFont);
static bool saveGame(const State *state, const char *path);
static bool loadGame(State *state, const char *path);
static void formatCoord(char *out, size_t outSize, int x, int y);
static void drawGhostStone(State *state);

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

    SetTargetFPS(60);

    resetGame(state);
    updateBoardLayout(state);
    state->uiFont = loadUIFont(&state->hasCustomFont);
}

static void updateBoardLayout(State *state)
{
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

static void recordHistory(State *state)
{
    int index = state->actionCount;
    if (index < 0 || index > MAX_ACTIONS)
    {
        return;
    }

    memcpy(state->historyBoards[index], state->board, sizeof(state->board));
    memcpy(state->koHistory[index], state->koBoard, sizeof(state->koBoard));
    state->hasKoHistory[index] = state->hasKoBoard;
    state->captureHistoryBlack[index] = state->capturesBlack;
    state->captureHistoryWhite[index] = state->capturesWhite;
}

static void resetGame(State *state)
{
    memset(state->board, CELL_EMPTY, sizeof(state->board));
    memset(state->koBoard, CELL_EMPTY, sizeof(state->koBoard));
    state->hasKoBoard = false;
    state->actionCount = 0;
    state->capturesBlack = 0;
    state->capturesWhite = 0;
    state->reviewMode = false;
    state->reviewIndex = 0;
    recordHistory(state);
}

static void undoMove(State *state)
{
    if (state->actionCount <= 0)
    {
        return;
    }

    state->actionCount--;
    memcpy(state->board, state->historyBoards[state->actionCount], sizeof(state->board));
    memcpy(state->koBoard, state->koHistory[state->actionCount], sizeof(state->koBoard));
    state->hasKoBoard = state->hasKoHistory[state->actionCount];
    state->capturesBlack = state->captureHistoryBlack[state->actionCount];
    state->capturesWhite = state->captureHistoryWhite[state->actionCount];

    if (state->reviewIndex > state->actionCount)
    {
        state->reviewIndex = state->actionCount;
    }
}

static CellState getNextPlayer(State *state)
{
    Action lastAction = {0};
    if (getLastAction(state, &lastAction))
    {
        return lastAction.cellState == CELL_BLACK ? CELL_WHITE : CELL_BLACK;
    }

    return CELL_BLACK;
}

static bool recordAction(State *state, int x, int y, CellState cellState, bool isPass)
{
    if (state->actionCount >= MAX_ACTIONS)
    {
        return false;
    }

    state->actions[state->actionCount].x = x;
    state->actions[state->actionCount].y = y;
    state->actions[state->actionCount].cellState = cellState;
    state->actions[state->actionCount].isPass = isPass;
    state->actionCount++;
    recordHistory(state);

    if (!state->reviewMode)
    {
        state->reviewIndex = state->actionCount;
    }

    return true;
}

static bool applyMove(State *state, int x, int y, CellState player, bool isPass)
{
    if (state->actionCount >= MAX_ACTIONS)
    {
        return false;
    }

    CellState boardBeforeMove[NUM_CELLS];
    CellState koBeforeMove[NUM_CELLS];
    bool hasKoBeforeMove = state->hasKoBoard;
    int capturesBlackBefore = state->capturesBlack;
    int capturesWhiteBefore = state->capturesWhite;

    memcpy(boardBeforeMove, state->board, sizeof(state->board));
    memcpy(koBeforeMove, state->koBoard, sizeof(state->koBoard));

    if (isPass)
    {
        memcpy(state->koBoard, boardBeforeMove, sizeof(state->board));
        state->hasKoBoard = true;
        if (!recordAction(state, -1, -1, player, true))
        {
            memcpy(state->koBoard, koBeforeMove, sizeof(state->koBoard));
            state->hasKoBoard = hasKoBeforeMove;
            return false;
        }
        return true;
    }

    if (!isInBounds(x, y) || BOARD_GET(state, x, y) != CELL_EMPTY)
    {
        return false;
    }

    BOARD_SET(state, x, y, player);

    CellState scratchpad[NUM_CELLS];
    memset(scratchpad, 0, sizeof(scratchpad));
    bool shouldRemove = shouldRemoveStones(state, scratchpad, x, y);
    if (shouldRemove)
    {
        removeStones(state, scratchpad, player);
    }

    if (countLiberties(state, x, y) == 0)
    {
        memcpy(state->board, boardBeforeMove, sizeof(state->board));
        state->capturesBlack = capturesBlackBefore;
        state->capturesWhite = capturesWhiteBefore;
        return false;
    }

    if (state->hasKoBoard &&
        memcmp(state->board, state->koBoard, sizeof(state->board)) == 0)
    {
        memcpy(state->board, boardBeforeMove, sizeof(state->board));
        state->capturesBlack = capturesBlackBefore;
        state->capturesWhite = capturesWhiteBefore;
        return false;
    }

    memcpy(state->koBoard, boardBeforeMove, sizeof(state->board));
    state->hasKoBoard = true;

    if (!recordAction(state, x, y, player, false))
    {
        memcpy(state->board, boardBeforeMove, sizeof(state->board));
        memcpy(state->koBoard, koBeforeMove, sizeof(state->koBoard));
        state->hasKoBoard = hasKoBeforeMove;
        state->capturesBlack = capturesBlackBefore;
        state->capturesWhite = capturesWhiteBefore;
        return false;
    }

    return true;
}

static Font loadUIFont(bool *hasCustomFont)
{
    *hasCustomFont = false;
    const char *candidates[] = {
        "assets/ui.ttf",
        "/System/Library/Fonts/Supplemental/Palatino.ttc",
        "/System/Library/Fonts/Supplemental/Georgia.ttf",
        "/System/Library/Fonts/Supplemental/Optima.ttf",
    };

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
    {
        if (FileExists(candidates[i]))
        {
            Font font = LoadFontEx(candidates[i], 24, NULL, 0);
            if (font.texture.id != 0)
            {
                *hasCustomFont = true;
                return font;
            }
        }
    }

    return GetFontDefault();
}

void doSimulation(State *state)
{
    state->simulationStep++;
    updateBoardLayout(state);

    if (IsKeyPressed(KEY_R))
    {
        state->reviewMode = !state->reviewMode;
        state->reviewIndex = state->actionCount;
    }

    if (state->reviewMode)
    {
        if (IsKeyPressed(KEY_RIGHT))
        {
            state->reviewIndex = state->reviewIndex < state->actionCount ? state->reviewIndex + 1 : state->reviewIndex;
        }
        if (IsKeyPressed(KEY_LEFT))
        {
            state->reviewIndex = state->reviewIndex > 0 ? state->reviewIndex - 1 : 0;
        }
    }

    if (IsKeyPressed(KEY_S))
    {
        saveGame(state, DEFAULT_SAVE_PATH);
    }

    if (IsKeyPressed(KEY_L))
    {
        loadGame(state, DEFAULT_SAVE_PATH);
    }

    if (!state->reviewMode && IsKeyPressed(KEY_U))
    {
        undoMove(state);
    }

    if (!state->reviewMode && IsKeyPressed(KEY_P))
    {
        applyMove(state, -1, -1, getNextPlayer(state), true);
    }

    if (state->reviewMode)
    {
        return;
    }

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
        applyMove(state, boardX, boardY, getNextPlayer(state), false);
    }
}

static CellState boardAt(const CellState *board, int x, int y)
{
    return board[y * BOARD_SIZE + x];
}

static void drawBoard(State *state, const CellState *board)
{
    int x = state->boardLayout.gridX - GRID_PADDING;
    int y = state->boardLayout.gridY - GRID_PADDING;
    DrawRectangle(x, y, state->boardLayout.boardSize, state->boardLayout.boardSize, GoBoardColor);

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
            if (boardAt(board, x, y) == CELL_BLACK)
            {
                color = &BLACK;
            }
            else if (boardAt(board, x, y) == CELL_WHITE)
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
    updateBoardLayout(state);
    const CellState *boardToDraw = state->reviewMode ? state->historyBoards[state->reviewIndex] : state->board;
    drawBoard(state, boardToDraw);
    if (!state->reviewMode)
    {
        drawGhostStone(state);
    }
    drawHud(state);
    EndDrawing();
}

static const char *playerName(CellState player)
{
    return player == CELL_BLACK ? "Black" : "White";
}

static void formatCoord(char *out, size_t outSize, int x, int y)
{
    if (x < 0 || y < 0)
    {
        snprintf(out, outSize, "pass");
        return;
    }

    snprintf(out, outSize, "%c%d", (char)('A' + x), y + 1);
}

static void drawHud(State *state)
{
    const int panelX = 16;
    const int panelY = 16;
    const int panelWidth = 300;
    const int panelHeight = 190;
    const Color panelColor = (Color){245, 238, 223, 220};
    const Color borderColor = (Color){120, 90, 60, 200};
    const Color textColor = (Color){35, 25, 15, 255};

    DrawRectangle(panelX - 8, panelY - 8, panelWidth + 16, panelHeight + 16, panelColor);
    DrawRectangleLines(panelX - 8, panelY - 8, panelWidth + 16, panelHeight + 16, borderColor);

    float fontSize = 20.0f;
    float spacing = 1.0f;
    Vector2 pos = {(float)panelX, (float)panelY};
    char line[128];

    snprintf(line, sizeof(line), "Mode: %s", state->reviewMode ? "Review" : "Play");
    DrawTextEx(state->uiFont, line, pos, fontSize, spacing, textColor);
    pos.y += fontSize + 6;

    if (state->reviewMode)
    {
        snprintf(line, sizeof(line), "Move: %d/%d", state->reviewIndex, state->actionCount);
    }
    else
    {
        snprintf(line, sizeof(line), "Turn: %s", playerName(getNextPlayer(state)));
    }
    DrawTextEx(state->uiFont, line, pos, fontSize, spacing, textColor);
    pos.y += fontSize + 6;

    int capBlack = state->reviewMode ? state->captureHistoryBlack[state->reviewIndex] : state->capturesBlack;
    int capWhite = state->reviewMode ? state->captureHistoryWhite[state->reviewIndex] : state->capturesWhite;
    snprintf(line, sizeof(line), "Captures: B %d  W %d", capBlack, capWhite);
    DrawTextEx(state->uiFont, line, pos, fontSize, spacing, textColor);
    pos.y += fontSize + 6;

    snprintf(line, sizeof(line), "File: %s", DEFAULT_SAVE_PATH);
    DrawTextEx(state->uiFont, line, pos, fontSize, spacing, textColor);
    pos.y += fontSize + 6;

    if (state->actionCount > 0)
    {
        int lastIndex = state->reviewMode ? state->reviewIndex - 1 : state->actionCount - 1;
        if (lastIndex >= 0)
        {
            Action last = state->actions[lastIndex];
            char coord[32];
            formatCoord(coord, sizeof(coord), last.x, last.y);
            snprintf(line, sizeof(line), "Last: %s %s", playerName(last.cellState), coord);
            DrawTextEx(state->uiFont, line, pos, fontSize, spacing, textColor);
            pos.y += fontSize + 6;
        }
    }

    DrawTextEx(state->uiFont, "Controls: Click=play  P=pass  U=undo", pos, fontSize - 2.0f, spacing, textColor);
    pos.y += fontSize + 4;
    DrawTextEx(state->uiFont, "R=review  Left/Right=step  S/L=save/load", pos, fontSize - 2.0f, spacing, textColor);
}

static void drawGhostStone(State *state)
{
    int mouseX = GetMouseX();
    int mouseY = GetMouseY();
    int boardX = -1;
    int boardY = -1;

    screenToBoardCoordinates(state, mouseX, mouseY, &boardX, &boardY);
    if (boardX == -1 || boardY == -1)
    {
        return;
    }

    if (BOARD_GET(state, boardX, boardY) != CELL_EMPTY)
    {
        return;
    }

    CellState player = getNextPlayer(state);
    Color baseColor = player == CELL_BLACK ? BLACK : WHITE;
    Color ghostColor = (Color){baseColor.r, baseColor.g, baseColor.b, 110};

    DrawCircle(state->boardLayout.gridX + boardX * state->boardLayout.cellSpacing,
               state->boardLayout.gridY + boardY * state->boardLayout.cellSpacing,
               STONE_RADIUS, ghostColor);
}

static bool saveGame(const State *state, const char *path)
{
    FILE *file = fopen(path, "w");
    if (!file)
    {
        return false;
    }

    fprintf(file, "(;GM[1]FF[4]SZ[%d]KM[6.5]AP[game-of-go]\n", BOARD_SIZE);
    int movesOnLine = 0;
    for (int i = 0; i < state->actionCount; i++)
    {
        Action action = state->actions[i];
        char coord[3] = {0};
        if (!action.isPass && action.x >= 0 && action.y >= 0)
        {
            coord[0] = (char)('a' + action.x);
            coord[1] = (char)('a' + action.y);
        }
        fprintf(file, ";%c[%s]", action.cellState == CELL_BLACK ? 'B' : 'W', coord);
        movesOnLine++;
        if (movesOnLine >= 10)
        {
            fprintf(file, "\n");
            movesOnLine = 0;
        }
    }
    fprintf(file, ")\n");
    fclose(file);
    return true;
}

static bool parseBoardSize(const char *data, int *outSize)
{
    const char *sz = strstr(data, "SZ[");
    if (!sz)
    {
        return false;
    }

    sz += 3;
    int size = 0;
    while (*sz && isdigit((unsigned char)*sz))
    {
        size = size * 10 + (*sz - '0');
        sz++;
    }

    if (size <= 0)
    {
        return false;
    }

    *outSize = size;
    return true;
}

static bool loadGame(State *state, const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file)
    {
        return false;
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (length <= 0)
    {
        fclose(file);
        return false;
    }

    char *data = malloc((size_t)length + 1);
    if (!data)
    {
        fclose(file);
        return false;
    }

    size_t readBytes = fread(data, 1, (size_t)length, file);
    fclose(file);
    data[readBytes] = '\0';

    int sizeFromFile = BOARD_SIZE;
    if (parseBoardSize(data, &sizeFromFile) && sizeFromFile != BOARD_SIZE)
    {
        free(data);
        return false;
    }

    resetGame(state);
    const char *cursor = data;
    while ((cursor = strchr(cursor, ';')) != NULL)
    {
        cursor++;
        if ((*cursor == 'B' || *cursor == 'W') && cursor[1] == '[')
        {
            CellState player = *cursor == 'B' ? CELL_BLACK : CELL_WHITE;
            cursor += 2;
            bool isPass = true;
            int x = -1;
            int y = -1;

            if (*cursor != ']')
            {
                if (cursor[0] && cursor[1])
                {
                    x = cursor[0] - 'a';
                    y = cursor[1] - 'a';
                    isPass = false;
                }
            }

            const char *end = strchr(cursor, ']');
            if (!end)
            {
                break;
            }
            cursor = end;

            if (!isPass && (!isInBounds(x, y)))
            {
                free(data);
                return false;
            }

            if (!applyMove(state, x, y, player, isPass))
            {
                free(data);
                return false;
            }
        }
    }

    state->reviewMode = false;
    state->reviewIndex = state->actionCount;
    free(data);
    return true;
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
void removeStones(State *state, CellState scratpad[NUM_CELLS], CellState actingColor)
{
    int removed = 0;
    for (int i = 0; i < NUM_CELLS; i++)
    {
        if (scratpad[i] != CELL_EMPTY)
        {
            state->board[i] = CELL_EMPTY;
            removed++;
        }
    }

    if (removed > 0)
    {
        if (actingColor == CELL_BLACK)
        {
            state->capturesBlack += removed;
        }
        else if (actingColor == CELL_WHITE)
        {
            state->capturesWhite += removed;
        }
    }
}
