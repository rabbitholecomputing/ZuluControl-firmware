var timer;
var g_deviceType = 'Unknown';
var g_scsiSelectId = -1;
var g_scsiSelectType = '';
var g_iterImgs = [];
var g_scsiSlimSelect;
var g_ideSlimSelect;
var g_sdPresent = true;
var g_sdSeen = false;
var usageTimer;
var USAGE_POLL_MS = 250;

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
      g_ideSlimSelect = new SlimSelect({
        select: '#ide-newImg',
        settings: {
          disabled: true,
          placeholderText: "Loading..."
        }
      });
      g_scsiSlimSelect = new SlimSelect({
        select: '#scsi-newImg',
        settings: {
          disabled: true,
          placeholderText: "Loading..."
        }
      });
    }
    document.head.appendChild(slimSelectScript);
  }
  else
  {
    g_ideSlimSelect = new SlimSelect({
      select: '#ide-newImg',
      settings: {
        disabled: true,
        placeholderText: "Loading..."
      }
    });
    g_scsiSlimSelect = new SlimSelect({
      select: '#scsi-newImg',
      settings: {
        disabled: true,
        placeholderText: "Loading..."
      }
    })
  }
});

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

function hideElementById(elem_id) {
  document.getElementById(elem_id).classList.add('hdn');
}

function showElementById(elem_id) {
  document.getElementById(elem_id).classList.remove('hdn');
}

function isElementShown(elem_id) {
  var elm = document.getElementById(elem_id);
  return !!elm && !elm.classList.contains('hdn');
}

function showView(devType) {
  showElementById('ctrl-row');
  showElementById('version');
  if (devType === 'ZuluSCSI') {
    showElementById('scsi-view');
  } else {
    showElementById('ide-view');
  }
}

function refresh() {
  fetch('status')
    .then(function(r) { return r.json(); })
    .then(function(status) { updateStatus(status); })
    .catch(function(err) { console.error('Status fetch failed: ' + err); });
}

function formatBytes(bytes) {
  if (bytes < 1024) return bytes + ' B';
  return (bytes / 1024).toFixed(1) + ' kB';
}

function percentOf(used, total) {
  if (!total) return '';
  return ' (' + Math.round((used * 100) / total) + '%)';
}

function loadUsage() {
  clearTimeout(usageTimer);
  fetch('usage')
    .then(function(r) { return r.json(); })
    .then(function(usage) {
      updateUsage(usage);
      if (usage.done)
      {
        hideElementById('ufc-loading');
        if (g_deviceType === 'ZuluIDE')
        {
          ideLoadFns();
        }
        else if (g_deviceType === 'ZuluSCSI')
        {
          if (isElementShown('scsi-si') && g_scsiSelectId >= 0)
          {
            scsiLoadFns(g_scsiSelectId);
          }
        }
      }
      else {
        showElementById('ufc-loading');
        usageTimer = setTimeout(loadUsage, USAGE_POLL_MS);
      }
    })
    .catch(function(err) {
      g_sdSeen = false;
      console.error('Usage fetch failed: ' + err);
      hideElementById('ufc-loading');
      document.getElementById('ufc').innerHTML = 'unavailable';
      document.getElementById('uci').innerHTML = 'unavailable';
    });
}

function updateUsage(usage) {
  var cache = usage.filenameCache || {};
  var index = usage.filenameIndex || {};
  var bytesUsed = cache.bytesUsed || 0;
  var bytesTotal = cache.bytesTotal || 0;
  var imgsUsed = index.imagesUsed || 0;
  var imgsTotal = index.imagesTotal || 0;

  document.getElementById('ufc').innerHTML =
    formatBytes(bytesUsed) + ' of ' + formatBytes(bytesTotal) + percentOf(bytesUsed, bytesTotal);
  document.getElementById('uci').innerHTML =
    imgsUsed + ' of ' + imgsTotal + ' images' + percentOf(imgsUsed, imgsTotal);
}

function updateStatus(status) {
  g_sdPresent = !!status.sdPresent;
  if (!g_sdPresent) {
    g_sdSeen = false;
    clearTimeout(usageTimer);
    clearImageLists();
    hideElementById('ufc-loading');
    document.getElementById('ufc').innerHTML = 'no SD card';
    document.getElementById('uci').innerHTML = 'no SD card';
  } else if (!g_sdSeen) {
    g_sdSeen = true;
    loadUsage();
  }

  if (g_deviceType === 'ZuluSCSI') {
    scsiUpdateStatus(status);
  } else {
    ideUpdateStatus(status);
  }
}

// -- Image select list ---------------------------------------------------------
function clearOptions(ni) {
  if (ni) { while (ni.options.length) { ni.options.remove(0); } }
}

function clearImageList(selectId, loadBtnId, slimSelect) {
  clearOptions(document.getElementById(selectId));
  var btn = document.getElementById(loadBtnId);
  if (btn) btn.disabled = true;
  slimSelect && (slimSelect.disable());
}

// Both views' lists (only one is ever visible) plus the iterator buffer they
// are built from.
function clearImageLists() {
  g_iterImgs = [];
  clearImageList('ide-newImg', 'ide-si-load', g_ideSlimSelect);
  clearImageList('scsi-newImg', 'scsi-si-load', g_scsiSlimSelect);
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

// -- ZuluIDE  ------------------------------------------------------------------
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
  hideElementById('ide-st');
  showElementById('ide-si');
  if (document.getElementById('ide-newImg').options.length === 0) {
    g_iterImgs = [];
    ideLoadFns();
  }
}

