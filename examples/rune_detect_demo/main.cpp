#include "include/rune_detect_demo/rune_detect_demo.h"
#include "include/rune_detect_demo/rune_detect_demo_param.h"

using namespace std;
using namespace cv;

int main(int argc, char **argv)
{
    parseCommandLine(argc, argv);
    VC_PASS_INFO("Video Path: %s", rune_detect_demo_param.video_path.c_str());
    VideoCapture cap(rune_detect_demo_param.video_path);
    if (!cap.isOpened())
        VC_THROW_ERROR("无法打开视频文件: %s", rune_detect_demo_param.video_path.c_str());
    setupVideoCapture(cap);
    initDiagnosticRun(cap);
    while (true)
    {
        bool has_frame = process(cap);
        if (!has_frame && rune_detect_demo_param.diagnostic_enabled)
            break;
        if (!has_frame)
            continue;
        if (rune_detect_demo_param.diagnostic_enabled)
            continue;
        DebugTools::get()->show();
        waitKey(1);
    }
    finishDiagnosticRun();
}
