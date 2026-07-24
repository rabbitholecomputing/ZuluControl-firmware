var timer;
var g_deviceType = 'Unknown';
var g_scsiSelectId = -1;
var g_scsiSelectType = '';
var g_iterImgs = [];

// Restore saved refresh settings once DOM is ready.
// load_version() (defined below) is invoked by version.js DOMContentLoaded listener.
document.addEventListener('DOMContentLoaded', function() {
  if (typeof(Storage) !== 'undefined') {
    var rt = localStorage.getItem('refreshTime');
    if (rt) document.getElementById('rt').value = rt;
    var ar = localStorage.getItem('autoRefreshOn');
    if (ar !== null) document.getElementById('ar').checked = !!ar;
  }
  if (!window.SlimSelect)
  {
    const slimSelectScript = document.createElement('script');
    slimSelectScript.src = 'slimselect.js'; 
    slimSelectScript.async = true;
    slimSelectScript.onload = function() {
      new SlimSelect({
        select: '#ide-newImg'});
      new SlimSelect({
        select: '#scsi-newImg'});
    }
    document.head.appendChild(slimSelectScript);
  }
  else
  {
    new SlimSelect({
      select: '#ide-newImg'});
    new SlimSelect({
      select: '#scsi-newImg'});
  }
});

// Override the load_version defined in version.js so we can show the right view.
function load_version() {
  fetch('version')
    .then(function(r) { return r.json(); })
    .then(function(version) {
      updateVersion(version);
      g_deviceType = version.deviceType || 'ZuluIDE';
      showView(g_deviceType);
      autoRefresh();
      refresh();
    });
}

function showView(devType) {
  document.getElementById('ctrl-row').removeAttribute('class');
  document.getElementById('version').removeAttribute('class');
  if (devType === 'ZuluSCSI') {
    document.getElementById('scsi-view').removeAttribute('class');
  } else {
    document.getElementById('ide-view').removeAttribute('class');
  }
}

function refresh() {
  fetch('status')
    .then(function(r) { return r.json(); })
    .then(function(status) { updateStatus(status); });
}

function updateStatus(status) {
  if (g_deviceType === 'ZuluSCSI') {
    scsiUpdateStatus(status);
  } else {
    ideUpdateStatus(status);
  }
}

function autoRefresh() {
  var refreshTimeElm = document.getElementById('rt');
  var interval = parseInt(refreshTimeElm.value, 10);
  var elm = document.getElementById('ar');
  if (typeof(Storage) !== 'undefined') {
    localStorage.setItem('autoRefreshOn', elm.checked ? 'true' : '');
    localStorage.setItem('refreshTime', refreshTimeElm.value);
  }
  if (elm.checked) {
    clearTimeout(timer);
    timer = setInterval(refresh, interval);
  } else if (timer) {
    clearTimeout(timer);
  }
}

// ── ZuluIDE ──────────────────────────────────────────────────────────────────

// The loaded image's media type is reported both as status.image.type
// (cdrom / zip100 / zip250 / zip750 / removable / harddrive) and, encoded in
// the filename, as a ZuluIDE image prefix (zipd / z100 / z250 / z750, plus the
// legacy long form "zipdrive"). Any of the Zip variants -- by either route --
// is shown as "Zip Drive"; everything else keeps the historical "CD-ROM" label.
function ideMediaTypeLabel(image) {
  if (image) {
    var type = (image.type || '').toLowerCase();
    if (type.indexOf('zip') === 0) return 'Zip Drive';
    var name = (image.filename || '').toLowerCase();
    var base = name.substring(name.lastIndexOf('/') + 1);
    if (/^(zipdrive|zipd|z100|z250|z750)/.test(base)) return 'Zip Drive';
  }
  return 'CD-ROM';
}

function ideUpdateStatus(status) {
  var sdElm = document.getElementById('ide-sd');
  sdElm.innerHTML = status.sdPresent ? 'Present' : 'Not present';
  sdElm.style.color = status.sdPresent ? '' : 'red';
  var elm = document.getElementById('ide-dt');
  elm.innerHTML = (status.isPrimary ? 'Primary' : 'Secondary') + ' ' + ideMediaTypeLabel(status.image);
  elm = document.getElementById('ide-img');
  var imgName = status.image ? status.image.filename : '(no image)';
  // isDeferred is serialized as the string "true"/"false" (the device writes
  // toString(bool), not a JSON boolean), so compare against the string rather
  // than testing truthiness -- the string "false" is itself truthy.
  if (status.isDeferred === 'true') {
    elm.innerHTML = imgName + '<br/>[Host deferred ejection]';
  } else {
    elm.innerHTML = imgName;
  }
}

