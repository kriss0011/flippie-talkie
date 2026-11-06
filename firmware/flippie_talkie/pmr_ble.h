#pragma once
#include <furi.h>
#include <profiles/serial_profile.h>
#include <services/serial_service.h>

typedef struct {
    FuriHalBleProfileBase base;
    BleServiceSerial* serial;
} PmrBleProfile;

static const FuriHalBleProfileTemplate pmr_ble_template;

static FuriHalBleProfileBase* pmr_ble_start(FuriHalBleProfileParams params) {
    UNUSED(params);
    PmrBleProfile* profile = malloc(sizeof(PmrBleProfile));
    profile->base.config = &pmr_ble_template;
    profile->serial = ble_svc_serial_start();
    return &profile->base;
}

static void pmr_ble_stop(FuriHalBleProfileBase* base) {
    furi_check(base->config == &pmr_ble_template);
    PmrBleProfile* profile = (PmrBleProfile*)base;
    ble_svc_serial_stop(profile->serial);
    free(profile);
}

static void pmr_ble_gap(GapConfig* config, FuriHalBleProfileParams params) {
    // Keep the existing secure pairing, device identity and browser filter.
    ble_profile_serial->get_gap_config(config, params);
}

// A distinct template prevents the Bluetooth service from attaching RPC to
// the audio service. The app owns the service until the default is restored.
static const FuriHalBleProfileTemplate pmr_ble_template = {
    .start = pmr_ble_start,
    .stop = pmr_ble_stop,
    .get_gap_config = pmr_ble_gap,
};
