// SPDX-FileCopyrightText: 2026 currygood
// SPDX-License-Identifier: Apache-2.0
//
// ESP32-MonitorWatch companion app for the seizure monitoring watch.
// See LICENSE in the project root for the full license text.

import 'package:flutter/material.dart';

import 'pages/config_page.dart';
import 'pages/monitor_page.dart';
import 'services/config_storage.dart';

void main() {
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'ESP32 监测看护',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: Colors.indigo),
        useMaterial3: true,
      ),
      home: FutureBuilder(
        future: _checkConfig(),
        builder: (context, snapshot) {
          if (snapshot.connectionState == ConnectionState.waiting) {
            return const Scaffold(
              body: Center(child: CircularProgressIndicator()),
            );
          }

          final hasConfig = snapshot.data ?? false;
          
          if (hasConfig) {
            return const MonitorPage();
          } else {
            return const ConfigPage();
          }
        },
      ),
    );
  }

  static Future<bool> _checkConfig() async {
    final configStorage = ConfigStorage();
    final config = await configStorage.load();
    return config != null;
  }
}