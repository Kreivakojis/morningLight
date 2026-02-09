// MorningLight Web UI

const api = {
    async get(url) {
        const res = await fetch(url);
        return res.json();
    },
    async post(url, data) {
        const res = await fetch(url, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(data)
        });
        return res.json();
    }
};

// State
let alarms = [];
let status = {};
let animationPresets = [];
let darkModeSchedules = [];

// DOM elements
const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => document.querySelectorAll(sel);

// Navigation
$$('.nav-btn').forEach(btn => {
    btn.addEventListener('click', () => {
        $$('.nav-btn').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        $$('.page').forEach(p => p.classList.remove('active'));
        $(`#page-${btn.dataset.page}`).classList.add('active');
    });
});

// Status updates
async function updateStatus() {
    try {
        status = await api.get('/api/status');

        // Update indicator
        const indicator = $('#status-indicator');
        indicator.className = 'status-dot';
        if (status.wifi_state === 'connected') {
            indicator.classList.add('connected');
        } else if (status.setup_complete) {
            indicator.classList.add('warning');
        }

        // Update values
        $('#wifi-status').textContent = status.wifi_state || '--';
        $('#time-display').textContent = status.time || '--:--';
        $('#sunrise-status').textContent = status.sunrise_state || 'idle';
        $('#brightness-display').textContent = `${status.brightness || 0}%`;

        var fmtTemp = function(v) { return (v !== null && v !== undefined) ? v.toFixed(1) + '\u00B0C' : '--'; };
        $('#temp-internal').textContent = fmtTemp(status.temp_internal);
        $('#temp-external-1').textContent = fmtTemp(status.temp_external_1);
        $('#temp-external-2').textContent = fmtTemp(status.temp_external_2);

        // Dark mode indicator
        const banner = $('#dark-mode-banner');
        if (banner) {
            banner.style.display = status.dark_mode_active ? 'block' : 'none';
        }

    } catch (e) {
        console.error('Status update failed:', e);
    }
}

// LED test - color temperature and brightness sliders
$('#test-color-temp').addEventListener('input', (e) => {
    $('#test-color-temp-val').textContent = `${colorTempFromSlider(parseInt(e.target.value))}K`;
});

$('#test-brightness').addEventListener('input', (e) => {
    $('#test-brightness-val').textContent = `${e.target.value}%`;
});

$('#btn-apply-test').addEventListener('click', async () => {
    const color_temp = colorTempFromSlider(parseInt($('#test-color-temp').value));
    const brightness = parseInt($('#test-brightness').value);
    await api.post('/api/led/test', { color_temp, brightness });
});

$('#btn-off').addEventListener('click', async () => {
    $('#test-brightness').value = 0;
    $('#test-brightness-val').textContent = '0%';
    await api.post('/api/led/test', { off: true });
});

// Alarms
async function loadAlarms() {
    try {
        const data = await api.get('/api/alarms');
        alarms = data.alarms || [];
        renderAlarms();
    } catch (e) {
        console.error('Failed to load alarms:', e);
    }
}

function renderAlarms() {
    const list = $('#alarm-list');

    if (alarms.length === 0) {
        list.innerHTML = '<p class="loading" style="opacity:0.5">No alarms set</p>';
        return;
    }

    const days = ['Su', 'Mo', 'Tu', 'We', 'Th', 'Fr', 'Sa'];

    list.innerHTML = alarms.map(a => {
        const time = `${String(a.hour).padStart(2, '0')}:${String(a.minute).padStart(2, '0')}`;
        const activeDays = days.filter((_, i) => a.days_mask & (1 << i)).join(' ');

        return `
            <div class="alarm-item" data-id="${a.id}">
                <div class="alarm-time">${time}</div>
                <div class="alarm-info">
                    <div class="alarm-days">${activeDays || 'No days'}</div>
                </div>
                <div class="alarm-toggle ${a.enabled ? 'active' : ''}" data-id="${a.id}"></div>
            </div>
        `;
    }).join('');

    // Toggle handlers
    $$('.alarm-toggle').forEach(toggle => {
        toggle.addEventListener('click', async (e) => {
            e.stopPropagation();
            const id = parseInt(toggle.dataset.id);
            const alarm = alarms.find(a => a.id === id);
            if (alarm) {
                alarm.enabled = !alarm.enabled;
                await api.post('/api/alarms', alarm);
                toggle.classList.toggle('active');
            }
        });
    });

    // Edit handlers
    $$('.alarm-item').forEach(item => {
        item.addEventListener('click', () => {
            const id = parseInt(item.dataset.id);
            editAlarm(id);
        });
    });
}

