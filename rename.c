#include <stdio.h>
#include <libavformat/avio.h>
#include <libavformat/avformat.h>

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    int ret;

    /* avpriv_io_move("1.txt", "2.txt"); */
    /* avpriv_io_delete("1.txt"); */
    ret = avpriv_io_delete("1.txt");
    if (ret < 0)
    {
    //    av_log(NULL, AV_LOG_ERROR, "Cannot delte 1.txt %s\n", av_err2str(ret));
        return ret;
    }
    return 0;
}
