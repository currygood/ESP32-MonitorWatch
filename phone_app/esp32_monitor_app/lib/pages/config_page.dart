import 'package:flutter/material.dart';

import '../models/device_config.dart';
import '../services/config_storage.dart';
import 'monitor_page.dart';

class ConfigPage extends StatefulWidget {
  final DeviceConfig? existingConfig;

  const ConfigPage({super.key, this.existingConfig});

  @override
  State<ConfigPage> createState() => _ConfigPageState();
}

class _ConfigPageState extends State<ConfigPage> {
  final _formKey = GlobalKey<FormState>();
  final _configStorage = ConfigStorage();
  final TextEditingController _mqttUsernameController = TextEditingController();
  final TextEditingController _mqttClientIdController = TextEditingController();
  final TextEditingController _mqttDeviceKeyController =
      TextEditingController();
  final TextEditingController _productAccessKeyController =
      TextEditingController();
  final TextEditingController _alarmLimitController = TextEditingController(
    text: '120',
  );

  bool _saving = false;

  @override
  void initState() {
    super.initState();
    _loadExistingConfig();
  }

  Future<void> _loadExistingConfig() async {
    final defaultConfig = DeviceConfig(
      mqttUsername: '1nF1D22kt0',
      mqttClientId: 'MyTest',
      mqttDeviceKey: 'cFJaTlc4UkNzbnhBdG5QajVuVko0U3JTMlFUZm5Sb2E=',
      onenetProductAccessKey: '3S1H19uDdRWLXVslxembo2+P0RWTXcXB56txJWnhA9c=',
      alarmHeartRateLimit: 120,
    );

    final config = widget.existingConfig ?? await _configStorage.load() ?? defaultConfig;
    if (!mounted) return;

    setState(() {
      _mqttUsernameController.text = config.mqttUsername;
      _mqttClientIdController.text = config.mqttClientId;
      _mqttDeviceKeyController.text = config.mqttDeviceKey;
      _productAccessKeyController.text = config.onenetProductAccessKey;
      _alarmLimitController.text = config.alarmHeartRateLimit.toString();
    });
  }

  @override
  void dispose() {
    _mqttUsernameController.dispose();
    _mqttClientIdController.dispose();
    _mqttDeviceKeyController.dispose();
    _productAccessKeyController.dispose();
    _alarmLimitController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('设备配置')),
      body: Padding(
        padding: const EdgeInsets.all(16),
        child: Form(
          key: _formKey,
          child: ListView(
            children: [
              TextFormField(
                controller: _mqttUsernameController,
                decoration: const InputDecoration(
                  labelText: 'MQTT Username / ProductID',
                ),
                validator: (value) =>
                    value == null || value.isEmpty ? '必填' : null,
              ),
              const SizedBox(height: 12),
              TextFormField(
                controller: _mqttClientIdController,
                decoration: const InputDecoration(
                  labelText: 'MQTT Client ID / DeviceName',
                ),
                validator: (value) =>
                    value == null || value.isEmpty ? '必填' : null,
              ),
              const SizedBox(height: 12),
              TextFormField(
                controller: _mqttDeviceKeyController,
                decoration: const InputDecoration(labelText: 'MQTT Device Key'),
                validator: (value) =>
                    value == null || value.isEmpty ? '必填' : null,
              ),
              const SizedBox(height: 12),
              TextFormField(
                controller: _productAccessKeyController,
                decoration: const InputDecoration(
                  labelText: 'OneNet Product Access Key',
                ),
                validator: (value) =>
                    value == null || value.isEmpty ? '必填' : null,
              ),
              const SizedBox(height: 12),
              TextFormField(
                controller: _alarmLimitController,
                decoration: const InputDecoration(labelText: '报警心率上限'),
                keyboardType: TextInputType.number,
              ),
              const SizedBox(height: 20),
              ElevatedButton.icon(
                onPressed: _saving ? null : _saveConfig,
                icon: const Icon(Icons.save),
                label: Text(widget.existingConfig != null ? '保存配置' : '保存配置并进入监控'),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Future<void> _saveConfig() async {
    if (!_formKey.currentState!.validate()) {
      return;
    }
    setState(() {
      _saving = true;
    });
    final config = DeviceConfig(
      mqttUsername: _mqttUsernameController.text.trim(),
      mqttClientId: _mqttClientIdController.text.trim(),
      mqttDeviceKey: _mqttDeviceKeyController.text.trim(),
      onenetProductAccessKey: _productAccessKeyController.text.trim(),
      alarmHeartRateLimit:
          int.tryParse(_alarmLimitController.text.trim()) ?? 120,
    );
    await _configStorage.save(config);
    if (!mounted) {
      return;
    }

    if (widget.existingConfig != null) {
      Navigator.of(context).pop(true);
    } else {
      Navigator.of(
        context,
      ).pushReplacement(MaterialPageRoute(builder: (_) => const MonitorPage()));
    }

    if (mounted) {
      setState(() {
        _saving = false;
      });
    }
  }
}