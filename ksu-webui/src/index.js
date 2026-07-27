import { exec, spawn, toast } from 'kernelsu';

const template = document.getElementById('app-template').content;
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

async function run(cmd) {
	const LOG_DIR = "/data/adb/modules/zygisk-detach/webui.log";
	const { errno, stdout, stderr } = await exec(cmd);
	if (errno != 0) {
		toast(`Command '${cmd}' fail.`)
		toast(stderr);
		// this is not properly escaped, whatever
		const fullLog = `\
CMD: ${cmd}

STDERR:
${stderr}

STDOUT:
${stdout}`.replaceAll("'", "\'");
		exec(`echo '${fullLog}' > '${LOG_DIR}'`).then(() => {
			toast(`Full logs are saved in '${LOG_DIR}'`);
		});
		return undefined;
	} else {
		return stdout;
	}
}

function updateStats() {
	statPill.textContent = `${detach_list.length} / ${allPackages} detached`;
}

function updatePendingLabel() {
	// pending = ada perubahan dibanding state awal
	const initial = initiallyDetached;
	const current = new Set(detach_list);
	let changed = initial.size !== current.size;
	if (!changed) {
		for (const p of current) {
			if (!initial.has(p)) { changed = true; break; }
		}
	}
	if (changed) {
		pendingLabel.textContent = 'Ada perubahan belum diterapkan';
		applyBtn.disabled = false;
	} else {
		pendingLabel.textContent = 'Tidak ada perubahan';
		applyBtn.disabled = detach_list.length === 0 && initial.size === 0;
	}
}

function setRowState(row, checkbox, name) {
	row.dataset.state = checkbox.checked ? 'detached' : 'attached';
	row.querySelector('.state-label').textContent = checkbox.checked
		? 'Detached — diabaikan Play Store'
		: 'Attached — mengikuti update normal';
}

function applyFilterAndSearch() {
	const searchVal = searchInput.value.toLowerCase();
	let visibleCount = 0;
	for (const row of appsList.children) {
		const name = row.querySelector('.name').textContent.toLowerCase();
		const state = row.dataset.state;
		const matchesSearch = !searchVal || name.includes(searchVal);
		const matchesFilter = currentFilter === 'all' || state === currentFilter;
		const visible = matchesSearch && matchesFilter;
		row.hidden = !visible;
		if (visible) visibleCount++;
	}
	emptyState.hidden = visibleCount !== 0;
}

function sortChecked() {
	[...appsList.children]
		.sort((a, _b) => a.querySelector('.checkbox').checked ? -1 : 1)
		.forEach(node => appsList.appendChild(node));
}

function populateApp(name, checked) {
	const node = document.importNode(template, true);
	const row = node.querySelector('.app-row');
	node.querySelector('.name').textContent = name;
	const checkbox = node.querySelector('.checkbox');
	checkbox.checked = checked;
	if (checked) {
		detach_list.push(name);
		initiallyDetached.add(name);
	}
	setRowState(row, checkbox, name);
	checkbox.addEventListener('change', () => {
		if (checkbox.checked) {
			detach_list.push(name);
		} else {
			const i = detach_list.indexOf(name);
			if (i !== -1) detach_list.splice(i, 1);
		}
		setRowState(row, checkbox, name);
		updateStats();
		updatePendingLabel();
	});
	appsList.appendChild(row);
}

async function main() {
	const pkgs = await run("pm list packages");
	if (pkgs === undefined) {
		loadingState.textContent = 'Gagal memuat daftar paket.';
		return;
	}

	const detached_list_out = await run("/data/adb/modules/zygisk-detach/detach list");
	if (detached_list_out === undefined) {
		loadingState.textContent = 'Gagal membaca status detach.';
		return;
	}
	const detached = detached_list_out ? detached_list_out.split('\n').filter(Boolean) : [];
	const uninstalled = detached ? [...detached] : [];
	const pkgNames = pkgs.split('\n').map((line) => line.split(':')[1]).filter(Boolean);

	for (const pkg of pkgNames) {
		const incls = detached.includes(pkg);
		populateApp(pkg, incls);
		if (incls) {
			const index = uninstalled.indexOf(pkg);
			if (index > -1) uninstalled.splice(index, 1);
		}
	}
	// paket yang tercatat detached tapi sudah tidak terinstall lagi
	for (const pkg of uninstalled) populateApp(pkg, true);

	allPackages = appsList.children.length;
	loadingState.hidden = true;
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
		filtersEl.querySelectorAll('.chip').forEach((c) => c.classList.remove('is-active'));
		btn.classList.add('is-active');
		currentFilter = btn.dataset.filter;
		applyFilterAndSearch();
	});

	applyBtn.addEventListener('click', () => {
		if (detach_list.length == 0) {
			run("/data/adb/modules/zygisk-detach/detach reset").then(() => {
				toast('Semua paket dikembalikan ke attached.');
				initiallyDetached.clear();
				updatePendingLabel();
			});
		} else {
			const detach_arg = detach_list.join(' ');
			run(`/data/adb/modules/zygisk-detach/detach detachall ${detach_arg}`).then((out) => {
				toast(out || 'Perubahan diterapkan.');
				initiallyDetached.clear();
				detach_list.forEach((p) => initiallyDetached.add(p));
				updatePendingLabel();
			});
		}
	});
}

await main();
