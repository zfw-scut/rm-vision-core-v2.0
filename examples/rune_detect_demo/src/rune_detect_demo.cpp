#include "../include/rune_detect_demo/rune_detect_demo.h"
#include "../include/rune_detect_demo/rune_detect_demo_param.h"
#include "vc/core/debug_tools/window_auto_layout.h"
#include "vc/detector/rune_detector_param.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

using namespace std;
using namespace cv;

namespace
{
/**
 * @brief 将字符串转义为 JSON 字符串内容
 * @param[in] value 原始字符串
 * @return 转义后的字符串
 */
std::string jsonEscape(const std::string &value)
{
    std::ostringstream oss;
    for (char ch : value)
    {
        switch (ch)
        {
        case '\\':
            oss << "\\\\";
            break;
        case '"':
            oss << "\\\"";
            break;
        case '\n':
            oss << "\\n";
            break;
        case '\r':
            oss << "\\r";
            break;
        case '\t':
            oss << "\\t";
            break;
        default:
            oss << ch;
            break;
        }
    }
    return oss.str();
}

/**
 * @brief 将字符串转义为 CSV 字段
 * @param[in] value 原始字符串
 * @return 可直接写入 CSV 的字段
 */
std::string csvEscape(const std::string &value)
{
    bool need_quote = value.find_first_of(",\"\n\r") != std::string::npos;
    if (!need_quote)
        return value;

    std::ostringstream oss;
    oss << '"';
    for (char ch : value)
    {
        if (ch == '"')
            oss << "\"\"";
        else
            oss << ch;
    }
    oss << '"';
    return oss.str();
}

/**
 * @brief 将路径名片段转换为适合作为目录名的字符串
 * @param[in] token 原始名称
 * @return 安全目录名
 */
std::string sanitizePathToken(const std::string &token)
{
    std::string result;
    result.reserve(token.size());
    for (unsigned char ch : token)
    {
        bool valid = std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.';
        result.push_back(valid ? static_cast<char>(ch) : '_');
    }
    return result.empty() ? "video" : result;
}

/**
 * @brief 生成当前时间戳字符串
 * @return 形如 YYYYMMDD_HHMMSS 的时间戳
 */
std::string makeTimestamp()
{
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
    localtime_r(&now_time, &local_time);

    std::ostringstream oss;
    oss << std::put_time(&local_time, "%Y%m%d_%H%M%S");
    return oss.str();
}

/**
 * @brief 写入二维浮点点
 * @param[in,out] os 输出流
 * @param[in] point 点坐标
 */
void writePoint2f(std::ostream &os, const cv::Point2f &point)
{
    os << '[' << point.x << ',' << point.y << ']';
}

/**
 * @brief 写入二维整数点
 * @param[in,out] os 输出流
 * @param[in] point 点坐标
 */
void writePoint(std::ostream &os, const cv::Point &point)
{
    os << '[' << point.x << ',' << point.y << ']';
}

/**
 * @brief 写入二维浮点点集
 * @param[in,out] os 输出流
 * @param[in] points 点集
 */
void writePoint2fArray(std::ostream &os, const std::vector<cv::Point2f> &points)
{
    os << '[';
    for (size_t i = 0; i < points.size(); ++i)
    {
        if (i > 0)
            os << ',';
        writePoint2f(os, points[i]);
    }
    os << ']';
}

/**
 * @brief 写入轮廓点集
 * @param[in,out] os 输出流
 * @param[in] contours 多轮廓点集
 */
void writeContours(std::ostream &os, const std::vector<std::vector<cv::Point>> &contours)
{
    os << '[';
    for (size_t i = 0; i < contours.size(); ++i)
    {
        if (i > 0)
            os << ',';
        os << '[';
        for (size_t j = 0; j < contours[i].size(); ++j)
        {
            if (j > 0)
                os << ',';
            writePoint(os, contours[i][j]);
        }
        os << ']';
    }
    os << ']';
}

/**
 * @brief 神符角点网络数据诊断导出器
 *
 * @note 该类只在 demo 离线诊断模式中使用，负责把每帧识别事实写入项目内 run 目录。
 */
class RuneKeypointDiagnosticWriter
{
public:
    /**
     * @brief 构造诊断导出器
     * @param[in] cap 视频捕获对象
     */
    explicit RuneKeypointDiagnosticWriter(cv::VideoCapture &cap)
    {
        namespace fs = std::filesystem;

        // 生成 run 目录名，默认包含时间戳和视频文件名。
        fs::path video_path(rune_detect_demo_param.video_path);
        std::string video_stem = sanitizePathToken(video_path.stem().string());
        std::string run_name = rune_detect_demo_param.diagnostic_run_name.empty()
                                   ? makeTimestamp() + "_" + video_stem
                                   : sanitizePathToken(rune_detect_demo_param.diagnostic_run_name);

        run_dir_ = fs::path(rune_detect_demo_param.diagnostic_output_root) / run_name;
        fs::create_directories(run_dir_);

        frames_path_ = run_dir_ / "frames.jsonl";
        summary_path_ = run_dir_ / "summary.csv";
        config_path_ = run_dir_ / "run_config.yaml";
        images_dir_ = run_dir_ / "images";
        image_index_path_ = run_dir_ / "image_index.csv";
        frames_.open(frames_path_);
        if (!frames_.is_open())
            VC_THROW_ERROR("无法创建诊断逐帧文件: %s", frames_path_.string().c_str());
        if (rune_detect_demo_param.diagnostic_save_images)
        {
            fs::create_directories(images_dir_);
            image_index_.open(image_index_path_);
            if (!image_index_.is_open())
                VC_THROW_ERROR("无法创建诊断图片索引文件: %s", image_index_path_.string().c_str());
            image_index_ << "image,frame,status,reason,active_fans,failure_stage,total_ms\n";
        }

        writeConfig(cap);
        VC_PASS_INFO("诊断导出目录: %s", run_dir_.string().c_str());
    }

