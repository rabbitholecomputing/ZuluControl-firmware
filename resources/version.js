document.addEventListener('DOMContentLoaded', (event) => {
    load_version();
});

function load_version()
{
    fetch('version')
    .then(response => response.json())
    .then(version => updateVersion(version));
}

function updateVersion(version) {
    let elm = document.getElementById('cav');
    if (elm) elm.innerHTML = version.clientAPIVersion;

    elm = document.getElementById('cfv');
    if (elm) elm.innerHTML = version.clientFWVersion;

    elm = document.getElementById('sdt');
    // Label the server side of the I2C link with the actual peer when known
    // ("ZuluIDE"/"ZuluSCSI"), falling back to the generic "ZuluIDE/SCSI".
    if (elm && (version.deviceType === 'ZuluIDE' || version.deviceType === 'ZuluSCSI'))
     elm.innerHTML = version.deviceType;

    elm = document.getElementById('sav');
    if (elm && version.serverAPIVersion)
     // serverAPIVersion is "<number> <DeviceName>" (e.g. "5.0.0 ZuluIDE"); the
     // device name is already covered by the fixed "ZuluIDE/SCSI" label, so show
     // only the version number here.
     elm.innerHTML = version.serverAPIVersion.split(' ')[0];

    elm = document.getElementById('avm');
    if (elm)
     // Present only on a major-version mismatch; the server-provided message
     // carries the repository-release link. Render it on its own line above the
     // firmware-upgrade link (empty -> nothing shown, upgrade link stays put).
     elm.innerHTML = version.message ? (version.message + '<br/>') : '';
}
