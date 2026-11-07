#include "configuration.hpp"
#include <chrono>

#include "application.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "events.hpp"
#include "filesystem.hpp"
#include "internet_config.hpp"
#include "nvs.h"
#include "scheduler.hpp"
#include "wifi.hpp"

#include "cJSON.h"

namespace internet
{
    namespace
    {
        constexpr char k_tag[] = "ConfService";

        constexpr char INDEX_HTML[] = R"rawliteral(<!DOCTYPE html>
        <html lang="en">
        <head>
          <meta charset="UTF-8">
          <meta name="viewport" content="width=device-width, initial-scale=1.0">
          <title>Internet Configuration</title>
          <link rel="stylesheet" href="/style.css">
        </head>
        <body>
          <div class="container">
            <header>
              <h1>Device Configuration</h1>
              <p class="subtitle">Wi-Fi & Internet settings</p>
            </header>

            <nav style="margin-bottom: 1.5rem; text-align: center;">
              <a href="/" style="color: var(--accent); text-decoration: none; font-weight: 600; margin-right: 1.25rem;">Wi-Fi Config</a>
              <a href="/schedule.html" style="color: var(--muted); text-decoration: none;">Schedule</a>
            </nav>

            <div id="status" class="status hidden"></div>

            <form id="config-form">
              <!-- Operating Mode -->
              <section class="card">
                <h2>Operating Mode</h2>
                <div class="field">
                  <label for="mode">Mode</label>
                  <select id="mode" name="mode">
                    <option value="0">Station (STA)</option>
                    <option value="1">Access Point (AP)</option>
                    <option value="2">AP + Station (APSTA)</option>
                  </select>
                </div>
              </section>

              <!-- Station -->
              <section class="card">
                <h2>Station (Client)</h2>
                <div class="field">
                  <label for="sta-ssid">SSID</label>
                  <input type="text" id="sta-ssid" name="sta_ssid" maxlength="32" required>
                </div>
                <div class="field">
                  <label for="sta-password">Password</label>
                  <input type="password" id="sta-password" name="sta_password" maxlength="64">
                </div>
              </section>

              <!-- Access Point -->
              <section class="card">
                <h2>Access Point</h2>
                <div class="field">
                  <label for="ap-ssid">SSID</label>
                  <input type="text" id="ap-ssid" name="ap_ssid" maxlength="32" value="Setup">
                </div>
                <div class="field">
                  <label for="ap-password">Password</label>
                  <input type="password" id="ap-password" name="ap_password" maxlength="64" value="Setup">
                </div>
                <div class="row">
                  <div class="field">
                    <label for="ap-channel">Channel</label>
                    <input type="number" id="ap-channel" name="ap_channel" min="1" max="13" value="1">
                  </div>
                  <div class="field">
                    <label for="ap-max-conn">Max connections</label>
                    <input type="number" id="ap-max-conn" name="ap_max_conn" min="1" max="10" value="4">
                  </div>
                </div>
                <div class="field checkbox">
                  <label>
                    <input type="checkbox" id="ap-hidden" name="ap_hidden">
                    Hidden network
                  </label>
                </div>
              </section>

              <!-- Reconnect Policy -->
              <section class="card">
                <h2>Reconnect Policy</h2>
                <div class="field checkbox">
                  <label>
                    <input type="checkbox" id="reconn-enabled" name="reconn_enabled" checked>
                    Enable automatic reconnect
                  </label>
                </div>
                <div class="field checkbox">
                  <label>
                    <input type="checkbox" id="reconn-stop" name="reconn_stop">
                    Stop after threshold reached
                  </label>
                </div>
                <div class="row">
                  <div class="field">
                    <label for="reconn-initial">Initial delay (ms)</label>
                    <input type="number" id="reconn-initial" name="reconn_initial" min="100" value="1000">
                  </div>
                  <div class="field">
                    <label for="reconn-max">Max delay (ms)</label>
                    <input type="number" id="reconn-max" name="reconn_max" min="1000" value="30000">
                  </div>
                </div>
                <div class="field">
                  <label for="reconn-threshold">Consecutive failures threshold (0 = unlimited)</label>
                  <input type="number" id="reconn-threshold" name="reconn_threshold" min="0" value="5">
                </div>
              </section>

              <div class="actions">
                <button type="button" id="btn-load" class="secondary">Reload</button>
                <button type="submit" id="btn-save">Save & Apply</button>
              </div>
            </form>
          </div>

          <script src="/script.js"></script>
        </body>
        </html>)rawliteral";