    /**
     * @brief 写入单帧诊断信息
     * @param[in] frame_index 帧号
     * @param[in] frame_time_ms 视频时间戳
     * @param[in] diagnostics detector 诊断快照
     * @param[in] groups 当前输出特征组
     * @param[in] image 当前绘制后的识别图片
     */
    void writeFrame(int frame_index,
                    double frame_time_ms,
                    const RuneDetectorFrameDiagnostics &diagnostics,
                    const std::vector<FeatureNode_ptr> &groups,
                    const cv::Mat &image)
    {
        frames_processed_++;
        total_ms_sum_ += diagnostics.stage_times.total_ms;
        if (diagnostics.status == "ok")
            ok_frames_++;
        else if (diagnostics.status == "vanish_ok")
            vanish_ok_frames_++;
        else
            failed_frames_++;
        if (!diagnostics.active_fans.empty())
            active_fan_frames_++;

        // 逐帧 JSONL 保持单行，方便后续脚本按行流式读取。
        frames_ << '{';
        writeFrameHeader(frame_index, frame_time_ms);
        writeDetectorState(diagnostics, groups);
        writeStageTimes(diagnostics.stage_times);
        writeCandidateCounts(diagnostics.candidate_counts);
        writeActiveFanSamples(diagnostics.active_fans);
        frames_ << "}\n";

        saveImageIfNeeded(frame_index, diagnostics, image);
    }

