const deviceIp = document.querySelector("#deviceIp");
const deviceKey = document.querySelector("#deviceKey");
const autoPost = document.querySelector("#autoPost");
const status = document.querySelector("#status");

load();

document.querySelector("#save").addEventListener("click", async () => {
  chrome.runtime.sendMessage({
    type: "TOKKANI_SAVE_SETTINGS",
    payload: {
      deviceIp: deviceIp.value,
      deviceKey: deviceKey.value,
      autoPost: autoPost.checked
    }
  }, (response) => {
    status.textContent = response?.ok ? "Saved" : response?.error || "Could not save settings.";
    window.setTimeout(() => {
      status.textContent = "";
    }, 1500);
  });
});

async function load() {
  const settings = await chrome.storage.local.get(["deviceIp", "deviceKey", "autoPost"]);
  deviceIp.value = settings.deviceIp || "";
  deviceKey.value = settings.deviceKey || "";
  autoPost.checked = settings.autoPost !== false;
}
