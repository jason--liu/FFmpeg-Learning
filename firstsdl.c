#include <SDL.h>
#include <stdio.h>

#define POINTS_COUNT 4
static SDL_Point points[POINTS_COUNT] = {
    {320, 200},
    {300, 240},
    {340, 240},
    {320, 280}};

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    int ret = -1;

    SDL_Window*   window   = NULL;
    SDL_Renderer* renderer = NULL;

    ret = SDL_Init(SDL_INIT_VIDEO);
    if (ret < 0)
    {
        printf("init sdl window error %s\n", SDL_GetError());
        goto __quit;
    }

    window = SDL_CreateWindow("FirstSDL",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        1024,
        800,
        SDL_WINDOW_SHOWN);

    if (!window)
    {
        printf("create sdl window error %s\n", SDL_GetError());
        goto __quit;
    }

    // create a renderer and bind to window
    renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer)
    {
        printf("create renderer error %s\n", SDL_GetError());
    }

    // fill the color, RGBA
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    // clear screen with target color
    SDL_RenderClear(renderer);

    // draw lines
    SDL_SetRenderDrawColor(renderer, 255,255,255,SDL_ALPHA_OPAQUE);
    SDL_RenderDrawLines(renderer,points,POINTS_COUNT);

    // draw a rectangle
    SDL_Rect rect = {800, 300, 100, 100}; //x,y,w,h
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    SDL_RenderDrawRect(renderer, &rect);
    SDL_RenderFillRect(renderer, &rect);

    /* SDL_RenderClear(renderer); */
    // until now everything is drawed, show the window with color
    SDL_RenderPresent(renderer);

    SDL_Delay(1000);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
__quit:
    SDL_Quit();

    return 0;
}
