#pragma once

#include <string>

struct RuneDetectDemoParam
{
    //! 识别颜色的二值化阈值
    int color_thresh = 50;
    //! 识别颜色的类型(红色:0,蓝色:1)
    int color_type = 0;
    //! 视频路径
    std::string video_path = "";
    //! 是否可获取视频总帧数
    bool is_get_total_frames = false;
    //! 视频总帧数
    int total_frames = 0;
    //! 是否可获取视频帧率
    bool is_get_fps = false;
    //! 视频帧率
    double fps = 0.0;
    //! 是否启用神符角点网络数据诊断导出
    bool diagnostic_enabled = false;
    //! 诊断导出根目录
    std::string diagnostic_output_root = "rune_keypoint_nn_runs";
    //! 诊断运行名称，为空时自动生成
    std::string diagnostic_run_name = "";
    //! 诊断模式下是否保存识别结果图片
    bool diagnostic_save_images = true;
    //! 诊断图片定期采样帧间隔
    int diagnostic_image_stride = 200;
    //! 单次诊断运行最多保存的图片数量
    int diagnostic_image_max_count = 80;
};
extern RuneDetectDemoParam rune_detect_demo_param;
