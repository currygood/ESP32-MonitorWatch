# 第三方组件与开源合规声明

本文件是 ESP32-MonitorWatch 的开源凭证之一，用于声明项目**使用到的全部第三方开源组件**、版本、许可证与获取地址。项目自身代码以 [Apache License 2.0](LICENSE) 授权（见 [NOTICE](NOTICE)）。

**分发义务（重要）**：Apache-2.0 与 BSD 系许可证均要求，当你以二进制形式（固件 `.bin`、App 安装包、成品手表）对外分发本项目时，必须同时提供 `LICENSE`、`NOTICE` 与本文件，并保留第三方版权声明。仅提供 GitHub 链接**不能**替代随附声明。

- 声明基准提交：`339b786`（Release-3.2）
- 组件版本来源：`dependencies.lock`（ESP-IDF Component Manager）、`phone_app/esp32_monitor_app/pubspec.lock`
- 最后核对日期：2026-11-01

---

## 一、固件侧（ESP32-S3 / ESP-IDF）

| 组件 | 版本 | 许可证 | 用途 | 上游 |
| --- | --- | --- | --- | --- |
| ESP-IDF | 5.2.6 | Apache-2.0 | 芯片 SDK、FreeRTOS、驱动、NVS、SPIFFS、ULP-RISC-V | https://github.com/espressif/esp-idf |
| espressif/esp-tflite-micro | 1.4.0 | Apache-2.0 | 端侧 int8 量化 CNN 推理引擎 | https://components.espressif.com/components/espressif/esp-tflite-micro |
| espressif/esp-nn | 1.3.2 | Apache-2.0 | ESP32-S3 向量指令优化算子库（TFLM 依赖） | https://components.espressif.com/components/espressif/esp-nn |
| espressif/cmake_utilities | 0.5.3 | Apache-2.0 | 组件构建辅助 CMake 脚本 | https://components.espressif.com/components/espressif/cmake_utilities |
| espressif/openai | 1.1.0 | Apache-2.0 | 已在 `dependencies.lock` 中声明（当前固件主流程未调用，若移除请同步更新本表） | https://components.espressif.com/components/espressif/openai |

**ESP-IDF 内嵌的下级组件**（随 SDK 一起编译，版权归各自作者）：FreeRTOS-Kernel（MIT）、lwIP（BSD-3-Clause）、mbedTLS（Apache-2.0）、ESP-Hosted/各类 `hal` 组件（Apache-2.0 为主，个别为 BSD-3-Clause）。完整清单见 ESP-IDF 仓库内各组件目录的 `LICENSE` 或 `COPYING` 文件。

**esp-tflite-micro 内嵌的上游代码**：TensorFlow Lite Micro（Apache-2.0）、FlatBuffers 生成代码（Apache-2.0）、gemmlowp（Apache-2.0）、ruy（Apache-2.0）、kissfft（BSD-3-Clause）。详见 `managed_components/espressif__esp-tflite-micro/` 目录内的许可证文件。

> `managed_components/` 已被 `.gitignore` 忽略，不随仓库分发；它们由 Component Manager 在构建时按 `dependencies.lock` 拉取，故本表即为分发时的随附声明。

## 二、手机 App 侧（Flutter / Dart）

| 包 | 版本 | 许可证 | 用途 |
| --- | --- | --- | --- |
| Flutter SDK / Dart SDK | 3.x | BSD-3-Clause | 应用框架与运行时 |
| dio | 5.10.0 | MIT | OneNET API HTTP 轮询 |
| crypto | 3.0.7 | BSD-3-Clause | OneNET Token 签名 |
| shared_preferences | 2.x | BSD-3-Clause | 本地保存 4 个 OneNET 密钥 |
| intl | 0.20.3 | BSD-3-Clause | 时间戳格式化 |
| flutter_local_notifications | 22.0.1 | BSD-3-Clause | 本地报警推送 |
| cupertino_icons | 1.0.9 | MIT | 图标字体 |
| http / http_parser | 1.6.0 / 4.1.2 | BSD-3-Clause | 传递依赖（dio 以外的 HTTP 能力） |
| dbus | 0.7.13 | **MPL-2.0** | 传递依赖，仅 Linux 桌面构建使用 |
| flutter_lints / lints | 6.0.0 / 6.1.0 | BSD-3-Clause | 仅开发期静态检查，不进入产物 |
| 其余传递依赖 | 见 `pubspec.lock` | 以 BSD-3-Clause 为主 | 平台插件接口、集合工具等 |

**MPL-2.0 提示**：`dbus` 为弱 Copyleft（文件级）。仅当构建 **Linux 桌面版**并分发时才产生源码提供义务（需提供被修改的 `dbus` 包源文件）；Android/iOS/Windows 构建不包含该包，无此义务。本项目当前仅发布 Android 版本，故不受影响。

**App 内自助查看**：Flutter 已内置许可证登记机制，调用 `showLicensePage()` 会在 App 内列出全部依赖的许可证原文，可与本表互相印证。

## 三、云端与外部服务（非开源组件，不受本项目许可证约束）

| 服务 | 用途 | 说明 |
| --- | --- | --- |
| OneNET（中国移动物联网平台） | MQTT 数据上报与物模型 | 第三方商业服务，使用需遵守其服务条款；项目代码不包含其任何代码 |
| 中景园电子 0.96" OLED 模块 | 显示屏硬件 | 硬件产品，不涉及软件许可；其驱动代码来源见第五节 |

## 四、依托开源项目但无代码复制关系

- **MQTT 协议**（OASIS 标准）：协议本身不受版权保护，`mqtt.c` 为基于 ESP-IDF `esp-mqtt` API 的自研封装。
- **MAX30102 / MPU6050 算法**：PPG 带通滤波、峰值检测、跌倒判定、卡尔曼融合均为本项目自研实现（见 `components/SignalFusion/`），未复制第三方源码。
- **AI 模型**（`model/model_int8.tflite`）：由本项目自行采集数据训练并量化产出，随本仓库以 Apache-2.0 授权。

## 五、待确认事项（分发前请自行核实）

| 项 | 说明 | 建议 |
| --- | --- | --- |
| OLED 驱动与字库 | `components/Middlewares/OLED/` 下的显示驱动与点阵字库**不含任何第三方版权头**，按现状视为本项目自有实现。但该屏幕（中景园电子 0.96"）的厂商通常随货提供示例代码，若实际代码移植自该示例，则存在未声明的上游权益。 | 分发前确认代码来源：若确为自研移植，无需处理；若源自厂商示例，建议取得书面授权或在 [NOTICE](NOTICE) 中补充来源与修改声明。 |
| `espressif/openai` 组件 | 已在依赖锁中声明但主流程未使用。 | 若不使用，建议从 `main/idf_component.yml` 移除后重新生成 `dependencies.lock`，并删除本表对应行。 |
| 组件版本漂移 | 上游升级后许可证可能变更。 | 每次发版前对照 `dependencies.lock` / `pubspec.lock` 复核本表。 |

## 六、本项目自身代码的许可范围

采用 SPDX 标识，逐文件标注：

- C / C++ 源码（`components/**`、`main/**`）：`SPDX-License-Identifier: Apache-2.0`
- Dart 源码（`phone_app/**`）：`SPDX-License-Identifier: Apache-2.0`
- 文档、图片、模型：随仓库整体以 Apache-2.0 授权（Apache-2.0 第 3 条商标条款不授予项目名称与 Logo 的使用权）

---

如有许可证问题或发现遗漏的第三方组件，请开 Issue 或联系仓库维护者 currygood <2294008910@qq.com>。
