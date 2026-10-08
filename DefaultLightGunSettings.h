#ifndef DEFAULTLIGHTGUNSETTINGS_H
#define DEFAULTLIGHTGUNSETTINGS_H

// Populate the built-in light-gun profile defaults used by both the GUI and
// the headless Batocera service. This must run before ComDeviceList creates
// any LightGun objects.
void initializeDefaultLightGunSettings();

#endif // DEFAULTLIGHTGUNSETTINGS_H
