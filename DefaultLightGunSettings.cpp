#include "DefaultLightGunSettings.h"

#include "Global.h"

// Shared storage for the built-in profiles. It lives here rather than in the
// GUI translation unit so headless service code has the same source of truth.
S_DEFAULTLG DEFAULTLG_ARRAY[NUM_DEFAULTLG];

namespace
{
// Store one built-in profile in the shared legacy array. Keeping this mapping
// in one place makes it obvious which values belong to each gun type.
void setDefaultLightGunSettings(int defaultLightGunType,
                                quint8 baudIndex,
                                quint8 dataBitsIndex,
                                quint8 parityIndex,
                                quint8 stopBitsIndex,
                                quint8 flowControlIndex,
                                const char *maximumAmmoCommand,
                                const char *reloadCommand,
                                quint16 maximumAmmo,
                                quint16 reloadValue)
{
    // The public constants use descriptive per-gun names, while the legacy
    // S_DEFAULTLG fields remain abbreviated for compatibility with the
    // existing editor and LightGun constructors.
    S_DEFAULTLG &profile = DEFAULTLG_ARRAY[defaultLightGunType];
    profile.BAUD = baudIndex;
    profile.DATA = dataBitsIndex;
    profile.PARITY = parityIndex;
    profile.STOP = stopBitsIndex;
    profile.FLOW = flowControlIndex;
    profile.MAXAMMO = QString::fromLatin1(maximumAmmoCommand);
    profile.RELOADVALUE = QString::fromLatin1(reloadCommand);
    profile.MAXAMMON = maximumAmmo;
    profile.RELOADVALUEN = reloadValue;
}
}

void initializeDefaultLightGunSettings()
{
    // These are profile defaults, not detected devices. They are shared by
    // the editor, the normal GUI mode, and automatic headless configuration.
    // Each call copies compile-time profile constants into the shared array.
    // The function is intentionally idempotent and may safely run more than
    // once during tests or different application startup modes.
    setDefaultLightGunSettings(RS3_REAPER, REAPERBAUD, REAPERDATA,
                               REAPERPARITY, REAPERSTOP, REAPERFLOW,
                               REAPERMAXAMMO, REAPERRELOAD,
                               REAPERMAXAMMONUM, REAPERRELOADNUM);
    setDefaultLightGunSettings(MX24, MX24BAUD, MX24DATA, MX24PARITY,
                               MX24STOP, MX24FLOW, MX24MAXAMMO, MX24RELOAD,
                               MX24MAXAMMONUM, MX24RELOADNUM);
    setDefaultLightGunSettings(JBGUN4IR, JBGUN4IRBAUD, JBGUN4IRDATA,
                               JBGUN4IRPARITY, JBGUN4IRSTOP, JBGUN4IRFLOW,
                               JBGUN4IRMAXAMMO, JBGUN4IRRELOAD,
                               JBGUN4IRMAXAMMONUM, JBGUN4IRRELOADNUM);
    setDefaultLightGunSettings(FUSION, FUSIONBAUD, FUSIONDATA, FUSIONPARITY,
                               FUSIONSTOP, FUSIONFLOW, FUSIONMAXAMMO,
                               FUSIONRELOAD, FUSIONMAXAMMONUM,
                               FUSIONRELOADNUM);
    setDefaultLightGunSettings(BLAMCON, BLAMCONBAUD, BLAMCONDATA,
                               BLAMCONPARITY, BLAMCONSTOP, BLAMCONFLOW,
                               BLAMCONMAXAMMO, BLAMCONRELOAD,
                               BLAMCONMAXAMMONUM, BLAMCONRELOADNUM);
    setDefaultLightGunSettings(OPENFIRE, OPENFIREBAUD, OPENFIREDATA,
                               OPENFIREPARITY, OPENFIRESTOP, OPENFIREFLOW,
                               OPENFIREMAXAMMO, OPENFIRERELOAD,
                               OPENFIREMAXAMMONUM, OPENFIRERELOADNUM);
    setDefaultLightGunSettings(ALIENUSB, ALIENUSBBAUD, ALIENUSBDATA,
                               ALIENUSBPARITY, ALIENUSBSTOP, ALIENUSBFLOW,
                               ALIENUSBMAXAMMO, ALIENUSBRELOAD,
                               ALIENUSBMAXAMMONUM, ALIENUSBRELOADNUM);
    setDefaultLightGunSettings(XGUNNER, XGUNNERBAUD, XGUNNERDATA,
                               XGUNNERPARITY, XGUNNERSTOP, XGUNNERFLOW,
                               XGUNNERMAXAMMO, XGUNNERRELOAD,
                               XGUNNERMAXAMMONUM, XGUNNERRELOADNUM);
    setDefaultLightGunSettings(AIMTRAK, AIMTRAKBAUD, AIMTRAKDATA,
                               AIMTRAKPARITY, AIMTRAKSTOP, AIMTRAKFLOW,
                               AIMTRAKMAXAMMO, AIMTRAKRELOAD,
                               AIMTRAKMAXAMMONUM, AIMTRAKRELOADNUM);
    setDefaultLightGunSettings(XENAS, XENASBAUD, XENASDATA, XENASPARITY,
                               XENASSTOP, XENASFLOW, XENASMAXAMMO,
                               XENASRELOAD, XENASMAXAMMONUM, XENASRELOADNUM);
    setDefaultLightGunSettings(XENASBTLE, XENASBTLEBAUD, XENASBTLEDATA,
                               XENASBTLEPARITY, XENASBTLESTOP, XENASBTLEFLOW,
                               XENASBTLEMAXAMMO, XENASBTLERELOAD,
                               XENASBTLEMAXAMMONUM, XENASBTLERELOADNUM);
    setDefaultLightGunSettings(SINDEN, SINDENBAUD, SINDENDATA, SINDENPARITY,
                               SINDENSTOP, SINDENFLOW, SINDENMAXAMMO,
                               SINDENRELOAD, SINDENMAXAMMONUM, SINDENRELOADNUM);
    setDefaultLightGunSettings(CUSTOMUSB, CUSTOMUSBBAUD, CUSTOMUSBDATA,
                               CUSTOMUSBPARITY, CUSTOMUSBSTOP, CUSTOMUSBFLOW,
                               CUSTOMUSBMAXAMMO, CUSTOMUSBRELOAD,
                               CUSTOMUSBMAXAMMONUM, CUSTOMUSBRELOADNUM);
}
