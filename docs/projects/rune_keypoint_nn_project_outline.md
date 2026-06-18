# 神符轮廓角点神经网络项目实现大纲

日期：2026-06-18

文档类型：项目路线图 / 新需求实现大纲

## 1. 项目目标

本项目旨在为当前神符识别库加入一个专门用于轮廓角点检测的轻量神经网络模块。第一阶段重点解决已激活扇叶 `RuneFanActive` 的角点提取问题，替代或辅助当前基于链码、线段匹配和突起检测的手写规则。

核心目标：

- 保留现有颜色二值化、轮廓提取、几何校验、PnP、滤波和追踪主链路。
- 新增一个针对候选轮廓点集的角点检测模型。
- 模型只作为角点候选器，最终输出仍由几何合法性、PnP 可解性和重投影误差共同校验。
- 首版必须支持规则回退，不能破坏现有实时识别能力。

首版不追求端到端姿态回归，也不直接替代全图二值化。

## 2. 首版识别范围

第一阶段只处理已激活扇叶角点：

```text
top_left
top_center
top_right
side_left
side_right
bottom_center
```

对应现有实现中的：

```text
TopHump 3 点
SideHump 2 点
BottomCenterHump 1 点
```

暂不处理：

- 未激活扇叶矩形角点。
- 已激活靶心中心点。
- 未激活靶心缺口角点。
- 中心 R 标。
- 直接输出整组神符位姿。

待 6 点检测稳定后，再评估是否扩展到 8 点、残缺扇叶或靶心缺口。

## 3. 总体技术路线

建议数据流：

```text
原图
  -> 传统颜色差分二值化
  -> findContours
  -> 几何规则筛选疑似 active fan 轮廓
  -> 轮廓重采样和归一化
  -> 轻量角点网络
  -> 几何合法性校验
  -> PnP / 重投影校验
  -> RuneGroup / RuneTracker
```

神经网络不进入每帧全图感知主干，而是作用在少量候选轮廓上，降低推理开销并保持实时性。

## 4. 推荐开发顺序

### 阶段 0：固定角点语义

- 固定 6 个角点的名称、顺序和物理含义。
- 明确遮挡情况下 `visible=false` 的表达方式。
- 明确角点坐标是图像坐标，还是归一化到轮廓局部坐标。
- 将关键点顺序写入模型配置文件，避免训练端和 C++ 推理端不一致。

### 阶段 1：建立数据格式与标注规范

每个样本至少包含：

```text
原图路径
候选轮廓点集
轮廓类型
6 个角点坐标
6 个角点 visible 标记
候选轮廓来源信息
当前规则算法输出，可选
人工修正标记，可选
```

推荐元信息使用 JSON/YAML，轮廓数组可使用 `.npy` 或 `.npz`。

示例：

```json
{
  "image": "frames/000123.png",
  "feature_type": "active_fan",
  "contour_id": 7,
  "keypoints": {
    "top_left": {"x": 120.3, "y": 84.1, "visible": true},
    "top_center": {"x": 153.0, "y": 77.5, "visible": true},
    "top_right": {"x": 185.6, "y": 84.9, "visible": true},
    "side_left": {"x": 132.2, "y": 145.0, "visible": true},
    "side_right": {"x": 176.4, "y": 145.8, "visible": true},
    "bottom_center": {"x": 153.4, "y": 205.2, "visible": true}
  }
}
```

### 阶段 2：离线数据导出工具

先建立从视频和现有算法导出候选轮廓的工具。工具应尽量复用当前二值化和轮廓提取逻辑，使训练数据分布贴近实际部署。

建议目录：

```text
ml/rune_keypoint/
  scripts/
    export_contours.py
    review_labels.py
    convert_labels.py
```

数据导出应支持：

- 从视频批量抽帧。
- 保存原图、二值图和候选轮廓。
- 保存当前规则算法输出的角点作为弱标签候选。
- 支持人工复核和修正。

### 阶段 3：训练第一版轻量模型

