# RuneDetector 模块说明

## 职责

`RuneDetector` 是当前神符识别主流程入口，负责把单帧图像转换为神符序列组输出。

主要流程：

- 根据目标颜色做颜色差分二值化。
- 查找轮廓并构造靶心、扇叶、中心等特征。
- 将特征配对为神符组合体。
- 通过 PnP 获取神符组位姿。
- 更新 `RuneGroup`、构造 5 个 `RuneCombo` 并同步 tracker。

## 诊断快照

`RuneDetector` 维护最近一帧的 `RuneDetectorFrameDiagnostics`，可通过 `getLastDiagnostics()` 读取。

该结构只用于离线数据集构建、参数扫描和 Codex 分析，不参与实时识别决策。

当前记录内容：

- `status`：`ok`、`vanish_ok`、`failed` 等状态。
- `failure_stage`：主流程失败阶段。
- `find_features_failure`：特征查找阶段的失败原因。
- `stage_times`：二值化、特征查找、PnP、组合体构造、匹配和总耗时。
- `candidate_counts`：轮廓、靶心、扇叶、中心、匹配特征组等数量。
- `binary_nonzero_ratio`：二值图非零像素比例，可用于快速判断阈值是否异常。
- `active_fans`：已激活扇叶弱标签样本。

`active_fans` 中包含：

- `contour_points`：扇叶轮廓点集。
- `corners`：当前规则算法提取并存入图像缓存的角点。
- `pnp_points`：当前 PnP 实际使用的点序列。
- `center`、`direction`、`width`、`height`：扇叶基础几何信息。

## Demo 诊断导出

`examples/rune_detect_demo` 支持离线诊断模式：

```bash
./bin/VisCore_rune_detect_demo_exe \
  -i /path/to/video.avi \
  --diagnostic \
  --color-type 0 \
  --color-thresh 100
```

默认输出到项目根目录：

```text
rune_keypoint_nn_runs/
  <run_name>/
    run_config.yaml
    frames.jsonl
    image_index.csv
    summary.csv
    images/
```

可选参数：

- `--diagnostic-output <dir>`：修改输出根目录。
- `--diagnostic-run <name>`：指定本次运行名称。
- `--color-type <0|1>`：固定识别颜色。
- `--color-thresh <0..255>`：固定二值化阈值。
- `--no-save-images`：诊断模式下不保存识别结果图片。
- `--image-stride <n>`：按固定帧间隔保存采样图片。
- `--image-max <n>`：限制单次 run 最多保存图片数量。

诊断模式会在视频结束后退出，不使用交互模式的视频循环播放逻辑，也会关闭 detector 内部二值图调试窗口。

默认会保存少量识别结果图片。保存策略包括：

- 第一帧，用于观察开头黑帧、错色或阈值异常。
- 少量 `active_fans` 非空的成功帧，用于检查弱标签质量。
- 少量失败帧，用于观察失败原因。
- 按 `--image-stride` 定期采样的帧。

每张图片左上角会叠加帧号、状态、保存原因、active fan 数量、候选轮廓数量、二值图比例和耗时。`image_index.csv` 记录图片路径、帧号、状态和失败阶段，便于从图片回查 `frames.jsonl`。

## 弱标注使用建议

后续构建角点网络数据集时，优先使用：

- `status == "ok"` 的帧。
- `active_fans` 非空的帧。
- `corners` 数量符合当前训练目标的帧。
- `binary_nonzero_ratio` 和候选数量没有明显异常的帧。
- 同一视频、同一参数配置下连续稳定的帧段。

`vanish_ok` 帧依赖历史预测，默认不作为第一版角点弱标签。
