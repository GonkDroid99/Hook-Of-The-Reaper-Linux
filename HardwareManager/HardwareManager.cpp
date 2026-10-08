#include "HardwareManager.h"
#include "../Global.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSerialPortInfo>
#include <QDebug>
#include <QRegularExpression>

#ifndef Q_OS_WIN
#include <hidapi/hidapi.h>
#endif

namespace
{
// HardwareManager uses strings in JSON and logs so the registry remains
// inspectable by a user. These helpers translate those strings to/from the
// strongly typed enums used by the C++ state machine.
QString enumName(HardwareConnectionState state)
{
    switch (state) {
    case HardwareConnectionState::Absent: return "Absent";
    case HardwareConnectionState::Detected: return "Detected";
    case HardwareConnectionState::Opening: return "Opening";
    case HardwareConnectionState::Connected: return "Connected";
    case HardwareConnectionState::Disconnected: return "Disconnected";
    case HardwareConnectionState::Recovering: return "Recovering";
    case HardwareConnectionState::NeedsSetup: return "NeedsSetup";
    case HardwareConnectionState::Disabled: return "Disabled";
    }
    return "Unknown";
}

QString assignmentName(HardwareAssignmentMode mode)
{
    switch (mode) {
    case HardwareAssignmentMode::Persistent: return "persistent";
    case HardwareAssignmentMode::PhysicalPort: return "physical-port";
    case HardwareAssignmentMode::FirstAvailable: return "first-available";
    case HardwareAssignmentMode::Manual: return "manual";
    }
    return "persistent";
}

QString normalizedUsbPart(const QString &value)
{
    // VID/PID values can arrive as "04B4", "04b4", or "0x04b4". Normalize
    // them before comparing or composing a persistent identity.
    return value.trimmed().toLower().remove(QRegularExpression("^0x"));
}

QString normalizedUsbParentPath(const QString &value)
{
    QString path = value.trimmed();
    if (path.isEmpty())
        return QString();

    // Linux serial sysfs paths end in e.g. 1-3:1.2 and HIDAPI paths use the
    // same interface notation (1-3:1.0). The part before the interface is
    // the physical USB parent shared by the serial and HID endpoints.
    const QString basename = QFileInfo(path).fileName();
    if (!basename.isEmpty())
        path = basename;
    const QRegularExpression interfaceSuffix(":\\d+\\.\\d+$");
    return path.remove(interfaceSuffix);
}

QString usbParentPathForFingerprint(const DeviceFingerprint &fingerprint)
{
    // Serial and HID endpoints often have different paths but share one USB
    // parent. Finding that parent is what lets the registry merge them into
    // one physical gun.
    if (fingerprint.transport == "serial")
    {
        const QString portName = QFileInfo(fingerprint.path).fileName();
        const QString sysfsDevice = QFileInfo(QString("/sys/class/tty/%1/device").arg(portName)).canonicalFilePath();
        if (!sysfsDevice.isEmpty())
        {
            // A serial tty sysfs path normally looks like:
            // .../usb1/1-3/1-3:1.2/ttyUSB2. The USB device identity is the
            // 1-3 component, not the tty name at the end of the path.
            const QRegularExpression usbDeviceSegment("^\\d+-\\d+(?:\\.\\d+)*$");
            QString parent;
            for (const QString &part : sysfsDevice.split('/', Qt::SkipEmptyParts))
            {
                if (usbDeviceSegment.match(part).hasMatch())
                    parent = part;
            }
            if (!parent.isEmpty())
                return parent;
        }
        return QString();
    }
    return normalizedUsbParentPath(fingerprint.path);
}

bool samePhysicalUsbDevice(const DeviceIdentity &left, const DeviceIdentity &right)
{
    // This check is used only while loading the registry to merge duplicate
    // records left by older versions. It requires a USB-specific key or an
    // exact VID/PID/parent combination to avoid merging unrelated devices.
    const QString leftPhysical = left.physicalKey();
    if (!leftPhysical.isEmpty() && leftPhysical == right.physicalKey() &&
        leftPhysical.startsWith("usb"))
        return true;
    return !left.vid.isEmpty() && left.vid == right.vid &&
           !left.pid.isEmpty() && left.pid == right.pid &&
           !left.usbParentPath.isEmpty() &&
           left.usbParentPath == right.usbParentPath;
}

bool isSupportedAutomaticHid(const DeviceFingerprint &fingerprint)
{
    // HIDAPI exposes every HID device connected to Batocera, including
    // keyboards, gamepads, and unrelated system controls. HOTR must only
    // create automatic gun profiles for the HID models it understands.
    const QString vid = normalizedUsbPart(fingerprint.vid);
    const QString pid = normalizedUsbPart(fingerprint.pid);

    // RS3 Reaper composite devices also expose a HID interface, but their
    // output/recoil transport is serial. They are deliberately excluded here
    // so that HOTR never opens their HID interface as a second gun.
    if (vid == "4b4" && pid == "6870") return true;  // Alien USB.
    if (vid == "d209" && pid == "1601") return true; // AimTrak.
    if (vid == "1b4f" && pid == "9206") return true; // Known custom USB gun.

    return false;
}

HardwareAssignmentMode assignmentModeFromString(const QString &value)
{
    if (value == "physical-port") return HardwareAssignmentMode::PhysicalPort;
    if (value == "first-available") return HardwareAssignmentMode::FirstAvailable;
    if (value == "manual") return HardwareAssignmentMode::Manual;
    return HardwareAssignmentMode::Persistent;
}

bool rememberEndpoint(HardwareDevice &device, const DeviceFingerprint &endpoint)
{
    // Keep one record per transport/interface, updating paths and metadata
    // when a replug changes the current /dev name.
    bool changed = false;
    const QString endpointPhysicalKey = endpoint.physicalKey();
    for (DeviceFingerprint &saved : device.endpoints)
    {
        const bool samePhysicalEndpoint = endpointPhysicalKey.startsWith("usb:") &&
            saved.physicalKey() == endpointPhysicalKey;
        if (saved.transport == endpoint.transport &&
            (saved.path == endpoint.path ||
             (saved.interfaceNumber >= 0 && saved.interfaceNumber == endpoint.interfaceNumber) ||
             samePhysicalEndpoint ||
             (!saved.stableByIdPath.isEmpty() && saved.stableByIdPath == endpoint.stableByIdPath) ||
             (!saved.stableByPath.isEmpty() && saved.stableByPath == endpoint.stableByPath)))
        {
            changed = saved.toJson() != endpoint.toJson();
            saved = endpoint;
            if (endpoint.transport == "serial" || device.fingerprint.transport != "serial")
            {
                changed = changed || device.fingerprint.toJson() != endpoint.toJson();
                device.fingerprint = endpoint;
            }
            return changed;
        }
    }
    device.endpoints.append(endpoint);
    changed = true;
    // Serial is the RS3 output endpoint. Keep it as the primary fingerprint
    // when the same physical device also exposes HID.
    if (endpoint.transport == "serial" || device.fingerprint.transport != "serial")
    {
        changed = changed || device.fingerprint.toJson() != endpoint.toJson();
        device.fingerprint = endpoint;
    }
    return changed;
}

bool canonicalizeSerialGun(HardwareDevice &device)
{
    // Older registries may contain an RS3 record whose identity was created
    // from the composite HID interface before the serial endpoint appeared.
    // If a serial endpoint is present, make it authoritative and remove the
    // redundant HID endpoint from the persisted record.
    if (device.deviceType != "rs3reaper")
        return false;

    int serialEndpointIndex = -1;
    for (int index = 0; index < device.endpoints.size(); ++index)
    {
        if (device.endpoints.at(index).transport == "serial")
        {
            serialEndpointIndex = index;
            break;
        }
    }
    if (serialEndpointIndex < 0)
        return false;

    bool changed = device.fingerprint.toJson() != device.endpoints.at(serialEndpointIndex).toJson() ||
                   device.identity.key() != device.endpoints.at(serialEndpointIndex).identity().key();
    device.fingerprint = device.endpoints.at(serialEndpointIndex);
    device.identity = device.fingerprint.identity();

    for (int index = device.endpoints.size() - 1; index >= 0; --index)
    {
        if (device.endpoints.at(index).transport == "hid")
        {
            device.endpoints.removeAt(index);
            changed = true;
        }
    }
    return changed;
}
}