function ideEjectClk() {
  fetch('eject')
    .then(function(r) { return r.json(); })
    .then(function(s) {
      if (s.status !== 'ok') alert('Eject failed.');
      else setTimeout(refresh, 1500);
    });
}

function ideSelectClk() {
  var sdElm = document.getElementById('ide-sd');
  if (sdElm) {
    sdElm.innerHTML = status.sdPresent ? 'Present' : 'Not present';
    sdElm.style.color = status.sdPresent ? '' : 'red';
  }
  document.getElementById('ide-st').setAttribute('class', 'hdn');
  document.getElementById('ide-si').removeAttribute('class');
  var ni = document.getElementById('ide-newImg');
  while (ni.options.length) { ni.options.remove(0); }
  g_iterImgs = [];
  ideLoadFns();
}

function ideCancelClicked() {
  document.getElementById('ide-si').setAttribute('class', 'hdn');
  document.getElementById('ide-st').removeAttribute('class');
  setTimeout(refresh, 1500);
}

function ideLoadClicked() {
  var si = document.getElementById('ide-newImg');
  fetch('image?' + new URLSearchParams({ imageName: si.value }))
    .then(function(r) { return r.json(); })
    .then(function(s) { if (s.status !== 'ok') alert('Select failed.'); });
  ideCancelClicked();
}

function ideLoadFns() {
  fetch('filenames')
    .then(function(r) { return r.json(); })
    .then(function(fns) {
      if (fns.status === 'wait') { setTimeout(ideLoadFns, 50); }
      else if (fns.status === 'overflow') { ideLoadImgIter(); }
      else { ideWriteFn(document.getElementById('ide-newImg'), fns); }
    });
}

function ideLoadImgIter() {
  fetch('nextImage')
    .then(function(r) { return r.json(); })
    .then(function(img) {
      if (img.status === 'wait') { setTimeout(ideLoadImgIter, 50); }
      else if (img.status === 'done') { ideWriteImgs(document.getElementById('ide-newImg')); }
      else { g_iterImgs.push(img); ideLoadImgIter(); }
    });
}

function ideWriteFn(ni, fns) {
  for (var i = 0; i < fns.filenames.length; i++) {
    ni.add(new Option(fns.filenames[i]));
  }
}

function ideWriteImgs(ni) {
  for (var i = 0; i < g_iterImgs.length; i++) {
    ni.add(new Option(g_iterImgs[i].filename));
  }
}

// ── ZuluSCSI ─────────────────────────────────────────────────────────────────

function scsiUpdateStatus(status) {
  var sdElm = document.getElementById('scsi-sd');
  if (sdElm) {
    sdElm.innerHTML = status.sdPresent ? 'Present' : 'Not present';
    sdElm.style.color = status.sdPresent ? '' : 'red';
  }
  var container = document.getElementById('scsi-devices');
  if (!status || !status.devices) { container.innerHTML = '<p>No devices.</p>'; return; }
  var html = '';
  for (var i = 0; i < status.devices.length; i++) {
    var d = status.devices[i];
    var img = d.image || '(no image)';
    var ejLabel = d.ejected ? ' [ejected]' : '';
    html += '<div class=\'dev-row\'>';
    html += 'ID ' + d.id + ' (' + d.type + '): <span class=\'wrap\'>' + img + ejLabel + '</span> ';
    html += '<button data-id=\'' + d.id + '\' class=\'eject-btn\' aria-label=\'Eject\'>&#x23CF;</button>';
    if (d.image && d.ejected) {
      html += '<button data-id=\'' + d.id + '\' class=\'insert-btn\'>&#x23F7; Insert</button>';
    }
    var typeIcon = 'Select Media';
    if (d.type === 'Zip' || d.type === 'Removable' || d.type == 'Floppy') {
      typeIcon = '&#x1F4BE;';
    } else if (d.type === 'CD-ROM' || d.type === 'MO') {
      typeIcon = '&#x1F4bF;';
    } else if (d.type === 'Tape') {
      typeIcon = '&#x1F4FC;';
    }


    html += '<button data-id=\'' + d.id + '\' data-type=\'' + d.type + '\' class=\'sel-btn\' aria-label=\'Load Media\'>' + typeIcon + '</button>';
    html += '</div>';
  }
  container.innerHTML = html;

  container.querySelectorAll('.eject-btn').forEach(function(btn) {
    btn.addEventListener('click', function() { scsiEjectClk(parseInt(btn.dataset.id, 10)); });
  });
  container.querySelectorAll('.insert-btn').forEach(function(btn) {
    btn.addEventListener('click', function() { scsiInsertClk(parseInt(btn.dataset.id, 10)); });
  });
  container.querySelectorAll('.sel-btn').forEach(function(btn) {
    btn.addEventListener('click', function() {
      scsiSelectClk(parseInt(btn.dataset.id, 10), btn.dataset.type);
    });
  });
}

