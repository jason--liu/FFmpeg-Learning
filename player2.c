#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <signal.h>

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

typedef struct PacketQueUe {
    AVPacketList *first_pkt, *last_pkt;
    int           nb_packets;
    int           size;
    SDL_mutex*    mutex;
    SDL_cond*     cond;
} PacketQueue;

typedef struct VideoStruct {
    // for multi-media file
    char             filename[1024];
    AVFormatContext* pFormatCtx;
    int              videoStream, audioStream;

    // for audio
    AVStream*          audio_st;
    AVCodecContext*    audio_ctx;
    PacketQueue        audioq;
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
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    q->cond  = SDL_CreateCond();
}

int packet_queue_put(PacketQueue* q, AVPacket* pkt)
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

int packet_queue_get(PacketQueue* q, AVPacket* pkt, int block)
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

                is->audio_pkt_data += ret;
                is->audio_pkt_size += ret;
            }
            return data_size;
        }
        if(pkt->data)
            av_packet_unref(pkt);
        if(is->quit)
            return -1;
        if(packet_queue_get(&is->audioq, pkt, 1)>0)
            return -1;
        is->audio_pkt_data = pkt->data;
        is->audio_pkt_size = pkt->size;
    }
}

void audio_callback(void* userdata, uint8_t* stream, int len)
{
    VideoState* is = (VideoState*)userdata;
    int         len1, audio_size;

    SDL_memset(stream, 0, len);
    while (len > 0) {
        if (is->audio_buf_index >= is->audio_buf_size) {
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
        printf("stream addr:%p, audio_buf_index:%d, audio_buf_size:%d, len1:%d, len:%d\n",
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

int decode_video_thread(void* arg)
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
    int              ret;

    if (stream_index < 0 || stream_index >= pFormatCtx->nb_streams)
        return -1;

    codec    = avcodec_find_decoder(pFormatCtx->streams[stream_index]->codecpar->codec_id);
    codecCtx = avcodec_alloc_context3(codec);
    /* if(avcodec_copy_context(codecCtx, pFormatCtx->streams[stream_index]->codec)!=0) */

    if ((ret = avcodec_parameters_to_context(codecCtx, pFormatCtx->streams[stream_index]->codecpar)) < 0) {
        fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
            av_get_media_type_string(AVMEDIA_TYPE_VIDEO));
        return -1;
    }
    if (codecCtx->codec_type == AVMEDIA_TYPE_AUDIO) {
        wanted_spec.freq     = codecCtx->sample_rate;
        wanted_spec.format   = AUDIO_S16SYS;
        wanted_spec.channels = 2;
        wanted_spec.silence  = 0;
        wanted_spec.samples  = SDL_AUDIO_BUFFER_SIZE;
        wanted_spec.callback = audio_callback;
        wanted_spec.userdata = (void*)is;

        if (SDL_OpenAudio(&wanted_spec, &spec) < 0) {
            printf("SDL_OpenAudio error %s\n", SDL_GetError());
            return -1;
        }
    }
    if (avcodec_open2(codecCtx, codec, NULL) < 0) {
        printf("Unsupported codec\n");
        return -1;
    }

    switch (codecCtx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        is->audioStream     = stream_index;
        is->audio_st        = pFormatCtx->streams[stream_index];
        is->audio_ctx       = codecCtx;
        is->audio_buf_size  = 0;
        is->audio_buf_index = 0;
        memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
        packet_queue_init(&is->audioq);
        SDL_PauseAudio(0);

        //re sample
        uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO;
        int      out_nb_samples     = is->audio_ctx->frame_size;
        int      out_sample_rate    = is->audio_ctx->sample_rate;
        int      out_channels       = av_get_channel_layout_nb_channels(out_channel_layout);
        int64_t  in_channel_layout  = av_get_default_channel_layout(is->audio_ctx->channels);

        struct SwrContext* audio_convert_ctx = NULL;

        audio_convert_ctx = swr_alloc();
        if (!audio_convert_ctx) {
            printf("failed to swr_alloc\n");
            return -1;
        }

        swr_alloc_set_opts(audio_convert_ctx, out_channel_layout, AV_SAMPLE_FMT_S16, out_sample_rate,
            in_channel_layout, is->audio_ctx->sample_rate, is->audio_ctx->sample_rate, 0, NULL);
        fprintf(stderr, "swr opts: out_channel_layout:%lld, out_sample_fmt:%d, out_sample_rate:%d, in_channel_layout:%lld, in_sample_fmt:%d, in_sample_rate:%d\n",
            out_channel_layout,
            AV_SAMPLE_FMT_S16,
            out_sample_rate,
            in_channel_layout,
            is->audio_ctx->sample_fmt,
            is->audio_ctx->sample_rate);
        swr_init(audio_convert_ctx);
        is->audio_swr_ctx = audio_convert_ctx;
        break;
    case AVMEDIA_TYPE_VIDEO:
        is->videoStream = stream_index;
        is->video_st    = pFormatCtx->streams[stream_index];
        is->video_ctx   = codecCtx;
        packet_queue_init(&is->videoq);
        is->video_tid = SDL_CreateThread(decode_video_thread, "video_thread", is);
        is->sws_ctx   = sws_getContext(is->video_ctx->width, is->video_ctx->height, is->video_ctx->pix_fmt,
            is->video_ctx->width, is->video_ctx->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);
        break;
    default:
        break;
    }

    return 0;
}

int demux_thread(void* arg)
{
    Uint32           pixformat;
    VideoState*      is         = (VideoState*)arg;
    AVFormatContext* pFormatCtx = NULL;
    AVPacket         pkt1, *packet = &pkt1;
    int              i;
    int              video_index = -1;
    int              audio_index = -1;

    global_video_state = is;
    if (avformat_open_input(&pFormatCtx, is->filename, NULL, NULL) != 0) {
        return -1;
    }
    is->pFormatCtx = pFormatCtx;
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        return -1;
    }
    av_dump_format(pFormatCtx, 0, is->filename, 0);
    // Find the first video stream
    for (i = 0; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_index < 0) {
            video_index = i;
        }
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_index < 0) {
            audio_index = i;
        }
    }

    if (audio_index >= 0) {
        stream_component_open(is, audio_index);
    }
    if (video_index >= 0) {
        stream_component_open(is, video_index);
    }
    if (is->videoStream < 0 || is->audioStream < 0) {
        fprintf(stderr, "%s: could not open codecs\n", is->filename);
        goto fail;
    }

    fprintf(stderr, "video context: width=%d, height=%d\n", is->video_ctx->width, is->video_ctx->height);
    win = SDL_CreateWindow("Media Player",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        is->video_ctx->width, is->video_ctx->height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

    renderer = SDL_CreateRenderer(win, -1, 0);

    pixformat = SDL_PIXELFORMAT_IYUV;
    texture   = SDL_CreateTexture(renderer,
        pixformat,
        SDL_TEXTUREACCESS_STREAMING,
        is->video_ctx->width,
        is->video_ctx->height);
    //main decode loop
    for (;;) {
        if (is->quit) {
            SDL_CondSignal(is->videoq.cond);
            SDL_CondSignal(is->audioq.cond);
            break;
        }
        //TODO:seek buffer

        if (av_read_frame(is->pFormatCtx, packet) < 0) {
            if (is->pFormatCtx->pb->error == 0) {
                SDL_Delay(100);
                continue;
            } else {
                break;
            }
        }

        if (packet->stream_index == is->videoStream) {
            packet_queue_put(&is->videoq, packet);
        } else if (packet->stream_index == is->audioStream) {
            packet_queue_put(&is->audioq, packet);
        } else {
            av_packet_unref(packet);
        }
    }
    while (!is->quit) {
        SDL_Delay(100);
    }
fail:
    if (1) {
        SDL_Event event;
        event.type       = FF_QUIT_EVENT;
        event.user.data1 = is;
        SDL_PushEvent(&event);
    }
    return 0;
}

