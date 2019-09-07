#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>
/*
    测试多线程解码性能
 */
//当前时间戳 clock
static long long GetNowMs()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int       sec = tv.tv_sec % 360000;
    long long t   = sec * 1000 + tv.tv_usec / 1000;
    return t;
}

int main(int argc, char* argv[])
{
    AVFormatContext* pFormatCtx = NULL;
    AVCodec*         codec      = NULL;
    printf("The process ID is %d \n", (int)getpid()); //本进程

    if (argc < 2) {
        printf("Usage: %d <file>\n", argv[0]);
    }

    int re = avformat_open_input(&pFormatCtx, argv[1], 0, 0);
    if (re != 0) {
        printf("avformat_open_input failed!:%s", av_err2str(re));
    }
    re = avformat_find_stream_info(pFormatCtx, 0);
    if (re != 0) {
        printf("avformat_find_stream_info failed!");
    }

    av_dump_format(pFormatCtx, 0, argv[1], 0);

    int videoStream = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);

    AVCodecContext* vctx = avcodec_alloc_context3(codec);

    re = avcodec_parameters_to_context(vctx, pFormatCtx->streams[videoStream]->codecpar);
    if (re) {
        printf("avcodec_parameters_to_context error\n");
        return -1;
    }
    /* 设置解码线程 */
    vctx->thread_count = 4;

    re = avcodec_open2(vctx, codec, 0);
    if (re) {
        printf("avcodec_open2 error\n");
        return -1;
    }
    //vc->time_base = ic->streams[videoStream]->time_base;
    printf("vctx timebase = %d/ %d\n", vctx->time_base.num, vctx->time_base.den); //num 怎么是0呢？

    AVPacket* pkt   = av_packet_alloc();
    AVFrame*  frame = av_frame_alloc();

    long long start      = GetNowMs();
    int       frameCount = 0;

    for (;;) {
        /* 每隔3S算一次fps平均值 */
        if (GetNowMs() - start >= 3000) {
            printf("now decode fps is %d, thread(%d)\n", frameCount / 3, vctx->thread_count);
            start      = GetNowMs();
            frameCount = 0;
        }

        int re = av_read_frame(pFormatCtx, pkt);

        if (re != 0) {

            printf("读取到结尾处!\n");
            //int pos = 20 * r2d(ic->streams[videoStream]->time_base);
            //向前跳且是关键帧，避免花屏
            // usleep(1000 * 1000);
            av_seek_frame(pFormatCtx, videoStream, 0, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_FRAME);
            continue;
            //break;
        }
        if (pkt->stream_index == videoStream) {
            re = avcodec_send_packet(vctx, pkt);
            if (re < 0) {
                fprintf(stderr, "Error sending a packet for decoding\n");
                exit(1);
            }

            for (;;) {
                re = avcodec_receive_frame(vctx, frame);
                if (re == AVERROR(EAGAIN) || re == AVERROR_EOF) {
                    //printf("avcodec receive frame error\n");
                    /* 如果返回上面两个错误，说明这一帧解码完成 */
                    break;
                } else if (re < 0) {
                    fprintf(stderr, "Error during decoding\n");
                    exit(1);
                }

                frameCount++;
            }
        }
        //av_packet_unref(pkt);
    }
    avformat_close_input(&pFormatCtx);
    av_packet_free(&pkt);
    av_frame_free(&frame);
    return 0;
}