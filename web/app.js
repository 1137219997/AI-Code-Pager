"use strict";

const UUID = {
  service: "12345678-1234-5678-1234-56789abcdef0",
  rx: "12345678-1234-5678-1234-56789abcdef1",
  tx: "12345678-1234-5678-1234-56789abcdef2",
};

const OP = { SET_KEY: 0x01, SAVE: 0x02, RESET: 0x03, GET: 0x04, TEXT: 0x10, PET: 0x11 };
const EVT = { INPUT: 0x80, ACK: 0x81, KEYMAP: 0x82 };
const inputs = ["按键 1", "按键 2", "按键 3", "按键 4", "按键 5", "按键 6", "上", "下", "左", "右", "确认", "旋钮逆时针", "旋钮顺时针"];
const petNames = ["挠头", "向下指", "欢呼", "睡觉"];
const keyOptions = [
  ["禁用", 0x00, 0], ["Enter", 0x28, 0], ["Escape", 0x29, 0], ["Space", 0x2c, 0],
  ["Tab", 0x2b, 0], ["Backspace", 0x2a, 0], ["↑", 0x52, 0], ["↓", 0x51, 0],
  ["←", 0x50, 0], ["→", 0x4f, 0], ["Page Up", 0x4b, 0], ["Page Down", 0x4e, 0],
  ["F13", 0x68, 0], ["F14", 0x69, 0], ["F15", 0x6a, 0], ["F16", 0x6b, 0],
  ["F17", 0x6c, 0], ["F18", 0x6d, 0], ["Ctrl+Enter", 0x28, 0x01],
  ["Shift+Enter", 0x28, 0x02], ["Alt+Enter", 0x28, 0x04], ["GUI+Enter", 0x28, 0x08],
];

let device;
let rx;
let tx;
const enc = new TextEncoder();
const $ = (id) => document.getElementById(id);

function buildKeyGrid() {
  inputs.forEach((name, index) => {
    const row = document.createElement("div");
    row.className = "key-row";
    row.id = `keyRow${index}`;
    const label = document.createElement("label");
    label.textContent = name;
    label.htmlFor = `key${index}`;
    const select = document.createElement("select");
    select.id = `key${index}`;
    for (const [title, usage, modifier] of keyOptions) {
      const option = document.createElement("option");
      option.value = `${modifier},${usage}`;
      option.textContent = title;
      select.append(option);
    }
    select.addEventListener("change", async () => {
      const [modifier, usage] = select.value.split(",").map(Number);
      await write([OP.SET_KEY, index, modifier, usage]);
    });
    row.append(label, select);
    $("keyGrid").append(row);
  });
  applyDefaults();
}

function applyDefaults() {
  const usages = [0x68,0x69,0x6a,0x6b,0x6c,0x6d,0x52,0x51,0x50,0x4f,0x28,0x4b,0x4e];
  usages.forEach((usage, index) => { $(`key${index}`).value = `0,${usage}`; });
}

async function connect() {
  if (!navigator.bluetooth) throw new Error("当前浏览器不支持 Web Bluetooth，请使用 Chrome 或 Edge。");
  device = await navigator.bluetooth.requestDevice({
    filters: [{ services: [UUID.service] }],
    optionalServices: [UUID.service],
  });
  device.addEventListener("gattserverdisconnected", disconnected);
  const server = await device.gatt.connect();
  const service = await server.getPrimaryService(UUID.service);
  [rx, tx] = await Promise.all([service.getCharacteristic(UUID.rx), service.getCharacteristic(UUID.tx)]);
  await tx.startNotifications();
  tx.addEventListener("characteristicvaluechanged", notification);
  setConnected(true);
  await write([OP.GET]);
}

function disconnected() { rx = tx = undefined; setConnected(false); }
function setConnected(connected) {
  $("statusDot").classList.toggle("online", connected);
  $("statusText").textContent = connected ? `已连接 · ${device.name || "Pager"}` : "未连接";
  $("connectButton").textContent = connected ? "重新连接" : "连接传呼机";
}

async function write(bytes) {
  if (!rx) throw new Error("请先连接传呼机。");
  await rx.writeValueWithoutResponse(Uint8Array.from(bytes));
}

function notification(event) {
  const bytes = new Uint8Array(event.target.value.buffer);
  if (bytes[0] === EVT.INPUT && bytes.length >= 3) {
    const id = bytes[1];
    $(`keyRow${id}`)?.classList.toggle("active", bytes[2] !== 0);
    addEvent(inputs[id] || `输入 ${id}`, bytes[2] ? "按下" : "释放");
  } else if (bytes[0] === EVT.ACK && bytes[2] !== 0) {
    addEvent(`命令 0x${bytes[1].toString(16)}`, `错误 ${bytes[2]}`);
  } else if (bytes[0] === EVT.KEYMAP && bytes.length >= 2 + bytes[1] * 2) {
    for (let i = 0; i < bytes[1]; i++) {
      const value = `${bytes[2+i*2]},${bytes[3+i*2]}`;
      const select = $(`key${i}`);
      if (select && [...select.options].some((option) => option.value === value)) select.value = value;
    }
  }
}

function addEvent(name, state) {
  const log = $("eventLog");
  log.querySelector(".empty")?.remove();
  const item = document.createElement("li");
  item.innerHTML = `<span>${new Date().toLocaleTimeString()}</span><span></span><span></span>`;
  item.children[1].textContent = name;
  item.children[2].textContent = state;
  log.prepend(item);
  while (log.children.length > 30) log.lastElementChild.remove();
}

async function sendText() {
  const bytes = enc.encode($("messageInput").value);
  if (!bytes.length) return;
  if (bytes.length > 480) throw new Error("消息的 UTF-8 编码不能超过 480 bytes。");
  const chunkSize = 18;
  for (let offset = 0; offset < bytes.length; offset += chunkSize) {
    const chunk = bytes.slice(offset, offset + chunkSize);
    const flags = (offset === 0 ? 1 : 0) | (offset + chunkSize >= bytes.length ? 2 : 0);
    await write([OP.TEXT, flags, ...chunk]);
  }
}

function reportError(error) { alert(error instanceof Error ? error.message : String(error)); }

$("connectButton").addEventListener("click", () => connect().catch(reportError));
$("readKeymap").addEventListener("click", () => write([OP.GET]).catch(reportError));
$("saveKeymap").addEventListener("click", () => write([OP.SAVE]).catch(reportError));
$("resetKeymap").addEventListener("click", () => write([OP.RESET]).then(applyDefaults).catch(reportError));
$("sendMessage").addEventListener("click", () => sendText().catch(reportError));
$("messageInput").addEventListener("input", (event) => {
  const length = enc.encode(event.target.value).length;
  $("charCount").textContent = `${length} / 480 bytes`;
  $("sendMessage").disabled = length > 480;
});
$("stateButtons").addEventListener("click", (event) => {
  const button = event.target.closest("button[data-state]");
  if (!button) return;
  [...$("stateButtons").children].forEach((item) => item.classList.toggle("selected", item === button));
  const state = Number(button.dataset.state);
  $("petLabel").textContent = petNames[state];
  $("petImage").src = `../assets/pet/${button.dataset.name}/00.png`;
  write([OP.PET, state]).catch(reportError);
});

buildKeyGrid();