function ideCancelClicked() {
  hideElementById('ide-si');
  showElementById('ide-st');
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
  const selectFns = document.getElementById('ide-newImg');
  fetch('filenames')
    .then(function(r) { return r.json(); })
    .then(function(fns) {
      if (fns.status === 'wait') {
        document.getElementById('ide-si-load').disabled = true;
        g_ideSlimSelect && (g_ideSlimSelect.disable());
        setTimeout(ideLoadFns, 250);
      }
      // The iterator appends to g_iterImgs, so start each walk from empty.
      else if (fns.status === 'overflow') { g_iterImgs = []; ideLoadImgIter(); }
      else if (!g_sdPresent) { clearImageLists(); }
      else {
        ideWriteFn(selectFns, fns);
        g_ideSlimSelect && (g_ideSlimSelect.enable());
        document.getElementById('ide-si-load').disabled = false;
      }
    })
    .catch(function(err) { console.error('Filenames fetch failed: ' + err); });
}

function ideLoadImgIter() {
  fetch('nextImage')
    .then(function(r) { return r.json(); })
    .then(function(img) {
      if (img.status === 'wait') {
        document.getElementById('ide-si-load').disabled = true;
        g_ideSlimSelect && (g_ideSlimSelect.disable());
        setTimeout(ideLoadImgIter, 250);
      }
      else if (img.status === 'done' && !g_sdPresent) { clearImageLists(); }
      else if (img.status === 'done') {
        ideWriteImgs(document.getElementById('ide-newImg'));
        g_ideSlimSelect && (g_ideSlimSelect.enable());
        document.getElementById('ide-si-load').disabled = false;
      }
      else { g_iterImgs.push(img); ideLoadImgIter();}
    });
}

function ideWriteFn(ni, fns) {
  clearOptions(ni);
  for (var i = 0; i < fns.filenames.length; i++) {
    ni.add(new Option(fns.filenames[i]));
  }
}

function ideWriteImgs(ni) {
  clearOptions(ni);
  for (var i = 0; i < g_iterImgs.length; i++) {
    ni.add(new Option(g_iterImgs[i].filename));
  }
}

// -- ZuluSCSI ------------------------------------------------------------------

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
  hideElementById('scsi-devices');
  showElementById('scsi-si');
  document.getElementById('scsi-si-id').innerHTML = id;
  document.getElementById('scsi-si-type').innerHTML = type;
  var ni = document.getElementById('scsi-newImg');
  while (ni.options.length) { ni.options.remove(0); }
  scsiLoadFns(id);
}

function scsiCancelClicked() {
  hideElementById('scsi-si');
  showElementById('scsi-devices');
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
      if (fns.status === 'wait') {
        g_scsiSlimSelect && (g_scsiSlimSelect.disable());
        document.getElementById('scsi-si-load').disabled = true;
        setTimeout(function() { 
          scsiLoadFns(id);
        }, 250);
      }
      // The iterator appends to g_iterImgs, so start each walk from empty.
      else if (fns.status === 'overflow') { g_iterImgs = []; scsiLoadImgIter(id); }
      else if (!g_sdPresent) { clearImageLists(); }
      else {
        scsiWriteFn(document.getElementById('scsi-newImg'), fns);
        g_scsiSlimSelect && (g_scsiSlimSelect.enable());
        document.getElementById('scsi-si-load').disabled = false;
      }
    })
    .catch(function(err) { console.error('Filenames fetch failed: ' + err); });
}

function scsiLoadImgIter(id) {
  fetch('nextImage?' + new URLSearchParams({ scsiId: id }))
    .then(function(r) { return r.json(); })
    .then(function(img) {
      g_scsiSlimSelect && (g_scsiSlimSelect.disable());
      document.getElementById('scsi-si-load').disabled = true;
      if (img.status === 'wait') {
        setTimeout(function() { 
          scsiLoadImgIter(id);
        }, 250);
      }
      else if (img.status === 'done' && !g_sdPresent) { clearImageLists(); }
      else if (img.status === 'done') {
        scsiWriteImgs(document.getElementById('scsi-newImg'));
        g_scsiSlimSelect && (g_scsiSlimSelect.enable());
        document.getElementById('scsi-si-load').disabled = false;
      }
      else { g_iterImgs.push(img); scsiLoadImgIter(id);}
    });
}

function scsiWriteFn(ni, fns) {
  clearOptions(ni);
  const fnOptions = Array.from(fns.filenames);
  fnOptions.sort((a, b) => a.localeCompare(b)); // Sort filenames alphabetically
  for (var i = 0; i < fnOptions.length; i++) {
    ni.add(new Option(fnOptions[i]));
  }
}

function scsiWriteImgs(ni) {
  // Use path as option value (full path needed by controlLoadImage); filename as display text
  clearOptions(ni);
  for (var i = 0; i < g_iterImgs.length; i++) {
    var img = g_iterImgs[i];
    ni.add(new Option(img.filename, img.path || img.filename));
  }
}
