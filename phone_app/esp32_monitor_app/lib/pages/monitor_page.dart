import 'dart:math' as math;
import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/animation.dart';

import '../models/device_config.dart';
import '../models/monitoring_snapshot.dart';
import '../services/config_storage.dart';
import '../services/notification_service.dart';
import '../services/onenet_api_service.dart';
import 'config_page.dart';

class MonitorPage extends StatefulWidget {
  const MonitorPage({super.key});

  @override
  State<MonitorPage> createState() => _MonitorPageState();
}

class _MonitorPageState extends State<MonitorPage>
    with SingleTickerProviderStateMixin {
  final ConfigStorage _configStorage = ConfigStorage();
  final OneNetApiService _apiService = OneNetApiService();
  final NotificationService _notificationService = NotificationService.instance;
  DeviceConfig? _config;
  MonitoringSnapshot? _snapshot;
  bool _isLoading = false;
  String _status = '等待配置';
  Timer? _timer;
  late AnimationController _refreshAnimationController;

  @override
  void initState() {
    super.initState();
    _refreshAnimationController = AnimationController(
      duration: const Duration(milliseconds: 1000),
      vsync: this,
    );
    _initialize();
  }

  Future<void> _initialize() async {
    await _notificationService.initialize();
    final config = await _configStorage.load();
    if (!mounted) {
      return;
    }
    setState(() {
      _config = config;
      _status = config == null ? '请先保存设备配置' : '已加载配置';
    });
    if (config != null) {
      _startPolling();
      _refreshData();
    }
  }

  void _startPolling() {
    _timer?.cancel();
    _timer = Timer.periodic(const Duration(seconds: 3), (_) => _refreshData());
  }

  Future<void> _refreshData() async {
    if (_config == null || _isLoading) {
      return;
    }
    
    setState(() {
      _isLoading = true;
      _status = '正在获取数据...';
    });
    
    _refreshAnimationController.repeat();

    try {
      final snapshot = await _apiService.fetchLatestData(
        productId: _config!.mqttUsername,
        accessKey: _config!.mqttDeviceKey,
        deviceName: _config!.mqttClientId,
      );
      
      if (!mounted) return;
      
      setState(() {
        _snapshot = snapshot;
        _status = snapshot.isOffline ? '设备离线' : '正常';
        _isLoading = false;
      });
      
      _refreshAnimationController.reset();
      
      if (snapshot.abnormalMotionDetected) {
        await _notificationService.showAlarm(
          title: '异常运动提醒',
          body: '检测到异常运动，请尽快查看设备状态。',
        );
      }
    } catch (e) {
      print('❌ [MonitorPage] 数据刷新失败: $e');
      if (!mounted) return;
      
      String errorMsg = e.toString();
      if (errorMsg.length > 200) {
        errorMsg = errorMsg.substring(0, 200) + '...(truncated)';
      }
      
      setState(() {
        _status = 'Error: $errorMsg';
        _isLoading = false;
      });
      
      _refreshAnimationController.reset();
    }
  }

  @override
  void dispose() {
    _timer?.cancel();
    _refreshAnimationController.dispose();
    super.dispose();
  }

  Future<void> _openSettings() async {
    final result = await Navigator.of(context).push<bool>(
      MaterialPageRoute(
        builder: (_) => ConfigPage(existingConfig: _config),
      ),
    );

    if (result == true && mounted) {
      await _initialize();
    }
  }

  @override
  Widget build(BuildContext context) {
    final snapshot = _snapshot;
    final size = MediaQuery.of(context).size;

    return Scaffold(
      appBar: AppBar(
        title: const Text('癫痫监测'),
        actions: [
          IconButton(
            icon: const Icon(Icons.settings),
            tooltip: '设置',
            onPressed: _openSettings,
          ),
        ],
      ),
      body: RefreshIndicator(
        onRefresh: _refreshData,
        child: SingleChildScrollView(
          physics: const AlwaysScrollableScrollPhysics(),
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: [
                  Text(
                    '状态: $_status',
                    style: Theme.of(context).textTheme.titleMedium?.copyWith(
                      color: _status.contains('Error') || _status.contains('失败')
                          ? Colors.red
                          : Colors.black87,
                    ),
                  ),
                  if (_isLoading)
                    SizedBox(
                      width: 20,
                      height: 20,
                      child: CircularProgressIndicator(
                        strokeWidth: 2,
                        valueColor: AlwaysStoppedAnimation<Color>(
                          Theme.of(context).primaryColor,
                        ),
                      ),
                    ),
                ],
              ),
              const SizedBox(height: 24),
              
              if (_config == null)
                Center(
                  child: Padding(
                    padding: const EdgeInsets.only(top: 100),
                    child: Column(
                      children: [
                        Icon(Icons.settings_remote, size: 64, color: Colors.grey[400]),
                        const SizedBox(height: 16),
                        Text(
                          '请先在主界面配置设备参数',
                          style: TextStyle(color: Colors.grey[600], fontSize: 16),
                        ),
                      ],
                    ),
                  ),
                )
              else
                Column(
                  children: [
                    Row(
                      mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                      children: [
                        SizedBox(
                          width: size.width * 0.42,
                          height: size.width * 0.42,
                          child: _GaugeWidget(
                            title: '心率',
                            value: snapshot?.heartRate?.toDouble() ?? 0,
                            minValue: 0,
                            maxValue: 200,
                            unit: 'bpm',
                            color: snapshot?.heartRate != null &&
                                snapshot!.heartRate! > _config!.alarmHeartRateLimit
                                ? Colors.redAccent
                                : Colors.blue,
                            warningValue: _config!.alarmHeartRateLimit.toDouble(),
                          ),
                        ),
                        SizedBox(
                          width: size.width * 0.42,
                          height: size.width * 0.42,
                          child: _GaugeWidget(
                            title: '血氧',
                            value: snapshot?.oxygenSaturation?.toDouble() ?? 0,
                            minValue: 0,
                            maxValue: 100,
                            unit: '%',
                            color: Colors.teal,
                            warningValue: 90,
                          ),
                        ),
                      ],
                    ),
                    
                    const SizedBox(height: 24),
                    
                    _MetricCard(
                      title: '风险等级',
                      value: snapshot?.seizureRiskLevel?.toString() ?? '--',
                      unit: '',
                      color: snapshot?.seizureRiskLevel != null
                          ? _riskColor(snapshot!.seizureRiskLevel!)
                          : Colors.grey,
                      icon: Icons.warning_amber_rounded,
                    ),
                  ],
                ),
              
              const SizedBox(height: 32),
              
              ElevatedButton.icon(
                onPressed: _isLoading ? null : _refreshData,
                icon: AnimatedBuilder(
                  animation: _refreshAnimationController,
                  builder: (context, child) {
                    return Transform.rotate(
                      angle: _refreshAnimationController.value * 2 * math.pi,
                      child: child,
                    );
                  },
                  child: const Icon(Icons.refresh),
                ),
                label: Text(_isLoading ? '获取中...' : '立即刷新'),
                style: ElevatedButton.styleFrom(
                  minimumSize: Size(double.infinity, 48),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Color _riskColor(int level) {
    if (level >= 70) return Colors.redAccent;
    if (level >= 31) return Colors.orange;
    return Colors.green;
  }
}

class _GaugeWidget extends StatelessWidget {
  final String title;
  final double value;
  final double minValue;
  final double maxValue;
  final String unit;
  final Color color;
  final double? warningValue;

  const _GaugeWidget({
    required this.title,
    required this.value,
    required this.minValue,
    required this.maxValue,
    required this.unit,
    required this.color,
    this.warningValue,
  });

  @override
  Widget build(BuildContext context) {
    return Card(
      elevation: 4,
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
      child: Padding(
        padding: const EdgeInsets.all(12),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Text(
              title,
              style: TextStyle(
                fontSize: 14,
                fontWeight: FontWeight.w500,
                color: Colors.grey[700],
              ),
            ),
            const SizedBox(height: 8),
            Expanded(
              child: CustomPaint(
                size: Size.infinite,
                painter: _GaugePainter(
                  value: value,
                  minValue: minValue,
                  maxValue: maxValue,
                  color: color,
                  warningValue: warningValue,
                ),
              ),
            ),
            const SizedBox(height: 8),
            Text(
              '${value.toInt()} $unit',
              style: TextStyle(
                fontSize: 18,
                fontWeight: FontWeight.bold,
                color: color,
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _GaugePainter extends CustomPainter {
  final double value;
  final double minValue;
  final double maxValue;
  final Color color;
  final double? warningValue;

  _GaugePainter({
    required this.value,
    required this.minValue,
    required this.maxValue,
    required this.color,
    this.warningValue,
  });

  @override
  void paint(Canvas canvas, Size size) {
    final center = Offset(size.width / 2, size.height * 0.85);
    final radius = math.min(size.width, size.height) * 0.38;

    final backgroundPaint = Paint()
      ..color = Colors.grey[200]!
      ..style = PaintingStyle.stroke
      ..strokeWidth = 12
      ..strokeCap = StrokeCap.round;

    canvas.drawArc(
      Rect.fromCircle(center: center, radius: radius),
      math.pi * 0.75,
      math.pi * 1.5,
      false,
      backgroundPaint,
    );

    if (warningValue != null) {
      final warningPaint = Paint()
        ..color = Colors.orange.withOpacity(0.3)
        ..style = PaintingStyle.stroke
        ..strokeWidth = 12
        ..strokeCap = StrokeCap.round;

      final warningAngle = ((warningValue! - minValue) / (maxValue - minValue)) * math.pi * 1.5 + math.pi * 0.75;
      canvas.drawArc(
        Rect.fromCircle(center: center, radius: radius),
        warningAngle,
        math.pi * 1.5 - (warningAngle - math.pi * 0.75),
        false,
        warningPaint,
      );
    }

    final foregroundPaint = Paint()
      ..color = color
      ..style = PaintingStyle.stroke
      ..strokeWidth = 12
      ..strokeCap = StrokeCap.round;

    final currentValue = value.clamp(minValue, maxValue);
    final sweepAngle = ((currentValue - minValue) / (maxValue - minValue)) * math.pi * 1.5;

    canvas.drawArc(
      Rect.fromCircle(center: center, radius: radius),
      math.pi * 0.75,
      sweepAngle,
      false,
      foregroundPaint,
    );

    final pointerLength = radius * 0.7;
    final pointerAngle = math.pi * 0.75 + sweepAngle;
    final pointerEnd = Offset(
      center.dx + pointerLength * math.cos(pointerAngle),
      center.dy + pointerLength * math.sin(pointerAngle),
    );

    final pointerPaint = Paint()
      ..color = color
      ..style = PaintingStyle.stroke
      ..strokeWidth = 4
      ..strokeCap = StrokeCap.round;

    canvas.drawLine(center, pointerEnd, pointerPaint);

    final centerDotPaint = Paint()..color = color;
    canvas.drawCircle(center, 6, centerDotPaint);

    final tickPaint = Paint()
      ..color = Colors.grey[400]!
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2;

    for (int i = 0; i <= 10; i++) {
      final angle = math.pi * 0.75 + (i / 10) * math.pi * 1.5;
      final innerRadius = radius * 0.85;
      final outerRadius = radius * 1.05;
      
      final start = Offset(
        center.dx + innerRadius * math.cos(angle),
        center.dy + innerRadius * math.sin(angle),
      );
      final end = Offset(
        center.dx + outerRadius * math.cos(angle),
        center.dy + outerRadius * math.sin(angle),
      );
      
      canvas.drawLine(start, end, tickPaint);
    }
  }

  @override
  bool shouldRepaint(covariant _GaugePainter oldDelegate) {
    return value != oldDelegate.value ||
           color != oldDelegate.color ||
           warningValue != oldDelegate.warningValue;
  }
}

class _MetricCard extends StatelessWidget {
  const _MetricCard({
    required this.title,
    required this.value,
    required this.unit,
    required this.color,
    required this.icon,
  });

  final String title;
  final String value;
  final String unit;
  final Color color;
  final IconData icon;

  @override
  Widget build(BuildContext context) {
    return Card(
      elevation: 3,
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
      child: ListTile(
        leading: Container(
          padding: const EdgeInsets.all(8),
          decoration: BoxDecoration(
            color: color.withOpacity(0.1),
            borderRadius: BorderRadius.circular(8),
          ),
          child: Icon(icon, color: color),
        ),
        title: Text(
          title,
          style: TextStyle(fontWeight: FontWeight.w500, color: Colors.grey[700]),
        ),
        trailing: Text(
          '$value $unit',
          style: TextStyle(
            fontSize: 24,
            fontWeight: FontWeight.bold,
            color: color,
          ),
        ),
      ),
    );
  }
}