static void sigterm_handler(int sig)
{
    exit(123);
}

int main(int argc, char* argv[])
{
    int         ret = -1;
    SDL_Event   event;
    VideoState* is;

    is = (VideoState *)av_mallocz(sizeof(VideoState));
    if (argc < 2) {
        printf("Usage: %s <file>\n", argv[0]);
        return -1;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        printf("could not initialize SDL - %s\n", SDL_GetError());
        return -1;
    }
    signal(SIGINT , sigterm_handler); /* Interrupt (ANSI).    */
    signal(SIGTERM, sigterm_handler); /* Termination (ANSI).  */

    texture_mutex = SDL_CreateMutex();
    SDL_strlcpy(is->filename, argv[1], sizeof(is->filename));

    schedule_refresh(is, 40);
    is->parse_tid = SDL_CreateThread(demux_thread, "demux thread", is);
    if (!is->parse_tid) {
        av_free(is);
        goto __FAIL;
    }
    for (;;) {
        SDL_WaitEvent(&event);
        switch (event.type) {
        case FF_QUIT_EVENT:
        case SDL_QUIT:
            is->quit = 1;
            goto __QUIT;
            break;
        case FF_REFRESH_EVENT:
            video_refresh_timer(event.user.data1);
            break;
        default:
            break;
        }
    }
__QUIT:
    ret = 0;
__FAIL:
    SDL_Quit();

    return ret;
}