function editAlarm(id) {
    const alarm = id >= 0 ? alarms.find(a => a.id === id) : null;

    $('#alarm-modal-title').textContent = alarm ? 'Edit Alarm' : 'New Alarm';
    $('#alarm-id').value = alarm ? alarm.id : -1;
    $('#alarm-hour').value = alarm ? alarm.hour : 7;
    $('#alarm-minute').value = alarm ? alarm.minute : 0;
    $('#alarm-duration').value = alarm ? alarm.duration_min : 30;
    $('#alarm-duration-val').textContent = `${alarm ? alarm.duration_min : 30} min`;
    $('#alarm-color-temp').value = alarm ? alarm.color_temp : 3000;
    $('#alarm-color-temp-val').textContent = `${alarm ? alarm.color_temp : 3000}K`;
    $('#alarm-brightness').value = alarm ? alarm.brightness : 100;
    $('#alarm-brightness-val').textContent = `${alarm ? alarm.brightness : 100}%`;

    // Days
    const daysMask = alarm ? alarm.days_mask : 0b0111110; // Mon-Fri
    $$('.day-picker input').forEach(cb => {
        const day = parseInt(cb.dataset.day);
        cb.checked = (daysMask & (1 << day)) !== 0;
    });

    // Animation preset dropdown
    updateAlarmAnimationDropdown();
    $('#alarm-animation-preset').value = alarm ? alarm.animation_preset : -1;

    // Cooldown
    const cooldownVal = alarm ? (alarm.cooldown_min || 0) : 0;
    $('#alarm-cooldown').value = cooldownVal;
    $('#alarm-cooldown-val').textContent = cooldownVal > 0 ? `${cooldownVal} min` : 'Off';

    $('#alarm-modal').classList.add('show');
}

function updateAlarmAnimationDropdown() {
    const select = $('#alarm-animation-preset');
    const val = select.value;
    select.innerHTML = '<option value="-1">Classic (uniform brightness)</option>' +
        animationPresets.map(p => `<option value="${p.id}">${p.name}</option>`).join('');
    select.value = val;
}

$('#btn-add-alarm').addEventListener('click', () => editAlarm(-1));

$('#btn-alarm-cancel').addEventListener('click', () => {
    $('#alarm-modal').classList.remove('show');
});

function timeInWindow(t, start, end) {
    // Check if time t falls within [start, end) handling midnight wrap
    if (end <= 1440) {
        return t >= start && t < end;
    }
    return t >= start || t < (end % 1440);
}

function checkAlarmClash(savingAlarm) {
    const startA = savingAlarm.hour * 60 + savingAlarm.minute;
    const endA = startA + savingAlarm.duration_min + (savingAlarm.cooldown_min || 0);

    const clashes = [];
    for (const other of alarms) {
        if (other.id === savingAlarm.id || !other.enabled) continue;
        if ((savingAlarm.days_mask & other.days_mask) === 0) continue;

        const startB = other.hour * 60 + other.minute;
        const endB = startB + other.duration_min + (other.cooldown_min || 0);

        // Check if other alarm's start falls within saving alarm's full window
        if (timeInWindow(startB, startA, endA)) { clashes.push(other); continue; }
        // Check if saving alarm's start falls within other alarm's full window
        if (timeInWindow(startA, startB, endB)) { clashes.push(other); }
    }

    return clashes.length > 0 ? clashes : null;
}