bool DeviceIdentity::isMeaningful() const
{
    // Anonymous UARTs do not have enough information to be persistent devices.
    return !value.isEmpty() || !serial.isEmpty() || (!vid.isEmpty() && !pid.isEmpty()) || !stableByPath.isEmpty();
}

// Return the stable logical identifier used by registry lookups and logs.
QString DeviceIdentity::key() const
{
    // Prefer a compact strong value. The fallback includes every available
    // discriminator so two weak identities are still unlikely to collide.
    if (!value.isEmpty()) return type + ":" + value;
    return type + ":" + vid + ":" + pid + ":" + serial + ":" + stableByPath + ":" + usbParentPath;
}

// Return an identifier for the physical USB device, independent of whether
// the current scan saw its serial endpoint, HID endpoint, or both.
QString DeviceIdentity::physicalKey() const
{
    // The physical key intentionally ignores transport. Serial and HID
    // interfaces of one USB gun therefore resolve to the same key.
    if (!serial.isEmpty() && !vid.isEmpty() && !pid.isEmpty())
        return "usb:" + normalizedUsbPart(vid) + ":" + normalizedUsbPart(pid) + ":" + serial;
    if (!vid.isEmpty() && !pid.isEmpty() && !usbParentPath.isEmpty())
        return "usb-port:" + normalizedUsbPart(vid) + ":" + normalizedUsbPart(pid) + ":" + usbParentPath;
    return key();
}

// Serialize only the identity fields. Runtime state is stored by
// HardwareDevice::toJson(), not in this lower-level identity object.
QJsonObject DeviceIdentity::toJson() const
{
    QJsonObject json{{"type", type}};
    if (!value.isEmpty()) json["value"] = value;
    if (!vid.isEmpty()) json["vid"] = vid;
    if (!pid.isEmpty()) json["pid"] = pid;
    if (!serial.isEmpty()) json["serial"] = serial;
    if (!stableByIdPath.isEmpty()) json["stableByIdPath"] = stableByIdPath;
    if (!stableByPath.isEmpty()) json["stableByPath"] = stableByPath;
    if (!usbParentPath.isEmpty()) json["usbParentPath"] = usbParentPath;
    return json;
}