首版模型建议使用 1D CNN / TCN，而不是 Transformer 或图像分割网络。

推荐输入：

```text
N x C
```

其中 `N` 为固定重采样长度，例如 256 或 512。每个轮廓点可包含：

```text
x_norm
y_norm
dx
dy
angle
curvature
distance_to_center
```

首版输出建议采用直接坐标回归：

```text
6 x (x_norm, y_norm, confidence)
```

后续可升级为序列 heatmap：

```text
6 x N heatmap
6 x offset
6 x visible_confidence
```

直接坐标回归更容易接入 C++，heatmap 更贴合“角点来自轮廓序列”的任务。

### 阶段 4：离线评估

模型进入 C++ 前必须先做离线评估。

指标建议：

```text
角点平均误差 px
PCK@2px / PCK@3px / PCK@5px
完整 6 点成功率
visible 分类准确率
PnP 可解率
PnP 重投影误差
单候选推理耗时
与原规则角点输出的对比
```

最终更关注 PnP 重投影误差和连续帧稳定性，而不仅是单点像素误差。

### 阶段 5：导出 ONNX

训练端输出：

```text
models/rune_keypoint/
  active_fan_keypoint_v1.onnx
  active_fan_keypoint_v1.yaml
```

模型配置文件需记录：

```text
模型版本
输入长度 N
输入特征顺序
关键点顺序
归一化方式
置信度阈值
后处理参数
训练数据版本
```

C++ 核心程序不依赖 PyTorch，只加载 ONNX 或部署端 engine。

### 阶段 6：核心库接入

不要直接把网络推理写进 `RuneFanActive::getActiveFunCorners()`。建议先抽象角点提取器。

建议新增：

```text
modules/feature/rune_fan/
  include/vc/feature/rune_fan_corner_extractor.h
  include/vc/feature/rune_fan_corner_net_param.h
  include/vc/feature/yml/RuneFanCornerNetParam.yml
  src/rune_fan_corner_extractor_rule.cpp
  src/rune_fan_corner_extractor_nn.cpp
```

建议接口：

```cpp
struct FanCornerResult
{
    std::vector<cv::Point2f> top;
    std::vector<cv::Point2f> side;
    std::vector<cv::Point2f> bottom_center;
    float confidence = 0.f;
};

class RuneFanCornerExtractor
{
public:
    virtual ~RuneFanCornerExtractor() = default;
    virtual bool extract(const Contour_cptr &contour, FanCornerResult &result) = 0;
};
```

首版实现：

```text
RuleBasedFanCornerExtractor
NeuralFanCornerExtractor
```

`RuneFanActive` 只依赖抽象接口，不直接依赖 ONNX Runtime 或 TensorRT。

### 阶段 7：推理后端独立封装

推理后端建议放在公共模块中，避免特征层绑定具体推理框架。

建议新增：

```text
common/modules/inference/
  CMakeLists.txt
  include/vc/inference/inference_backend.h
  include/vc/inference/onnx_backend.h
  src/onnx_backend.cpp
```

建议接口：

```cpp
class InferenceBackend
{
public:
    virtual ~InferenceBackend() = default;
    virtual bool infer(const cv::Mat &input, cv::Mat &output) = 0;
};
```

CMake 选项：

```text
VISCORE_ENABLE_ONNX
VISCORE_ENABLE_TENSORRT
```

默认关闭，保证无神经网络依赖时主工程仍可编译。

### 阶段 8：运行时策略

推荐提供三种模式：

```text
RULE_ONLY
NN_ONLY
NN_WITH_RULE_FALLBACK
```

部署初期默认使用：

```text
NN_WITH_RULE_FALLBACK
```

建议流程：

```text
1. 规则筛出 active fan 候选轮廓。
2. NN 输出角点候选。
3. 检查角点数量、顺序、方向、距离比例。
4. 尝试 PnP 或局部重投影校验。
5. 通过则使用 NN 结果。
6. 失败则回退当前规则角点提取。
```

