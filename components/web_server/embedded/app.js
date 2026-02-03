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

    } catch (e) {
        console.error('Status update failed:', e);
    }
}

// LED test - color temperature and brightness sliders
$('#test-color-temp').addEventListener('input', (e) => {
    $('#test-color-temp-val').textContent = `${e.target.value}K`;
});

$('#test-brightness').addEventListener('input', (e) => {
    $('#test-brightness-val').textContent = `${e.target.value}%`;
});

$('#btn-apply-test').addEventListener('click', async () => {
    const color_temp = parseInt($('#test-color-temp').value);
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

    $('#alarm-modal').classList.add('show');
}

$('#btn-add-alarm').addEventListener('click', () => editAlarm(-1));

$('#btn-alarm-cancel').addEventListener('click', () => {
    $('#alarm-modal').classList.remove('show');
});

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
        brightness: parseInt($('#alarm-brightness').value)
    };

    if (alarm.id < 0) delete alarm.id;

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

$('#btn-save-settings').addEventListener('click', async () => {
    const config = {
        timezone: $('#timezone').value,
        brightness_max: parseInt($('#max-brightness').value),
        pwm_frequency: parseInt($('#pwm-frequency').value)
    };

    await api.post('/api/config', config);
    alert('Settings saved');
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
    } catch (e) {
        console.error('Failed to load config:', e);
    }
}

// Init
updateStatus();
loadAlarms();
loadConfig();

// Poll status every 5 seconds
setInterval(updateStatus, 5000);
