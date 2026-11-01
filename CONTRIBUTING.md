# 贡献指南

感谢你愿意改进 ESP32-MonitorWatch。提交代码前请阅读以下约定。

## 许可证

本项目以 [Apache License 2.0](LICENSE) 授权。**提交 Pull Request 即表示你同意**：

1. 你的贡献以 Apache-2.0 授权给本项目及所有下游使用者；
2. 你拥有所提交内容的著作权，或已获得权利人许可（不要提交来源不明或带有限制性条款的代码）；
3. 你的贡献中不包含第三方专有代码、未公开的厂商 SDK 片段，或违反其许可证的内容。

## 开发者原创声明（DCO）

每个提交需包含 `Signed-off-by` 行，声明你有权按上述条款提交该贡献（[Developer Certificate of Origin 1.1](https://developercertificate.org/)）：

```bash
git commit -s -m "feat: 加入血氧温度补偿"
```

生成的提交信息末尾应形如：

```
Signed-off-by: 你的名字 <你的邮箱>
```

未签署的提交会被要求补签。

## 第三方代码与资源

- 引入新依赖时，请同时在 `THIRD_PARTY_NOTICES.md` 登记：名称、版本、许可证、上游地址。
- ESP-IDF 组件请通过 `idf_component.yml` 声明并由组件管理器拉取，不要把第三方源码直接拷进 `components/`。
- 新增图片来源请确认可商用/可再分发，并在 PR 描述中注明出处。

## 源码文件头

新建源码文件请带 SPDX 头（C/C++）：

```c
/*
 * SPDX-FileCopyrightText: 2026 <你的名字>
 * SPDX-License-Identifier: Apache-2.0
 *
 * ESP32-MonitorWatch - Seizure pre-symptom monitoring watch firmware.
 * See LICENSE in the project root for the full license text.
 */
```

Dart：

```dart
// SPDX-FileCopyrightText: 2026 <你的名字>
// SPDX-License-Identifier: Apache-2.0
```

## 提交前自查

- [ ] `idf.py build` 通过（不要在 PR 中提交 `build/`、`managed_components/`、`sdkconfig`）
- [ ] 未新增硬编码的 WiFi / MQTT / OneNET 密钥
- [ ] 若改动依赖，`dependencies.lock` 与 `THIRD_PARTY_NOTICES.md` 已同步
- [ ] 提交信息含 `Signed-off-by`

## 安全与隐私

发现密钥泄露或安全漏洞请勿公开提 Issue，直接邮件联系维护者：currygood <2294008910@qq.com>。
