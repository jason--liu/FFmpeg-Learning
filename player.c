#include <stdio.h>
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

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "sdl init failed\n");
        return ret;
    }

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
    /* pCodecCtxOrg = pFormatCtx->streams[videoStream]->codec; */
    pCodecCtx = avcodec_alloc_context3(pCodec);
    /* if(avcodec_copy_context()) */

    // open codec
    if (avcodec_open2(pCodecCtx, pCodec, NULL)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to open avcodec");
        goto exit;
    }

    pFrame   = av_frame_alloc();
    w_width  = pCodecCtx->width;
    w_height = pCodecCtx->height;

    // create window
    win = SDL_CreateWindow("Player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w_width, w_height, SDL_WINDOW_OPENGL | SDL_WINDOWEVENT_RESIZED);
    if (!win) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to create sdl window\n");
        goto exit;
    }

    // create renderer
    renderer = SDL_CreateRenderer(win, -1, 0);
    if (!renderer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "failed to create sdl renderer\n");
        goto exit;
    }

    pixformat = SDL_PIXELFORMAT_IYUV;
    texture   = SDL_CreateTexture(renderer, pixformat, SDL_TEXTUREACCESS_STREAMING, w_width, w_height);

    sws_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);

    pict = (AVPicture*)malloc(sizeof(AVPicture));
    avpicture_alloc(pict, AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height);

    while (av_read_frame(pFormatCtx, &packet) >= 0) {
        if (packet.stream_index == videoStream) {
            decode(pCodecCtx, pFrame, &packet, NULL);
            sws_scale(sws_ctx, pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pict->data, pict->linesize);

            SDL_UpdateYUVTexture(texture, NULL, pict->data[0], pict->linesize[0], pict->data[1], pict->linesize[1], pict->data[2], pict->linesize[2]);

            rect.x = 0;
            rect.y = 0;
            rect.w = pCodecCtx->width;
            rect.h = pCodecCtx->height;

            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, &rect);
            SDL_RenderPresent(renderer);
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

    SDL_Quit();

    return 0;
}
