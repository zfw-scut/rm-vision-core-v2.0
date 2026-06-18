# 2026-06-18 神符角点网络诊断导出模式

## 本次目标

为神符角点识别网络的数据准备阶段增加离线诊断导出能力，使 Codex 能读取每帧识别状态、候选特征数量、已激活扇叶轮廓点集和角点弱标签。

## 修改文件列表

- `.gitignore`
- `modules/detector/rune_detector/include/vc/detector/rune_detector.h`
- `modules/detector/rune_detector/src/rune_detector.cpp`
- `modules/detector/rune_detector/src/rune_detector_find.cpp`
- `modules/detector/rune_detector/include/vc/detector/rune_detector_param.h`
- `modules/detector/rune_detector/include/vc/detector/yml/RuneDetectorParam.yml`
- `examples/rune_detect_demo/include/rune_detect_demo/rune_detect_demo.h`
- `examples/rune_detect_demo/include/rune_detect_demo/rune_detect_demo_param.h`
- `examples/rune_detect_demo/main.cpp`
- `examples/rune_detect_demo/src/rune_detect_demo.cpp`
- `tools/rune_keypoint_export_candidates.py`
- `docs/modules/rune_detector.md`
- `docs/projects/rune_keypoint_nn_project_outline.md`

## 核心改动概括

- 为 `RuneDetector` 增加 `RuneDetectorFrameDiagnostics` 单帧诊断快照。
- 记录二值化、特征查找、PnP、组合体构造、匹配和总耗时。
- 记录轮廓、靶心、扇叶、中心、匹配特征组等候选数量。
- 提取已激活扇叶的轮廓点集、图像角点和 PnP 点序列，作为后续弱标签数据来源。
- 为 `rune_detect_demo` 增加 `--diagnostic` 离线导出模式。
- 诊断模式会在视频结束后退出，并生成 `run_config.yaml`、`frames.jsonl`、`summary.csv`。
- 增加 `--color-type` 和 `--color-thresh`，便于逐视频参数复跑。
- 诊断模式关闭交互显示和 detector 内部二值图窗口，避免批处理依赖 UI。
- 诊断模式默认保存少量识别结果图片，并生成 `image_index.csv`。
- 增加 `--no-save-images`、`--image-stride`、`--image-max` 控制图片保存。
- 增加 `tools/rune_keypoint_export_candidates.py`，用于从诊断 `frames.jsonl` 导出弱标签候选样本。
- 在 `.gitignore` 中隔离 `rune_keypoint_nn_runs/` 本地生成目录。
- 更新模块文档和项目大纲，记录当前弱标注数据闭环。

## 批量诊断结果

本轮使用统一参数跑完 7 个素材视频：

- `color_type = 0`
- `color_thresh = 100`
- 输出批次目录：`rune_keypoint_nn_runs/batch_20260618_baseline_t100/`

保留给用户查看的结果文件：

- `rune_keypoint_nn_runs/batch_20260618_baseline_t100/batch_report.md`
- `rune_keypoint_nn_runs/batch_20260618_baseline_t100/batch_summary.csv`
- `rune_keypoint_nn_runs/batch_20260618_baseline_t100/sample_active_frames.csv`
- `rune_keypoint_nn_runs/batch_20260618_baseline_t100/video_<n>_t100/image_index.csv`
- `rune_keypoint_nn_runs/batch_20260618_baseline_t100/video_<n>_t100/images/`
- `rune_keypoint_nn_runs/batch_20260618_baseline_t100/candidate_samples/`

图片保存设置：

- `image_stride = 300`
- `image_max = 24`
- 每个视频当前保留 24 张识别结果图片。

最新批次汇总：

| 视频 | 帧数 | ok | vanish | failed | active fan | ok率 | ok+vanish率 | active fan率 | 平均耗时(ms) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `1.avi` | 3057 | 2345 | 147 | 565 | 1111 | 76.71% | 81.52% | 36.34% | 1.555 |
| `2.avi` | 1990 | 1007 | 88 | 895 | 551 | 50.60% | 55.03% | 27.69% | 1.351 |
| `3.avi` | 2836 | 1529 | 230 | 1077 | 900 | 53.91% | 62.02% | 31.73% | 1.597 |
| `4.avi` | 4418 | 1850 | 195 | 2373 | 1191 | 41.87% | 46.29% | 26.96% | 1.178 |
| `5.avi` | 1649 | 855 | 35 | 759 | 493 | 51.85% | 53.97% | 29.90% | 1.306 |
| `6.avi` | 1069 | 485 | 44 | 540 | 247 | 45.37% | 49.49% | 23.11% | 1.146 |
| `7.avi` | 1666 | 679 | 52 | 935 | 211 | 40.76% | 43.88% | 12.67% | 0.958 |