$('#alarm-form').addEventListener('submit', async (e) => {
    e.preventDefault();

    let daysMask = 0;
    $$('.day-picker input').forEach(cb => {
        if (cb.checked) {
            daysMask |= (1 << parseInt(cb.dataset.day));
        }
    });

    const alarm = {
        id: parseInt($('#alarm-id').value),
        enabled: true,
        hour: parseInt($('#alarm-hour').value),
        minute: parseInt($('#alarm-minute').value),
        duration_min: parseInt($('#alarm-duration').value),
        days_mask: daysMask,
        color_temp: parseInt($('#alarm-color-temp').value),
        brightness: parseInt($('#alarm-brightness').value),
        animation_preset: parseInt($('#alarm-animation-preset').value),
        cooldown_min: parseInt($('#alarm-cooldown').value)
    };

    if (alarm.id < 0) delete alarm.id;

    // Check for time clashes with other alarms (active + cooldown windows)
    const clashes = checkAlarmClash(alarm);
    if (clashes) {
        const details = clashes.map(c =>
            `${String(c.hour).padStart(2,'0')}:${String(c.minute).padStart(2,'0')}`
        ).join(', ');
        alert(`This alarm overlaps with alarm(s) at ${details}. ` +
              `Please adjust the start time or duration to avoid overlap.`);
        return;
    }

    await api.post('/api/alarms', alarm);
    $('#alarm-modal').classList.remove('show');
    loadAlarms();
});

// Slider value updates
$('#alarm-duration').addEventListener('input', (e) => {
    $('#alarm-duration-val').textContent = `${e.target.value} min`;
});

$('#alarm-color-temp').addEventListener('input', (e) => {
    $('#alarm-color-temp-val').textContent = `${e.target.value}K`;
});

$('#alarm-brightness').addEventListener('input', (e) => {
    $('#alarm-brightness-val').textContent = `${e.target.value}%`;
});

$('#alarm-cooldown').addEventListener('input', (e) => {
    const val = parseInt(e.target.value);
    $('#alarm-cooldown-val').textContent = val > 0 ? `${val} min` : 'Off';
});

// WiFi
$('#btn-scan').addEventListener('click', async () => {
    $('#wifi-networks').innerHTML = '<div class="loading">Scanning</div>';

    try {
        const data = await api.get('/api/wifi/scan');
        const networks = data.networks || [];

        if (networks.length === 0) {
            $('#wifi-networks').innerHTML = '<p class="loading" style="opacity:0.5">No networks found</p>';
            return;
        }

        $('#wifi-networks').innerHTML = networks.map(n => `
            <div class="wifi-item" data-ssid="${n.ssid}" data-secure="${n.secure}">
                <span class="wifi-name">${n.ssid}</span>
                <span class="wifi-signal">${n.rssi} dBm</span>
                ${n.secure ? '<span class="wifi-lock">&#x1F512;</span>' : ''}
            </div>
        `).join('');

        $$('.wifi-item').forEach(item => {
            item.addEventListener('click', () => {
                $('#wifi-ssid').value = item.dataset.ssid;
                $('#wifi-connect-form').style.display = 'block';
                if (item.dataset.secure === 'false') {
                    $('#wifi-password').style.display = 'none';
                } else {
                    $('#wifi-password').style.display = 'block';
                }
            });
        });
    } catch (e) {
        $('#wifi-networks').innerHTML = '<p class="loading" style="opacity:0.5">Scan failed</p>';
    }
});

$('#btn-connect').addEventListener('click', async () => {
    const ssid = $('#wifi-ssid').value;
    const password = $('#wifi-password').value;

    $('#btn-connect').textContent = 'Connecting...';
    $('#btn-connect').disabled = true;

    try {
        await api.post('/api/wifi/connect', { ssid, password });
        $('#wifi-connect-form').style.display = 'none';
        alert('Connecting to WiFi. The device will restart.');
    } catch (e) {
        alert('Connection failed');
    } finally {
        $('#btn-connect').textContent = 'Connect';
        $('#btn-connect').disabled = false;
    }
});

// Time sync
$('#btn-sync-time').addEventListener('click', async () => {
    await api.post('/api/time/sync', {});
    updateStatus();
});

// Settings
$('#max-brightness').addEventListener('input', (e) => {
    $('#max-brightness-val').textContent = `${e.target.value}%`;
});

