#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

static void decode(AVCodecContext* dec_ctx, AVFrame* frame, AVPacket* pkt, const char* filename)
{
    char buf[1024];
    int  ret;

    ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0) {
        fprintf(stderr, "Error sending a packet for decoding\n");
        exit(1);
    }

    while (ret >= 0) {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            fprintf(stderr, "Error during decoding\n");
            exit(1);
        }
    }
}

int main(int argc, char* argv[])
{
    int ret = -1;
    int i, videoStream;

    AVFormatContext*   pFormatCtx = NULL;
    struct SwsContext* sws_ctx    = NULL;

    AVCodecContext* pCodecCtxOrg = NULL;
    AVCodecContext* pCodecCtx    = NULL;

    AVCodec*   pCodec = NULL;
    AVFrame*   pFrame = NULL;
    AVPicture* pict   = NULL;

    AVPacket packet;
    SDL_Rect rect;
    Uint32   pixformat;
    float    aspect_ratio;

    // for renderer
    SDL_Window*   win      = NULL;
    SDL_Renderer* renderer = NULL;
    SDL_Texture*  texture  = NULL;

    // window default size
    int w_width  = 640;
    int w_height = 480;

    if (argc < 2) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Usage: %s <file>\n", argv[0]);
        return ret;
    }

    FILE* yuv_file = fopen("yuv_file", "ab");
    if (!yuv_file)
        return 0;

    /* if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) { */
    /*     SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "sdl init failed\n"); */
    /*     return ret; */
    /* } */

    //av_register_all()

    // open target file
    if (avformat_open_input(&pFormatCtx, argv[1], NULL, NULL)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, " failed to open target input file\n");
        goto exit;
    }

    // retrieve stream information
    if (avformat_find_stream_info(pFormatCtx, NULL)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to find stream information\n");
        goto exit;
    }

    av_dump_format(pFormatCtx, 0, argv[1], 0);

    if ((ret = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &pCodec, 0))) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to find best stream information\n");
        goto exit;
    }

    videoStream = ret;
    /* pCodecCtxOrg = pFormatCtx->streams[videoStream]->codec; */ //deprecated
    pCodecCtx = avcodec_alloc_context3(pCodec);

    /* Copy codec parameters from input stream to output codec context */
    if ((ret = avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoStream]->codecpar)) < 0) {
        fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
            av_get_media_type_string(AVMEDIA_TYPE_VIDEO));
        goto exit;
    }

    // open codec
    if (avcodec_open2(pCodecCtx, pCodec, NULL)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to open avcodec");
        goto exit;
    }

    pFrame   = av_frame_alloc();
    w_width  = pCodecCtx->width;
    w_height = pCodecCtx->height;

    // create window
    printf("w=%d h=%d\n", w_width, w_height);
    /* win = SDL_CreateWindow("Player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, */
    /*     w_width, w_height, SDL_WINDOW_OPENGL | SDL_WINDOWEVENT_RESIZED); */
    /* if (!win) { */
    /*     SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to create sdl window\n"); */
    /*     goto exit; */
    /* } */

    // create renderer
    /* renderer = SDL_CreateRenderer(win, -1, 0); */
    /* if (!renderer) { */
    /*     SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to create sdl renderer\n"); */
    /*     goto exit; */
    /* } */

    pixformat = SDL_PIXELFORMAT_IYUV;
    /* texture   = SDL_CreateTexture(renderer, pixformat, SDL_TEXTUREACCESS_STREAMING, w_width, w_height); */

    /* sws_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width, */
    /*     pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL); */

    av_init_packet(&packet);
    packet.data = NULL;
    packet.size = 0;

    char* buf = (char*)malloc(w_height * w_width * 3 / 2);
    while (av_read_frame(pFormatCtx, &packet) >= 0) {
        if (packet.stream_index == videoStream) {
            ret = avcodec_send_packet(pCodecCtx, &packet);
            if (ret < 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(pCodecCtx, pFrame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                }
                /* sws_scale(sws_ctx, pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pict->data, pict->linesize); */

                memset(buf, 0, w_width * w_height * 3 / 2);
                int a = 0, i;
                for (i = 0; i < w_height; i++) {
                    memcpy(buf + a, pFrame->data[0] + i * pFrame->linesize[0], w_width);
                    a += w_width;
                }
                for (i = 0; i < w_height / 2; i++) {
                    memcpy(buf + a, pFrame->data[1] + i * pFrame->linesize[1], w_width / 2);
                    a += w_width / 2;
                }
                for (i = 0; i < w_height / 2; i++) {
                    memcpy(buf + a, pFrame->data[2] + i * pFrame->linesize[2], w_width / 2);
                    a += w_width / 2;
                }
                fwrite(buf, 1, w_height * w_width * 3 / 2, yuv_file);
                /*
                SDL_UpdateYUVTexture(texture, NULL,
                    pFrame->data[0], pFrame->linesize[0],
                    pFrame->data[1], pFrame->linesize[1],
                    pFrame->data[2], pFrame->linesize[2]);

                rect.x = 0;
                rect.y = 0;
                rect.w = pCodecCtx->width;
                rect.h = pCodecCtx->height;

                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, texture, NULL, &rect);
                SDL_RenderPresent(renderer);*/
            }
        }
        av_packet_unref(&packet);
    }
exit:
    if (pFormatCtx)
        avformat_close_input(&pFormatCtx);
    if (win)
        SDL_DestroyWindow(win);
    if (renderer)
        SDL_DestroyRenderer(renderer);
    if (texture)
        SDL_DestroyTexture(texture);
    av_frame_free(&pFrame);

    SDL_Quit();

    return 0;
}