function scsiEjectClk(id) {
  fetch('eject?' + new URLSearchParams({ scsiId: id }))
    .then(function(r) { return r.json(); })
    .then(function(s) {
      if (s.status !== 'ok') alert('Eject failed.');
      else setTimeout(refresh, 1500);
    });
}

function scsiInsertClk(id) {
  fetch('insertMedia?' + new URLSearchParams({ scsiId: id }))
    .then(function(r) { return r.json(); })
    .then(function(s) {
      if (s.status !== 'ok') alert('Insert failed.');
      else setTimeout(refresh, 1500);
    });
}

function scsiSelectClk(id, type) {
  g_scsiSelectId = id;
  g_scsiSelectType = type;
  g_iterImgs = [];
  document.getElementById('scsi-devices').setAttribute('class', 'hdn');
  document.getElementById('scsi-si').removeAttribute('class');
  document.getElementById('scsi-si-id').innerHTML = id;
  document.getElementById('scsi-si-type').innerHTML = type;
  var ni = document.getElementById('scsi-newImg');
  while (ni.options.length) { ni.options.remove(0); }
  scsiLoadFns(id);
}

function scsiCancelClicked() {
  document.getElementById('scsi-si').setAttribute('class', 'hdn');
  document.getElementById('scsi-devices').removeAttribute('class');
  setTimeout(refresh, 1500);
}

function scsiLoadClicked() {
  var si = document.getElementById('scsi-newImg');
  fetch('image?' + new URLSearchParams({ scsiId: g_scsiSelectId, imageName: si.value }))
    .then(function(r) { return r.json(); })
    .then(function(s) { if (s.status !== 'ok') alert('Select failed.'); });
  scsiCancelClicked();
}

function scsiLoadFns(id) {
  fetch('filenames?' + new URLSearchParams({ scsiId: id }))
    .then(function(r) { return r.json(); })
    .then(function(fns) {
      if (fns.status === 'wait') { setTimeout(function() { scsiLoadFns(id); }, 50); }
      else if (fns.status === 'overflow') { scsiLoadImgIter(id); }
      else { scsiWriteFn(document.getElementById('scsi-newImg'), fns); }
    });
}

function scsiLoadImgIter(id) {
  fetch('nextImage?' + new URLSearchParams({ scsiId: id }))
    .then(function(r) { return r.json(); })
    .then(function(img) {
      if (img.status === 'wait') { setTimeout(function() { scsiLoadImgIter(id); }, 50); }
      else if (img.status === 'done') { scsiWriteImgs(document.getElementById('scsi-newImg')); }
      else { g_iterImgs.push(img); scsiLoadImgIter(id); }
    });
}

function scsiWriteFn(ni, fns) {
  const fnOptions = Array.from(fns.filenames);
  fnOptions.sort((a, b) => a.localeCompare(b)); // Sort filenames alphabetically
  for (var i = 0; i < fnOptions.length; i++) {
    ni.add(new Option(fnOptions[i]));
  }
}

function scsiWriteImgs(ni) {
  // Use path as option value (full path needed by controlLoadImage); filename as display text
  for (var i = 0; i < g_iterImgs.length; i++) {
    var img = g_iterImgs[i];
    ni.add(new Option(img.filename, img.path || img.filename));
  }
}
