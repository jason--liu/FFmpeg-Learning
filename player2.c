#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)
#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)

#define FF_REFRESH_EVENT (SDL_USEREVENT)
#define FF_QUIT_EVENT (SDL_USEREVENT + 1)

#define VIDEO_PICTURE_QUEUE_SIZE 1

typedef struct PacketQueue {
    AVPacketList *first_pkt, *last_pkt;
    int           nb_packets;
    int           size;
    SDL_mutex*    mutex;
    SDL_cond*     cond;
} PACketQueue;

typedef struct VideoStruct {
    // for multi-media file
    char             filename[1024];
    AVFormatContext* pFormatCtx;
    int              videoStream, audioStream;

    // for audio
    AVStream*          audio_st;
    AVCodecContext*    audio_ctx;
    PACketQueue        audioq;
    uint8_t            audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
    uint32_t           audio_buf_size;
    uint32_t           audio_buf_index;
    AVFrame            audio_frame;
    AVPacket           audio_pkt;
    uint8_t*           audio_pkt_data;
    int                audio_pkt_size;
    struct SwrContext* audio_swr_ctx;

    // for video
    AVStream*          video_st;
    AVCodecContext*    video_ctx;
    PacketQueue        videoq;
    struct SwsContext* sws_ctx;

    //for thread
    SDL_Thread* parse_tid;
    SDL_Thread* video_tid;
    int         quit;

} VideoState;

SDL_mutex*    texture_mutex;
SDL_Window*   win;
SDL_Renderer* renderer;
SDL_Texture*  texture;

FILE* audiofd  = NULL;
FILE* audiofd1 = NULL;

VideoState* global_video_state;

void packet_queue_init(PacketQueue* q)
{
    memset(q, 0, sizeof(PACketQueue));
    q->mutex = SDL_CreateMutex();
    q->cond  = SDL_CreateCond();
}

int packet_queue_put(PACketQueue* q, AVPacket* pkt)
{
    AVPacketList* pkt1;

    pkt1 = (AVPacketList*)av_malloc(sizeof(AVPacketList));
    if (!pkt1)
        return -1;

    pkt1->pkt  = *pkt;
    pkt1->next = NULL;
    SDL_LockMutex(q->mutex);

    if (!q->last_pkt)
        q->first_pkt = pkt1;
    else
        q->last_pkt->next = pkt1;
    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size;

    SDL_CondSignal(q->cond);
    SDL_UnlockMutex(q->mutex);
    return 0;
}

int packet_queue_get(PACketQueue* q, AVPacket* pkt, int block)
{
    AVPacketList* pkt1;
    int           ret = -1;

    SDL_LockMutex(q->mutex);
    for (;;) {
        if (global_video_state->quit) {
            printf("quit frome queue get\n");
            return -1;
        }
        pkt1 = q->first_pkt;
        if (pkt1) {
            q->first_pkt = pkt1->next;
            if (!q->first_pkt)
                q->last_pkt = NULL;
            q->nb_packets--;
            q->size -= pkt1->pkt.size;
            *pkt = pkt1->pkt;
            av_free(pkt1);
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            printf("queue is empty, wait...\n");
            SDL_CondWait(q->cond, q->mutex);
        }
    }
    SDL_UnlockMutex(q->mutex);
    return ret;
}

