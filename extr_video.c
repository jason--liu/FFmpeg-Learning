#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libavutil/error.h>
#include <libavcodec/avcodec.h>
#include <errno.h>

static int alloc_and_copy(AVPacket* pOutPkt, uint8_t* spspps, uint32_t spsppsSize, uint8_t* pIn, uint32_t inSize)
{
    int err;
    int startCodeLen = 3;

    err = av_grow_packet(pOutPkt, spsppsSize + inSize + startCodeLen);
    if (err < 0)
        return err;
    if (spspps)
    {
        memcpy(pOutPkt->data, spspps, spsppsSize); // we already set startCode
    }

    (pOutPkt->data + spsppsSize)[0] = 0;
    (pOutPkt->data + spsppsSize)[1] = 0;
    (pOutPkt->data + spsppsSize)[2] = 1;
    memcpy(pOutPkt->data + spsppsSize + startCodeLen, pIn, inSize);
    return 0;
}

static int h264_extradata_to_annexb(
    const unsigned char* pCodecExtraData, const int CodecextraDataSize, AVPacket* pOutExtradata, int padding)
{
    char*          pExtraData = NULL;
    int            len        = 0;
    int            spsUnitNum, ppsUnitNum;
    int            unitSize, totolSize = 0;
    unsigned char  startCode[] = {0, 0, 0, 1};
    unsigned char* pOut        = NULL;
    int            err;

    pExtraData = pCodecExtraData + 4; // get rid of version,profile,etc
    len        = (*pExtraData++ & 0x03) + 1;

    /* get sps */
    spsUnitNum = (*pExtraData++ & 0x1);
    while (spsUnitNum--)
    {
        unitSize = (pExtraData[0] << 8 | pExtraData[1]); // sps size,two bytes
        pExtraData += 2;
        totolSize += unitSize + sizeof(startCode);
        av_log(NULL, AV_LOG_INFO, "unitSize:%d\n", unitSize);

        if (totolSize > INT_MAX - padding)
        {
            av_log(NULL, AV_LOG_ERROR, "Too big extradatasize\n");
            /* av_free(pOut); */
            return AVERROR(EINVAL);
        }

        if (pExtraData + unitSize > pCodecExtraData + CodecextraDataSize)
        {
            av_log(NULL, AV_LOG_ERROR, "Packet header is not contained in global extradata\n");
            /* av_free(pOut); */
            return AVERROR(EINVAL);
        }

        if ((err = av_reallocp(&pOut, totolSize + padding)) < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "av_reallocp error\n");
            return err;
        }

        // copy startcode
        memcpy(pOut + totolSize - unitSize - sizeof(startCode), startCode, sizeof(startCode));
        // copy sps data
        memcpy(pOut + totolSize - unitSize, pExtraData, unitSize);

        pExtraData += unitSize;
    }
    /* get pps */
    ppsUnitNum = (*pExtraData++ & 0x1f);
    while (ppsUnitNum--)
    {
        unitSize = (pExtraData[0] << 8 | pExtraData[1]); // sps size,two bytes
        pExtraData += 2;
        totolSize += unitSize + sizeof(startCode);
        av_log(NULL, AV_LOG_INFO, "unitSize2:%d\n", unitSize);

        if (totolSize > INT_MAX - padding)
        {
            av_log(NULL, AV_LOG_ERROR, "Too big extradatasize\n");
            /* av_free(pOut); */
            return AVERROR(EINVAL);
        }

        if (pExtraData + unitSize > pCodecExtraData + CodecextraDataSize)
        {
            av_log(NULL, AV_LOG_ERROR, "Packet header is not contained in global extradata\n");
            /* av_free(pOut); */
            return AVERROR(EINVAL);
        }

        if ((err = av_reallocp(&pOut, totolSize + padding)) < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "av_reallocp error\n");
            return err;
        }

        // copy startcode
        memcpy(pOut + totolSize - unitSize - sizeof(startCode), startCode, sizeof(startCode));
        // copy pps data
        memcpy(pOut + totolSize - unitSize, pExtraData, unitSize);
        pExtraData += unitSize;
    }
    pOutExtradata->data = pOut;
    pOutExtradata->size = totolSize;
    return len;
}
static int h264Mp4ToAnnexb(AVFormatContext* pAVFormatContext, AVPacket* PAvPkt, FILE* pFd)
{
    unsigned char* pData    = PAvPkt->data;
    unsigned char* pEnd     = NULL;
    int            dataSize = PAvPkt->size;

    int           curSize  = 0;
    int           nalusize = 0;
    int           i;
    unsigned char nalHeader, nalType;
    AVPacket      spsppsPkt;
    AVPacket*     pOutPkt;
    int           len;
    int           ret;

    pOutPkt        = av_packet_alloc();
    pOutPkt->data  = NULL;
    pOutPkt->size  = 0;
    spsppsPkt.data = NULL;
    spsppsPkt.size = 0;
    pEnd           = pData + dataSize;

    while (curSize < dataSize)
    {
        if (pEnd - pData < 4)
            goto fail;

        for (i = 0; i < 4; i++)
        {
            nalusize <<= 8;
            nalusize |= pData[i];
        }

        pData += 4;

        if (nalusize > (pEnd - pData) + 1 || nalusize < 0)
            goto fail;

        nalHeader = *pData;
        nalType   = nalHeader & 0x1F;
        if (nalType == 5)
        {
            // get sps pps
            h264_extradata_to_annexb(pAVFormatContext->streams[PAvPkt->stream_index]->codecpar->extradata,
                pAVFormatContext->streams[PAvPkt->stream_index]->codecpar->extradata_size, &spsppsPkt,
                AV_INPUT_BUFFER_PADDING_SIZE);

            ret = alloc_and_copy(pOutPkt, spsppsPkt.data, spsppsPkt.size, pData, nalusize);
            if (ret < 0)
                goto fail;
        } else
        {
            /* add start code */
            ret = alloc_and_copy(pOutPkt, NULL, 0, pData, nalusize);
            if (ret < 0)
                goto fail;
        }

        len = fwrite(pOutPkt->data, 1, pOutPkt->size, pFd);
        if (len != pOutPkt->size)
            av_log(NULL, AV_LOG_WARNING, "fwrite warning(%d %d)\n", len, pOutPkt->size);

        fflush(pFd);
        curSize += nalusize + 4; // add start code;
        pData += nalusize;
    }
fail:
    av_packet_free(&pOutPkt);
    if (spsppsPkt.data)
    {
        free(spsppsPkt.data);
        spsppsPkt.data = NULL;
    }
    return 0;
}

int main(int argc, char* argv[])
{
    AVFormatContext* fmt_ctx = NULL;
    int              ret;
    AVPacket         pkt;

    int video_stream_index = -1;

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

    video_stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_stream_index < 0)
    {
        av_log(NULL, AV_LOG_WARNING, "could not find best stream\n");
        goto error;
    }

    pkt.data = NULL;
    pkt.size = 0;

    while (av_read_frame(fmt_ctx, &pkt) >= 0)
    {
        if (pkt.stream_index == video_stream_index)
        {
            h264Mp4ToAnnexb(fmt_ctx, &pkt, dst_fd);
        }
        av_packet_unref(&pkt);
    }

error:
    av_log(NULL, AV_LOG_INFO, "start free\n");
    avformat_close_input(&fmt_ctx);
    fclose(dst_fd);

    return 0;
}
