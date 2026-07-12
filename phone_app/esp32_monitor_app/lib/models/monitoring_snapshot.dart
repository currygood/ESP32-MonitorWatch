class MonitoringSnapshot {
  MonitoringSnapshot({
    this.heartRate,
    this.oxygenSaturation,
    this.seizureRiskLevel,
    this.abnormalMotionDetected = false,
    DateTime? timestamp,
  }) : timestamp = timestamp ?? DateTime.now();

  final int? heartRate;
  final int? oxygenSaturation;
  final int? seizureRiskLevel;
  final bool abnormalMotionDetected;
  final DateTime timestamp;

  bool get isOffline {
    return DateTime.now().difference(timestamp).inSeconds > 30;
  }

  factory MonitoringSnapshot.fromJson(Map<String, dynamic> json) {
    final dataList = json['data'] as List<dynamic>? ?? const <dynamic>[];
    int? heartRate;
    int? oxygenSaturation;
    int? seizureRiskLevel;
    bool abnormalMotionDetected = false;

    for (final item in dataList) {
      final map = item as Map<String, dynamic>? ?? const <String, dynamic>{};
      final identifier = map['identifier']?.toString() ?? '';
      final value = map['value'];

      if (identifier == 'heart_rate') {
        if (value is num) {
          heartRate = value.toInt();
        } else if (value is String) {
          heartRate = int.tryParse(value);
        }
      } else if (identifier == 'oxygen_saturation') {
        if (value is num) {
          oxygenSaturation = value.toInt();
        } else if (value is String) {
          oxygenSaturation = int.tryParse(value);
        }
      } else if (identifier == 'seizure_risk_level') {
        if (value is num) {
          seizureRiskLevel = value.toInt();
        } else if (value is String) {
          seizureRiskLevel = int.tryParse(value);
        }
      } else if (identifier == 'abnormal_motion_detected') {
        if (value is bool) {
          abnormalMotionDetected = value;
        } else if (value is String) {
          abnormalMotionDetected = value.toLowerCase() == 'true';
        }
      }
    }

    final timestampValue = json['timestamp'] ?? json['time'];
    DateTime timestamp = DateTime.now();
    if (timestampValue is int) {
      timestamp = DateTime.fromMillisecondsSinceEpoch(timestampValue);
    } else if (timestampValue is String) {
      timestamp = DateTime.tryParse(timestampValue) ?? DateTime.now();
    }

    return MonitoringSnapshot(
      heartRate: heartRate,
      oxygenSaturation: oxygenSaturation,
      seizureRiskLevel: seizureRiskLevel,
      abnormalMotionDetected: abnormalMotionDetected,
      timestamp: timestamp,
    );
  }
}