// Restore an identity from JSON without inventing missing hardware values.
DeviceIdentity DeviceIdentity::fromJson(const QJsonObject &json)
{
    DeviceIdentity identity;
    identity.type = json.value("type").toString();
    identity.value = json.value("value").toString();
    identity.vid = json.value("vid").toString();
    identity.pid = json.value("pid").toString();
    identity.serial = json.value("serial").toString();
    identity.stableByIdPath = json.value("stableByIdPath").toString();
    identity.stableByPath = json.value("stableByPath").toString();
    identity.usbParentPath = json.value("usbParentPath").toString();
    return identity;
}

// Convert the live endpoint snapshot into the persistent identity format.
DeviceIdentity DeviceFingerprint::identity() const
{
    // Convert a live endpoint into the stable fields stored in the registry.
    DeviceIdentity result;
    result.type = transport;
    result.vid = vid;
    result.pid = pid;
    result.serial = serialNumber;
    result.stableByIdPath = stableByIdPath;
    result.stableByPath = stableByPath;
    result.usbParentPath = usbParentPath;
    if (transport == "tcp")
        result.value = path;
    else if (!serialNumber.isEmpty() && !vid.isEmpty() && !pid.isEmpty())
        result.value = vid + ":" + pid + ":" + serialNumber;
    return result;
}

// Prefer serial number, then USB port, then the complete endpoint identity.
QString DeviceFingerprint::physicalKey() const
{
    // Prefer serial identity, then physical USB port, then the general key.
    if (!serialNumber.isEmpty() && !vid.isEmpty() && !pid.isEmpty())
        return "usb:" + normalizedUsbPart(vid) + ":" + normalizedUsbPart(pid) + ":" + serialNumber;
    if (!vid.isEmpty() && !pid.isEmpty() && !usbParentPath.isEmpty())
        return "usb-port:" + normalizedUsbPart(vid) + ":" + normalizedUsbPart(pid) + ":" + usbParentPath;
    return identity().physicalKey();
}

// Serialize the complete endpoint metadata so a changed tty/HID path can be
// compared on the next scan.
QJsonObject DeviceFingerprint::toJson() const
{
    return QJsonObject{{"transport", transport}, {"vid", vid}, {"pid", pid},
                       {"serial", serialNumber}, {"manufacturer", manufacturer},
                       {"product", product}, {"path", path},
                       {"stableByIdPath", stableByIdPath}, {"stableByPath", stableByPath},
                       {"usbParentPath", usbParentPath},
                       {"interface", interfaceNumber}};
}

// Restore one endpoint and derive its USB parent for older HID records that
// did not store that field.
DeviceFingerprint DeviceFingerprint::fromJson(const QJsonObject &json)
{
    // Older registries may contain only the identity/path fields, so the
    // caller below supplies those fields as a compatibility fallback.
    DeviceFingerprint fingerprint;
    fingerprint.transport = json.value("transport").toString();
    fingerprint.vid = json.value("vid").toString();
    fingerprint.pid = json.value("pid").toString();
    fingerprint.serialNumber = json.value("serial").toString();
    fingerprint.manufacturer = json.value("manufacturer").toString();
    fingerprint.product = json.value("product").toString();
    fingerprint.path = json.value("path").toString();
    fingerprint.stableByIdPath = json.value("stableByIdPath").toString();
    fingerprint.stableByPath = json.value("stableByPath").toString();
    fingerprint.usbParentPath = json.value("usbParentPath").toString();
    fingerprint.interfaceNumber = json.value("interface").toInt(-1);
    if (fingerprint.usbParentPath.isEmpty() && fingerprint.transport == "hid")
        fingerprint.usbParentPath = normalizedUsbParentPath(fingerprint.path);
    return fingerprint;
}

// Serialize the manager's runtime and assignment state. Empty optional paths
// are omitted to keep devices.json readable.
QJsonObject HardwareDevice::toJson() const
{
    // Keep the registry human-readable while omitting empty optional fields.
    QJsonObject json{{"identity", identity.toJson()}, {"deviceType", deviceType},
                     {"profile", profile}, {"player", player},
                     {"assignmentMode", assignmentName(assignmentMode)},
                     {"state", enumName(state)}, {"manual", manual},
                     {"lastSeen", lastSeen.toUTC().toString(Qt::ISODate)} };
    if (!fingerprint.path.isEmpty()) json["path"] = fingerprint.path;
    if (!fingerprint.stableByIdPath.isEmpty()) json["lastStablePath"] = fingerprint.stableByIdPath;
    if (!fingerprint.transport.isEmpty()) json["fingerprint"] = fingerprint.toJson();
    QJsonArray endpointArray;
    for (const DeviceFingerprint &endpoint : endpoints)
        endpointArray.append(endpoint.toJson());
    if (!endpointArray.isEmpty()) json["endpoints"] = endpointArray;
    if (reservationUntilMs > 0) json["reservationUntil"] = QString::number(reservationUntilMs);
    return json;
}

