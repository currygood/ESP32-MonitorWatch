// SPDX-FileCopyrightText: 2026 currygood
// SPDX-License-Identifier: Apache-2.0
//
// ESP32-MonitorWatch companion app for the seizure monitoring watch.
// See LICENSE in the project root for the full license text.

import 'dart:convert';

import 'package:crypto/crypto.dart';

enum SigMethod { md5, sha1, sha256 }

class OnenetTokenGenerator {
  static String generate({
    required String productId,
    required String? deviceName,
    required String accessKey,
    required SigMethod method,
    required int expiry,
  }) {
    final version = '2018-10-31';
    final methodName = method.name;
    final resource = deviceName == null || deviceName.isEmpty
        ? 'products/$productId'
        : 'products/$productId/devices/$deviceName';
    final encodedResource = Uri.encodeComponent(resource);

    print('🔐 [Token] Generating token...');
    print('   Product ID: $productId');
    print('   Device Name: $deviceName');
    print('   Resource: $resource');
    print('   Access Key (first 10 chars): ${accessKey.substring(0, accessKey.length > 10 ? 10 : accessKey.length)}...');
    print('   Access Key length: ${accessKey.length}');
    print('   Method: $methodName');
    print('   Expiry: $expiry (${DateTime.fromMillisecondsSinceEpoch(expiry * 1000)})');

    final StringBuffer buffer = StringBuffer('version=$version')
      ..write('&res=$encodedResource')
      ..write('&et=$expiry')
      ..write('&method=$methodName');

    final signatureSource = '$expiry\n$methodName\n$resource\n$version';
    print('📝 [Token] Signature source string prepared');

    try {
      final keyBytes = base64Decode(accessKey);
      print('✅ [Token] AccessKey base64 decoded successfully (length: ${keyBytes.length} bytes)');
      final contentBytes = utf8.encode(signatureSource);
      final signatureBytes = _hmac(method, keyBytes, contentBytes);
      final encodedSignature = base64Encode(
        signatureBytes,
      ).replaceAll('+', '%2B').replaceAll('/', '%2F').replaceAll('=', '%3D');

      buffer.write('&sign=$encodedSignature');
      final token = buffer.toString();

      print('✅ [Token] Token generated successfully!');
      print('🎫 [Token] Token (first 50 chars): ${token.substring(0, token.length > 50 ? 50 : token.length)}...');

      return token;
    } catch (e) {
      print('❌ [Token] Error generating token: $e');
      print('❌ [Token] This usually means the AccessKey is not valid base64 format!');
      rethrow;
    }
  }

  static List<int> _hmac(SigMethod method, List<int> key, List<int> content) {
    switch (method) {
      case SigMethod.md5:
        return Hmac(md5, key).convert(content).bytes;
      case SigMethod.sha1:
        return Hmac(sha1, key).convert(content).bytes;
      case SigMethod.sha256:
        return Hmac(sha256, key).convert(content).bytes;
    }
  }
}