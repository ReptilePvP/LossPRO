#include "WiFiManager.h"

WiFiManager::WiFiManager() :
    _state(WiFiState::WIFI_DISABLED),
    _enabled(false),
    _manualDisconnect(false),
    _initialized(false),
    _scanInProgress(false),
    _lastConnectionAttempt(0),
    _scanStartTime(0),
    _connectionAttempts(0) {
}

WiFiManager::~WiFiManager() {
    if (_enabled) disconnect(true);
}

void WiFiManager::begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true); // Enable power saving by default
    loadSavedNetworks();
    _state = WiFiState::WIFI_DISCONNECTED;
    _enabled = true;
    _initialized = true;
    notifyStatus("WiFi initialized");
}

void WiFiManager::setStatusCallback(StatusCallback cb) {
    _statusCallback = cb;
}

void WiFiManager::setScanCallback(ScanCallback cb) {
    _scanCallback = cb;
}

void WiFiManager::setEnabled(bool enabled) {
    if (_enabled == enabled) return;
    _enabled = enabled;
    if (enabled) {
        WiFi.mode(WIFI_STA);
        _state = WiFiState::WIFI_DISCONNECTED;
        connectToBestNetwork();
    } else {
        disconnect(true);
        WiFi.mode(WIFI_OFF);
        _state = WiFiState::WIFI_DISABLED;
        notifyStatus("WiFi disabled");
    }
}

bool WiFiManager::connect(const String& ssid, const String& password, bool save, int priority) {
    if (!_enabled) return false;
    _state = WiFiState::WIFI_CONNECTING;
    _connectingSSID = ssid;
    _connectingPassword = password;
    _lastConnectionAttempt = millis();
    _connectionAttempts = 1;
    _manualDisconnect = false;
    WiFi.disconnect();
    WiFi.begin(ssid.c_str(), password.c_str());
    notifyStatus("Connecting to " + ssid);
    if (save) addNetwork(ssid, password, priority);
    return true;
}

bool WiFiManager::connectToBestNetwork() {
    if (!_enabled || _savedNetworks.empty()) return false;
    sortNetworksByPriority(_savedNetworks);
    for (const auto& network : _savedNetworks) {
        if (connect(network.ssid, network.password, false, network.priority)) return true;
    }
    return false;
}

void WiFiManager::disconnect(bool manual) {
    WiFi.disconnect();
    _state = WiFiState::WIFI_DISCONNECTED;
    _manualDisconnect = manual;
    notifyStatus("Disconnected");
}

bool WiFiManager::startScan() {
    if (!_enabled || _scanInProgress) return false;
    _state = WiFiState::WIFI_SCANNING;
    _scanInProgress = true;
    _scanStartTime = millis();
    _scanResults.clear();
    WiFi.scanDelete();
    WiFi.scanNetworks(true); // Async scan
    notifyStatus("Scanning networks...");
    return true;
}

void WiFiManager::update() {
    if (!_enabled) return;
    updateState();
}

void WiFiManager::updateState() {
    switch (_state) {
        case WiFiState::WIFI_SCANNING: {
            int scanStatus = WiFi.scanComplete();
            if (scanStatus >= 0) {
                _scanResults.clear();
                for (int i = 0; i < scanStatus; i++) {
                    NetworkInfo info;
                    info.ssid = WiFi.SSID(i);
                    info.rssi = WiFi.RSSI(i);
                    info.encryptionType = WiFi.encryptionType(i);
                    info.saved = findNetwork(info.ssid, _savedNetworks) >= 0;
                    info.connected = (WiFi.status() == WL_CONNECTED && WiFi.SSID() == info.ssid);
                    info.priority = info.saved ? _savedNetworks[findNetwork(info.ssid, _savedNetworks)].priority : 0;
                    _scanResults.push_back(info);
                }
                sortNetworksByPriority(_scanResults);
                _scanInProgress = false;
                _state = isConnected() ? WiFiState::WIFI_CONNECTED : WiFiState::WIFI_DISCONNECTED;
                notifyStatus("Scan complete: " + String(_scanResults.size()) + " networks found");
                if (_scanCallback) _scanCallback(_scanResults);
            } else if (millis() - _scanStartTime > SCAN_TIMEOUT) {
                WiFi.scanDelete();
                _scanInProgress = false;
                _state = isConnected() ? WiFiState::WIFI_CONNECTED : WiFiState::WIFI_DISCONNECTED;
                notifyStatus("Scan timed out");
            }
            break;
        }
        case WiFiState::WIFI_CONNECTING: {
            if (WiFi.status() == WL_CONNECTED) {
                _state = WiFiState::WIFI_CONNECTED;
                _connectionAttempts = 0;
                notifyStatus("Connected to " + _connectingSSID);
            } else if (millis() - _lastConnectionAttempt > CONNECTION_TIMEOUT) {
                if (_connectionAttempts < MAX_CONNECTION_ATTEMPTS) {
                    _lastConnectionAttempt = millis();
                    _connectionAttempts++;
                    WiFi.begin(_connectingSSID.c_str(), _connectingPassword.c_str());
                    notifyStatus("Retrying connection (" + String(_connectionAttempts) + "/" + String(MAX_CONNECTION_ATTEMPTS) + ")");
                } else {
                    _state = WiFiState::WIFI_DISCONNECTED;
                    notifyStatus("Connection failed to " + _connectingSSID);
                }
            }
            break;
        }
        case WiFiState::WIFI_CONNECTED: {
            if (WiFi.status() != WL_CONNECTED) {
                _state = WiFiState::WIFI_DISCONNECTED;
                if (!_manualDisconnect) connectToBestNetwork();
            }
            break;
        }
        case WiFiState::WIFI_DISCONNECTED: {
            if (!_manualDisconnect && millis() - _lastConnectionAttempt > RECONNECT_INTERVAL) {
                connectToBestNetwork();
            }
            break;
        }
        default:
            break;
    }
}