// Restore one manager record, including compatibility migration for old
// registries and reconstruction of missing endpoint data.
HardwareDevice HardwareDevice::fromJson(const QJsonObject &json)
{
    // Load one record and normalize it so current discovery can reconcile it
    // with registries written by older HOTR versions.
    HardwareDevice device;
    device.identity = DeviceIdentity::fromJson(json.value("identity").toObject());
    device.deviceType = json.value("deviceType").toString();
    device.profile = json.value("profile").toString();
    device.player = static_cast<quint8>(json.value("player").toInt());
    if (device.player > MAXPLAYERLIGHTGUNS)
        device.player = 0;
    device.assignmentMode = assignmentModeFromString(json.value("assignmentMode").toString());
    device.manual = json.value("manual").toBool();
    device.state = HardwareConnectionState::Absent;
    device.fingerprint = DeviceFingerprint::fromJson(json.value("fingerprint").toObject());
    if (device.fingerprint.transport.isEmpty())
    {
        device.fingerprint.transport = device.identity.type;
        device.fingerprint.vid = device.identity.vid;
        device.fingerprint.pid = device.identity.pid;
        device.fingerprint.serialNumber = device.identity.serial;
        device.fingerprint.path = json.value("path").toString();
        device.fingerprint.stableByIdPath = json.value("lastStablePath").toString();
        device.fingerprint.stableByPath = device.identity.stableByPath;
    }
    // Recompute serial USB ancestry when the tty is currently present. This
    // migrates older registries that incorrectly stored ttyUSBx as the USB
    // parent and allows their serial/HID endpoints to merge safely.
    const QString currentUsbParent = usbParentPathForFingerprint(device.fingerprint);
    if (!currentUsbParent.isEmpty())
        device.fingerprint.usbParentPath = currentUsbParent;
    if (!device.identity.usbParentPath.isEmpty() &&
        device.identity.usbParentPath.startsWith("tty"))
        device.identity.usbParentPath.clear();
    if (device.identity.usbParentPath.isEmpty() && !device.fingerprint.usbParentPath.isEmpty())
        device.identity.usbParentPath = device.fingerprint.usbParentPath;
    for (const QJsonValue &endpoint : json.value("endpoints").toArray())
        device.endpoints.append(DeviceFingerprint::fromJson(endpoint.toObject()));
    if (device.endpoints.isEmpty() && !device.fingerprint.transport.isEmpty())
        device.endpoints.append(device.fingerprint);
    device.lastSeen = QDateTime::fromString(json.value("lastSeen").toString(), Qt::ISODate);
    device.reservationUntilMs = json.value("reservationUntil").toString().toLongLong();
    return device;
}

// Build the manager around the writable registry location and scan interval.
HardwareManager::HardwareManager(QObject *parent) : QObject(parent)
{
    // HOTR_DATA_DIR is used by Batocera to place writable state outside the
    // read-only AppImage. Desktop installs use the user's normal data folder.
    QString dataDir = QString::fromUtf8(qgetenv("HOTR_DATA_DIR"));
    if (dataDir.isEmpty())
        dataDir = QDir(QDir::homePath()).filePath(".HookOfTheReaper/data");
    QDir().mkpath(dataDir);
    m_registryPath = QDir(dataDir).filePath("devices.json");
    int scanInterval = qEnvironmentVariableIntValue("HOTR_HARDWARE_SCAN_MS");
    if (scanInterval <= 0)
        scanInterval = 3000;
    m_scanTimer.setInterval(scanInterval);
    connect(&m_scanTimer, &QTimer::timeout, this, &HardwareManager::safetyScan);
    loadRegistry();
}

void HardwareManager::start()
{
    // The immediate scan makes startup responsive; the timer handles unplug,
    // replug, and path changes while HOTR remains running.
    if (m_started) return;
    m_started = true;
    rescan();
    m_scanTimer.start();
}

void HardwareManager::stop()
{
    // Saving here preserves the latest assignment and endpoint information
    // even when the service is stopped by Batocera.
    m_scanTimer.stop();
    m_started = false;
    saveRegistry();
}

// Mark the game boundary used by HookerEngine to defer profile creation.
void HardwareManager::setGameActive(bool active)
{
    m_gameActive = active;
}

// Change a player's device. Manual mode is the only mode allowed to replace
// an occupied slot because automatic matching must never steal a player.
bool HardwareManager::assignPlayer(const QString &identity, quint8 player, HardwareAssignmentMode mode)
{
    // Non-manual assignment refuses to steal a player slot. Manual UI
    // assignment is allowed to move the current occupant so users can swap
    // guns without editing JSON by hand.
    HardwareDevice *device = findByIdentity(identity);
    if (!device || player == 0 || player > MAXPLAYERLIGHTGUNS) return false;
    for (const HardwareDevice &other : m_devices)
    {
        if (&other != device && other.player == player)
        {
            if (mode != HardwareAssignmentMode::Manual)
            {
                qWarning() << "HardwareManager: player already assigned" << player
                           << "identity=" << other.identity.key();
                return false;
            }
        }
    }

    // Manual assignment is an intentional reorder from the configuration UI.
    // Move the current occupant out of the slot rather than failing halfway
    // through a swap (P1 gun B / P2 gun A).
    if (mode == HardwareAssignmentMode::Manual)
    {
        for (HardwareDevice &other : m_devices)
        {
            if (&other == device || other.player != player)
                continue;
            other.player = 0;
            other.assignmentMode = HardwareAssignmentMode::Manual;
            if (other.present && other.state != HardwareConnectionState::NeedsSetup)
                transition(other, HardwareConnectionState::NeedsSetup, "player slot reassigned");
        }
    }
    device->player = player;
    device->assignmentMode = mode;
    if (mode == HardwareAssignmentMode::Manual)
        device->manual = true;
    if (device->present && device->state == HardwareConnectionState::NeedsSetup &&
        !device->deviceType.isEmpty() && !device->profile.isEmpty())
        transition(*device, HardwareConnectionState::Detected, "player assigned");
    saveRegistry();
    emit playerAssignmentChanged(player, identity);
    emit devicesChanged();
    return true;
}