$('#pwm-frequency').addEventListener('input', (e) => {
    $('#pwm-frequency-val').textContent = `${e.target.value} Hz`;
});

// LED type selection
$('#led-type').addEventListener('change', (e) => {
    const isWS2811 = e.target.value === '1';
    $('#led-count-group').style.display = isWS2811 ? 'block' : 'none';
    $('#pwm-freq-group').style.display = isWS2811 ? 'none' : 'block';
});

$('#btn-save-settings').addEventListener('click', async () => {
    const ledType = parseInt($('#led-type').value);
    const config = {
        timezone: $('#timezone').value,
        brightness_max: parseInt($('#max-brightness').value),
        led_type: ledType,
        led_count: parseInt($('#led-count').value)
    };

    // Only include PWM frequency for PWM mode
    if (ledType === 0) {
        config.pwm_frequency = parseInt($('#pwm-frequency').value);
    }

    const result = await api.post('/api/config', config);

    if (result.restart_required) {
        if (confirm('LED settings changed. Device needs to restart. Restart now?')) {
            await api.post('/api/reboot', {});
            alert('Device is rebooting...');
        }
    } else {
        alert('Settings saved');
    }
});

// Load config
async function loadConfig() {
    try {
        const config = await api.get('/api/config');
        $('#timezone').value = config.timezone || 'UTC0';
        $('#max-brightness').value = config.brightness_max || 100;
        $('#max-brightness-val').textContent = `${config.brightness_max || 100}%`;
        $('#pwm-frequency').value = config.pwm_frequency || 200;
        $('#pwm-frequency-val').textContent = `${config.pwm_frequency || 200} Hz`;

        // LED configuration
        const ledType = config.led_type || 0;
        $('#led-type').value = ledType;
        $('#led-count').value = config.led_count || 30;

        // Show/hide fields based on LED type
        const isWS2811 = ledType === 1;
        $('#led-count-group').style.display = isWS2811 ? 'block' : 'none';
        $('#pwm-freq-group').style.display = isWS2811 ? 'none' : 'block';
    } catch (e) {
        console.error('Failed to load config:', e);
    }
}

// Animation
async function loadAnimationPresets() {
    try {
        const data = await api.get('/api/animation/presets');
        animationPresets = data.presets || [];

        // Update preset dropdown
        const select = $('#anim-preset');
        select.innerHTML = animationPresets.map(p =>
            `<option value="${p.id}">${p.name}</option>`
        ).join('');

        // Update status
        $('#anim-running').textContent = data.running ? 'Running' : 'Stopped';
        $('#anim-running').style.color = data.running ? '#4caf50' : '';

        // Load first preset into form
        if (animationPresets.length > 0) {
            loadPresetIntoForm(0);
        }
    } catch (e) {
        console.error('Failed to load animation presets:', e);
    }
}

// Logarithmic slider conversions
// Speed: 0.05 - 5.0 Hz, slider 0-100
function speedFromSlider(pos) {
    var hz = 0.05 * Math.pow(100, pos / 100);
    return Math.round(hz / 0.05) * 0.05;
}
function speedToSlider(hz) {
    if (hz <= 0.05) return 0;
    return Math.round(50 * Math.log10(hz / 0.05));
}
function formatSpeed(hz) {
    return hz < 1.0 ? hz.toFixed(2) : hz.toFixed(1);
}

// Color temp: 2000 - 6500K, slider 0-100
function colorTempFromSlider(pos) {
    return Math.round(2000 * Math.pow(3.25, pos / 100) / 100) * 100;
}
function colorTempToSlider(k) {
    if (k <= 2000) return 0;
    return Math.round(100 * Math.log(k / 2000) / Math.log(3.25));
}

