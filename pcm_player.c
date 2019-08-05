#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <SDL.h>

#define BLOCK_SIZE 4096000
static int      buffer_len = 0;
static uint8_t* audio_buf  = NULL;
static uint8_t* audio_pos  = NULL;

void read_audio_callback(void* udata, Uint8* stream, int len)
{
    if (buffer_len == 0)
        return;

    SDL_memset(stream, 0, len);
    len = (len < buffer_len) ? len : buffer_len;
    SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);

    audio_pos += len;
    buffer_len -= len;
}

int main(int argc, char* argv[])
{
    int   ret       = -1;
    FILE* audio_fd  = NULL;
    char* audio_buf = NULL;

    if (argc < 2) {
        fprintf(stderr, "Usage %s <file[.pcm]>\n", argv[0]);
        return -1;
    }

    char*         path = argv[1];
    SDL_AudioSpec spec;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        fprintf(stderr, "Could not initialize SDL %s\n", SDL_GetError());
    }

    audio_fd = fopen(path, "r");
    if (!audio_fd) {
        fprintf(stderr, "Failed to open target file\n");
        goto __FAIL;
    }

    audio_buf = (char*)malloc(BLOCK_SIZE);
    if (!audio_buf) {
        goto __FAIL;
    }

    spec.freq      = 44100;
    spec.format    = AUDIO_S16SYS;
    spec.channels  = 2;
    spec.silence   = 0;
    spec.samples   = 2048;
    spec.callback  = read_audio_callback;
    spec.userdata = NULL;

    if (SDL_OpenAudio(&spec, NULL)) {
        fprintf(stderr, "Failed to open audio spec %s\n", SDL_GetError());
        goto __FAIL;
    }

    SDL_PauseAudio(0);

    do {
        buffer_len = fread(audio_buf, 1, BLOCK_SIZE, audio_fd);
        printf("block size is %zu\n", buffer_len);

        audio_pos = audio_buf;
        while (audio_pos < (audio_buf + buffer_len)) {
            SDL_Delay(1);
        }
    } while (buffer_len != 0);

    SDL_CloseAudio();

__FAIL:
    if (audio_buf) {
        free(audio_buf);
    }
    if (audio_fd) {
        fclose(audio_fd);
    }
    SDL_Quit();
    return 0;
}
