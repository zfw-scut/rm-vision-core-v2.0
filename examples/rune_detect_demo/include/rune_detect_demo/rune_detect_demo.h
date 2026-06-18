#pragma once

#include "rune_detect_demo/rune_detect_demo.h"
#include "vc/math/pose_node.hpp"
#include "vc/contour_proc/contour_wrapper.hpp"

#include "vc/feature/rune_target_inactive.h"
#include "vc/feature/rune_target_active.h"
#include "vc/feature/rune_fan_hump.h"
#include "vc/feature/rune_fan_active.h"
#include "vc/feature/rune_fan_inactive.h"
#include "vc/feature/rune_center.h"
#include "vc/feature/rune_group.h"
#include "vc/feature/rune_combo.h"
#include "vc/feature/rune_fan.h"

#include "vc/core/logging.h"
#include "vc/core/debug_tools/window_auto_layout.h"
#include "vc/core/debug_tools/param_view_manager.h"
#include "vc/core/yml_manager.hpp"
#include "vc/core/debug_tools.h"
#include "vc/core/command_line.h"

#include "vc/detector/rune_detector.h"


/**
 * @brief 核心处理函数
 * @param vid_cap 视频捕获对象
 * @return 成功读取并处理一帧返回 true，视频结束或读取失败返回 false
 */
bool process(cv::VideoCapture &vid_cap);

/**
 * @brief 初始化诊断导出目录和输出文件
 * @param cap 视频捕获对象
 */
void initDiagnosticRun(cv::VideoCapture &cap);

/**
 * @brief 完成诊断导出并写入汇总信息
 */
void finishDiagnosticRun();


/**
 * @brief 更新参数
 */
void updateParam(cv::VideoCapture &cap);

/**
 * @brief 命令行参数处理
 */
void parseCommandLine(int argc, char **argv);

/**
 * @brief 配置VideoCapture对象
 */
void setupVideoCapture(cv::VideoCapture &cap);
