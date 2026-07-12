import 'dart:convert';

import 'package:dio/dio.dart';

import '../models/monitoring_snapshot.dart';
import '../utils/onenet_token_generator.dart';

class OneNetApiService {
  OneNetApiService({Dio? dio}) : _dio = dio ?? Dio();

  final Dio _dio;

  Future<MonitoringSnapshot> fetchLatestData({
    required String productId,
    required String accessKey,
    String? deviceName,
  }) async {
    print('🔍 [API] Starting request...');
    print('   Product ID: $productId');
    print('   Device Name: $deviceName');
    print('   AccessKey length: ${accessKey.length}');
    print('   AccessKey (first 20): ${accessKey.substring(0, accessKey.length > 20 ? 20 : accessKey.length)}...');
    print('   Has + char: ${accessKey.contains('+')}');

    try {
      final token = OnenetTokenGenerator.generate(
        productId: productId,
        deviceName: deviceName,
        accessKey: accessKey,
        method: SigMethod.sha256,
        expiry:
            DateTime.now()
                .add(const Duration(minutes: 10))
                .millisecondsSinceEpoch ~/
            1000,
      );

      print('✅ [API] Token生成成功');

      final response = await _dio.get(
        'https://iot-api.heclouds.com/thingmodel/query-device-property',
        queryParameters: {'product_id': productId, 'device_name': deviceName},
        options: Options(
          headers: {
            'Authorization': token,
            'Accept': 'application/json, text/plain, */*',
          },
        ),
      );

      print('📡 [API] HTTP状态码: ${response.statusCode}');
      print('📄 [API] 响应头: ${response.headers.map}');
      print('📦 [API] 响应数据类型: ${response.data.runtimeType}');
      print('📋 [API] 响应数据内容: ${response.data}');

      if (response.statusCode != 200) {
        print('❌ [API] 请求失败! 完整响应:');
        print('   Status: ${response.statusCode}');
        print('   Data: ${response.data}');
        print('   Headers: ${response.headers.map}');
        throw DioException(
          requestOptions: response.requestOptions,
          response: response,
          error: 'HTTP ${response.statusCode}: ${response.data}',
        );
      }

      print('✅ [API] 请求成功, 解析数据...');
      final body = response.data;
      print('📄 [API] 原始响应: $body');

      if (body is String) {
        return MonitoringSnapshot.fromJson(
          jsonDecode(body) as Map<String, dynamic>,
        );
      }
      if (body is Map<String, dynamic>) {
        return MonitoringSnapshot.fromJson(body);
      }
      throw FormatException('Unexpected response payload');
    } catch (e) {
      print('💥 [API] 异常捕获: $e');
      rethrow;
    }
  }
}