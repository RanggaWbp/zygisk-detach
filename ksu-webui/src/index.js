import { exec, spawn, toast } from 'kernelsu';

const appsList = document.getElementById('apps-list');
const statPill = document.getElementById('stat-pill');
const searchInput = document.getElementById('search');
const filtersEl = document.getElementById('filters');
const emptyState = document.getElementById('empty-state');
const loadingState = document.getElementById('loading-state');
const actionBar = document.getElementById('action-bar');
const pendingLabel = document.getElementById('pending-label');
const applyBtn = document.getElementById('detach');

let currentFilter = 'all';
let allPackages = 0;
const detach_list = [];
const initiallyDetached = new Set();

const LOG_DIR = '/data/adb/modules/zygisk-detach/webui.log';

function shellSingleQuoteEscape(str) {
    return str.replaceAll("'", `'\\''`);
}

async function run(cmd) {
    const { errno, stdout, stderr } = await exec(cmd);
    if (errno != 0) {
        toast(`Command failed.`);
        toast(stderr);
        const fullLog = `CMD: ${cmd}\n\nSTDERR:\n${stderr}\n\nSTDOUT:\n${stdout}`;
        exec(`echo '${shellSingleQuoteEscape(fullLog)}' > '${LOG_DIR}'`).then(() => {
            toast(`Log saved to ${LOG_DIR}`);
        });
        return undefined;
    }
    return stdout;
}

function updateStats() {
    statPill.textContent = `${detach_list.length} / ${allPackages} detached`;
}

function updatePendingLabel() {
    const initial = initiallyDetached;
    const current = new Set(detach_list);
    let changed = initial.size !== current.size;
    if (!changed) {
        for (const p of current) {
            if (!initial.has(p)) { changed = true; break; }
        }
    }
    if (changed) {
        pendingLabel.textContent = 'You have unapplied changes';
        applyBtn.disabled = false;
    } else {
        pendingLabel.textContent = 'No pending changes';
        applyBtn.disabled = true;
    }
}

// Perbaikan Search & Filter
function applyFilterAndSearch() {
    const searchVal = searchInput.value.toLowerCase().trim();
    let visibleCount = 0;

    for (const row of appsList.children) {
        const name = row.querySelector('.name').textContent.toLowerCase();
        const state = row.dataset.state;
        const matchesSearch = name.includes(searchVal);
        const matchesFilter = currentFilter === 'all' || state === currentFilter;

        if (matchesSearch && matchesFilter) {
            row.style.display = 'flex'; // Paksa flex agar tidak di-override
            visibleCount++;
        } else {
            row.style.display = 'none';
        }
    }
    emptyState.style.display = visibleCount === 0 ? 'flex' : 'none';
}

function sortChecked() {
    [...appsList.children]
        .sort((a, b) => a.querySelector('input').checked ? -1 : 1)
        .forEach(node => appsList.appendChild(node));
}

// Perbaikan Toggle Detach/Attach (Generate Dinamis)
function populateApp(name, checked) {
    const row = document.createElement('div');
    row.className = 'app-row';
    row.dataset.state = checked ? 'detached' : 'attached';
    
    row.innerHTML = `
        <div class="app-info">
            <div class="name">${name}</div>
            <div class="state-label">${checked ? 'Detached — ignored by Play Store' : 'Attached — receives normal updates'}</div>
        </div>
        <label class="switch">
            <input type="checkbox" class="checkbox" ${checked ? 'checked' : ''}>
            <span class="slider"></span>
        </label>
    `;

    const checkbox = row.querySelector('.checkbox');
    const stateLabel = row.querySelector('.state-label');

    if (checked) {
        detach_list.push(name);
        initiallyDetached.add(name);
    }

    checkbox.addEventListener('change', () => {
        const isChecked = checkbox.checked;
        row.dataset.state = isChecked ? 'detached' : 'attached';
        stateLabel.textContent = isChecked 
            ? 'Detached — ignored by Play Store' 
            : 'Attached — receives normal updates';

        if (isChecked) {
            if (!detach_list.includes(name)) detach_list.push(name);
        } else {
            const i = detach_list.indexOf(name);
            if (i !== -1) detach_list.splice(i, 1);
        }
        
        updateStats();
        updatePendingLabel();
    });

    appsList.appendChild(row);
}

async function main() {
    const pkgs = await run('pm list packages');
    if (pkgs === undefined) {
        loadingState.innerHTML = '<span>Failed to load package list.</span>';
        return;
    }

    const detached_list_out = await run('/data/adb/modules/zygisk-detach/detach list');
    if (detached_list_out === undefined) {
        loadingState.innerHTML = '<span>Failed to read detach status.</span>';
        return;
    }
    
    const detached = detached_list_out ? detached_list_out.split('\n').filter(Boolean) : [];
    const uninstalled = [...detached];
    const pkgNames = pkgs.split('\n').map((line) => line.split(':')[1]).filter(Boolean);

    for (const pkg of pkgNames) {
        const isDetached = detached.includes(pkg);
        populateApp(pkg, isDetached);
        if (isDetached) {
            const index = uninstalled.indexOf(pkg);
            if (index > -1) uninstalled.splice(index, 1);
        }
    }
    
    for (const pkg of uninstalled) populateApp(pkg, true);

    allPackages = appsList.children.length;
    loadingState.style.display = 'none';
    sortChecked();
    updateStats();
    updatePendingLabel();
    applyFilterAndSearch();

    searchInput.addEventListener('input', () => {
        if (!searchInput.value) sortChecked();
        applyFilterAndSearch();
    });

    filtersEl.addEventListener('click', (e) => {
        const btn = e.target.closest('.chip');
        if (!btn) return;
        filtersEl.querySelectorAll('.chip').forEach((c) => {
            c.classList.remove('is-active');
        });
        btn.classList.add('is-active');
        currentFilter = btn.dataset.filter;
        applyFilterAndSearch();
    });

    applyBtn.addEventListener('click', () => {
        applyBtn.disabled = true;
        applyBtn.textContent = 'Applying...';
        
        if (detach_list.length === 0) {
            run('/data/adb/modules/zygisk-detach/detach reset').then((out) => {
                applyBtn.textContent = 'Apply Changes';
                if (out === undefined) {
                    updatePendingLabel();
                    return;
                }
                toast('All packages restored to attached.');
                initiallyDetached.clear();
                updatePendingLabel();
            });
        } else {
            const detach_arg = detach_list.join(' ');
            run(`/data/adb/modules/zygisk-detach/detach detachall ${detach_arg}`).then((out) => {
                applyBtn.textContent = 'Apply Changes';
                if (out === undefined) {
                    updatePendingLabel();
                    return;
                }
                toast(out || 'Changes applied.');
                initiallyDetached.clear();
                detach_list.forEach((p) => initiallyDetached.add(p));
                updatePendingLabel();
            });
        }
    });
}

await main();
