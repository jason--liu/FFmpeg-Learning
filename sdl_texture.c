#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

#define POINTS_COUNT 4
static SDL_Point points[POINTS_COUNT] = {{320, 200}, {300, 240}, {340, 240}, {320, 280}};

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    int ret  = -1;
    int quit = 0;

    SDL_Window*   window   = NULL;
    SDL_Renderer* renderer = NULL;
    SDL_Event     event;
    SDL_Texture*  texture;
    SDL_Rect      r;

    ret = SDL_Init(SDL_INIT_VIDEO);
    if (ret < 0)
    {
        printf("init sdl window error %s\n", SDL_GetError());
        goto __quit;
    }

    window = SDL_CreateWindow("FirstSDL", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1024, 800, SDL_WINDOW_SHOWN);

    if (!window)
    {
        printf("create sdl window error %s\n", SDL_GetError());
        goto __quit;
    }

    r.w = 100;
    r.h = 50;

    // create a renderer and bind to window
    renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer)
    {
        printf("create renderer error %s\n", SDL_GetError());
    }

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 1024, 800);

    // fill the color, RGBA
    /* SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); */
    // clear screen with target color
    /* SDL_RenderClear(renderer); */



    /* SDL_RenderClear(renderer); */
    // until now everything is drawed, show the window with color
    SDL_RenderPresent(renderer);

    while (!quit)
    {
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_QUIT:
                quit = 1;
                break;
            default:
                SDL_Log("event.type %d\n", event.type);
            }
            r.x = rand() % 1024;
            r.y = rand() % 800;

            SDL_SetRenderTarget(renderer, texture);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
            SDL_RenderClear(renderer);

            SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
            SDL_RenderDrawRect(renderer, &r);
            SDL_RenderFillRect(renderer, &r);

            SDL_SetRenderTarget(renderer, NULL);
            SDL_RenderCopy(renderer, texture, NULL, NULL);
            SDL_RenderPresent(renderer);
        }
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
__quit:
    SDL_Quit();

    return 0;
}
