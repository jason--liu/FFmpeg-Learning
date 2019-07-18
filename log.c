#include <libavutil/log.h>
#include <stdio.h>

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    av_log_set_level(AV_LOG_INFO);
    av_log(NULL, AV_LOG_INFO, "hello world\n");

    return 0;
}