## 5. 推荐文件层级

训练工程与核心 C++ 库隔离：

```text
rm_vision_core/
  ml/
    rune_keypoint/
      README.md
      pyproject.toml
      configs/
        active_fan_v1.yaml
      scripts/
        export_contours.py
        review_labels.py
        train.py
        eval.py
        export_onnx.py
      rune_keypoint/
        dataset.py
        model.py
        losses.py
        transforms.py
        metrics.py
        export.py

  models/
    rune_keypoint/
      active_fan_keypoint_v1.onnx
      active_fan_keypoint_v1.yaml

  common/
    modules/
      inference/
        CMakeLists.txt
        include/vc/inference/inference_backend.h
        include/vc/inference/onnx_backend.h
        src/onnx_backend.cpp

  modules/
    feature/
      rune_fan/
        include/vc/feature/rune_fan_corner_extractor.h
        include/vc/feature/rune_fan_corner_net_param.h
        include/vc/feature/yml/RuneFanCornerNetParam.yml
        src/rune_fan_corner_extractor_rule.cpp
        src/rune_fan_corner_extractor_nn.cpp
```

## 6. 数据与标注注意事项

必须重点处理：

- 轮廓起点任意导致的循环位移。
- 轮廓方向可能顺时针或逆时针。
- 不同轮廓长度需要重采样。
- 角点语义需要根据神符中心方向建立一致局部坐标系。
- 遮挡、拖影、光晕、过曝和轮廓断裂。
- 网络输出坐标与原图坐标的反归一化精度。

建议训练增强：

- 轮廓循环平移。
- 轮廓反向。
- 点坐标噪声。
- 局部点丢失。
- 轮廓膨胀/收缩模拟。
- 仿射变换和尺度变化。

## 7. 性能原则

当前传统链路单帧约 10ms，其中二值化约 2-3ms。神经网络接入必须遵守：

- 不替代高速二值化主路径。
- 不做每帧全图分割。
- 只对少量候选轮廓推理。
- 支持关闭 NN 后保持原性能。
- 记录单候选推理耗时和整帧新增耗时。

首版目标建议：

```text
单候选推理 <= 1ms
整帧平均新增耗时 <= 2ms
失败回退不影响原规则输出
```

## 8. 最小可交付版本

第一版项目应完成：

```text
1. active_fan 轮廓样本导出工具。
2. 6 点标注和复核流程。
3. 1D CNN / TCN 轮廓角点模型。
4. 离线评估脚本。
5. ONNX 导出。
6. C++ 抽象角点提取器。
7. NN_WITH_RULE_FALLBACK 模式。
8. 与原规则在同一批视频上的对比报告。
```

对比报告至少包含：

```text
原规则成功率
NN 成功率
回退后总成功率
角点误差
PnP 重投影误差
平均帧耗时
失败样例分类
```

## 9. 风险与开放问题

主要风险：

- 标注质量不足导致模型学到当前规则的错误。
- 轮廓序列规范化不稳定，导致训练和部署分布不一致。
- 网络调用开销超过模型计算本身。
- NN 输出角点偶发跳变，影响 PnP 和 tracker。
- 遮挡样本不足，导致残缺扇叶收益不明显。

待确认问题：

- 首版输入长度使用 256 还是 512。
- 首版输出采用坐标回归还是 heatmap。
- 推理后端优先 ONNX Runtime、OpenVINO、TensorRT 还是 ncnn。
- 模型文件是否进入仓库，或只提交配置与下载说明。
- 标注工具使用现有脚本、CVAT，还是自研轻量复核工具。

## 10. 设计原则

- 网络做角点候选，几何做最终裁判。
- 训练工程与核心 C++ 工程解耦。
- 推理后端与特征模块解耦。
- 首版只做已激活扇叶，不扩大范围。
- 全程保留规则回退路径。
- 以实时性、重投影误差和连续帧稳定性作为最终验收标准。