int audio_decode_frame(VideoState* is, uint8_t* audio_buf, int buf_size)
{
    int       data_size = 0;
    int       ret       = -1;
    AVPacket* pkt       = &is->audio_pkt;

    for (;;) {
        while (is->audio_pkt_size > 0) {
            ret = avcodec_send_packet(is->audio_ctx, pkt);
            if (ret < 0) {
                printf("send audio packet error\n");
                exit(1);
            }
            while (ret >= 0) {
                ret = avcodec_receive_frame(is->audio_ctx, &is->audio_frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                    return -1;
                else if (ret < 0) {
                    fprintf(stderr, "Error during decoding\n");
                    exit(1);
                }

                data_size = 2 * is->audio_frame.nb_samples * 2;
                assert(data_size <= buf_size);
                swr_convert(is->audio_swr_ctx, &audio_buf, MAX_AUDIO_FRAME_SIZE * 3 / 2,
                    (const uint8_t**)is->audio_frame.data, is->audio_frame.nb_samples);
                fwrite(audio_buf, 1, data_size, audiofd);
                fflush(audiofd);
            }

            is->audio_pkt_data += ret;
        }
        //TODO:
    }
}

void audio_callback(void* userdata, uint8_t* stream, int len)
{
    VideoState* is = (VideoState*)userdata;
    int         len1, audio_size;

    SDL_memset(stream, 0, len);
    while (len > 0) {
        if (is->audio_buf_index >= is->audio_pkt_size) {
            audio_size = audio_decode_frame(is, is->audio_buf, sizeof(is->audio_buf));
            if (audio_size < 0) {
                is->audio_buf_size = 1024 * 2 * 2;
                memset(is->audio_buf, 0, is->audio_buf_size);
            } else {
                is->audio_buf_size = audio_size;
            }
            is->audio_buf_index = 0;
        }
        len1 = is->audio_buf_size - is->audio_buf_index;
        printf("stream addr:%p, audio_buf_index:%d, len1:%d, len:%d\n",
            stream, is->audio_buf_index, is->audio_buf_size, len1, len);

        if (len1 > len)
            len1 = len;
        SDL_MixAudio(stream, (uint8_t*)is->audio_buf + is->audio_buf_index, len1, SDL_MIX_MAXVOLUME);
        len -= len1;
        stream += len1;
        is->audio_buf_index += len1;
    }
}

static Uint32 sdl_refresh_timer_cb(Uint32 interval, void* opaque)
{
    SDL_Event event;
    event.type       = FF_REFRESH_EVENT;
    event.user.data1 = opaque;
    SDL_PushEvent(&event);
    return 0;
}

static void schedule_refresh(VideoState* is, int delay)
{
    SDL_AddTimer(delay, sdl_refresh_timer_cb, is);
}

void video_display(VideoState* is)
{
    SDL_Rect rect;
    float    aspect_ratio;
    int      w, h, x, y;
    int      i;
    //TODO:

    /* SDL_UpdateYUVTexture(texture, rect, is->videoStream, int Ypitch, const Uint8 *Uplane, int Upitch, const Uint8 *Vplane, int Vpitch ) */

    rect.x = 0;
    rect.y = 0;
    rect.w = is->video_ctx->width;
    rect.h = is->video_ctx->height;

    SDL_LockMutex(texture_mutex);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, &rect);
    SDL_RenderPresent(renderer);
    SDL_UnlockMutex(texture_mutex);
}

void video_refresh_timer(void* userdata)
{
    VideoState* is = (VideoState*)(userdata);
    if (is->video_st) {
        /* if(//) */
        //TODO:

        schedule_refresh(is, 40);
        video_display(is);
        //TODO:
    } else {
        schedule_refresh(is, 100);
    }
}

int queue_picture(VideoState* is, AVFrame* pFrame)
{
    //TODO;
}

int video_thread(void* arg)
{
    VideoState* is = (VideoState*)(arg);
    AVPacket    pkt1, *packet = &pkt1;
    int         ret;
    AVFrame*    pFrame = av_frame_alloc();
    for (;;) {
        if (packet_queue_get(&is->videoq, packet, 1) < 0) {
            break;
        }
        ret = avcodec_send_packet(is->video_ctx, packet);
        if (ret < 0) {
            fprintf(stderr, "Error sending a packet for decoding\n");
            exit(1);
        }

        while (ret >= 0) {
            ret = avcodec_receive_frame(is->video_ctx, pFrame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                return -1;
            else if (ret < 0) {
                fprintf(stderr, "Error during decoding\n");
                exit(1);
            }
            //TODO: queue frame
            av_packet_unref(packet);
        }
    }
    av_frame_free(&pFrame);
    return 0;
}

int stream_component_open(VideoState* is, int stream_index)
{
    int64_t          in_channel_layout, out_channel_layout;
    AVFormatContext* pFormatCtx = is->pFormatCtx;
    AVCodecContext*  codecCtx   = NULL;
    AVCodec*         codec      = NULL;
    SDL_AudioSpec    wanted_spec, spec;

    return 0;
}
int main()

{

    return 0;
}