// Clear only the player relationship; keep the device for future detection.
bool HardwareManager::unassignPlayer(const QString &identity, HardwareAssignmentMode mode)
{
    // Unassignment retains the device record so it can be selected again
    // later; it only clears the player relationship.
    HardwareDevice *device = findByIdentity(identity);
    if (!device || device->player == 0)
        return false;

    const quint8 oldPlayer = device->player;
    device->player = 0;
    device->assignmentMode = mode;
    if (device->present && device->state != HardwareConnectionState::NeedsSetup)
        transition(*device, HardwareConnectionState::NeedsSetup, "player assignment cleared");
    saveRegistry();
    emit playerAssignmentChanged(oldPlayer, QString());
    emit devicesChanged();
    return true;
}

// Apply user-selected type/profile metadata without changing physical identity.
bool HardwareManager::configureDevice(const QString &identity, const QString &deviceType,
                                      const QString &profile, bool manual, HardwareAssignmentMode mode)
{
    // Configuration changes affect future automatic profile creation. The
    // physical device identity is deliberately left unchanged.
    HardwareDevice *device = findByIdentity(identity);
    if (!device) return false;
    device->deviceType = deviceType;
    device->profile = profile;
    device->manual = manual;
    device->assignmentMode = mode;
    if (device->present && device->player != 0 && !device->profile.isEmpty())
        transition(*device, HardwareConnectionState::Detected, "profile configured");
    else if (device->present)
        transition(*device, HardwareConnectionState::NeedsSetup, "profile configured; player assignment required");
    else
        transition(*device, HardwareConnectionState::Absent, "profile configured while absent");
    saveRegistry();
    emit devicesChanged();
    return true;
}

// Record a successful serial/HID transport transition for the matching player.
void HardwareManager::markPlayerTransportConnected(quint8 player)
{
    // These four methods are called by the legacy serial layer. The manager
    // does not open the port itself; it records what the transport layer did.
    for (HardwareDevice &device : m_devices)
    {
        if (device.player != player || !device.present)
            continue;
        const HardwareConnectionState oldState = device.state;
        transition(device, HardwareConnectionState::Connected, "serial transport opened");
        if (oldState != device.state)
        {
            saveRegistry();
            emit devicesChanged();
        }
        return;
    }
}

// Record that the game layer has begun opening the player's transport.
void HardwareManager::markPlayerTransportOpening(quint8 player)
{
    for (HardwareDevice &device : m_devices)
    {
        if (device.player != player || !device.present)
            continue;
        const HardwareConnectionState oldState = device.state;
        transition(device, HardwareConnectionState::Opening, "serial transport opening");
        if (oldState != device.state)
        {
            saveRegistry();
            emit devicesChanged();
        }
        return;
    }
}

// Record a scheduled retry after a temporary transport failure.
void HardwareManager::markPlayerTransportRecovering(quint8 player)
{
    for (HardwareDevice &device : m_devices)
    {
        if (device.player != player || !device.present)
            continue;
        const HardwareConnectionState oldState = device.state;
        transition(device, HardwareConnectionState::Recovering, "serial transport retry scheduled");
        if (oldState != device.state)
        {
            saveRegistry();
            emit devicesChanged();
        }
        return;
    }
}

// Record that the game layer lost the player's transport.
void HardwareManager::markPlayerTransportDisconnected(quint8 player)
{
    for (HardwareDevice &device : m_devices)
    {
        if (device.player != player || !device.present)
            continue;
        const HardwareConnectionState oldState = device.state;
        transition(device, HardwareConnectionState::Disconnected, "serial transport lost");
        if (oldState != device.state)
        {
            saveRegistry();
            emit devicesChanged();
        }
        return;
    }
}

// Convert the scan timer callback into the same path used by manual rescans.
void HardwareManager::safetyScan()
{
    rescan();
}

// Public name conversion used by status/logging callers.
QString HardwareManager::connectionStateName(HardwareConnectionState state)
{
    return enumName(state);
}

