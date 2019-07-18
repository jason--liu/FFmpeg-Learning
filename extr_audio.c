#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libavutil/error.h>
#include <libavcodec/avcodec.h>
#include <errno.h>

#define ADTS_HEADER_LEN 7
#define ERROR_STR_SIZE 1024

void adts_header(char* szAdtsHeader, int dataLen)
{

    int audio_object_type        = 2;
    int sampling_frequency_index = 4;
    int channel_config           = 2;

    int adtsLen = dataLen + 7;

    szAdtsHeader[0] = 0xff;      // syncword:0xfff                          高8bits
    szAdtsHeader[1] = 0xf0;      // syncword:0xfff                          低4bits
    szAdtsHeader[1] |= (0 << 3); // MPEG Version:0 for MPEG-4,1 for MPEG-2  1bit
    szAdtsHeader[1] |= (0 << 1); // Layer:0                                 2bits
    szAdtsHeader[1] |= 1;        // protection absent:1                     1bit

    szAdtsHeader[2] = (audio_object_type - 1) << 6; // profile:audio_object_type - 1                      2bits
    szAdtsHeader[2] |= (sampling_frequency_index & 0x0f)
        << 2;                                        // sampling frequency index:sampling_frequency_index  4bits
    szAdtsHeader[2] |= (0 << 1);                     // private bit:0                                      1bit
    szAdtsHeader[2] |= (channel_config & 0x04) >> 2; // channel configuration:channel_config               高1bit

    szAdtsHeader[3] = (channel_config & 0x03) << 6; // channel configuration:channel_config      低2bits
    szAdtsHeader[3] |= (0 << 5);                    // original：0                               1bit
    szAdtsHeader[3] |= (0 << 4);                    // home：0                                   1bit
    szAdtsHeader[3] |= (0 << 3);                    // copyright id bit：0                       1bit
    szAdtsHeader[3] |= (0 << 2);                    // copyright id start：0                     1bit
    szAdtsHeader[3] |= ((adtsLen & 0x1800) >> 11);  // frame length：value   高2bits

    szAdtsHeader[4] = (uint8_t)((adtsLen & 0x7f8) >> 3); // frame length:value    中间8bits
    szAdtsHeader[5] = (uint8_t)((adtsLen & 0x7) << 5);   // frame length:value    低3bits
    szAdtsHeader[5] |= 0x1f;                             // buffer fullness:0x7ff 高5bits
    szAdtsHeader[6] = 0xfc;
}
int main(int argc, char* argv[])
{
    AVFormatContext* fmt_ctx = NULL;
    int              ret;
    AVPacket         pkt;
    AVFrame*         frame;

    int audio_stream_index = -1;

    AVFormatContext* ofmt_ctx   = NULL;
    AVOutputFormat*  output_fmt = NULL;

    AVStream* in_stream  = NULL;
    AVStream* out_stream = NULL;

    char errors[1024] = {0};
    if (argc < 3)
    {
        fprintf(stderr, "too few arguments\n");
        return -1;
    }

    if (!argv[1] || !argv[2])
    {
        fprintf(stderr, "argument cannot be null\n");
        return -1;
    }
    char* src_file = argv[1];
    char* dst_file = argv[2];
    FILE* dst_fd   = NULL;

    dst_fd = fopen(dst_file, "w+");
    if (!dst_fd)
    {
        av_log(NULL, AV_LOG_ERROR, "open dst file error\n");
        return -1;
    }

    av_log_set_level(AV_LOG_DEBUG);

    frame = av_frame_alloc();
    if (!frame)
    {
        av_log(NULL, AV_LOG_ERROR, "av_frame_alloc error\n");
        return -1;
    }

    // open input file and get format context
    if ((ret = avformat_open_input(&fmt_ctx, src_file, NULL, NULL)) < 0)
    {
        /* av_strerror() */
        fprintf(stderr, "open input file error %s %d\n", src_file, errno);
        return -1;
    }

    if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0)
    {
        fprintf(stderr, "open input file error %s %d\n", src_file, errno);
        return -1;
    }

    av_dump_format(fmt_ctx, 0, src_file, 0);
#if 0
    in_stream                      = fmt_ctx->streams[1];
    AVCodecParameters* in_codecpar = in_stream->codecpar;
    if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
    {
        av_log(NULL, AV_LOG_ERROR, "The Codec type is invalid!\n");
        exit(1);
    }

    // out file
    ofmt_ctx   = avformat_alloc_context();
    output_fmt = av_guess_format(NULL, dst_file, NULL);
    if (!output_fmt)
    {
        av_log(NULL, AV_LOG_DEBUG, "Cloud not guess file format \n");
        exit(1);
    }

    ofmt_ctx->oformat = output_fmt;

    out_stream = avformat_new_stream(ofmt_ctx, NULL);
    if (!out_stream)
    {
        av_log(NULL, AV_LOG_DEBUG, "Failed to create out stream!\n");
        exit(1);
    }

    if (fmt_ctx->nb_streams < 2)
    {
        av_log(NULL, AV_LOG_ERROR, "the number of stream is too less!\n");
        exit(1);
    }
    if ((ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar)) < 0)
    {
        av_strerror(ret, errors, ERROR_STR_SIZE);
        av_log(NULL, AV_LOG_ERROR, "Failed to copy codec parameter, %d(%s)\n", ret, errors);
    }

    out_stream->codecpar->codec_tag = 0;

    if ((ret = avio_open(&ofmt_ctx->pb, dst_file, AVIO_FLAG_WRITE)) < 0)
    {
        /* av_strerror(err_code, errors, 1024); */
        av_log(NULL, AV_LOG_DEBUG, "Could not open file %s, %d(%s)\n", dst_file, ret, errors);
        exit(1);
    }

    av_dump_format(ofmt_ctx, 0, dst_file, 1);

#endif
    audio_stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (audio_stream_index < 0)
    {
        av_log(NULL, AV_LOG_WARNING, "could not find best stream\n");
        goto error;
    }

    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;
    int len  = 0;
    /*
        if (avformat_write_header(ofmt_ctx, NULL) < 0) {
            av_log(NULL, AV_LOG_DEBUG, "Error occurred when opening output file");
            exit(1);
            }*/

    while (av_read_frame(fmt_ctx, &pkt) >= 0)
    {
        if (pkt.stream_index == audio_stream_index)
        {
            char adts_header_buf[7];

            adts_header(adts_header_buf, pkt.size);
            /* addADTStoPacket(adts_header_buf, pkt.size); */
            fwrite(adts_header_buf, 1, 7, dst_fd);

            len = fwrite(pkt.data, 1, pkt.size, dst_fd);
            if (len != pkt.size)
            {
                av_log(NULL, AV_LOG_WARNING, "write size is not equal pkt size\n");
            }
            // release a packet after write it
            /* pkt.pts = av_rescale_q_rnd( */
            /*     pkt.pts, in_stream->time_base, out_stream->time_base, (AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX)); */
            /* pkt.dts          = pkt.pts; */
            /* pkt.duration     = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base); */
            /* pkt.pos          = -1; */
            /* pkt.stream_index = 0; */
            /* av_interleaved_write_frame(ofmt_ctx, &pkt); */
            av_packet_unref(&pkt);
            /* av_packet_unref(&pkt); */
        }
    }
    /* av_write_trailer(ofmt_ctx); */

error:
    av_log(NULL, AV_LOG_INFO, "start free\n");
    av_frame_free(&frame);
    avformat_close_input(&fmt_ctx);
    fclose(dst_fd);

    return 0;
}