    /**
     * @brief 写入诊断汇总文件
     */
    void finish()
    {
        std::ofstream summary(summary_path_);
        if (!summary.is_open())
        {
            VC_WARNING_INFO("无法创建诊断汇总文件: %s", summary_path_.string().c_str());
            return;
        }

        // summary.csv 只保存高层统计，详细内容全部在 frames.jsonl。
        summary << "metric,value\n";
        summary << "run_dir," << run_dir_.string() << "\n";
        summary << "video_path," << rune_detect_demo_param.video_path << "\n";
        summary << "frames_processed," << frames_processed_ << "\n";
        summary << "ok_frames," << ok_frames_ << "\n";
        summary << "vanish_ok_frames," << vanish_ok_frames_ << "\n";
        summary << "failed_frames," << failed_frames_ << "\n";
        summary << "active_fan_frames," << active_fan_frames_ << "\n";
        summary << "saved_images," << saved_images_ << "\n";
        summary << "mean_detector_ms," << (frames_processed_ == 0 ? 0.0 : total_ms_sum_ / static_cast<double>(frames_processed_)) << "\n";
        VC_PASS_INFO("诊断汇总文件: %s", summary_path_.string().c_str());
    }

private:
    /**
     * @brief 写入运行配置文件
     * @param[in] cap 视频捕获对象
     */
    void writeConfig(cv::VideoCapture &cap)
    {
        std::ofstream config(config_path_);
        if (!config.is_open())
            VC_THROW_ERROR("无法创建诊断配置文件: %s", config_path_.string().c_str());

        // 配置文件用于复现本次弱标注运行。
        config << "video_path: \"" << rune_detect_demo_param.video_path << "\"\n";
        config << "fps: " << cap.get(cv::CAP_PROP_FPS) << "\n";
        config << "total_frames: " << static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT)) << "\n";
        config << "color_type: " << rune_detect_demo_param.color_type << "\n";
        config << "color_thresh: " << rune_detect_demo_param.color_thresh << "\n";
        config << "diagnostic_output_root: \"" << rune_detect_demo_param.diagnostic_output_root << "\"\n";
        config << "diagnostic_run_name: \"" << run_dir_.filename().string() << "\"\n";
        config << "diagnostic_save_images: " << (rune_detect_demo_param.diagnostic_save_images ? 1 : 0) << "\n";
        config << "diagnostic_image_stride: " << rune_detect_demo_param.diagnostic_image_stride << "\n";
        config << "diagnostic_image_max_count: " << rune_detect_demo_param.diagnostic_image_max_count << "\n";
    }

    /**
     * @brief 写入逐帧基础字段
     * @param[in] frame_index 帧号
     * @param[in] frame_time_ms 视频时间戳
     */
    void writeFrameHeader(int frame_index, double frame_time_ms)
    {
        frames_ << "\"video\":\"" << jsonEscape(std::filesystem::path(rune_detect_demo_param.video_path).filename().string()) << "\",";
        frames_ << "\"frame\":" << frame_index << ',';
        frames_ << "\"frame_time_ms\":" << frame_time_ms << ',';
        frames_ << "\"color_type\":" << rune_detect_demo_param.color_type << ',';
        frames_ << "\"color_thresh\":" << rune_detect_demo_param.color_thresh << ',';
    }

    /**
     * @brief 写入 detector 输出状态
     * @param[in] diagnostics detector 诊断快照
     * @param[in] groups 当前输出特征组
     */
    void writeDetectorState(const RuneDetectorFrameDiagnostics &diagnostics, const std::vector<FeatureNode_ptr> &groups)
    {
        int tracker_count = 0;
        if (!groups.empty())
        {
            auto group = RuneGroup::cast(groups.front());
            if (group)
                tracker_count = static_cast<int>(group->getTrackers().size());
        }

        frames_ << "\"status\":\"" << jsonEscape(diagnostics.status) << "\",";
        frames_ << "\"output_valid\":" << (diagnostics.output_valid ? "true" : "false") << ',';
        frames_ << "\"used_vanish_update\":" << (diagnostics.used_vanish_update ? "true" : "false") << ',';
        frames_ << "\"failure_stage\":\"" << jsonEscape(diagnostics.failure_stage) << "\",";
        frames_ << "\"find_features_failure\":\"" << jsonEscape(diagnostics.find_features_failure) << "\",";
        frames_ << "\"group_count\":" << groups.size() << ',';
        frames_ << "\"tracker_count\":" << tracker_count << ',';
        frames_ << "\"binary_nonzero\":" << diagnostics.binary_nonzero << ',';
        frames_ << "\"binary_nonzero_ratio\":" << diagnostics.binary_nonzero_ratio << ',';
    }

    /**
     * @brief 写入阶段耗时字段
     * @param[in] stage_times 阶段耗时
     */
    void writeStageTimes(const RuneDetectorFrameDiagnostics::StageTimes &stage_times)
    {
        frames_ << "\"stage_time_ms\":{";
        frames_ << "\"binary\":" << stage_times.binary_ms << ',';
        frames_ << "\"find_features\":" << stage_times.find_features_ms << ',';
        frames_ << "\"pnp\":" << stage_times.pnp_ms << ',';
        frames_ << "\"group_update\":" << stage_times.group_update_ms << ',';
        frames_ << "\"get_runes\":" << stage_times.get_runes_ms << ',';
        frames_ << "\"match\":" << stage_times.match_ms << ',';
        frames_ << "\"total\":" << stage_times.total_ms;
        frames_ << "},";
    }

    /**
     * @brief 写入候选数量字段
     * @param[in] counts 候选数量
     */
    void writeCandidateCounts(const RuneDetectorFrameDiagnostics::CandidateCounts &counts)
    {
        frames_ << "\"candidates\":{";
        frames_ << "\"contours_raw\":" << counts.contours_raw << ',';
        frames_ << "\"contours_filtered\":" << counts.contours_filtered << ',';
        frames_ << "\"targets_active\":" << counts.targets_active << ',';
        frames_ << "\"targets_inactive\":" << counts.targets_inactive << ',';
        frames_ << "\"fans_active\":" << counts.fans_active << ',';
        frames_ << "\"fans_active_incomplete\":" << counts.fans_active_incomplete << ',';
        frames_ << "\"fans_inactive\":" << counts.fans_inactive << ',';
        frames_ << "\"centers\":" << counts.centers << ',';
        frames_ << "\"matched_feature_groups\":" << counts.matched_feature_groups << ',';
        frames_ << "\"final_feature_nodes\":" << counts.final_feature_nodes;
        frames_ << "},";
    }

    /**
     * @brief 写入已激活扇叶样本字段
     * @param[in] samples 已激活扇叶样本
     */
    void writeActiveFanSamples(const std::vector<RuneDetectorFrameDiagnostics::ActiveFanSample> &samples)
    {
        frames_ << "\"active_fans\":[";
        for (size_t i = 0; i < samples.size(); ++i)
        {
            if (i > 0)
                frames_ << ',';
            const auto &sample = samples[i];
            frames_ << '{';
            frames_ << "\"center\":";
            writePoint2f(frames_, sample.center);
            frames_ << ",\"direction\":";
            writePoint2f(frames_, sample.direction);
            frames_ << ",\"width\":" << sample.width;
            frames_ << ",\"height\":" << sample.height;
            frames_ << ",\"corners\":";
            writePoint2fArray(frames_, sample.corners);
            frames_ << ",\"pnp_points\":";
            writePoint2fArray(frames_, sample.pnp_points);
            frames_ << ",\"contour_points\":";
            writeContours(frames_, sample.contour_points);
            frames_ << '}';
        }
        frames_ << ']';
    }

    /**
     * @brief 判断当前帧是否需要保存识别图片
     * @param[in] frame_index 帧号
     * @param[in] diagnostics detector 诊断快照
     * @return 保存原因，为空表示不保存
     */
    std::string chooseImageSaveReason(int frame_index, const RuneDetectorFrameDiagnostics &diagnostics) const
    {
        if (!rune_detect_demo_param.diagnostic_save_images)
            return "";

        int max_count = std::max(0, rune_detect_demo_param.diagnostic_image_max_count);
        if (max_count == 0 || saved_images_ >= static_cast<size_t>(max_count))
            return "";

        // 第一帧通常能快速暴露视频开头黑帧、错色或阈值异常。
        if (!saved_first_image_)
            return "first";

        // 优先保留少量可形成弱标签的 active fan 成功样本。
        size_t active_limit = std::max<size_t>(10, static_cast<size_t>(max_count) / 3);
        if (!diagnostics.active_fans.empty() && saved_active_images_ < active_limit)
            return "active";

        // 失败帧也保留少量，便于后续观察二值化或几何规则问题。
        size_t failed_limit = std::max<size_t>(10, static_cast<size_t>(max_count) / 3);
        bool is_early_failed_sample = saved_failed_images_ < 4;
        bool is_periodic_failed_sample = frame_index % 50 == 0;
        if (diagnostics.status == "failed" && saved_failed_images_ < failed_limit &&
            (is_early_failed_sample || is_periodic_failed_sample))
        {
            return "failed";
        }

        // 最后按固定间隔保留少量时序采样帧。
        int stride = std::max(0, rune_detect_demo_param.diagnostic_image_stride);
        if (stride > 0 && frame_index % stride == 0)
            return "stride";

        return "";
    }

    /**
     * @brief 在识别图片上绘制诊断文字
     * @param[in,out] image 图片
     * @param[in] frame_index 帧号
     * @param[in] reason 保存原因
     * @param[in] diagnostics detector 诊断快照
     */
    void drawImageOverlay(cv::Mat &image, int frame_index, const std::string &reason, const RuneDetectorFrameDiagnostics &diagnostics) const
    {
        if (image.empty())
            return;

        std::vector<std::string> lines;
        {
            std::ostringstream oss;
            oss << "frame=" << frame_index
                << " status=" << diagnostics.status
                << " reason=" << reason
                << " active=" << diagnostics.active_fans.size()
                << " total_ms=" << std::fixed << std::setprecision(2) << diagnostics.stage_times.total_ms;
            lines.emplace_back(oss.str());
        }
        {
            std::ostringstream oss;
            oss << "contours=" << diagnostics.candidate_counts.contours_filtered
                << " matched=" << diagnostics.candidate_counts.matched_feature_groups
                << " binary_ratio=" << std::fixed << std::setprecision(5) << diagnostics.binary_nonzero_ratio;
            lines.emplace_back(oss.str());
        }
        if (!diagnostics.failure_stage.empty() || !diagnostics.find_features_failure.empty())
        {
            std::ostringstream oss;
            oss << "failure=" << diagnostics.failure_stage;
            if (!diagnostics.find_features_failure.empty())
                oss << ":" << diagnostics.find_features_failure;
            lines.emplace_back(oss.str());
        }

        int line_height = 24;
        int panel_height = static_cast<int>(lines.size()) * line_height + 10;
        int panel_width = std::min(image.cols, 920);
        cv::Mat overlay = image.clone();
        cv::rectangle(overlay, cv::Rect(0, 0, panel_width, panel_height), cv::Scalar(0, 0, 0), cv::FILLED);
        cv::addWeighted(overlay, 0.55, image, 0.45, 0, image);

        for (size_t i = 0; i < lines.size(); ++i)
        {
            cv::putText(image, lines[i], cv::Point(10, 22 + static_cast<int>(i) * line_height),
                        cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
        }
    }

    /**
     * @brief 按策略保存当前帧识别图片
     * @param[in] frame_index 帧号
     * @param[in] diagnostics detector 诊断快照
     * @param[in] image 当前绘制后的识别图片
     */
    void saveImageIfNeeded(int frame_index, const RuneDetectorFrameDiagnostics &diagnostics, const cv::Mat &image)
    {
        std::string reason = chooseImageSaveReason(frame_index, diagnostics);
        if (reason.empty() || image.empty())
            return;

        cv::Mat save_image = image.clone();
        drawImageOverlay(save_image, frame_index, reason, diagnostics);

        std::ostringstream filename;
        filename << "frame_" << std::setw(6) << std::setfill('0') << frame_index
                 << "_" << sanitizePathToken(reason)
                 << "_" << sanitizePathToken(diagnostics.status) << ".jpg";
        std::filesystem::path image_path = images_dir_ / filename.str();

        if (!cv::imwrite(image_path.string(), save_image))
        {
            VC_WARNING_INFO("无法保存诊断图片: %s", image_path.string().c_str());
            return;
        }

        saved_images_++;
        if (reason == "first")
            saved_first_image_ = true;
        else if (reason == "active")
            saved_active_images_++;
        else if (reason == "failed")
            saved_failed_images_++;

        if (image_index_.is_open())
        {
            image_index_ << csvEscape((std::filesystem::path("images") / filename.str()).string()) << ','
                         << frame_index << ','
                         << csvEscape(diagnostics.status) << ','
                         << csvEscape(reason) << ','
                         << diagnostics.active_fans.size() << ','
                         << csvEscape(diagnostics.failure_stage.empty() ? diagnostics.find_features_failure : diagnostics.failure_stage) << ','
                         << diagnostics.stage_times.total_ms << "\n";
        }
    }

private:
    std::filesystem::path run_dir_;
    std::filesystem::path frames_path_;
    std::filesystem::path summary_path_;
    std::filesystem::path config_path_;
    std::filesystem::path images_dir_;
    std::filesystem::path image_index_path_;
    std::ofstream frames_;
    std::ofstream image_index_;
    size_t frames_processed_ = 0;
    size_t ok_frames_ = 0;
    size_t vanish_ok_frames_ = 0;
    size_t failed_frames_ = 0;
    size_t active_fan_frames_ = 0;
    size_t saved_images_ = 0;
    size_t saved_active_images_ = 0;
    size_t saved_failed_images_ = 0;
    bool saved_first_image_ = false;
    double total_ms_sum_ = 0.0;
};

