Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$sourcePath = Join-Path $PSScriptRoot 'ptp_ocp.c'
$readmePath = Join-Path $PSScriptRoot 'README.md'
$source = Get-Content -Raw $sourcePath
$readme = Get-Content -Raw $readmePath

function Assert-Match {
    param(
        [Parameter(Mandatory)] [string] $Text,
        [Parameter(Mandatory)] [string] $Pattern,
        [Parameter(Mandatory)] [string] $Description
    )

    if (-not [regex]::IsMatch($Text, $Pattern,
            [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
        throw "Missing FPGA contract: $Description"
    }
}

function Assert-NoMatch {
    param(
        [Parameter(Mandatory)] [string] $Text,
        [Parameter(Mandatory)] [string] $Pattern,
        [Parameter(Mandatory)] [string] $Description
    )

    if ([regex]::IsMatch($Text, $Pattern,
            [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
        throw "Unsafe FPGA contract: $Description"
    }
}

Assert-Match $source 'module_param_named\(clock_optional_registers,\s*clock_optional_registers,\s*bool,\s*0444\)' `
    'Clock optional-register opt-in is read-only and defaults off'
Assert-Match $source 'module_param_named\(tod_optional_telemetry,\s*tod_optional_telemetry,\s*bool,\s*0444\)' `
    'ToD optional-telemetry opt-in is read-only and defaults off'
Assert-Match $source 'module_param_named\(irig_optional_features,\s*irig_optional_features,\s*bool,\s*0444\)' `
    'IRIG synthesis-feature opt-in is read-only and defaults off'
Assert-Match $source 'module_param_named\(optional_image_version,\s*optional_image_version,\s*uint,\s*0444\)' `
    'the exact raw image contract is read-only and defaults to zero'
Assert-Match $source 'module_param_named\(optional_image_device,\s*optional_image_device,\s*charp,\s*0444\)' `
    'the legacy exact-image word requires a read-only PCI-device binding'
Assert-Match $source 'module_param_array_named\(optional_image_contracts,\s*optional_image_contracts,\s*charp,\s*&optional_image_contract_count,\s*0444\)' `
    'multiple read-only per-device exact-image contracts are supported'
Assert-Match $source 'ptp_ocp_optional_image_contract_expected\(.*?pci_name\(bp->pdev\).*?optional_image_device.*?strcasecmp\(optional_image_device, device\).*?optional_image_version.*?optional_image_contract_count.*?selected != candidate.*?return false;' `
    'legacy and array contracts are bound to one PCI function and conflicts fail closed'
Assert-Match $source 'ptp_ocp_optional_image_contract_matches\(.*?!bp->image \|\| bp->fw_loader \|\|\s*!ptp_ocp_optional_image_contract_expected\(bp, &expected\).*?if \(!image_version \|\| image_version == U32_MAX\).*?return image_version == expected;' `
    'missing, loader, untargeted, invalid and mismatched per-device image contracts are rejected'
Assert-NoMatch $source 'return image_version == optional_image_version;' `
    'a module-wide raw image word must never unlock every matching card'
Assert-Match $source 'ptp_ocp_clock_optional_registers_enabled\(.*?clock_optional_registers.*?ptp_ocp_optional_image_contract_matches' `
    'Clock optional registers require the exact image contract'
Assert-Match $source 'ptp_ocp_tod_optional_utc_enabled\(.*?tod_optional_telemetry.*?ptp_ocp_optional_image_contract_matches' `
    'ToD optional UTC telemetry requires the exact image contract'
Assert-Match $source 'ptp_ocp_tod_optional_gnss_enabled\(.*?tod_optional_telemetry.*?ptp_ocp_optional_image_contract_matches' `
    'ToD optional GNSS telemetry requires the exact image contract'
Assert-Match $source 'ptp_ocp_irig_optional_features_enabled\(.*?irig_optional_features.*?ptp_ocp_optional_image_contract_matches' `
    'IRIG optional features require the exact image contract'
Assert-Match $source 'return version && version != U32_MAX && version >= minimum;' `
    'invalid version values cannot satisfy a revision gate'

Assert-Match $source '#define CLOCK_VERSION_STATUS\s+FPGA_CORE_VERSION\(1, 2\)' `
    'Clock status begins at 1.2'
Assert-Match $source '#define PPS_MASTER_VERSION_STATUS\s+FPGA_CORE_VERSION\(1, 2\)' `
    'PPS Master status begins at 1.2'
Assert-Match $source '#define PPS_SLAVE_VERSION_STATUS\s+FPGA_CORE_VERSION\(1, 3\)' `
    'PPS Slave status begins at 1.3'
Assert-Match $source '#define TIMECODE_VERSION_STATUS\s+FPGA_CORE_VERSION\(1, 1\)' `
    'IRIG/DCF status begins at 1.1'
Assert-Match $source '#define TOD_VERSION_STATUS\s+FPGA_CORE_VERSION\(1, 2\)' `
    'ToD Slave status begins at 1.2'
Assert-Match $source '#define TOD_MASTER_VERSION_STATUS\s+FPGA_CORE_VERSION\(1, 1\)' `
    'ToD Master status begins at 1.1'
Assert-Match $source '#define TOD_MASTER_VERSION_GNSS\s+FPGA_CORE_VERSION\(1, 3\).*?#define TOD_MASTER_VERSION_RMC\s+FPGA_CORE_VERSION\(1, 4\).*?#define TOD_MASTER_VERSION_UTC\s+FPGA_CORE_VERSION\(1, 6\)' `
    'ToD Master GNSS, RMC and UTC revision gates'
Assert-Match $source '#define TOD_VERSION_NMEA_GNSS_TELEMETRY\s+FPGA_CORE_VERSION\(2, 2\).*?#define TOD_VERSION_PFEC\s+FPGA_CORE_VERSION\(2, 3\).*?#define TOD_MESSAGE_PFEC_MASK\s+0x7fU' `
    'NMEA telemetry and PFEC revision/mask contracts match ToD Slave 2.3'
Assert-Match $source 'nmea_local_offset_minutes_store.*?TOD_MASTER_LOCAL_MAX_MINUTES.*?ctrl\s*&\s*~TOD_MASTER_CTRL_ENABLE.*?local_offset.*?readback' `
    'NMEA local offset is range checked, disabled during write and verified'
Assert-Match $source 'nmea_message_disable_mask_store.*?ptp_ocp_nmea_message_mask.*?value\s*&\s*~mask.*?ptp_ocp_nmea_ctrl_update_locked' `
    'NMEA message mask is revision checked and applied safely'
Assert-Match $source 'nmea_gnss_store.*?TOD_MASTER_VERSION_GNSS.*?ptp_ocp_nmea_ctrl_update_locked' `
    'NMEA GNSS selector requires core 1.3 and preserves control bits'
Assert-Match $source 'nmea_enable_store.*?kstrtobool.*?TOD_MASTER_CTRL_ENABLE.*?readback' `
    'NMEA output enable is explicit and read-back verified'
Assert-Match $source 'nmea_uart_polarity_store.*?TOD_MASTER_VERSION_POLARITY.*?ctrl\s*&\s*~TOD_MASTER_CTRL_ENABLE.*?uart_polarity.*?readback_ctrl.*?TOD_MASTER_CTRL_ENABLE' `
    'NMEA polarity is revision gated and verifies enable restoration'
Assert-Match $source 'nmea_baud_rate_store.*?ptp_ocp_tod_baud_rates.*?ctrl\s*&\s*~TOD_MASTER_CTRL_ENABLE.*?uart_baud.*?readback_ctrl.*?TOD_MASTER_CTRL_ENABLE' `
    'NMEA baud uses the documented table and verifies enable restoration'
Assert-Match $source 'nmea_errors_store.*?TOD_MASTER_VERSION_STATUS.*?errors\s*!=\s*TOD_MASTER_STATUS_ERROR.*?nmea_out->status' `
    'NMEA sticky status uses the 1.1 gate and explicit W1C acknowledgement'
Assert-Match $source 'nmea_correction_seconds_store.*?correction\s*==\s*INT_MIN.*?ctrl\s*&\s*~TOD_MASTER_CTRL_ENABLE.*?adj_sec.*?readback_ctrl.*?TOD_MASTER_CTRL_ENABLE' `
    'NMEA signed correction is bounded and verifies enable restoration'
Assert-NoMatch $source 'ptp_ocp_nmea_out_init\s*\(' `
    'the synthesis-optional ToD Master must not be probed during PCI initialization'
Assert-NoMatch $source 'static void\s+ptp_ocp_info\((?:(?!static void\s+ptp_ocp_detach_sysfs).)*nmea_out->' `
    'probe diagnostics must not read the synthesis-optional ToD Master'
Assert-NoMatch $source 'static void\s+ptp_ocp_utc_distribute\((?:(?!static void\s+ptp_ocp_watchdog).)*nmea_out->' `
    'generic UTC distribution must not probe the synthesis-optional ToD Master'
Assert-NoMatch $source 'nmea_(local_offset_minutes|gnss|message_disable_mask).*?utc_info' `
    'NMEA sysfs settings must not access optional UTC-info registers'

Assert-Match $source 'ptp_ocp_init_clock\(.*?if \(ptp_ocp_clock_optional_registers_enabled\(bp\)\) \{.*?servo_offset_p.*?servo_drift_i.*?OCP_CTRL_ADJUST_SERVO' `
    'Clock servo MMIO is inside the explicit image-contract guard'
Assert-Match $source 'ptp_ocp_init_clock\(.*?version = ioread32\(&bp->reg->version\);.*?version == U32_MAX.*?return -ENODEV;.*?iowrite32\(0, &bp->reg->ctrl\)' `
    'Clock Version is validated before the first configuration write'
Assert-Match $source 'clock_status_drift_show\(.*?if \(!ptp_ocp_clock_optional_registers_enabled\(bp\)\).*?return -EOPNOTSUPP;.*?status_drift' `
    'optional Clock drift telemetry is guarded'
Assert-Match $source 'clock_status_offset_show\(.*?if \(!ptp_ocp_clock_optional_registers_enabled\(bp\)\).*?return -EOPNOTSUPP;.*?status_offset' `
    'optional Clock offset telemetry is guarded'

Assert-Match $source 'ptp_ocp_tod_init\(.*?Preserve the reset/image-selected protocol.*?ctrl = ioread32\(&bp->tod->ctrl\);.*?if \(!\(ctrl & TOD_CTRL_ENABLE\)\).*?ptp_ocp_tod_optional_utc_enabled\(bp\)' `
    'ToD initialization preserves the image-selected parser and gates UTC telemetry'
Assert-NoMatch $source 'ptp_ocp_tod_init\(.*?ctrl\s*&=\s*~\(TOD_CTRL_PROTOCOL_MASK' `
    'ToD initialization must not infer or overwrite a parser protocol'
Assert-Match $source 'ptp_ocp_watchdog\(.*?ptp_ocp_tod_optional_utc_enabled\(bp\).*?utc_status' `
    'watchdog UTC telemetry is opt-in and protocol/revision gated'
Assert-Match $source 'ptp_ocp_utc_distribute\(.*?irig_out.*?ptp_ocp_core_version_at_least.*?irig_in.*?ptp_ocp_core_version_at_least.*?dcf_out.*?ptp_ocp_core_version_at_least.*?dcf_in.*?ptp_ocp_core_version_at_least' `
    'UTC correction distribution validates every common timecode target core'
Assert-Match $source 'ptp_ocp_tod_status_show\(.*?if \(!tod_optional_telemetry\).*?return 0;.*?ptp_ocp_tod_optional_utc_enabled\(bp\).*?utc_status.*?ptp_ocp_tod_optional_gnss_enabled\(bp\).*?gnss_status.*?num_sat' `
    'debugfs optional telemetry is contract and revision gated'

Assert-Match $source '#define TOD_MESSAGE_NMEA_BASE_MASK\s+0x1bU' `
    'pre-2.0 NMEA message mask excludes the UTC/status message'
Assert-Match $source '#define TOD_MESSAGE_UBX_BASE_MASK\s+0x07U' `
    'pre-1.7 UBX message mask exposes only the original messages'
Assert-Match $source 'case 0:.*?TOD_VERSION_NMEA_STATUS_MESSAGE.*?case 1:.*?TOD_VERSION_GNSS_TELEMETRY.*?case 2:.*?TOD_MESSAGE_TSIP_MASK.*?case 3:.*?TOD_MESSAGE_ESIP_MASK.*?case 4:.*?TOD_MESSAGE_PFEC_MASK' `
    'ToD message masks depend on selected protocol and revision'
Assert-Match $source 'ptp_ocp_tod_protocol\[\].*?\.name = "PFEC",\s*\.value = 4' `
    'PFEC is exposed as protocol selector 4'
Assert-Match $source 'ptp_ocp_tod_protocol_supported\(.*?case 4:.*?TOD_VERSION_PFEC' `
    'PFEC selection requires ToD Slave 2.3'
Assert-Match $source 'ptp_ocp_tod_optional_utc_enabled\(.*?case 4:.*?TOD_VERSION_PFEC' `
    'PFEC UTC telemetry requires the optional contract and core 2.3'
Assert-Match $source 'ptp_ocp_tod_optional_gnss_enabled\(.*?case 0:.*?TOD_VERSION_NMEA_GNSS_TELEMETRY.*?case 4:.*?TOD_VERSION_PFEC' `
    'NMEA 2.2 and PFEC 2.3 GNSS telemetry use conservative revision gates'
Assert-Match $source 'disable_mask & ~\(mask >> TOD_CTRL_DISABLE_MESSAGE_SHIFT\)' `
    'unsupported ToD message-disable bits are rejected'

Assert-Match $source 'ptp_ocp_signal_irq\(.*?SIGNAL_VERSION_CURRENT_MAP.*?return IRQ_HANDLED;.*?reg->intr' `
    'legacy Signal Generator IRQ layouts are rejected before shifted MMIO'
Assert-Match $source 'ptp_ocp_signal_enable\(.*?SIGNAL_VERSION_CURRENT_MAP.*?return enable \? -EOPNOTSUPP : 0;.*?reg->intr_mask' `
    'Signal Generator operation requires the 1.3 register map'
Assert-Match $source '_ptp_ocp_signal_init\(.*?version < SIGNAL_VERSION_CURRENT_MAP\).*?return;.*?reg->polarity' `
    'Signal Generator initialization does not read shifted legacy fields'
Assert-Match $source 'ptp_ocp_timecode_status_reg\(.*?TIMECODE_VERSION_STATUS' `
    'IRIG/DCF status and W1C access uses the 1.1 gate'
Assert-Match $source '#define IRIG_MASTER_VERSION_AM\s+FPGA_CORE_VERSION\(1, 5\).*?#define IRIG_SLAVE_VERSION_CODE_YEAR\s+FPGA_CORE_VERSION\(1, 5\).*?#define IRIG_SLAVE_VERSION_AM\s+FPGA_CORE_VERSION\(1, 6\)' `
    'IRIG Master AM and Slave code/year/AM revision gates match current manuals'
Assert-Match $source 'offsetof\(struct irig_slave_reg, year\) == 0x24' `
    'IRIG Slave manual-year register uses offset 0x24'
Assert-Match $source 'irig_output_mode_store\(.*?ptp_ocp_irig_master_core\(bp,\s*IRIG_MASTER_VERSION_MODE_CONTROL\).*?ptp_ocp_irig_ctrl_update_locked' `
    'ordinary IRIG Master mode is revision gated without a synthesis contract'
Assert-Match $source 'irig_input_mode_store\(.*?ptp_ocp_irig_slave_core\(bp,\s*IRIG_SLAVE_VERSION_MODE_CONTROL\).*?ptp_ocp_irig_ctrl_update_locked' `
    'ordinary IRIG Slave mode is revision gated without a synthesis contract'
Assert-Match $source 'irig_b_mode_store\(.*?ptp_ocp_irig_master_core\(bp,\s*IRIG_MASTER_VERSION_MODE_CONTROL\).*?IRIG_CTRL_MODE_B.*?IRIG_CTRL_CODE_SHIFT' `
    'ordinary IRIG Master code selection is revision gated without a synthesis contract'
Assert-Match $source 'irig_output_control_bits_store\(.*?ptp_ocp_irig_master_feature\(bp,\s*IRIG_MASTER_VERSION_MODE_CONTROL\).*?old_control_bits.*?readback' `
    'synthesis-dependent IRIG Master control bits remain exact-contract gated'
Assert-Match $source 'irig_output_am_store\(.*?ptp_ocp_irig_master_feature\(bp, IRIG_MASTER_VERSION_AM\).*?ptp_ocp_irig_ctrl_update_locked' `
    'IRIG Master AM uses the exact contract and transactional control update'
Assert-Match $source 'irig_input_code_store\(.*?IRIG_SLAVE_VERSION_CODE_YEAR.*?code > 7.*?ptp_ocp_irig_ctrl_update_locked' `
    'IRIG Slave code selection is revision gated and bounded'
Assert-Match $source 'irig_input_manual_year_store\(.*?IRIG_SLAVE_VERSION_CODE_YEAR.*?IRIG_YEAR_MIN.*?IRIG_YEAR_MAX.*?IRIG_SLAVE_CTRL_YEAR_VALID.*?rollback:' `
    'IRIG Slave manual year uses the documented range, strobe and rollback'
Assert-Match $source 'irig_input_am_store\(.*?ptp_ocp_irig_slave_feature\(bp, IRIG_SLAVE_VERSION_AM\).*?ptp_ocp_irig_ctrl_update_locked' `
    'IRIG Slave AM uses the exact contract and transactional control update'

Assert-Match $readme 'optional_image_device=0000:03:00\.0.*?optional_image_version=0xXXXXXXXX.*?clock_optional_registers=1 tod_optional_telemetry=1.*?irig_optional_features=1' `
    'the legacy optional-register contract is documented with its PCI binding'
Assert-Match $readme 'optional_image_contracts=0000:03:00\.0=0xXXXXXXXX,0000:41:00\.0=0xYYYYYYYY' `
    'multi-card per-device exact-image contracts are documented'
Assert-Match $readme 'Unlisted cards remain gated even if they report\s+the same image word.*?Conflicting per-device entries and zero, invalid,\s*mismatched, loader, ART, or missing Image Versions can never satisfy the\s+contract' `
    'default and mismatch fail-closed behavior is documented'
Assert-Match $readme 'IRIG Master mode and code selection.*?require only Master 1\.2; they do not\s+require a synthesis opt-in' `
    'baseline IRIG Master mode and code behavior is documented'
Assert-Match $readme 'IRIG Slave mode selection similarly requires only\s+Slave 1\.3' `
    'baseline IRIG Slave mode behavior is documented'
Assert-Match $readme 'PFEC `0x7f` from 2.3' `
    'PFEC protocol mask is documented'
Assert-Match $readme 'configure.*UTC-to-TAI.*utc_tai_offset' `
    'manual UTC offset behavior is documented for the safe default'
Assert-Match $readme 'raw FPGA convention: `1` is normal' `
    'ToD raw polarity semantics are documented'
Assert-Match $readme 'does not probe, enable, or\s+rewrite this synthesis-optional core\s+during PCI initialization' `
    'NMEA boot-safety policy is documented'
Assert-Match $readme 'nmea_correction_seconds' `
    'explicit NMEA correction ownership is documented'

Write-Host 'Linux FPGA register contract checks passed.' -ForegroundColor Green
