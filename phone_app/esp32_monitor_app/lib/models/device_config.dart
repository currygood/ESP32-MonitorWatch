// SPDX-FileCopyrightText: 2026 currygood
// SPDX-License-Identifier: Apache-2.0
//
// ESP32-MonitorWatch companion app for the seizure monitoring watch.
// See LICENSE in the project root for the full license text.

class DeviceConfig {
  const DeviceConfig({
    required this.mqttUsername,
    required this.mqttClientId,
    required this.mqttDeviceKey,
    required this.onenetProductAccessKey,
    this.alarmHeartRateLimit = 120,
  });

  final String mqttUsername;
  final String mqttClientId;
  final String mqttDeviceKey;
  final String onenetProductAccessKey;
  final int alarmHeartRateLimit;

  Map<String, dynamic> toJson() {
    return {
      'mqttUsername': mqttUsername,
      'mqttClientId': mqttClientId,
      'mqttDeviceKey': mqttDeviceKey,
      'onenetProductAccessKey': onenetProductAccessKey,
      'alarmHeartRateLimit': alarmHeartRateLimit,
    };
  }

  factory DeviceConfig.fromJson(Map<String, dynamic> json) {
    return DeviceConfig(
      mqttUsername: json['mqttUsername']?.toString() ?? '',
      mqttClientId: json['mqttClientId']?.toString() ?? '',
      mqttDeviceKey: json['mqttDeviceKey']?.toString() ?? '',
      onenetProductAccessKey: json['onenetProductAccessKey']?.toString() ?? '',
      alarmHeartRateLimit:
          int.tryParse(json['alarmHeartRateLimit']?.toString() ?? '') ?? 120,
    );
  }
}