// Compare one saved identity with one live endpoint and return a confidence
// score: 100 exact, 80 stable port, 40 model only, and 0 incompatible.
int HardwareManager::matchConfidence(const DeviceIdentity &saved, const DeviceFingerprint &current)
{
    // Scores are ordered from strongest to weakest. Reconciliation only
    // accepts scores of 80 or higher, so VID/PID alone cannot steal a saved
    // player assignment from another identical gun.
    if (!saved.serial.isEmpty() && !current.serialNumber.isEmpty() &&
        saved.physicalKey() == current.physicalKey()) return 100;
    // A HID endpoint may omit its serial string after a replug. If it is on
    // the same USB parent as the known serial endpoint, treat it as another
    // interface of that physical gun. This is deliberately cross-transport
    // only; two serial devices on the same port must still not be merged.
    if (saved.type != current.transport &&
        saved.vid == current.vid && saved.pid == current.pid &&
        (saved.serial.isEmpty() || current.serialNumber.isEmpty()) &&
        !saved.usbParentPath.isEmpty() &&
        saved.usbParentPath == current.usbParentPath)
        return 95;
    if (saved.type != current.transport) return 0;
    const DeviceIdentity incoming = current.identity();
    if (!saved.value.isEmpty() && saved.value == incoming.value) return 100;
    if (!saved.serial.isEmpty() && saved.serial == current.serialNumber &&
        saved.vid == current.vid && saved.pid == current.pid) return 100;
    if (!saved.vid.isEmpty() && saved.vid == current.vid && saved.pid == current.pid &&
        !saved.stableByPath.isEmpty() && saved.stableByPath == current.stableByPath)
        return 80;
    if (!saved.vid.isEmpty() && saved.vid == current.vid && saved.pid == current.pid)
        return 40;
    return 0;
}

// Enumerate serial ports and enrich each one with stable Linux symlink paths.
QList<DeviceFingerprint> HardwareManager::discoverSerialDevices() const
{
    // QSerialPortInfo finds live ports; by-id/by-path symlinks provide stable
    // names that survive ttyUSB number changes.
    QList<DeviceFingerprint> result;
    const QDir byId("/dev/serial/by-id");
    const QDir byPath("/dev/serial/by-path");
    for (const QSerialPortInfo &port : QSerialPortInfo::availablePorts()) {
        DeviceFingerprint fingerprint;
        fingerprint.transport = "serial";
        fingerprint.path = port.systemLocation();
        fingerprint.vid = port.hasVendorIdentifier() ? QString::number(port.vendorIdentifier(), 16) : QString();
        fingerprint.pid = port.hasProductIdentifier() ? QString::number(port.productIdentifier(), 16) : QString();
        fingerprint.serialNumber = port.serialNumber();
        fingerprint.manufacturer = port.manufacturer();
        fingerprint.product = port.description();
        fingerprint.usbParentPath = usbParentPathForFingerprint(fingerprint);
        const QString canonicalPort = QFileInfo(fingerprint.path).canonicalFilePath();
        for (const QString &entry : byId.entryList(QDir::System | QDir::AllEntries)) {
            const QString candidate = byId.filePath(entry);
            if (!canonicalPort.isEmpty() && QFileInfo(candidate).canonicalFilePath() == canonicalPort)
                fingerprint.stableByIdPath = candidate;
        }
        for (const QString &entry : byPath.entryList(QDir::System | QDir::AllEntries)) {
            const QString candidate = byPath.filePath(entry);
            if (!canonicalPort.isEmpty() && QFileInfo(candidate).canonicalFilePath() == canonicalPort)
                fingerprint.stableByPath = candidate;
        }
        // Ignore anonymous motherboard UARTs. They have no stable identity and
        // are not useful HOTR devices; retaining them would create a new
        // NeedsSetup record on every safety scan.
        if (fingerprint.vid.isEmpty() && fingerprint.pid.isEmpty() &&
            fingerprint.serialNumber.isEmpty() && fingerprint.manufacturer.isEmpty() &&
            fingerprint.product.isEmpty() && fingerprint.stableByIdPath.isEmpty() &&
            fingerprint.stableByPath.isEmpty())
            continue;
        result.append(fingerprint);
    }
    return result;
}

// Enumerate HID endpoints and retain their interface/path metadata.
QList<DeviceFingerprint> HardwareManager::discoverHidDevices() const
{
    // HIDAPI provides the HID endpoint and USB strings. It is intentionally
    // kept separate from serial discovery because the two APIs expose paths
    // in different formats.
    QList<DeviceFingerprint> result;
#ifndef Q_OS_WIN
    if (hid_init() != 0) return result;
    hid_device_info *list = hid_enumerate(0, 0);
    for (hid_device_info *item = list; item; item = item->next) {
        DeviceFingerprint fingerprint;
        fingerprint.transport = "hid";
        fingerprint.path = QString::fromUtf8(item->path ? item->path : "");
        fingerprint.vid = QString::number(item->vendor_id, 16);
        fingerprint.pid = QString::number(item->product_id, 16);
        fingerprint.serialNumber = item->serial_number ? QString::fromWCharArray(item->serial_number) : QString();
        fingerprint.manufacturer = item->manufacturer_string ? QString::fromWCharArray(item->manufacturer_string) : QString();
        fingerprint.product = item->product_string ? QString::fromWCharArray(item->product_string) : QString();
        fingerprint.interfaceNumber = item->interface_number;
        fingerprint.usbParentPath = usbParentPathForFingerprint(fingerprint);
        if (isSupportedAutomaticHid(fingerprint))
            result.append(fingerprint);
    }
    hid_free_enumeration(list);
    hid_exit();
#endif
    return result;
}

