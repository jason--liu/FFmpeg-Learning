#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#define OUTPUT_YUV420P 1
int main(int argc, char *argv[]) {
    AVFormatContext *pFormatCtx;
    int i, videoindex;
    AVCodecContext *pCodecCtx;
    AVCodec *pCodec;

    av_register_all();
    avformat_network_init();
    pFormatCtx = avformat_alloc_context();

    // Open File
    // char filepath[]="src01_480x272_22.h265";
    // avformat_open_input(&pFormatCtx,filepath,NULL,NULL)

    // Register Device
    avdevice_register_all();

    // Linux
    AVInputFormat *ifmt = av_find_input_format("video4linux2");
    if (avformat_open_input(&pFormatCtx, "/dev/video0", ifmt, NULL) != 0) {
        printf("Couldn't open input stream./dev/video0\n");
        return -1;
    }

    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        printf("Couldn't find stream information.\n");
        return -1;
    }
    videoindex = -1;
    for (i = 0; i < pFormatCtx->nb_streams; i++)
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoindex = i;
            break;
        }
    if (videoindex == -1) {
        printf("Couldn't find a video stream.\n");
        return -1;
    }
    pCodecCtx = pFormatCtx->streams[videoindex]->codec;
    pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
    if (pCodec == NULL) {
        printf("Codec not found.\n");
        return -1;
    }
    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
        printf("Could not open codec.\n");
        return -1;
    }
    AVFrame *pFrame, *pFrameYUV;
    pFrame = av_frame_alloc();
    pFrameYUV = av_frame_alloc();
    unsigned char *out_buffer = (unsigned char *)av_malloc(avpicture_get_size(
        AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height));
    avpicture_fill((AVPicture *)pFrameYUV, out_buffer, AV_PIX_FMT_YUV420P,
                   pCodecCtx->width, pCodecCtx->height);
    //根据获取摄像头的宽高和指定的像素格式420，分配空间

    printf("camera width=%d height=%d \n", pCodecCtx->width, pCodecCtx->height);
    int screen_w = 0, screen_h = 0;

    int ret, got_picture;

    AVPacket *packet = (AVPacket *)av_malloc(sizeof(AVPacket));

#if OUTPUT_YUV420P
    FILE *fp_yuv = fopen("outputt.yuv", "wb+");
#endif

    struct SwsContext *img_convert_ctx;
    img_convert_ctx =
        sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
                       pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P,
                       SWS_BICUBIC, NULL, NULL, NULL);
    //配置图像格式转换以及缩放参数
    //int i = 0;
    for(i = 0;i<1000;i++){
    if (av_read_frame(pFormatCtx, packet) >= 0) {
        if (packet->stream_index == videoindex) {
            ret = avcodec_decode_video2(
                pCodecCtx, pFrame, &got_picture,
                packet);  //解码从摄像头获取的数据，pframe结构
            if (ret < 0) {
                printf("Decode Error.\n");
                return -1;
            }
            if (got_picture) {
                sws_scale(
                    img_convert_ctx, (const unsigned char *const *)pFrame->data,
                    pFrame->linesize, 0, pCodecCtx->height, pFrameYUV->data,
                    pFrameYUV
                        ->linesize);  //根据前面配置的缩放参数，进行图像格式转换以及缩放等操作
#if OUTPUT_YUV420P
                int y_size = pCodecCtx->width * pCodecCtx->height;
                fwrite(pFrameYUV->data[0], 1, y_size, fp_yuv);      // Y
                fwrite(pFrameYUV->data[1], 1, y_size / 4, fp_yuv);  // U
                fwrite(pFrameYUV->data[2], 1, y_size / 4, fp_yuv);  // V
#endif
            }
        }
        av_free_packet(packet);
    }
    }
    sws_freeContext(img_convert_ctx);
#if OUTPUT_YUV420P
    fclose(fp_yuv);
#endif

    // av_free(out_buffer);
    av_free(pFrameYUV);
    avcodec_close(pCodecCtx);
    avformat_close_input(&pFormatCtx);

    return 0;
}