        constexpr char STYLE_CSS[] = R"rawliteral(:root {
          --bg: #0f172a;
          --card: #1e293b;
          --border: #334155;
          --text: #f1f5f9;
          --muted: #94a3b8;
          --accent: #3b82f6;
          --accent-hover: #2563eb;
          --success: #22c55e;
          --error: #ef4444;
          --radius: 12px;
        }

        * {
          box-sizing: border-box;
          margin: 0;
          padding: 0;
        }

        body {
          font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
          background: var(--bg);
          color: var(--text);
          line-height: 1.5;
          min-height: 100vh;
          padding: 1.5rem;
        }

        .container {
          max-width: 520px;
          margin: 0 auto;
        }

        header {
          text-align: center;
          margin-bottom: 2rem;
        }

        h1 {
          font-size: 1.75rem;
          font-weight: 700;
        }

        .subtitle {
          color: var(--muted);
          margin-top: 0.25rem;
        }

        .card {
          background: var(--card);
          border: 1px solid var(--border);
          border-radius: var(--radius);
          padding: 1.25rem;
          margin-bottom: 1.25rem;
        }

        .card h2 {
          font-size: 1.1rem;
          margin-bottom: 1rem;
          color: var(--accent);
        }

        .field {
          margin-bottom: 1rem;
        }

        .field:last-child {
          margin-bottom: 0;
        }

        label {
          display: block;
          font-size: 0.875rem;
          color: var(--muted);
          margin-bottom: 0.35rem;
        }

        input[type="text"],
        input[type="password"],
        input[type="number"],
        select {
          width: 100%;
          padding: 0.6rem 0.75rem;
          background: var(--bg);
          border: 1px solid var(--border);
          border-radius: 8px;
          color: var(--text);
          font-size: 1rem;
        }

        input:focus,
        select:focus {
          outline: none;
          border-color: var(--accent);
          box-shadow: 0 0 0 3px rgba(59, 130, 246, 0.25);
        }

        .row {
          display: grid;
          grid-template-columns: 1fr 1fr;
          gap: 1rem;
        }

        .checkbox label {
          display: flex;
          align-items: center;
          gap: 0.5rem;
          cursor: pointer;
          color: var(--text);
        }

        .checkbox input {
          width: 1.1rem;
          height: 1.1rem;
          accent-color: var(--accent);
        }

        .actions {
          display: flex;
          gap: 0.75rem;
          margin-top: 1.5rem;
        }

        button {
          flex: 1;
          padding: 0.75rem 1rem;
          border: none;
          border-radius: 8px;
          font-size: 1rem;
          font-weight: 600;
          cursor: pointer;
          transition: background 0.15s;
        }

        button[type="submit"] {
          background: var(--accent);
          color: white;
        }

        button[type="submit"]:hover {
          background: var(--accent-hover);
        }

        button.secondary {
          background: transparent;
          border: 1px solid var(--border);
          color: var(--text);
        }

        button.secondary:hover {
          background: var(--card);
        }

        button:disabled {
          opacity: 0.6;
          cursor: not-allowed;
        }

        .status {
          padding: 0.75rem 1rem;
          border-radius: 8px;
          margin-bottom: 1.25rem;
          font-weight: 500;
          text-align: center;
        }

        .status.success {
          background: rgba(34, 197, 94, 0.15);
          color: var(--success);
          border: 1px solid rgba(34, 197, 94, 0.3);
        }

        .status.error {
          background: rgba(239, 68, 68, 0.15);
          color: var(--error);
          border: 1px solid rgba(239, 68, 68, 0.3);
        }

        .status.hidden {
          display: none;
        }

        @media (max-width: 480px) {
          .row {
            grid-template-columns: 1fr;
          }
        })rawliteral";

        constexpr char SCRIPT_JS[] = R"rawliteral(const form = document.getElementById('config-form');
        const statusEl = document.getElementById('status');
        const btnLoad = document.getElementById('btn-load');
        const btnSave = document.getElementById('btn-save');

        function showStatus(message, isError = false) {
          statusEl.textContent = message;
          statusEl.className = 'status ' + (isError ? 'error' : 'success');
          setTimeout(() => statusEl.classList.add('hidden'), 4000);
        }

        function setFormValues(cfg) {
          document.getElementById('mode').value = cfg.configuration_service?.mode ?? 1;

          const sta = cfg.wifi?.station ?? {};
          document.getElementById('sta-ssid').value = sta.ssid ?? '';
          document.getElementById('sta-password').value = sta.password ?? '';

          const ap = cfg.wifi?.access_point ?? {};
          document.getElementById('ap-ssid').value = ap.ssid ?? 'Setup';
          document.getElementById('ap-password').value = ap.password ?? 'Setup';
          document.getElementById('ap-channel').value = ap.channel ?? 1;
          document.getElementById('ap-max-conn').value = ap.max_connections ?? 4;
          document.getElementById('ap-hidden').checked = !!ap.hidden;

          const reconn = cfg.wifi?.reconnect ?? {};
          document.getElementById('reconn-enabled').checked = reconn.enabled !== false;
          document.getElementById('reconn-stop').checked = !!reconn.stop_if_reached_threshold;
          document.getElementById('reconn-initial').value = reconn.initial_delay_ms ?? 1000;
          document.getElementById('reconn-max').value = reconn.max_delay_ms ?? 30000;
          document.getElementById('reconn-threshold').value = reconn.consecutive_failures_threshold ?? 5;
        }

        function collectFormValues() {
          return {
            configuration_service: {
              mode: parseInt(document.getElementById('mode').value, 10)
            },
            wifi: {
              station: {
                ssid: document.getElementById('sta-ssid').value.trim(),
                password: document.getElementById('sta-password').value
              },
              access_point: {
                ssid: document.getElementById('ap-ssid').value.trim(),
                password: document.getElementById('ap-password').value,
                channel: parseInt(document.getElementById('ap-channel').value, 10) || 1,
                max_connections: parseInt(document.getElementById('ap-max-conn').value, 10) || 4,
                hidden: document.getElementById('ap-hidden').checked
              },
              reconnect: {
                enabled: document.getElementById('reconn-enabled').checked,
                stop_if_reached_threshold: document.getElementById('reconn-stop').checked,
                initial_delay_ms: parseInt(document.getElementById('reconn-initial').value, 10) || 1000,
                max_delay_ms: parseInt(document.getElementById('reconn-max').value, 10) || 30000,
                consecutive_failures_threshold: parseInt(document.getElementById('reconn-threshold').value, 10) || 0
              }
            }
          };
        }

        async function loadConfig() {
          btnLoad.disabled = true;
          try {
            const res = await fetch('/api/config');
            if (!res.ok) throw new Error(`HTTP ${res.status}`);
            const cfg = await res.json();
            setFormValues(cfg);
            showStatus('Configuration loaded');
          } catch (err) {
            showStatus('Failed to load: ' + err.message, true);
          } finally {
            btnLoad.disabled = false;
          }
        }

        form.addEventListener('submit', async (e) => {
          e.preventDefault();
          btnSave.disabled = true;
          btnSave.textContent = 'Saving…';

          try {
            const body = collectFormValues();
            const res = await fetch('/api/config', {
              method: 'POST',
              headers: { 'Content-Type': 'application/json' },
              body: JSON.stringify(body)
            });

            if (!res.ok) {
              const text = await res.text();
              throw new Error(text || `HTTP ${res.status}`);
            }

            showStatus('Configuration saved. Device will apply changes…');
          } catch (err) {
            showStatus('Save failed: ' + err.message, true);
          } finally {
            btnSave.disabled = false;
            btnSave.textContent = 'Save & Apply';
          }
        });

        btnLoad.addEventListener('click', loadConfig);

        // Load on page open
        loadConfig();)rawliteral";
        constexpr char SCHEDULE_HTML[] = R"rawliteral(<!DOCTYPE html>
        <html lang="en">
        <head>
          <meta charset="UTF-8">
          <meta name="viewport" content="width=device-width, initial-scale=1.0">
          <title>Schedule Configuration</title>
          <link rel="stylesheet" href="/style.css">
          <style>
            .schedule-header {
              display: flex;
              justify-content: space-between;
              align-items: center;
              margin-bottom: 1rem;
              gap: 0.75rem;
              flex-wrap: wrap;
            }
            .schedule-actions {
              display: flex;
              gap: 0.5rem;
              flex-wrap: wrap;
            }
            .schedule-tabs {
              display: flex;
              gap: 0.5rem;
              flex-wrap: wrap;
              margin-bottom: 1rem;
            }
            .schedule-tab {
              padding: 0.4rem 0.9rem;
              border-radius: 999px;
              border: 1px solid var(--border);
              background: transparent;
              color: var(--text);
              font-size: 0.875rem;
              cursor: pointer;
            }
            .schedule-tab.active {
              background: var(--accent);
              border-color: var(--accent);
              color: white;
            }
            .period-row {
              display: grid;
              grid-template-columns: 60px 1fr 1fr auto;
              gap: 0.75rem;
              align-items: end;
              margin-bottom: 0.75rem;
              padding-bottom: 0.75rem;
              border-bottom: 1px solid var(--border);
            }
            .period-row:last-child {
              border-bottom: none;
              margin-bottom: 0;
              padding-bottom: 0;
            }
            .period-row label {
              margin-bottom: 0.25rem;
            }
            .btn-icon {
              width: 2.25rem;
              height: 2.25rem;
              padding: 0;
              display: flex;
              align-items: center;
              justify-content: center;
              border-radius: 8px;
              background: transparent;
              border: 1px solid var(--border);
              color: var(--muted);
              cursor: pointer;
              font-size: 1.1rem;
            }
            .btn-icon:hover {
              background: rgba(239, 68, 68, 0.15);
              border-color: var(--error);
              color: var(--error);
            }
            .btn-add {
              width: 100%;
              margin-top: 0.75rem;
              background: transparent;
              border: 1px dashed var(--border);
              color: var(--muted);
            }
            .btn-add:hover {
              border-color: var(--accent);
              color: var(--accent);
            }
            .btn-danger {
              background: transparent;
              border: 1px solid var(--error);
              color: var(--error);
            }
            .btn-danger:hover:not(:disabled) {
              background: rgba(239, 68, 68, 0.15);
            }
            .btn-danger:disabled {
              opacity: 0.45;
              cursor: not-allowed;
            }
            .global-settings {
              display: grid;
              grid-template-columns: 1fr 1fr;
              gap: 1rem;
            }
            @media (max-width: 520px) {
              .period-row {
                grid-template-columns: 1fr 1fr;
              }
              .period-row .id-field {
                grid-column: 1 / -1;
              }
              .global-settings {
                grid-template-columns: 1fr;
              }
            }
          </style>
        </head>
        <body>
          <div class="container">
            <header>
              <h1>Bell Schedule</h1>
              <p class="subtitle">Configure periods and timing</p>
            </header>

            <nav style="margin-bottom: 1.5rem; text-align: center;">
              <a href="/" style="color: var(--muted); text-decoration: none; margin-right: 1.25rem;">Wi-Fi Config</a>
              <a href="/schedule.html" style="color: var(--accent); text-decoration: none; font-weight: 600;">Schedule</a>
            </nav>

            <div id="status" class="status hidden"></div>

            <form id="schedule-form">
              <!-- Global settings -->
              <section class="card">
                <h2>Global Settings</h2>
                <div class="field">
                  <label for="timezone">Timezone</label>
                  <input type="text" id="timezone" name="timezone" placeholder="Europe/Vilnius" required>
                </div>
                <div class="global-settings">
                  <div class="field">
                    <label for="bell-duration">Bell duration (ms)</label>
                    <input type="number" id="bell-duration" name="bell_duration" min="100" step="100" value="1000">
                  </div>
                  <div class="field">
                    <label for="warning-lead">Warning lead time (ms)</label>
                    <input type="number" id="warning-lead" name="warning_lead" min="0" step="500" value="5000">
                  </div>
                </div>
                <div class="field">
                  <label for="active-schedule">Active schedule</label>
                  <select id="active-schedule" name="active_schedule"></select>
                </div>
              </section>

              <!-- Schedules -->
              <section class="card">
                <div class="schedule-header">
                  <h2 style="margin: 0;">Schedules</h2>
                  <div class="schedule-actions">
                    <button type="button" id="btn-add-schedule" class="secondary" style="padding: 0.4rem 0.9rem;">
                      + New schedule
                    </button>
                    <button type="button" id="btn-delete-schedule" class="btn-danger" style="padding: 0.4rem 0.9rem;">
                      Delete
                    </button>
                  </div>
                </div>

                <div class="schedule-tabs" id="schedule-tabs"></div>

                <div id="schedule-editor">
                  <div class="field">
                    <label for="sched-desc">Description</label>
                    <input type="text" id="sched-desc" placeholder="Weekday bell schedule">
                  </div>

                  <div id="periods-container"></div>

                  <button type="button" id="btn-add-period" class="btn-add">
                    + Add period
                  </button>
                </div>
              </section>

              <div class="actions">
                <button type="button" id="btn-load" class="secondary">Reload</button>
                <button type="submit" id="btn-save">Save Schedule</button>
              </div>
            </form>
          </div>

          <script src="/schedule.js"></script>
        </body>
        </html>)rawliteral";
        
        constexpr char SCHEDULE_JS[] = R"rawliteral(let scheduleData = {
        timezone: "Europe/Vilnius",
        bell_duration_ms: 1000,
        warning_lead_time_ms: 5000,
        active_schedule_id: "weekday",
        schedules: {
          weekday: {
            description: "Weekday bell schedule",
            periods: [
              { id: 1, start: "08:00", end: "08:45" },
              { id: 2, start: "09:00", end: "09:45" },
              { id: 3, start: "10:00", end: "10:45" }
            ]
          },
          weekend: {
            description: "Weekend bell schedule",
            periods: [
              { id: 1, start: "09:00", end: "09:45" },
              { id: 2, start: "10:00", end: "10:45" }
            ]
          }
        }
      };

      const MAX_SCHEDULES = 5;
      const MAX_PERIODS_PER_SCHEDULE = 30;

      let currentScheduleId = null;

      const statusEl       = document.getElementById('status');
      const form           = document.getElementById('schedule-form');
      const tabsEl         = document.getElementById('schedule-tabs');
      const periodsEl      = document.getElementById('periods-container');
      const activeSelect   = document.getElementById('active-schedule');
      const descInput      = document.getElementById('sched-desc');
      const btnAddSchedule = document.getElementById('btn-add-schedule');
      const btnDeleteSchedule = document.getElementById('btn-delete-schedule');
      const btnAddPeriod   = document.getElementById('btn-add-period');

      function showStatus(msg, isError = false) {
        statusEl.textContent = msg;
        statusEl.className = 'status ' + (isError ? 'error' : 'success');
        setTimeout(() => statusEl.classList.add('hidden'), 4000);
      }

      function timeToMinutes(t) {
        const [h, m] = t.split(':').map(Number);
        return h * 60 + m;
      }

      function nextPeriodId(periods) {
        if (!periods.length) return 1;
        return Math.max(...periods.map(p => p.id)) + 1;
      }

      function scheduleCount() {
        return Object.keys(scheduleData.schedules).length;
      }

      /* ---------- Rendering ---------- */

      function renderTabs() {
        tabsEl.innerHTML = '';
        const ids = Object.keys(scheduleData.schedules);

        ids.forEach(id => {
          const btn = document.createElement('button');
          btn.type = 'button';
          btn.className = 'schedule-tab' + (id === currentScheduleId ? ' active' : '');
          btn.textContent = id;
          btn.onclick = () => selectSchedule(id);
          tabsEl.appendChild(btn);
        });

        // Active schedule dropdown
        activeSelect.innerHTML = '';
        ids.forEach(id => {
          const opt = document.createElement('option');
          opt.value = id;
          opt.textContent = id;
          if (id === scheduleData.active_schedule_id) opt.selected = true;
          activeSelect.appendChild(opt);
        });

        // Disable buttons according to limits
        btnAddSchedule.disabled = scheduleCount() >= MAX_SCHEDULES;
        btnAddSchedule.title = btnAddSchedule.disabled
          ? `Maximum of ${MAX_SCHEDULES} schedules reached`
          : '';

        btnDeleteSchedule.disabled = scheduleCount() <= 1;
        btnDeleteSchedule.title = btnDeleteSchedule.disabled
          ? 'At least one schedule must remain'
          : 'Delete the selected schedule';
      }

      function renderPeriods() {
        periodsEl.innerHTML = '';
        const sched = scheduleData.schedules[currentScheduleId];
        if (!sched) return;

        descInput.value = sched.description || '';

        const periods = sched.periods
          .slice()
          .sort((a, b) => timeToMinutes(a.start) - timeToMinutes(b.start));

        periods.forEach((p, idx) => {
          const row = document.createElement('div');
          row.className = 'period-row';
          row.innerHTML = `
            <div class="field id-field">
              <label>ID</label>
              <input type="number" class="period-id" value="${p.id}" min="1" data-idx="${idx}">
            </div>
            <div class="field">
              <label>Start</label>
              <input type="time" class="period-start" value="${p.start}" data-idx="${idx}" required>
            </div>
            <div class="field">
              <label>End</label>
              <input type="time" class="period-end" value="${p.end}" data-idx="${idx}" required>
            </div>
            <button type="button" class="btn-icon" title="Remove period" data-idx="${idx}">×</button>
          `;
          periodsEl.appendChild(row);
        });

        periodsEl.querySelectorAll('.period-id, .period-start, .period-end').forEach(el => {
          el.addEventListener('change', onPeriodChange);
        });
        periodsEl.querySelectorAll('.btn-icon').forEach(btn => {
          btn.addEventListener('click', () => removePeriod(+btn.dataset.idx));
        });

        const atLimit = periods.length >= MAX_PERIODS_PER_SCHEDULE;
        btnAddPeriod.disabled = atLimit;
        btnAddPeriod.title = atLimit
          ? `Maximum of ${MAX_PERIODS_PER_SCHEDULE} periods per schedule`
          : '';
        btnAddPeriod.textContent = atLimit
          ? `Maximum ${MAX_PERIODS_PER_SCHEDULE} periods reached`
          : '+ Add period';
      }

      function selectSchedule(id) {
        if (currentScheduleId && scheduleData.schedules[currentScheduleId]) {
          scheduleData.schedules[currentScheduleId].description = descInput.value.trim();
        }
        currentScheduleId = id;
        renderTabs();
        renderPeriods();
      }

      /* ---------- Mutations ---------- */

      function onPeriodChange(e) {
        const idx = +e.target.dataset.idx;
        const periods = scheduleData.schedules[currentScheduleId].periods;
        if (!periods[idx]) return;

        if (e.target.classList.contains('period-id')) {
          periods[idx].id = parseInt(e.target.value, 10) || 1;
        } else if (e.target.classList.contains('period-start')) {
          periods[idx].start = e.target.value;
        } else if (e.target.classList.contains('period-end')) {
          periods[idx].end = e.target.value;
        }
      }

      function removePeriod(idx) {
        scheduleData.schedules[currentScheduleId].periods.splice(idx, 1);
        renderPeriods();
      }

      btnAddPeriod.addEventListener('click', () => {
        const periods = scheduleData.schedules[currentScheduleId].periods;

        if (periods.length >= MAX_PERIODS_PER_SCHEDULE) {
          showStatus(`Maximum of ${MAX_PERIODS_PER_SCHEDULE} periods per schedule`, true);
          return;
        }

        periods.push({
          id: nextPeriodId(periods),
          start: "08:00",
          end: "08:45"
        });
        renderPeriods();
      });

      btnAddSchedule.addEventListener('click', () => {
        if (scheduleCount() >= MAX_SCHEDULES) {
          showStatus(`Maximum of ${MAX_SCHEDULES} schedules allowed`, true);
          return;
        }

        const name = prompt("New schedule ID (letters, numbers, underscore):");
        if (!name) return;

        const id = name.trim().toLowerCase().replace(/[^a-z0-9_]/g, '_');

        if (!id) {
          showStatus("Schedule ID cannot be empty", true);
          return;
        }

        if (scheduleData.schedules[id]) {
          showStatus(`Schedule ID "${id}" already exists – IDs must be unique`, true);
          return;
        }

        scheduleData.schedules[id] = {
          description: id.charAt(0).toUpperCase() + id.slice(1) + " schedule",
          periods: []
        };
        selectSchedule(id);
      });

      btnDeleteSchedule.addEventListener('click', () => {
        if (scheduleCount() <= 1) {
          showStatus("At least one schedule must remain", true);
          return;
        }

        const idToDelete = currentScheduleId;
        if (!idToDelete || !scheduleData.schedules[idToDelete]) return;

        const confirmed = confirm(
          `Delete schedule "${idToDelete}"?\n\nThis cannot be undone (until you press Save).`
        );
        if (!confirmed) return;

        delete scheduleData.schedules[idToDelete];

        // If we deleted the active schedule, pick another one
        if (scheduleData.active_schedule_id === idToDelete) {
          scheduleData.active_schedule_id = Object.keys(scheduleData.schedules)[0];
        }

        // Switch to another schedule
        currentScheduleId = scheduleData.active_schedule_id;
        renderTabs();
        renderPeriods();
        showStatus(`Schedule "${idToDelete}" deleted`);
      });

      /* ---------- Load / Save ---------- */

      function applyGlobalFromForm() {
        scheduleData.timezone             = document.getElementById('timezone').value.trim();
        scheduleData.bell_duration_ms     = parseInt(document.getElementById('bell-duration').value, 10) || 1000;
        scheduleData.warning_lead_time_ms = parseInt(document.getElementById('warning-lead').value, 10) || 0;
        scheduleData.active_schedule_id   = activeSelect.value;

        if (currentScheduleId && scheduleData.schedules[currentScheduleId]) {
          scheduleData.schedules[currentScheduleId].description = descInput.value.trim();
        }
      }

      function fillGlobalForm() {
        document.getElementById('timezone').value      = scheduleData.timezone || "Europe/Vilnius";
        document.getElementById('bell-duration').value = scheduleData.bell_duration_ms ?? 1000;
        document.getElementById('warning-lead').value  = scheduleData.warning_lead_time_ms ?? 5000;
      }

      function validateScheduleData() {
        const ids = Object.keys(scheduleData.schedules);

        if (ids.length === 0) {
          return "At least one schedule is required";
        }
        if (ids.length > MAX_SCHEDULES) {
          return `Maximum of ${MAX_SCHEDULES} schedules allowed`;
        }

        const unique = new Set(ids);
        if (unique.size !== ids.length) {
          return "Schedule IDs must be unique";
        }

        for (const id of ids) {
          const sched = scheduleData.schedules[id];
          if (!sched.periods) sched.periods = [];
          if (sched.periods.length > MAX_PERIODS_PER_SCHEDULE) {
            return `Schedule "${id}" has more than ${MAX_PERIODS_PER_SCHEDULE} periods`;
          }
        }

        if (!scheduleData.active_schedule_id ||
            !scheduleData.schedules[scheduleData.active_schedule_id]) {
          return "Active schedule is invalid";
        }

        return null;
      }

      async function loadSchedule() {
        try {
          const res = await fetch('/api/schedule');
          if (!res.ok) throw new Error(`HTTP ${res.status}`);
          scheduleData = await res.json();

          if (!scheduleData.schedules) scheduleData.schedules = {};
          if (!Object.keys(scheduleData.schedules).length) {
            scheduleData.schedules.weekday = { description: "Weekday", periods: [] };
          }
          if (!scheduleData.active_schedule_id ||
              !scheduleData.schedules[scheduleData.active_schedule_id]) {
            scheduleData.active_schedule_id = Object.keys(scheduleData.schedules)[0];
          }

          fillGlobalForm();
          currentScheduleId = scheduleData.active_schedule_id;
          renderTabs();
          renderPeriods();
          showStatus("Schedule loaded");
        } catch (err) {
          showStatus("Failed to load: " + err.message, true);
        }
      }

      form.addEventListener('submit', async (e) => {
        e.preventDefault();
        applyGlobalFromForm();

        const error = validateScheduleData();
        if (error) {
          showStatus(error, true);
          return;
        }

        const btn = document.getElementById('btn-save');
        btn.disabled = true;
        btn.textContent = "Saving…";

        try {
          const res = await fetch('/api/schedule', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(scheduleData, null, 2)
          });
          if (!res.ok) {
            const txt = await res.text();
            throw new Error(txt || `HTTP ${res.status}`);
          }
          showStatus("Schedule saved successfully");
        } catch (err) {
          showStatus("Save failed: " + err.message, true);
        } finally {
          btn.disabled = false;
          btn.textContent = "Save Schedule";
        }
      });

      document.getElementById('btn-load').addEventListener('click', loadSchedule);

      // Initial load
      loadSchedule();)rawliteral";
        constexpr char k_namespace[] = "schedule";
        constexpr char k_key[]       = "json";

          // Default schedule used when nothing is stored yet
        constexpr char k_default_json[] = R"({
          "timezone": "Europe/Vilnius",
          "bell_duration_ms": 1000,
          "warning_lead_time_ms": 5000,
          "active_schedule_id": "weekday",
          "schedules": {
            "weekday": {
              "description": "Weekday bell schedule",
              "periods": [
                { "id": 1, "start": "08:00", "end": "08:45" },
                { "id": 2, "start": "09:00", "end": "09:45" },
                { "id": 3, "start": "10:00", "end": "10:45" }
              ]
            },
            "weekend": {
              "description": "Weekend bell schedule",
              "periods": [
                { "id": 1, "start": "09:00", "end": "09:45" },
                { "id": 2, "start": "10:00", "end": "10:45" }
              ]
            }
          }
        })";

        cJSON* config_to_json(const InternetConfig& cfg)
        {
            cJSON* root = cJSON_CreateObject();

            // configuration_service
            cJSON* cs = cJSON_CreateObject();
            cJSON_AddNumberToObject(cs, "mode", static_cast<int>(cfg.configuration_service.mode));
            cJSON_AddItemToObject(root, "configuration_service", cs);

            // wifi
            cJSON* wifi = cJSON_CreateObject();

            // station
            cJSON* sta = cJSON_CreateObject();
            cJSON_AddStringToObject(sta, "ssid", cfg.wifi.station.ssid.c_str());
            cJSON_AddStringToObject(sta, "password", cfg.wifi.station.password.c_str());
            cJSON_AddItemToObject(wifi, "station", sta);

            // access_point
            cJSON* ap = cJSON_CreateObject();
            cJSON_AddStringToObject(ap, "ssid", cfg.wifi.access_point.ssid.c_str());
            cJSON_AddStringToObject(ap, "password", cfg.wifi.access_point.password.c_str());
            cJSON_AddNumberToObject(ap, "channel", cfg.wifi.access_point.channel);
            cJSON_AddNumberToObject(ap, "max_connections", cfg.wifi.access_point.max_connections);
            cJSON_AddBoolToObject(ap, "hidden", cfg.wifi.access_point.hidden);
            cJSON_AddItemToObject(wifi, "access_point", ap);

            // reconnect
            cJSON* reconn = cJSON_CreateObject();
            cJSON_AddBoolToObject(reconn, "enabled", cfg.wifi.reconnect.enabled);
            cJSON_AddBoolToObject(reconn, "stop_if_reached_threshold", cfg.wifi.reconnect.stop_if_reached_threshold);
            cJSON_AddNumberToObject(reconn, "initial_delay_ms", cfg.wifi.reconnect.initial_delay_ms);
            cJSON_AddNumberToObject(reconn, "max_delay_ms", cfg.wifi.reconnect.max_delay_ms);
            cJSON_AddNumberToObject(reconn, "consecutive_failures_threshold", cfg.wifi.reconnect.consecutive_failures_threshold);
            cJSON_AddItemToObject(wifi, "reconnect", reconn);

            cJSON_AddItemToObject(root, "wifi", wifi);
            return root;
        }

        bool json_to_config(cJSON* root, InternetConfig& cfg)
        {
            if (!root) return false;

            // configuration_service.mode
            if (cJSON* cs = cJSON_GetObjectItem(root, "configuration_service")) 
            {
                if (cJSON* mode = cJSON_GetObjectItem(cs, "mode")) 
                {
                    cfg.configuration_service.mode =
                        static_cast<ConfigurationServiceConfig::OperatingMode>(mode->valueint);
                }
            }

            cJSON* wifi = cJSON_GetObjectItem(root, "wifi");
            if (!wifi) return true; // partial update ok

            // station
            if (cJSON* sta = cJSON_GetObjectItem(wifi, "station")) 
            {
                if (cJSON* s = cJSON_GetObjectItem(sta, "ssid"))
                {
                    cfg.wifi.station.ssid = s->valuestring ? s->valuestring : "";
                }
                    
                if (cJSON* p = cJSON_GetObjectItem(sta, "password"))
                {
                    cfg.wifi.station.password = p->valuestring ? p->valuestring : "";
                }
            }

            // access_point
            if (cJSON* ap = cJSON_GetObjectItem(wifi, "access_point")) 
            {
                if (cJSON* s = cJSON_GetObjectItem(ap, "ssid"))
                    cfg.wifi.access_point.ssid = s->valuestring ? s->valuestring : "Setup";
                if (cJSON* p = cJSON_GetObjectItem(ap, "password"))
                    cfg.wifi.access_point.password = p->valuestring ? p->valuestring : "Setup";
                if (cJSON* ch = cJSON_GetObjectItem(ap, "channel"))
                    cfg.wifi.access_point.channel = ch->valueint;
                if (cJSON* mc = cJSON_GetObjectItem(ap, "max_connections"))
                    cfg.wifi.access_point.max_connections = mc->valueint;
                if (cJSON* h = cJSON_GetObjectItem(ap, "hidden"))
                    cfg.wifi.access_point.hidden = cJSON_IsTrue(h);
            }

            // reconnect
            if (cJSON* r = cJSON_GetObjectItem(wifi, "reconnect")) 
            {
                if (cJSON* e = cJSON_GetObjectItem(r, "enabled"))
                    cfg.wifi.reconnect.enabled = cJSON_IsTrue(e);
                if (cJSON* s = cJSON_GetObjectItem(r, "stop_if_reached_threshold"))
                    cfg.wifi.reconnect.stop_if_reached_threshold = cJSON_IsTrue(s);
                if (cJSON* i = cJSON_GetObjectItem(r, "initial_delay_ms"))
                    cfg.wifi.reconnect.initial_delay_ms = i->valueint;
                if (cJSON* m = cJSON_GetObjectItem(r, "max_delay_ms"))
                    cfg.wifi.reconnect.max_delay_ms = m->valueint;
                if (cJSON* t = cJSON_GetObjectItem(r, "consecutive_failures_threshold"))
                    cfg.wifi.reconnect.consecutive_failures_threshold = t->valueint;
            }

            return true;
        }

        constexpr std::string k_schedule_path = "schedules.json";

        constexpr char k_default_schedule_json[] = R"({
          "timezone": "Europe/Vilnius",
          "bell_duration_ms": 1000,
          "warning_lead_time_ms": 5000,
          "active_schedule_id": "weekday",
          "schedules": {
            "weekday": {
              "description": "Weekday bell schedule",
              "periods": [
                { "id": 1, "start": "08:00", "end": "08:45" },
                { "id": 2, "start": "09:00", "end": "09:45" },
                { "id": 3, "start": "10:00", "end": "10:45" }
              ]
            },
            "weekend": {
              "description": "Weekend bell schedule",
              "periods": [
                { "id": 1, "start": "09:00", "end": "09:45" },
                { "id": 2, "start": "10:00", "end": "10:45" }
              ]
            }
          }
        })";
    }

    std::string load_schedule_json(storage::FilesystemService& fs)
    {
        ESP_LOGI(k_tag, "load_schedule_json() enter");

        // 1. Check existence first
        auto exists_res = fs.exists(k_schedule_path);
        if (!exists_res) 
        {
            ESP_LOGE(k_tag, "exists() failed: %s", esp_err_to_name(exists_res.error()));
            return k_default_json;
        }

        if (!*exists_res) 
        {
            ESP_LOGW(k_tag, "schedule.json does not exist – using default");
            return k_default_json;
        }

        // 2. Read the file
        auto read_res = fs.read_text(k_schedule_path);
        if (!read_res) 
        {
            ESP_LOGE(k_tag, "read_text() failed: %s", esp_err_to_name(read_res.error()));
            return k_default_json;
        }

        std::string json = std::move(*read_res);   // move, don't copy

        ESP_LOGI(k_tag, "read %u bytes from schedule.json", (unsigned)json.size());

        // 3. Basic sanity checks – never return garbage
        if (json.empty()) 
        {
            ESP_LOGW(k_tag, "file is empty – using default");
            return k_default_json;
        }

        // Strip UTF-8 BOM if present
        if (json.size() >= 3 &&
            static_cast<uint8_t>(json[0]) == 0xEF &&
            static_cast<uint8_t>(json[1]) == 0xBB &&
            static_cast<uint8_t>(json[2]) == 0xBF) 
        {
            json.erase(0, 3);
        }

        if (json.front() != '{' || json.back() != '}') 
        {
            ESP_LOGW(k_tag, "file does not look like a JSON object – using default");
            return k_default_json;
        }

        // Optional size guard (protect against corrupted huge files)
        if (json.size() > 8192) 
        {
            ESP_LOGW(k_tag, "file too large (%u) – using default", (unsigned)json.size());
            return k_default_json;
        }
        return json;
    }

    esp_err_t save_schedule_json(storage::FilesystemService& fs, std::string_view body)
    {
        if (body.size() < 50 || body.front() != '{' || body.back() != '}') 
        {
            ESP_LOGE(k_tag, "refusing to save incomplete JSON");
            return ESP_ERR_INVALID_ARG;
        }
        if (body.empty())
        {
            ESP_LOGE(k_tag, "Refusing to save empty schedule");
            return ESP_ERR_INVALID_ARG;
        }

        // Optional: basic size sanity check (matches the 8 KiB limit used in the handler)
        if (body.size() > 8192)
        {
            ESP_LOGE(k_tag, "Schedule JSON too large (%u bytes)", static_cast<unsigned>(body.size()));
            return ESP_ERR_INVALID_SIZE;
        }

        esp_err_t err = fs.write_text(k_schedule_path, body);
        if (err != ESP_OK)
        {
            ESP_LOGE(k_tag, "Failed to write schedule.json: %s", esp_err_to_name(err));
            return err;
        }

        ESP_LOGI(k_tag, "schedule.json saved (%u bytes)", static_cast<unsigned>(body.size()));
        return ESP_OK;
    }

    ConnectivityChecker::ConnectivityChecker(int timeout_ms)
    {
        esp_http_client_config_t config{};
        config.timeout_ms = timeout_ms;
        config.disable_auto_redirect = true;

        init_error_ = client_.init(config);

        if (init_error_ != ESP_OK) 
        {
            ESP_LOGE(k_tag, "HTTP client initialization failed: %s",
                     esp_err_to_name(init_error_));
        }
    }

    ConnectivityStatus ConnectivityChecker::check_connectivity()
    {
        if (init_error_ != ESP_OK) 
        {
            return ConnectivityStatus::NoConnectivity;
        }

        for (const auto& probe : k_connectivity_probes) 
        {
            const Request request{
                .method = HTTP_METHOD_GET,
                .url = probe.url,
                .max_response_body_size = k_max_probe_body_size,
            };

            auto result = client_.perform(request);

            if (!result)
            {
                ESP_LOGD(k_tag, "Probe failed: %.*s (%s)",
                         static_cast<int>(probe.url.size()),
                         probe.url.data(),
                         esp_err_to_name(result.error()));
                continue;
            }

            const Response& response = *result;

            if (response.status_code != probe.expected_status) 
            {
                ESP_LOGD(k_tag, "Probe returned HTTP %d: %.*s",
                         response.status_code,
                         static_cast<int>(probe.url.size()),
                         probe.url.data());
                continue;
            }

            // An empty expected body means that the status code is sufficient.
            // Otherwise, require the expected text to occur in the response.
            if (!probe.expected_body.empty() &&
                response.body.find(probe.expected_body) == std::string::npos) 
            {
                ESP_LOGD(k_tag, "Probe response body did not match: %.*s",
                         static_cast<int>(probe.url.size()),
                         probe.url.data());
                continue;
            }

            ESP_LOGD(k_tag, "Connectivity confirmed by %.*s",
                     static_cast<int>(probe.url.size()),
                     probe.url.data());

            return ConnectivityStatus::Connected;
        }

        return ConnectivityStatus::NoConnectivity;
    }

    ConfigurationService::ConfigurationService(event::EventDispatcher& event_dispatcher, 
      WifiService& wifi_service, 
      storage::FilesystemService& filesystem_service, 
      scheduler::SntpClient& sntp_client, 
      scheduler::BellScheduler& scheduler)
        : event_dispatcher_(event_dispatcher), wifi_service_(wifi_service), filesystem_service_(filesystem_service),
        sntp_client_(sntp_client), scheduler_(scheduler)
    {}

    esp_err_t ConfigurationService::start()
    {
        auto result = InternetConfigManager::load_from_nvs();
        if (result.has_value())
        {
            config_ = std::move(*result);
        }
        else if (result.error() == ESP_ERR_NVS_NOT_FOUND)
        {
            config_ = {};
        }
        else
        {
            return result.error();
        }

        register_event_handlers();

        auto err = wifi_service_.configure_wifi_service(config_.wifi);
        if (err != ESP_OK)
        {
            return err;
        }

        err = wifi_service_.start();
        if (err != ESP_OK)
        {
            return err;
        }

        err = wifi_service_.start_station();
        if (err != ESP_OK)
        {
            return err;
        }

        err = scheduler_.start();
        if (err != ESP_OK)
        {
            return err;
        }

        return ESP_OK;
    }

    esp_err_t ConfigurationService::stop()
    {
        if (server_.is_running())
        {
            auto result = stop_website();
            if (result != ESP_OK)
            {
                return result;
            }
        }
        if (mdns_.is_running())
        {
            auto result = stop_mdns();
            if (result != ESP_OK)
            {
                return result;
            }
        }
        return ESP_OK;
    }

    void ConfigurationService::start_configuration()
    {
      ESP_LOGI(k_tag, "Start configuration");
        // Assuming WifiService is configured.
        bool wifi_state = wifi_service_.is_started();

        if (!wifi_state)
        {
            auto result = wifi_service_.start();
            if (result != ESP_OK)
            {
                ESP_LOGE(k_tag, "Failed to start wifi_service: %s (0x%x)",
                esp_err_to_name(result),
                static_cast<unsigned>(result));

                stop();
                return;
            }
        }

        // Proceed with started WifiService
        switch (config_.configuration_service.mode)
        {
            case ConfigurationServiceConfig::OperatingMode::Sta:
            {
                auto station_state = wifi_service_.station_state();
                if (station_state == WifiService::StationState::Stopped)
                {
                    auto result = wifi_service_.start_station();
                    if (result != ESP_OK)
                    {
                        stop();
                        return;
                    }
                }
                else if (station_state == WifiService::StationState::Connected)
                {
                    start_website();
                }
                break;
            }

            case ConfigurationServiceConfig::OperatingMode::Ap:
            {
                ESP_LOGI(k_tag, "Start AP configuration");
                auto ap_state = wifi_service_.access_point_state();
                if (ap_state == WifiService::AccessPointState::Stopped)
                {
                    auto result = wifi_service_.start_access_point();
                    if (result != ESP_OK)
                    {
                        stop();
                        return;
                    }
                }
                else
                {
                    start_website();
                }
                break;
            }

            case ConfigurationServiceConfig::OperatingMode::ApSta:
            {
                auto ap_state = wifi_service_.access_point_state();
                if (ap_state == WifiService::AccessPointState::Stopped)
                {
                    auto result = wifi_service_.start_access_point();
                    if (result != ESP_OK)
                    {
                        stop();
                        return;
                    }
                }
                else 
                {
                    start_website();
                }

                auto station_state = wifi_service_.station_state();
                if (station_state == WifiService::StationState::Stopped)
                {
                    auto result = wifi_service_.start_station();
                    // Is able to continue without STA.
                }
                break;
            }
        }
    }

    void ConfigurationService::start_synchronization()
    {
        // TODO: implement
    }

    void ConfigurationService::start_recovery()
    {

        if (mode_ == OperationMode::Recovery)
        {
            return;
        }
        ESP_LOGI(k_tag, "Recovery started");
        mode_ = OperationMode::Recovery;
        auto access_point_state = wifi_service_.access_point_state();
        if (access_point_state == WifiService::AccessPointState::Stopped)
        {
            auto result = wifi_service_.start_access_point();
        }
        else 
        {
            start_website();
        }
    }

    esp_err_t ConfigurationService::start_mdns()
    {
        ESP_LOGI(k_tag, "Start mDNS");
        if (mdns_.is_running())
        {
            return ESP_ERR_INVALID_STATE;
        }

        auto result = Mdns::create("sbs-config", "ESP Device");

        if (!result)
        {
            ESP_LOGE(k_tag, "mDNS initialization failed");
            return result.error();
        }

        mdns_ = std::move(*result);

        Mdns::Service http
        {
            .instance = "ESP Device",
            .type = "_http",
            .protocol = "_tcp",
            .port = 80,
            .txt = {},
        };

        const auto err = mdns_.add_service(http);

        if (err != ESP_OK)
        {
            ESP_LOGE("APP", "mDNS service registration failed");
            return result.error();
        }
        ESP_LOGI(k_tag, "Successfully started mDNS");
        return ESP_OK;
    }
    
    esp_err_t ConfigurationService::stop_mdns()
    {
        if (!mdns_.is_running())
        {
            return ESP_ERR_INVALID_STATE;
        }

        auto err = mdns_.remove_all_services();
        if (err != ESP_OK)
        {
            return err;
        }

        err = mdns_.shutdown();

        return err;
    }

    esp_err_t ConfigurationService::start_website()
    {
        ESP_LOGI(k_tag, "Start website");
        if (server_.is_running())
        {
            return ESP_ERR_INVALID_STATE;
        }

        auto result = server_.start();
        if (result != ESP_OK)
        {
            return result;
        }

        result = register_website_handlers();
        if (result != ESP_OK)
        {
            return result;
        }
        return ESP_OK;
    }

    esp_err_t ConfigurationService::stop_website()
    {
        if (!server_.is_running())
        {
            return ESP_ERR_INVALID_STATE;
        }

        auto result = server_.stop();
        if (result != ESP_OK)
        {
            return result;
        }
        ESP_LOGI(k_tag, "Stop website");
        return ESP_OK;
    }

    esp_err_t ConfigurationService::register_website_handlers()
    {
        // GET /
        auto r = server_.register_handler("/", HTTP_GET,
            [this](httpd_req_t* req) -> esp_err_t 
            {
                httpd_resp_set_type(req, "text/html");
                return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
            });
        if (r != ESP_OK) return r;

        // GET /style.css
        r = server_.register_handler("/style.css", HTTP_GET,
            [](httpd_req_t* req) -> esp_err_t 
            {
                httpd_resp_set_type(req, "text/css");
                return httpd_resp_send(req, STYLE_CSS, HTTPD_RESP_USE_STRLEN);
            });
        if (r != ESP_OK) return r;

        // GET /script.js
        r = server_.register_handler("/script.js", HTTP_GET,
            [](httpd_req_t* req) -> esp_err_t 
            {
                httpd_resp_set_type(req, "application/javascript");
                return httpd_resp_send(req, SCRIPT_JS, HTTPD_RESP_USE_STRLEN);
            });
        if (r != ESP_OK) return r;

        // GET /api/config
        r = server_.register_handler("/api/config", HTTP_GET,
            [this](httpd_req_t* req) -> esp_err_t 
            {
                cJSON* json = config_to_json(config_);
                char* printed = cJSON_PrintUnformatted(json);
                cJSON_Delete(json);

                if (!printed) 
                {
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON error");
                    return ESP_FAIL;
                }

                httpd_resp_set_type(req, "application/json");
                esp_err_t err = httpd_resp_send(req, printed, HTTPD_RESP_USE_STRLEN);
                free(printed);
                return err;
            });
        if (r != ESP_OK) return r;

        // POST /api/config
        r = server_.register_handler("/api/config", HTTP_POST,
            [this](httpd_req_t* req) -> esp_err_t 
            {
                // Limit body size
                constexpr size_t k_max_body = 2048;
                if (req->content_len > k_max_body) 
                {
                    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body too large");
                    return ESP_FAIL;
                }

                std::string body(req->content_len, '\0');
                int received = httpd_req_recv(req, body.data(), req->content_len);
                if (received <= 0) 
                {
                    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to read body");
                    return ESP_FAIL;
                }

                cJSON* root = cJSON_Parse(body.c_str());
                if (!root) 
                {
                    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
                    return ESP_FAIL;
                }

                InternetConfig new_cfg = config_;          // start from current
                bool ok = json_to_config(root, new_cfg);
                cJSON_Delete(root);

                if (!ok) 
                {
                    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid config");
                    return ESP_FAIL;
                }

                // Persist
                esp_err_t err = InternetConfigManager::save_to_nvs(new_cfg);
                if (err != ESP_OK) 
                {
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS write failed");
                    return err;
                }

                config_ = std::move(new_cfg);

                httpd_resp_set_type(req, "application/json");
                httpd_resp_sendstr(req, R"({"status":"ok"})");
                return ESP_OK;
            });
        if (r != ESP_OK) return r;
        
        // GET /schedule.html
        r = server_.register_handler("/schedule.html", HTTP_GET,
            [](httpd_req_t* req) -> esp_err_t 
            {
                httpd_resp_set_type(req, "text/html");
                return httpd_resp_send(req, SCHEDULE_HTML, HTTPD_RESP_USE_STRLEN);
            });
        if (r != ESP_OK) return r;

        // GET /schedule.js
        r = server_.register_handler("/schedule.js", HTTP_GET,
            [](httpd_req_t* req) -> esp_err_t 
            {
                httpd_resp_set_type(req, "application/javascript");
                return httpd_resp_send(req, SCHEDULE_JS, HTTPD_RESP_USE_STRLEN);
            });
        if (r != ESP_OK) return r;

        // GET /api/schedule
        r = server_.register_handler("/api/schedule", HTTP_GET,
        [this](httpd_req_t* req) -> esp_err_t 
        {
            std::string json = load_schedule_json(filesystem_service_);

            // Final safety net
            if (json.empty() || json.front() != '{') 
            {
                json = k_default_json;
            }

            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "Cache-Control", "no-store");
            return httpd_resp_send(req, json.c_str(), json.size());
        });
        if (r != ESP_OK) return r;

        // POST /api/schedule
        r = server_.register_handler("/api/schedule", HTTP_POST,
        [this](httpd_req_t* req) -> esp_err_t 
        {
            constexpr size_t k_max_body = 8192;

            if (req->content_len == 0 || req->content_len > k_max_body) 
            {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid Content-Length");
                return ESP_FAIL;
            }

            std::string body;
            body.resize(req->content_len);

            size_t received = 0;
            while (received < req->content_len) 
            {
                int ret = httpd_req_recv(req, body.data() + received,
                                        req->content_len - received);
                if (ret <= 0) 
                {
                    if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Recv failed");
                    return ESP_FAIL;
                }
                received += ret;
            }

            // Optional: quick sanity check
            if (body.front() != '{' || body.back() != '}') 
            {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Malformed JSON");
                return ESP_FAIL;
            }

            esp_err_t err = save_schedule_json(filesystem_service_, body);
            if (err != ESP_OK) 
            {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Save failed");
                return err;
            }

            auto recalc = scheduler_.recalculate();
            if (recalc != ESP_OK)
            {
                ESP_LOGW(k_tag, "Schedule saved, but recalculation failed: %s",
                    esp_err_to_name(recalc)); 
            }
            else 
            {
                ESP_LOGW(k_tag, "Recalculation successful");
            }

            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, R"({"status":"ok"})");
            return ESP_OK;
        });
        if (r != ESP_OK) return r;

        return ESP_OK;
    }

    esp_err_t ConfigurationService::unregister_website_handlers()
    {
        auto result = server_.unregister_handler("/", HTTP_GET);
        if (result != ESP_OK)
        {
            return result;
        }
        result = server_.unregister_handler("/style.css", HTTP_GET);

        if (result != ESP_OK)
        {
            return result;
        }
        result = server_.unregister_handler("/script.js", HTTP_GET);

        if (result != ESP_OK)
        {
            return result;
        }
        result = server_.unregister_handler("/api/config", HTTP_GET);

        if (result != ESP_OK)
        {
            return result;
        }
        result = server_.unregister_handler("/api/config", HTTP_POST);

        if (result != ESP_OK)
        {
            return result;
        }
        return ESP_OK;
    }

    void ConfigurationService::register_event_handlers()
    {
        event_dispatcher_.subscribe<event::ApplicationStateChanged>(
            on_application_state_changed, 
            this);

        event_dispatcher_.subscribe<event::WifiStationGotIp>(
            on_station_got_ip,
            this);

        event_dispatcher_.subscribe<event::WifiApStarted>(
            on_ap_started, 
            this);

        event_dispatcher_.subscribe<event::WifiReconnectThresholdReached>(
            on_wifi_reconnect_threshold_reached,
            this);
        event_dispatcher_.subscribe<event::SntpTimeSynced>(
            on_sync,
            this);
    }

    void ConfigurationService::on_application_state_changed(const event::Event& event, const event::ApplicationStateChanged& data, void* context)
    {
        auto& service = *static_cast<ConfigurationService*>(context);

        service.application_state_ = data.state;

        switch (data.state)
        {
            case event::ApplicationState::Configuration:
            {
                service.start_configuration();
                break;
            }
            case event::ApplicationState::Synchronization:
            {
                service.start_synchronization();
                break;
            }
            default:
            {
                service.stop();
                break;
            }
        }
    }

    void ConfigurationService::on_station_got_ip(const event::Event& event, const event::WifiStationGotIp& data, void* context)
    {
        auto& service = *static_cast<ConfigurationService*>(context);

        // Try starting sntp_client_ regardless of application state

        if (!service.sntp_client_.is_started())
        {
            ESP_ERROR_CHECK(service.sntp_client_.start());
            ESP_LOGI(k_tag, "sntp_client_ started");
        }

        if (service.mode_ == OperationMode::Recovery)
        {
            // If we get IP from the station,
            // then recovery process is complete.
            service.mode_ = OperationMode::Normal;
        }

        if (service.application_state_ == event::ApplicationState::Configuration && 
            service.config_.configuration_service.mode == ConfigurationServiceConfig::OperatingMode::Sta)
        {
            service.start_mdns();
            service.start_website();
        }
    }

    void ConfigurationService::on_ap_started(const event::Event& event, const event::WifiApStarted& data, void* context)
    {
        auto& service = *static_cast<ConfigurationService*>(context);

        if ((service.application_state_ == event::ApplicationState::Configuration && 
            (service.config_.configuration_service.mode == ConfigurationServiceConfig::OperatingMode::Ap || 
            service.config_.configuration_service.mode == ConfigurationServiceConfig::OperatingMode::ApSta)) ||

            service.mode_ == ConfigurationService::OperationMode::Recovery)
        {
            service.start_mdns();
            service.start_website();
        }
    }

    // This is irrelevant for ConfigurationServiceConfig::OperatingMode::Ap
    void ConfigurationService::on_wifi_reconnect_threshold_reached(const event::Event& event, const event::WifiReconnectThresholdReached& data, void* context)
    {
        auto& service = *static_cast<ConfigurationService*>(context);

        if (service.application_state_ == event::ApplicationState::Configuration && 
            service.config_.configuration_service.mode == ConfigurationServiceConfig::OperatingMode::ApSta)
        {
            // Recovery is unnecessary: the user can access the configuration website
            // through the AP and update the STA credentials.
        }
        else 
        {
            service.start_recovery();
        }
    }

    void ConfigurationService::on_sync(const event::Event& event, const event::SntpTimeSynced& data, void* context)
    {
        auto& service = *static_cast<ConfigurationService*>(context);

        ESP_LOGI(k_tag, "on_sync() triggered");
        ESP_ERROR_CHECK(service.scheduler_.recalculate());
    }
} // namespace internet