std::unique_ptr<RuneKeypointDiagnosticWriter> g_diagnostic_writer;
}

bool process(cv::VideoCapture &vid_cap)
{
    static auto rune_groups = vector<FeatureNode_ptr>{};
    static auto rune_detector = RuneDetector::make_detector();

    // 读取识别参数
    updateParam(vid_cap);

    Mat frame;
    int frame_index = static_cast<int>(vid_cap.get(cv::CAP_PROP_POS_FRAMES));
    vid_cap.read(frame); // 从摄像头捕获一帧
    if (frame.empty())
    {
        return false;
    }
    DebugTools::get()->setImage(frame);
    DetectorInput input;
    DetectorOutput output;
    input.setImage(frame);
    input.setGyroData(GyroData()); // 空数据
    input.setTick(cv::getTickCount());
    input.setColor(rune_detect_demo_param.color_type == 0 ? PixChannel::RED : PixChannel::BLUE);
    input.setColorThresh(rune_detect_demo_param.color_thresh);
    input.setFeatureNodes(rune_groups);
    rune_detector->detect(input, output);
    rune_groups = output.getFeatureNodes();

    // 优先完成绘制，再统一写入诊断文件和图片。
    do
    {
        if (rune_groups.empty())
            break;
        auto rune_group = RuneGroup::cast(rune_groups.front());
        if (!rune_group || rune_group->childFeatures().empty())
            break;

        FeatureNode_cptr target_tracker = nullptr;
        for (auto tracker : rune_group->getTrackers())
        {
            auto tracker_ = TrackingFeatureNode::cast(tracker);
            if (tracker_->getHistoryNodes().size() < 2)
                continue;
            auto type = RuneCombo::cast(tracker_->getHistoryNodes().front())->getRuneType();
            if (type == RuneType::PENDING_STRUCK)
            {
                target_tracker = tracker;
                break;
            }
        }
        if (!target_tracker)
            break;

        // 绘制当前目标神符序列组。
        Mat img_show = DebugTools::get()->getImage();
        rune_group->drawFeature(img_show);
    } while (0);

    // 诊断模式下每帧都写入结构化结果，失败帧同样保留。
    if (g_diagnostic_writer)
    {
        double frame_time_ms = rune_detect_demo_param.is_get_fps && rune_detect_demo_param.fps > 0
                                   ? static_cast<double>(frame_index) * 1000.0 / rune_detect_demo_param.fps
                                   : 0.0;
        g_diagnostic_writer->writeFrame(frame_index, frame_time_ms, rune_detector->getLastDiagnostics(), rune_groups, DebugTools::get()->getImage());
    }

    return true;
}