function loadPresetIntoForm(id) {
    const preset = animationPresets.find(p => p.id === id);
    if (!preset) return;

    $('#anim-name').value = preset.name;
    $('#anim-wavelength').value = preset.wavelength;
    $('#anim-wavelength-val').textContent = preset.wavelength;
    $('#anim-amplitude').value = Math.round(preset.amplitude / 10) * 10;
    $('#anim-amplitude-val').textContent = `${Math.round(preset.amplitude / 10) * 10}%`;
    $('#anim-speed').value = speedToSlider(preset.speed);
    $('#anim-speed-val').textContent = `${formatSpeed(preset.speed)} Hz`;
    $('#anim-base').value = Math.round(preset.base_brightness / 10) * 10;
    $('#anim-base-val').textContent = `${Math.round(preset.base_brightness / 10) * 10}%`;
    $('#anim-variation').value = Math.round(preset.variation / 10) * 10;
    $('#anim-variation-val').textContent = `${Math.round(preset.variation / 10) * 10}%`;
    $('#anim-color-temp').value = colorTempToSlider(preset.color_temp);
    $('#anim-color-temp-val').textContent = `${preset.color_temp}K`;
}

$('#anim-preset').addEventListener('change', (e) => {
    loadPresetIntoForm(parseInt(e.target.value));
});

// Animation slider value updates
$('#anim-wavelength').addEventListener('input', (e) => {
    $('#anim-wavelength-val').textContent = e.target.value;
});

$('#anim-amplitude').addEventListener('input', (e) => {
    $('#anim-amplitude-val').textContent = `${e.target.value}%`;
});

$('#anim-speed').addEventListener('input', (e) => {
    const speed = speedFromSlider(parseInt(e.target.value));
    $('#anim-speed-val').textContent = `${formatSpeed(speed)} Hz`;
});

$('#anim-base').addEventListener('input', (e) => {
    $('#anim-base-val').textContent = `${e.target.value}%`;
});

$('#anim-variation').addEventListener('input', (e) => {
    $('#anim-variation-val').textContent = `${e.target.value}%`;
});

$('#anim-color-temp').addEventListener('input', (e) => {
    const k = colorTempFromSlider(parseInt(e.target.value));
    $('#anim-color-temp-val').textContent = `${k}K`;
});

// Save animation preset
$('#anim-save').addEventListener('click', async () => {
    const id = parseInt($('#anim-preset').value);
    const preset = {
        id: id,
        name: $('#anim-name').value,
        wavelength: parseFloat($('#anim-wavelength').value),
        amplitude: parseInt($('#anim-amplitude').value),
        speed: parseFloat(speedFromSlider(parseInt($('#anim-speed').value)).toFixed(3)),
        base_brightness: parseInt($('#anim-base').value),
        variation: parseInt($('#anim-variation').value),
        color_temp: colorTempFromSlider(parseInt($('#anim-color-temp').value))
    };

    const result = await api.post('/api/animation/presets', preset);
    if (result.success) {
        // Reload presets to update dropdown
        await loadAnimationPresets();
        alert('Preset saved');
    }
});

// Start animation
$('#anim-start').addEventListener('click', async () => {
    const presetId = parseInt($('#anim-preset').value);
    const result = await api.post('/api/animation/start', { preset_id: presetId });
    if (result.success) {
        $('#anim-running').textContent = 'Running';
        $('#anim-running').style.color = '#4caf50';
    } else {
        alert('Failed to start animation: ' + (result.error || 'Unknown error'));
    }
});

// Stop animation
$('#anim-stop').addEventListener('click', async () => {
    await api.post('/api/animation/stop', {});
    $('#anim-running').textContent = 'Stopped';
    $('#anim-running').style.color = '';
});

// Dark Mode
async function loadDarkMode() {
    try {
        const data = await api.get('/api/darkmode');
        darkModeSchedules = data.schedules || [];
        renderDarkMode();
    } catch (e) {
        console.error('Failed to load dark mode:', e);
    }
}

