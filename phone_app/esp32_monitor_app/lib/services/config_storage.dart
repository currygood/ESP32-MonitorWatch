import 'dart:convert';

import 'package:shared_preferences/shared_preferences.dart';

import '../models/device_config.dart';

class ConfigStorage {
  static const _key = 'device_config';

  Future<void> save(DeviceConfig config) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_key, jsonEncode(config.toJson()));
  }

  Future<DeviceConfig?> load() async {
    final prefs = await SharedPreferences.getInstance();
    final encoded = prefs.getString(_key);
    if (encoded == null || encoded.isEmpty) {
      return null;
    }
    return DeviceConfig.fromJson(jsonDecode(encoded) as Map<String, dynamic>);
  }
}