void initDiagnosticRun(cv::VideoCapture &cap)
{
    if (!rune_detect_demo_param.diagnostic_enabled)
        return;

    // 诊断模式以批处理为主，关闭 detector 内部二值图窗口。
    rune_detector_param.ENABLE_BINARY_DEBUG_VIEW = false;

    // 每个进程只维护一次诊断导出运行。
    if (!g_diagnostic_writer)
        g_diagnostic_writer = std::make_unique<RuneKeypointDiagnosticWriter>(cap);
}

void finishDiagnosticRun()
{
    if (!g_diagnostic_writer)
        return;

    // 显式完成汇总写入，随后释放文件句柄。
    g_diagnostic_writer->finish();
    g_diagnostic_writer.reset();
}

void updateParam(cv::VideoCapture &cap)
{
    if (rune_detect_demo_param.diagnostic_enabled)
        return;

    // 计数器
    static int count = 0;
    static std::string winname = "param";
    static int color_type_static = 0;
    static int color_thresh_static = 160;
    count++;

    // 判断窗口是否初始化
    auto layout = WindowAutoLayout::get();
    layout->addWindow(winname);

    // 判断参数滑动条是否存在
    static bool has_init_param = false;
    if (layout->hasWindow(winname) && !has_init_param)
    {
        //! 颜色类型参数类型
        std::string color_type_param_name = "Color Type (0: Red, 1: Blue)";
        //! 颜色二值化阈值
        std::string color_thresh_param_name = "Color Thresh (0~255)";
        createTrackbar(color_type_param_name, winname, nullptr, 1, [](int pos, void *userdata)
                       {
                         int *color_type_static_ptr = static_cast<int *>(userdata);
                         *color_type_static_ptr = pos; }, &color_type_static);
        setTrackbarPos(color_type_param_name, winname, 0);
        createTrackbar(color_thresh_param_name, winname, nullptr, 255, [](int pos, void *userdata)
                       {
                           int *color_thresh_static_ptr = static_cast<int *>(userdata);
                           *color_thresh_static_ptr = pos; }, &color_thresh_static);
        setTrackbarPos(color_thresh_param_name, winname, 100);
        has_init_param = true;
    }
    // 初始化进度条
    static bool has_init_progress = false;
    static int target_pos = 0;
    static int last_set_pos = -1; // 上次设置的位置
    do
    {
        if (rune_detect_demo_param.is_get_total_frames == false)
            break;
        if (layout->hasWindow(DebugTools::get()->getWindowName()))
        {
            if (!has_init_progress)
            {
                auto total_frames = rune_detect_demo_param.total_frames;
                createTrackbar("Frame Processed", DebugTools::get()->getWindowName(), nullptr, total_frames, [](int pos, void *userdata)
                               {
                                      int *target_pos_ptr = static_cast<int *>(userdata);
                                      *target_pos_ptr = pos; }, &target_pos);
                setTrackbarPos("Frame Processed", DebugTools::get()->getWindowName(), 0);
                has_init_progress = true;
            }
            // 获取当前的进度条
            if (target_pos != last_set_pos)
            {
                // 设置视频帧位置
                cap.set(cv::CAP_PROP_POS_FRAMES, target_pos);
                last_set_pos = target_pos;
            }
            if (count % 50 == 0) // 每30帧更新一次
            {
                // 获取当前帧位置
                int current_frame = static_cast<int>(cap.get(cv::CAP_PROP_POS_FRAMES));
                setTrackbarPos("Frame Processed", DebugTools::get()->getWindowName(), current_frame);
            }
        }
        else
        {
            has_init_progress = false;
        }

        // 判断是否到达视频末尾
        if (cap.get(cv::CAP_PROP_POS_FRAMES) >= cap.get(cv::CAP_PROP_FRAME_COUNT))
        {
            // 重新开始
            cap.set(cv::CAP_PROP_POS_FRAMES, 0);
            last_set_pos = -1;
            target_pos = 0;
            setTrackbarPos("Frame Processed", DebugTools::get()->getWindowName(), 0);
        }

    } while (0);

    // 进行参数赋值
    rune_detect_demo_param.color_type = color_type_static;
    rune_detect_demo_param.color_thresh = color_thresh_static;
}