function renderDarkMode() {
    const list = $('#darkmode-list');
    const enabled = darkModeSchedules.filter(s => s.enabled);

    if (enabled.length === 0) {
        list.innerHTML = '<p class="loading" style="opacity:0.5">No schedules</p>';
        return;
    }

    const days = ['Su', 'Mo', 'Tu', 'We', 'Th', 'Fr', 'Sa'];

    list.innerHTML = enabled.map(s => {
        const start = `${String(s.start_hour).padStart(2,'0')}:${String(s.start_minute).padStart(2,'0')}`;
        const end = `${String(s.end_hour).padStart(2,'0')}:${String(s.end_minute).padStart(2,'0')}`;
        const activeDays = days.filter((_, i) => s.days_mask & (1 << i)).join(' ');
        const override = s.allow_override ? ' (override)' : '';

        return `
            <div class="alarm-item darkmode-item" data-id="${s.id}">
                <div class="alarm-time">${start} - ${end}</div>
                <div class="alarm-info">
                    <div class="alarm-days">${activeDays}${override}</div>
                </div>
                <div class="alarm-toggle ${s.enabled ? 'active' : ''}" data-id="${s.id}"></div>
            </div>
        `;
    }).join('');

    // Toggle handlers
    $$('.darkmode-item .alarm-toggle').forEach(toggle => {
        toggle.addEventListener('click', async (e) => {
            e.stopPropagation();
            const id = parseInt(toggle.dataset.id);
            const schedule = darkModeSchedules.find(s => s.id === id);
            if (schedule) {
                schedule.enabled = !schedule.enabled;
                await api.post('/api/darkmode', schedule);
                toggle.classList.toggle('active');
            }
        });
    });

    // Edit handlers
    $$('.darkmode-item').forEach(item => {
        item.addEventListener('click', () => editDarkMode(parseInt(item.dataset.id)));
    });
}

function editDarkMode(id) {
    const schedule = id >= 0 ? darkModeSchedules.find(s => s.id === id) : null;

    $('#darkmode-modal-title').textContent = schedule ? 'Edit Dark Mode' : 'New Dark Mode';
    $('#darkmode-id').value = schedule ? schedule.id : -1;
    $('#darkmode-start-hour').value = schedule ? schedule.start_hour : 22;
    $('#darkmode-start-minute').value = schedule ? schedule.start_minute : 0;
    $('#darkmode-end-hour').value = schedule ? schedule.end_hour : 6;
    $('#darkmode-end-minute').value = schedule ? schedule.end_minute : 0;
    $('#darkmode-override').checked = schedule ? schedule.allow_override : false;

    $('#btn-darkmode-delete').style.display = schedule ? '' : 'none';

    const daysMask = schedule ? schedule.days_mask : 0b1111111;
    $$('#darkmode-days input').forEach(cb => {
        cb.checked = (daysMask & (1 << parseInt(cb.dataset.day))) !== 0;
    });

    $('#darkmode-modal').classList.add('show');
}

$('#btn-add-darkmode').addEventListener('click', () => {
    const enabledCount = darkModeSchedules.filter(s => s.enabled).length;
    if (enabledCount >= 3) {
        alert('Maximum 3 dark mode schedules');
        return;
    }
    editDarkMode(-1);
});

$('#btn-darkmode-cancel').addEventListener('click', () => {
    $('#darkmode-modal').classList.remove('show');
});

$('#btn-darkmode-delete').addEventListener('click', async () => {
    const id = parseInt($('#darkmode-id').value);
    if (id >= 0) {
        await api.post('/api/darkmode', { id, enabled: false,
            start_hour: 0, start_minute: 0, end_hour: 0, end_minute: 0,
            days_mask: 0, allow_override: false });
        $('#darkmode-modal').classList.remove('show');
        loadDarkMode();
    }
});

$('#darkmode-form').addEventListener('submit', async (e) => {
    e.preventDefault();

    let daysMask = 0;
    $$('#darkmode-days input').forEach(cb => {
        if (cb.checked) daysMask |= (1 << parseInt(cb.dataset.day));
    });

    const schedule = {
        id: parseInt($('#darkmode-id').value),
        enabled: true,
        start_hour: parseInt($('#darkmode-start-hour').value),
        start_minute: parseInt($('#darkmode-start-minute').value),
        end_hour: parseInt($('#darkmode-end-hour').value),
        end_minute: parseInt($('#darkmode-end-minute').value),
        days_mask: daysMask,
        allow_override: $('#darkmode-override').checked
    };

    // Find free slot if new
    if (schedule.id < 0) {
        const used = darkModeSchedules.filter(s => s.enabled).map(s => s.id);
        for (let i = 0; i < 3; i++) {
            if (!used.includes(i)) { schedule.id = i; break; }
        }
        if (schedule.id < 0) { alert('Maximum 3 schedules'); return; }
    }

    await api.post('/api/darkmode', schedule);
    $('#darkmode-modal').classList.remove('show');
    loadDarkMode();
});

