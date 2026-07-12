import 'package:flutter_test/flutter_test.dart';
import 'package:esp32_monitor_app/utils/onenet_token_generator.dart';

void main() {
  group('OnenetTokenGenerator', () {
    test('builds a device token with expected query parameters', () {
      final token = OnenetTokenGenerator.generate(
        productId: 'test-product',
        deviceName: 'watch-001',
        accessKey: 'dGVzdC1rZXk=',
        method: SigMethod.sha256,
        expiry: 1_700_000_000,
      );

      expect(token, contains('version=2018-10-31'));
      expect(
        token,
        contains('res=products%2Ftest-product%2Fdevices%2Fwatch-001'),
      );
      expect(token, contains('method=sha256'));
      expect(token, contains('et='));
      expect(token, contains('sign='));
    });
  });
}