void parseCommandLine(int argc, char **argv)
{
    CommandLine cli;
    // 1. 帮助项说明 ：-h, --help
    cli.addOption("-h", "help", "显示帮助信息");
    // 2. 输入视频路径 ：-i, --input <path>
    cli.addOption("-i", "input", "输入视频路径", true);
    // 3. 开启神符角点网络数据诊断导出
    cli.addOption("-d", "diagnostic", "启用神符角点网络数据诊断导出");
    // 4. 诊断导出根目录
    cli.addOption("-o", "diagnostic-output", "诊断导出根目录", true, "rune_keypoint_nn_runs");
    // 5. 诊断运行名称
    cli.addOption("-r", "diagnostic-run", "诊断运行名称", true);
    // 6. 识别颜色类型
    cli.addOption("-c", "color-type", "识别颜色类型(红色:0,蓝色:1)", true);
    // 7. 颜色二值化阈值
    cli.addOption("-t", "color-thresh", "颜色二值化阈值(0~255)", true);
    // 8. 禁用诊断图片保存
    cli.addOption("-N", "no-save-images", "诊断模式下不保存识别结果图片");
    // 9. 诊断图片定期采样间隔
    cli.addOption("-s", "image-stride", "诊断图片定期采样帧间隔", true, "200");
    // 10. 单次诊断最多保存图片数量
    cli.addOption("-m", "image-max", "单次诊断最多保存图片数量", true, "80");

    // 解析参数
    bool valid = cli.parse(argc, argv);
    if (!valid)
    {
        cli.printHelp(argv[0]);
        VC_WARNING_INFO("程序使用示例：\n./VisCore_rune_detect_demo_exe -i ./test_video/rune_video.mp4\n./VisCore_rune_detect_demo_exe -i ./test_video/rune_video.mp4 --diagnostic --color-type 0 --color-thresh 100\n注意:\n 1.视频路径为绝对路径\n 2.视频路径不要带外括号");
        exit(1);
    }

    if (cli.isSet("-h"))
    {
        cli.printHelp(argv[0]);
        exit(0);
    }

    if (cli.isSet("-i"))
    {
        rune_detect_demo_param.video_path = cli.get("-i");
        if (rune_detect_demo_param.video_path.empty())
        {
            VC_WARNING_INFO("无效的视频路径");
        }
    }

    if (cli.isSet("-d"))
        rune_detect_demo_param.diagnostic_enabled = true;
    if (cli.isSet("-o"))
        rune_detect_demo_param.diagnostic_output_root = cli.get("-o");
    if (cli.isSet("-r"))
        rune_detect_demo_param.diagnostic_run_name = cli.get("-r");
    if (cli.isSet("-c"))
        rune_detect_demo_param.color_type = std::clamp(std::stoi(cli.get("-c")), 0, 1);
    if (cli.isSet("-t"))
        rune_detect_demo_param.color_thresh = std::clamp(std::stoi(cli.get("-t")), 0, 255);
    if (cli.isSet("-N"))
        rune_detect_demo_param.diagnostic_save_images = false;
    if (cli.isSet("-s"))
        rune_detect_demo_param.diagnostic_image_stride = std::max(0, std::stoi(cli.get("-s")));
    if (cli.isSet("-m"))
        rune_detect_demo_param.diagnostic_image_max_count = std::max(0, std::stoi(cli.get("-m")));

    if (cli.getPositionalArgs().size() > 0)
    {
        VC_WARNING_INFO("检测到多余的参数，已忽略");
    }
}