初步判断：

- `1.avi` 当前参数下最稳，适合作为第一批弱标签检查样本。
- `3.avi` 可用帧较多，但 warning 较多，需要人工审查。
- `4.avi`、`6.avi`、`7.avi` 失败比例偏高，后续优先逐视频调参。
- `7.avi` 有解码 warning，且无法获取总帧数，需要确认素材文件完整性。

候选样本导出结果：

- 输出目录：`rune_keypoint_nn_runs/batch_20260618_baseline_t100/candidate_samples/`
- `samples.jsonl`：8715 条候选样本
- `index.csv`：候选样本索引
- `summary.csv`：按视频统计
- `review_images/`：91 张可直接观察的样例图

候选样本导出规则：

- `status == "ok"`
- `used_vanish_update == false`
- `active_fans` 非空
- `corners` 数量为 6
- `pnp_points` 数量为 6
- `contour_points` 存在

## 已运行的检查/测试

- `cmake -S . -B build`
- `cmake --build build --target VisCore_rune_detect_demo_exe -j2`
- `./bin/VisCore_rune_detect_demo_exe -h`
- `git diff --check`
- `find /home/yefan/Desktop/神符角点识别网络的训练素材 -maxdepth 1 -type f`
- `./bin/VisCore_rune_detect_demo_exe -i /home/yefan/Desktop/神符角点识别网络的训练素材/1.avi --diagnostic --diagnostic-run smoke_1 --color-type 0 --color-thresh 100`
- 依次对 `1.avi` 到 `7.avi` 运行统一参数批量诊断，输出到 `rune_keypoint_nn_runs/batch_20260618_baseline_t100/`
- `./bin/VisCore_rune_detect_demo_exe -i /home/yefan/Desktop/神符角点识别网络的训练素材/6.avi --diagnostic --diagnostic-output rune_keypoint_nn_runs/image_smoke --diagnostic-run video_6_images --color-type 0 --color-thresh 100 --image-stride 300 --image-max 12`
- 重新对 `1.avi` 到 `7.avi` 运行统一参数批量诊断，并为每个视频保存 24 张识别结果图片。
- 使用 `view_image` 查看 `frame_000255_active_ok.jpg` 和 `frame_000050_failed_failed.jpg`，确认图片可用于后续直接观察。
- `python3 -m py_compile tools/rune_keypoint_export_candidates.py`
- `python3 tools/rune_keypoint_export_candidates.py --input rune_keypoint_nn_runs/batch_20260618_baseline_t100 --output rune_keypoint_nn_runs/batch_20260618_baseline_t100/candidate_samples --copy-review-images --max-review-images 300`
- 使用 `view_image` 查看 `candidate_samples/review_images/1_frame_000711_fan_00.jpg`，确认候选样本图片可直接观察。

真实视频 smoke run 结果：

- 输入视频：`1.avi`
- 总帧数：3057
- 成功帧：2346
- 掉帧预测成功帧：127
- 失败帧：584
- 含已激活扇叶样本帧：1111
- 平均 detector 耗时：约 1.47 ms
- 输出目录：`rune_keypoint_nn_runs/smoke_1/`，该目录已被 `.gitignore` 忽略。

## 未解决问题

- 尚未对 7 个视频执行完整诊断导出。
- 尚未生成用于人工审查的角点覆盖视频。
- 尚未实现按黑名单帧号排除候选样本的工具。
- 尚未将 `samples.jsonl` 转换为训练框架直接读取的数据集格式。

## 后续建议

- 先选 1 个视频，用 1 到 2 组阈值跑完整诊断，检查 `frames.jsonl` 中 `status`、`active_fans` 和候选数量是否符合预期。
- 根据首个视频结果决定人工审查视频中需要显示哪些字段。
- 再批量跑完 7 个视频，按视频和参数配置整理弱标签质量。