bool WiFiManager::addNetwork(const String& ssid, const String& password, int priority) {
    int idx = findNetwork(ssid, _savedNetworks);
    if (idx >= 0) {
        _savedNetworks[idx].password = password;
        _savedNetworks[idx].priority = priority;
    } else {
        if (_savedNetworks.size() >= MAX_SAVED_NETWORKS) {
            sortNetworksByPriority(_savedNetworks);
            _savedNetworks.pop_back();
        }
        NetworkInfo info{ssid, password, 0, WIFI_AUTH_OPEN, true, false, priority};
        _savedNetworks.push_back(info);
    }
    saveNetworks();
    return true;
}

bool WiFiManager::removeNetwork(const String& ssid) {
    int idx = findNetwork(ssid, _savedNetworks);
    if (idx < 0) return false;
    _savedNetworks.erase(_savedNetworks.begin() + idx);
    saveNetworks();
    return true;
}

bool WiFiManager::setNetworkPriority(const String& ssid, int priority) {
    int idx = findNetwork(ssid, _savedNetworks);
    if (idx < 0) return false;
    _savedNetworks[idx].priority = priority;
    saveNetworks();
    return true;
}

void WiFiManager::loadSavedNetworks() {
    _preferences.begin("wifi_config", false);
    int num = _preferences.getInt("numNetworks", 0);
    for (int i = 0; i < min(num, MAX_SAVED_NETWORKS); i++) {
        char ssidKey[16], passKey[16], prioKey[16];
        sprintf(ssidKey, "ssid%d", i);
        sprintf(passKey, "pass%d", i);
        sprintf(prioKey, "prio%d", i);
        String ssid = _preferences.getString(ssidKey, "");
        String pass = _preferences.getString(passKey, "");
        int prio = _preferences.getInt(prioKey, 0);
        if (ssid.length() > 0) {
            NetworkInfo info{ssid, pass, 0, WIFI_AUTH_OPEN, true, false, prio};
            _savedNetworks.push_back(info);
        }
    }
    _preferences.end();
    sortNetworksByPriority(_savedNetworks);
}

void WiFiManager::saveNetworks() {
    _preferences.begin("wifi_config", false);
    _preferences.putInt("numNetworks", _savedNetworks.size());
    for (size_t i = 0; i < _savedNetworks.size(); i++) {
        char ssidKey[16], passKey[16], prioKey[16];
        sprintf(ssidKey, "ssid%d", i);
        sprintf(passKey, "pass%d", i);
        sprintf(prioKey, "prio%d", i);
        _preferences.putString(ssidKey, _savedNetworks[i].ssid);
        _preferences.putString(passKey, _savedNetworks[i].password);
        _preferences.putInt(prioKey, _savedNetworks[i].priority);
    }
    _preferences.end();
}

void WiFiManager::notifyStatus(const String& message) {
    if (_statusCallback) _statusCallback(_state, message);
}

void WiFiManager::sortNetworksByPriority(std::vector<NetworkInfo>& networks) {
    std::sort(networks.begin(), networks.end(), 
              [](const NetworkInfo& a, const NetworkInfo& b) {
                  return a.priority > b.priority || (a.priority == b.priority && a.rssi > b.rssi);
              });
}

int WiFiManager::findNetwork(const String& ssid, const std::vector<NetworkInfo>& networks) const {
    for (size_t i = 0; i < networks.size(); i++) {
        if (networks[i].ssid == ssid) return i;
    }
    return -1;
}

// Remaining getters
bool WiFiManager::isEnabled() const { return _enabled; }
bool WiFiManager::isInitialized() const { return _initialized; }
bool WiFiManager::isConnected() const { return WiFi.status() == WL_CONNECTED; }
String WiFiManager::getCurrentSSID() const { return WiFi.SSID(); }
int WiFiManager::getRSSI() const { return WiFi.RSSI(); }
String WiFiManager::getIPAddress() const { return WiFi.localIP().toString(); }
bool WiFiManager::isScanning() const { return _scanInProgress; }
std::vector<NetworkInfo> WiFiManager::getScanResults() const { return _scanResults; }
std::vector<NetworkInfo> WiFiManager::getSavedNetworks() const { return _savedNetworks; }
WiFiState WiFiManager::getState() const { return _state; }
String WiFiManager::getStateString() const {
    switch (_state) {
        case WiFiState::WIFI_DISABLED: return "Disabled";
        case WiFiState::WIFI_DISCONNECTED: return "Disconnected";
        case WiFiState::WIFI_CONNECTING: return "Connecting";
        case WiFiState::WIFI_CONNECTED: return "Connected";
        case WiFiState::WIFI_SCANNING: return "Scanning";
        default: return "Unknown";
    }
}