// Combine all supported endpoint transports into one scan result.
QList<DeviceFingerprint> HardwareManager::discoverDevices() const
{
    // A single physical gun may produce two entries here. reconcile() merges
    // them using physical identity and stores both as endpoints.
    QList<DeviceFingerprint> result = discoverSerialDevices();
    result.append(discoverHidDevices());
    return result;
}

// Map raw manufacturer/product identifiers to HOTR's normalized device types.
QString HardwareManager::detectDeviceType(const DeviceFingerprint &fingerprint) const
{
    // Detection uses stable VID/PID values first, then manufacturer/product
    // text. Unknown hardware is retained for user setup instead of discarded.
    const QString text = (fingerprint.manufacturer + " " + fingerprint.product).toLower();
    const QString vid = normalizedUsbPart(fingerprint.vid);
    const QString pid = normalizedUsbPart(fingerprint.pid);
    if (text.contains("retro shooter") ||
        (vid == "483" && (pid == "5750" || pid == "5751"))) return "rs3reaper";
    if (vid == "4b4" && pid == "6870") return "alienusb";
    if (vid == "d209" && pid == "1601") return "aimtrak";
    if (vid == "1b4f" && pid == "9206") return "custom";
    if (text.contains("gun4ir")) return "gun4ir";
    if (text.contains("fusion")) return "fusion";
    if (text.contains("blamcon")) return "blamcon";
    if (text.contains("openfire")) return "openfire";
    if (text.contains("xgunner")) return "xgunner";
    if (text.contains("xenas")) return "xenas";
    if (fingerprint.transport == "hid") return "custom";
    return "unknown";
}

// Map a normalized device type to the legacy .hor profile stem.
QString HardwareManager::profileForType(const QString &deviceType) const
{
    // This is the filename stem used by the existing .hor profile system.
    if (deviceType == "rs3reaper") return "rs3Reaper";
    if (deviceType == "gun4ir") return "jbgun4ir";
    if (deviceType == "fusion") return "fusion";
    if (deviceType == "blamcon") return "blamcon";
    if (deviceType == "openfire") return "openFire";
    if (deviceType == "xgunner") return "xGunner";
    if (deviceType == "xenas") return "xenas";
    if (deviceType == "alienusb") return "alienUSB";
    if (deviceType == "aimtrak") return "aimtrak";
    if (deviceType == "custom") return "customUSB";
    return QString();
}

// Find the highest-confidence existing record for a live endpoint.
int HardwareManager::findMatch(const DeviceFingerprint &fingerprint) const
{
    // Return the best existing record, but never match on a weak model-only
    // score because several guns can share the same VID/PID.
    int best = -1;
    int confidence = 0;
    for (int i = 0; i < m_devices.size(); ++i) {
        const int score = matchConfidence(m_devices[i].identity, fingerprint);
        // VID/PID alone identifies a model, not a physical gun. Never
        // silently transfer a persistent player assignment on that evidence.
        if (score >= 80 && score > confidence) { confidence = score; best = i; }
    }
    return best;
}

// Find an unused player, respecting short-lived reservations for replugging.
quint8 HardwareManager::firstFreePlayer() const
{
    // A disconnected device keeps its player slot reserved briefly so a
    // replug does not get assigned to a different player during recovery.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (quint8 player = 1; player <= MAXPLAYERLIGHTGUNS; ++player) {
        bool used = false;
        for (const HardwareDevice &device : m_devices)
            if (device.player == player && (device.present || device.reservationUntilMs > now)) used = true;
        if (!used) return player;
    }
    return 0;
}

// Apply and log one state transition; callers decide when to persist it.
void HardwareManager::transition(HardwareDevice &device, HardwareConnectionState next, const QString &reason)
{
    // All state changes pass through this function so logs use one consistent
    // format and no duplicate transition messages are generated.
    if (device.state == next) return;
    qInfo().noquote() << QString("[HOTR] Device state identity=%1 old=%2 new=%3 reason=%4")
                         .arg(device.identity.key(), enumName(device.state), enumName(next), reason);
    device.state = next;
}