void setupVideoCapture(cv::VideoCapture &cap)
{
    int total_frames = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
    if (total_frames <= 0)
        VC_WARNING_INFO("无法获取视频总帧数，可能是摄像头或无效视频文件");
    else
        VC_PASS_INFO("视频总帧数: %d", total_frames);

    double fps = cap.get(cv::CAP_PROP_FPS);
    if (fps <= 0)
        VC_WARNING_INFO("无法获取视频帧率，可能是摄像头或无效视频文件");
    else
        VC_PASS_INFO("视频帧率: %.2f", fps);

    bool is_get_total_frames = total_frames > 0;
    bool is_get_fps = fps > 0;
    if (!is_get_total_frames && !is_get_fps)
    {
        VC_WARNING_INFO("无法获取视频总帧数和帧率，默认按30FPS处理");
        cap.set(cv::CAP_PROP_FPS, 30);
    }
    else if (!is_get_total_frames && is_get_fps)
    {
        VC_PASS_INFO("只能获取视频帧率，按此帧率处理");
    }
    else if (is_get_total_frames && !is_get_fps)
    {
        VC_WARNING_INFO("只能获取视频总帧数，默认按30FPS处理");
        cap.set(cv::CAP_PROP_FPS, 30);
    }
    else
    {
        double duration = total_frames / fps;
        VC_PASS_INFO("视频时长: %.2f 秒", duration);
    }

    cap.set(cv::CAP_PROP_FPS, fps);
    cap.set(cv::CAP_PROP_POS_FRAMES, 0); // 从头开始
    rune_detect_demo_param.is_get_fps = is_get_fps;
    rune_detect_demo_param.fps = fps;
    rune_detect_demo_param.is_get_total_frames = is_get_total_frames;
    rune_detect_demo_param.total_frames = total_frames;
}
