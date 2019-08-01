#include <SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define REFRESH_EVENT (SDL_USEREVENT + 1)
#define BREAK_EVENT (SDL_USEREVENT + 2)

static int screen_w = 1000, screen_h = 1000;
const pixel_w = 1280, pixel_h = 720;

#define PIXEL_SIZE pixel_w* pixel_h * 12 / 8
static thread_exit = 0;
static int refresh_frame(void* arg)
{
    (void)arg;
    thread_exit = 0;
    SDL_Event event;

    while (!thread_exit) {
        SDL_PushEvent(&event);
        SDL_Delay(40);
    }

    thread_exit = 0;
    event.type  = BREAK_EVENT;
    SDL_PushEvent(&event);
    return 0;
}
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
    SDL_Thread*   refresh_thread = NULL;
    SDL_Rect      r;

    FILE* fp = NULL;

    ret = SDL_Init(SDL_INIT_VIDEO);
    if (ret < 0) {
        printf("init sdl window error %s\n", SDL_GetError());
        goto __quit;
    }

    window = SDL_CreateWindow("YUVPlayer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, screen_w, screen_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

    if (!window) {
        printf("create sdl window error %s\n", SDL_GetError());
        goto __quit;
    }

    // create a renderer and bind to window
    renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) {
        printf("create renderer error %s\n", SDL_GetError());
    }

    // create texture according to pixel size
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pixel_w, pixel_h);

    fp = fopen("out.yuv", "rb+");
    if (!fp) {
        printf("can not open yuv file\n");
        goto __quit;
    }

    uint8_t* buffer = (uint8_t*)malloc(PIXEL_SIZE);
    if (!buffer) {
        printf("malloc failed!\n");
        goto __quit1;
    }
    
    refresh_thread = SDL_CreateThread(refresh_frame, NULL, NULL);

    for (;;) {
        SDL_WaitEvent(&event);
        if (event.type == REFRESH_EVENT) {
            memset(buffer, 0, PIXEL_SIZE);
            if ((fread(buffer, 1, PIXEL_SIZE, fp)) != PIXEL_SIZE) {
                fseek(fp, 0, SEEK_SET);
                fread(buffer, 1, PIXEL_SIZE, fp);
            }

            SDL_UpdateTexture(texture, NULL, buffer, pixel_w);

            r.x = 0;
            r.y = 0;
            r.w = screen_w;
            r.h = screen_h;

            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, NULL);
            SDL_RenderPresent(renderer);

        } else if (event.type == SDL_WINDOWEVENT) {
            SDL_GetWindowSize(window, &screen_w, &screen_h);
        } else if (event.type == SDL_QUIT) {
            thread_exit = 1;
        } else if (event.type == BREAK_EVENT) {

            break;
        }
    }

    if (!buffer) {
        free(buffer);
    }

__quit1:
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
__quit:
    SDL_Quit();

    return 0;
}