// Merge one complete discovery pass into the persistent registry and emit
// events for new, recovered, disconnected, or changed devices.
void HardwareManager::reconcile(const QList<DeviceFingerprint> &found)
{
    // Reconciliation compares one scan with the persistent registry:
    //   1. add or update every endpoint found now;
    //   2. mark previously present records missing from this scan disconnected;
    //   3. expire player reservations after the recovery grace period.
    bool changed = false;
    QList<bool> matched(m_devices.size(), false);
    for (const DeviceFingerprint &fingerprint : found) {
        int index = findMatch(fingerprint);
        if (index < 0) {
            HardwareDevice device;
            device.identity = fingerprint.identity();
            device.fingerprint = fingerprint;
            device.endpoints.append(fingerprint);
            device.deviceType = detectDeviceType(fingerprint);
            device.profile = profileForType(device.deviceType);
            device.manual = false;
            device.present = true;
            device.lastSeen = QDateTime::currentDateTimeUtc();
            device.player = device.deviceType == "unknown" ? 0 : firstFreePlayer();
            device.state = (device.deviceType == "unknown" || device.player == 0)
                ? HardwareConnectionState::NeedsSetup : HardwareConnectionState::Detected;
            qInfo().noquote() << "[HOTR] New device detected identity=" + device.identity.key();
            m_devices.append(device);
            matched.append(true);
            emit deviceConnected(device);
            changed = true;
            continue;
        }
        matched[index] = true;
        HardwareDevice &device = m_devices[index];
        const bool wasPresent = device.present;
        const HardwareConnectionState oldState = device.state;
        const QString oldPath = device.fingerprint.path;
        const QString oldIdentity = device.identity.key();
        device.present = true;
        const bool endpointChanged = rememberEndpoint(device, fingerprint);
        if (!fingerprint.usbParentPath.isEmpty() &&
            device.identity.usbParentPath != fingerprint.usbParentPath)
        {
            device.identity.usbParentPath = fingerprint.usbParentPath;
        }
        if (fingerprint.transport == "serial" &&
            !fingerprint.serialNumber.isEmpty())
        {
            // A composite RS3 gun can first be discovered through HID and
            // then through serial. Serial is the output transport, so make it
            // the canonical identity whenever that endpoint is available.
            // This prevents records such as hid:483:5751 from later being
            // applied as transport=serial.
            device.identity = fingerprint.identity();
        }
        device.lastSeen = QDateTime::currentDateTimeUtc();
        if (device.deviceType.isEmpty() || device.deviceType == "unknown") {
            device.deviceType = detectDeviceType(fingerprint);
            device.profile = profileForType(device.deviceType);
        }
        transition(device, device.state == HardwareConnectionState::Recovering || !wasPresent
                   ? HardwareConnectionState::Detected : device.state, "discovered");
        if (!wasPresent && oldState == HardwareConnectionState::Disconnected)
            emit deviceRecovered(device);
        else if (!wasPresent)
            emit deviceConnected(device);
        if (endpointChanged || !wasPresent || oldState != device.state || oldPath != fingerprint.path ||
            oldIdentity != device.identity.key())
            changed = true;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int i = 0; i < m_devices.size(); ++i) {
        if (matched.value(i)) continue;
        HardwareDevice &device = m_devices[i];
        if (!device.present) {
            if (device.reservationUntilMs > 0 && device.reservationUntilMs <= now) {
                device.reservationUntilMs = 0;
                transition(device, HardwareConnectionState::Absent, "player reservation expired");
                changed = true;
            }
            continue;
        }
        device.present = false;
        device.reservationUntilMs = now + 30000;
        transition(device, HardwareConnectionState::Disconnected, "device absent from scan");
        emit deviceDisconnected(device);
        changed = true;
    }
    if (changed)
        saveRegistry();
    if (changed)
        emit devicesChanged();
}

// Run discovery and reconciliation as one atomic manager operation.
void HardwareManager::rescan()
{
    // Keep discovery and reconciliation together so callers get one complete
    // snapshot rather than observing a half-updated device list.
    reconcile(discoverDevices());
}

// Locate a registry record by the stable identity string exposed to callers.
HardwareDevice *HardwareManager::findByIdentity(const QString &identity)
{
    for (HardwareDevice &device : m_devices)
        if (device.identity.key() == identity) return &device;
    return nullptr;
}

// Load saved records and merge duplicates created by older serial/HID logic.
void HardwareManager::loadRegistry()
{
    // Loading is intentionally tolerant: a missing registry means first run,
    // while duplicate physical records from older versions are merged.
    QFile file(m_registryPath);
    if (!file.open(QIODevice::ReadOnly)) return;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    bool migrated = false;
    for (const QJsonValue &value : document.object().value("devices").toArray())
    {
        HardwareDevice loaded = HardwareDevice::fromJson(value.toObject());
        int duplicate = -1;
        for (int i = 0; i < m_devices.size(); ++i)
        {
            if (samePhysicalUsbDevice(m_devices[i].identity, loaded.identity))
            {
                duplicate = i;
                break;
            }
        }
        if (duplicate < 0)
        {
            m_devices.append(loaded);
            continue;
        }

        HardwareDevice &device = m_devices[duplicate];
        if (device.player == 0) device.player = loaded.player;
        if (device.deviceType.isEmpty() || device.deviceType == "unknown") device.deviceType = loaded.deviceType;
        if (device.profile.isEmpty()) device.profile = loaded.profile;
        for (const DeviceFingerprint &endpoint : loaded.endpoints)
            rememberEndpoint(device, endpoint);
    }

    for (HardwareDevice &device : m_devices)
        migrated = canonicalizeSerialGun(device) || migrated;
    if (migrated)
        saveRegistry();
}

// Atomically write all records so a shutdown or power loss cannot leave a
// partially written devices.json.
bool HardwareManager::saveRegistry() const
{
    // QSaveFile writes a temporary file and replaces devices.json atomically,
    // preventing a power loss from leaving a half-written registry.
    QJsonArray devices;
    for (const HardwareDevice &device : m_devices) devices.append(device.toJson());
    QSaveFile file(m_registryPath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "HardwareManager: cannot write" << m_registryPath << file.errorString();
        return false;
    }
    file.write(QJsonDocument(QJsonObject{{"version", 1}, {"devices", devices}}).toJson(QJsonDocument::Indented));
    return file.commit();
}