// MQTT
async function loadMqtt() {
    try {
        const data = await api.get('/api/mqtt');
        $('#mqtt-enabled').checked = data.enabled;
        $('#mqtt-broker').value = data.broker_uri || '';
        $('#mqtt-username').value = data.username || '';
        $('#mqtt-password').value = '';
        $('#mqtt-password').placeholder = data.password_set ? 'Leave blank to keep current' : 'Optional';
        $('#mqtt-prefix').value = data.topic_prefix || 'morninglight';
        $('#mqtt-device-name').value = data.device_name || 'MorningLight';
        $('#mqtt-settings').style.display = data.enabled ? 'block' : 'none';

        const statusEl = $('#mqtt-status');
        if (data.connected) {
            statusEl.textContent = 'Connected';
            statusEl.style.color = '#4caf50';
        } else if (data.enabled) {
            statusEl.textContent = 'Disconnected';
            statusEl.style.color = '#f44336';
        } else {
            statusEl.textContent = 'Disabled';
            statusEl.style.color = '';
        }
    } catch (e) {
        console.error('Failed to load MQTT config:', e);
    }
}

$('#mqtt-enabled').addEventListener('change', (e) => {
    $('#mqtt-settings').style.display = e.target.checked ? 'block' : 'none';
});

$('#btn-save-mqtt').addEventListener('click', async () => {
    const data = {
        enabled: $('#mqtt-enabled').checked,
        broker_uri: $('#mqtt-broker').value,
        username: $('#mqtt-username').value,
        topic_prefix: $('#mqtt-prefix').value,
        device_name: $('#mqtt-device-name').value
    };
    const pw = $('#mqtt-password').value;
    if (pw.length > 0) {
        data.password = pw;
    }
    const result = await api.post('/api/mqtt', data);
    if (result.success) {
        alert('MQTT settings saved');
        setTimeout(loadMqtt, 2000);
    }
});

// Config export
$('#btn-export-config').addEventListener('click', () => {
    window.location.href = '/api/config/export';
});

// Config import
$('#file-import-config').addEventListener('change', async (e) => {
    const file = e.target.files[0];
    if (!file) return;
    if (!confirm('Import will overwrite all settings. Continue?')) {
        e.target.value = '';
        return;
    }
    try {
        const text = await file.text();
        const resp = await fetch('/api/config/import', { method: 'POST', body: text });
        const result = await resp.json();
        if (result.success) {
            if (result.restart_required) {
                if (confirm('Config imported. Restart now to apply LED changes?')) {
                    await api.post('/api/reboot', {});
                    alert('Device is rebooting...');
                }
            } else {
                alert('Config imported successfully');
                loadConfig(); loadAlarms(); loadDarkMode(); loadAnimationPresets();
            }
        }
    } catch (err) { alert('Import failed: ' + err.message); }
    e.target.value = '';
});

// Reboot
$('#btn-reboot').addEventListener('click', async () => {
    if (!confirm('Reboot the device?')) return;
    await api.post('/api/reboot', {});
    alert('Device is rebooting...');
});

// Factory Reset
$('#btn-factory-reset').addEventListener('click', async () => {
    if (!confirm('Reset all settings to factory defaults? WiFi credentials will be kept.')) return;
    await api.post('/api/factory-reset', {});
    alert('Device is resetting and rebooting...');
});

// Hidden WiFi — Manual Entry
$('#btn-manual-wifi').addEventListener('click', () => {
    $('#wifi-ssid').value = '';
    $('#wifi-password').value = '';
    $('#wifi-password').style.display = 'block';
    $('#wifi-connect-form').style.display = 'block';
});

// Init
updateStatus();
loadAlarms();
loadDarkMode();
loadConfig();
loadAnimationPresets();
loadMqtt();

// Poll status every 5 seconds
setInterval(updateStatus, 5000);
