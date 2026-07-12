import 'package:flutter_local_notifications/flutter_local_notifications.dart';

class NotificationService {
  NotificationService._();

  static final NotificationService instance = NotificationService._();

  final FlutterLocalNotificationsPlugin _plugin =
      FlutterLocalNotificationsPlugin();

  Future<void> initialize() async {
    const AndroidInitializationSettings androidSettings =
        AndroidInitializationSettings('@mipmap/ic_launcher');
    const DarwinInitializationSettings iosSettings =
        DarwinInitializationSettings();
    const InitializationSettings settings = InitializationSettings(
      android: androidSettings,
      iOS: iosSettings,
    );

    await _plugin.initialize(
      settings: settings,
    );

    final androidPlugin = _plugin
        .resolvePlatformSpecificImplementation<
          AndroidFlutterLocalNotificationsPlugin
        >();
    await androidPlugin?.createNotificationChannel(
      const AndroidNotificationChannel(
        'monitor_alarm_channel',
        '监测报警',
        description: '用于在异常情况时提醒用户',
        importance: Importance.max,
      ),
    );
    await androidPlugin?.requestNotificationsPermission();
  }

  Future<void> showAlarm({required String title, required String body}) async {
    await _plugin.show(
      id: DateTime.now().millisecondsSinceEpoch.remainder(100000),
      title: title,
      body: body,
      payload: null,
      notificationDetails: NotificationDetails(
        android: AndroidNotificationDetails(
          'monitor_alarm_channel',
          '监测报警',
          channelDescription: '用于在异常情况时提醒用户',
          importance: Importance.max,
          priority: Priority.high,
          ticker: '监测报警',
        ),
        iOS: const DarwinNotificationDetails(),
      ),
    );
  }
}
