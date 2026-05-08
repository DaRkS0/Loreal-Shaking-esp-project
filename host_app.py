import socket
import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox
import math
import asyncio
from bleak import BleakScanner, BleakClient

UDP_IP = "0.0.0.0"
UDP_PORT = 5000

SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
CHAR_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8"

class BottleApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Bottle Shake Detector Host")
        self.root.geometry("400x600")
        self.root.configure(bg="#2C3E50")
        self.root.resizable(False, False)
        
        self.shake_active = False
        self.last_shake_time = 0
        self.anim_angle = 0
        self.anim_dir = 1
        
        # Title Label
        self.status_label = tk.Label(
            root, text="Bottle is resting...", 
            font=("Helvetica", 20, "bold"), 
            bg="#2C3E50", fg="#ECF0F1"
        )
        self.status_label.pack(pady=30)
        
        # Canvas for Drawing Bottle
        self.canvas = tk.Canvas(root, width=300, height=350, bg="#2C3E50", highlightthickness=0)
        self.canvas.pack()
        
        self.base_cx = 150
        self.base_cy = 180
        self.bottle_id = self.draw_bottle(self.base_cx, self.base_cy, "#3498DB")
        
        # Settings Button
        self.settings_btn = tk.Button(
            root, text="⚙ Configure WiFi (BLE)", font=("Helvetica", 12),
            bg="#34495E", fg="white", relief="flat", cursor="hand2",
            command=self.open_settings
        )
        self.settings_btn.pack(pady=20)
        
        # Start UDP Thread
        self.udp_thread = threading.Thread(target=self.udp_listener, daemon=True)
        self.udp_thread.start()
        
        # Start Animation Loop
        self.update_animation()

    def draw_bottle(self, cx, cy, color):
        points = [
            cx - 20, cy - 120, cx + 20, cy - 120, cx + 20, cy - 60,
            cx + 50, cy - 20, cx + 50, cy + 120, cx - 50, cy + 120,
            cx - 50, cy - 20, cx - 20, cy - 60,
        ]
        return self.canvas.create_polygon(points, fill=color, outline="#2980B9", width=4)
        
    def udp_listener(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind((UDP_IP, UDP_PORT))
        print(f"[*] Listening for UDP broadcasts on port {UDP_PORT}...")
        
        while True:
            try:
                data, addr = sock.recvfrom(1024)
                message = data.decode('utf-8', errors='ignore').strip()
                if "SHAKE" in message:
                    print(f"[+] Shake signal received from {addr[0]}!")
                    self.trigger_shake()
            except Exception as e:
                print("[-] UDP Error:", e)

    def trigger_shake(self):
        self.shake_active = True
        self.last_shake_time = time.time()
        self.root.after(0, self.update_ui_state, "Bottle is being shaked!", "#E74C3C", "#E67E22")

    def update_ui_state(self, text, text_color, bottle_color):
        self.status_label.config(text=text, fg=text_color)
        self.canvas.itemconfig(self.bottle_id, fill=bottle_color)

    def update_animation(self):
        current_time = time.time()
        if self.shake_active and (current_time - self.last_shake_time > 1.5):
            self.shake_active = False
            self.update_ui_state("Bottle is resting...", "#ECF0F1", "#3498DB")
            
        if self.shake_active:
            offset_x = math.sin(self.anim_angle) * 20
            self.canvas.move(self.bottle_id, offset_x * self.anim_dir, 0)
            self.anim_dir *= -1
            self.anim_angle += 1.0
        else:
            self.canvas.delete(self.bottle_id)
            self.bottle_id = self.draw_bottle(self.base_cx, self.base_cy, "#3498DB")
            self.anim_angle = 0
            
        self.root.after(40, self.update_animation)

    # ==========================
    # BLE Settings UI
    # ==========================
    def open_settings(self):
        self.settings_win = tk.Toplevel(self.root)
        self.settings_win.title("BLE WiFi Setup")
        self.settings_win.geometry("350x350")
        self.settings_win.configure(bg="#34495E")
        self.settings_win.grab_set()
        
        tk.Label(self.settings_win, text="ESP32 WiFi Config", font=("Helvetica", 16, "bold"), bg="#34495E", fg="white").pack(pady=15)
        
        self.scan_btn = tk.Button(self.settings_win, text="1. Scan ESP32 Networks", font=("Helvetica", 11), bg="#2980B9", fg="white", command=self.start_ble_scan)
        self.scan_btn.pack(pady=10)
        
        self.status_var = tk.StringVar(value="Status: Not connected")
        tk.Label(self.settings_win, textvariable=self.status_var, bg="#34495E", fg="#BDC3C7").pack()
        
        # Network Dropdown
        tk.Label(self.settings_win, text="Select Network:", bg="#34495E", fg="white").pack(pady=(15,2))
        self.network_combo = ttk.Combobox(self.settings_win, values=[], state="disabled")
        self.network_combo.pack()
        
        # Password Entry
        tk.Label(self.settings_win, text="Password:", bg="#34495E", fg="white").pack(pady=(10,2))
        self.password_entry = tk.Entry(self.settings_win, show="*", width=23)
        self.password_entry.pack()
        
        self.send_btn = tk.Button(self.settings_win, text="2. Send Credentials", font=("Helvetica", 11), bg="#27AE60", fg="white", state="disabled", command=self.send_credentials)
        self.send_btn.pack(pady=20)
        
        self.networks_found = []

    def start_ble_scan(self):
        self.scan_btn.config(state="disabled")
        self.status_var.set("Status: Connecting to ESP32...")
        threading.Thread(target=self.run_asyncio_task, args=(self.ble_scan_networks,), daemon=True).start()

    def send_credentials(self):
        ssid = self.network_combo.get()
        pwd = self.password_entry.get()
        if not ssid:
            messagebox.showwarning("Warning", "Please select a network.")
            return
            
        self.send_btn.config(state="disabled")
        self.status_var.set("Status: Sending credentials...")
        payload = f"{ssid};{pwd}"
        threading.Thread(target=self.run_asyncio_task, args=(self.ble_send_creds, payload), daemon=True).start()

    def run_asyncio_task(self, coro, *args):
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        loop.run_until_complete(coro(*args))

    # ==========================
    # BLE Async Logic
    # ==========================
    async def find_device(self):
        print("\n[BLE] Scanning for 10 seconds...")
        devices = await BleakScanner.discover(timeout=10.0, return_adv=True)
        
        target_device = None
        for addr, (d, adv) in devices.items():
            name = d.name or adv.local_name or "Unknown"
            uuids = adv.service_uuids if adv.service_uuids else []
            print(f"Found: {name} [{addr}] | UUIDs: {uuids}")
            
            # Match by Name
            if "Bottle" in name:
                target_device = d
                print(f"  >> Match found by name!")
            
            # Match by Service UUID
            for uuid in uuids:
                if SERVICE_UUID.lower() in str(uuid).lower():
                    target_device = d
                    print(f"  >> Match found by Service UUID: {uuid}")
                    
        if target_device:
            print(f"--> SELECTED: {target_device.name} [{target_device.address}]")
        else:
            print("--> No matching device found.")
        return target_device

    async def ble_scan_networks(self):
        device = await self.find_device()
        if not device:
            self.root.after(0, lambda: self.status_var.set("Status: ESP32 not found!"))
            self.root.after(0, lambda: self.scan_btn.config(state="normal"))
            return

        self.root.after(0, lambda: self.status_var.set("Status: Asking ESP32 to scan..."))
        
        networks = []
        scan_done = asyncio.Event()

        def notify_handler(sender, data):
            msg = data.decode('utf-8')
            if msg.startswith("NETWORKS:"):
                net_str = msg.replace("NETWORKS:", "")
                if net_str:
                    networks.extend(net_str.split(","))
                scan_done.set()

        try:
            async with BleakClient(device) as client:
                await client.start_notify(CHAR_UUID, notify_handler)
                await client.write_gatt_char(CHAR_UUID, b"SCAN")
                
                # Wait up to 15 seconds for the scan to finish
                try:
                    await asyncio.wait_for(scan_done.wait(), timeout=15.0)
                except asyncio.TimeoutError:
                    self.root.after(0, lambda: self.status_var.set("Status: Scan timeout!"))
                
                await client.stop_notify(CHAR_UUID)
                
            if networks:
                self.root.after(0, self.update_network_ui, networks)
            else:
                self.root.after(0, lambda: self.status_var.set("Status: No networks found."))
                
        except Exception as e:
            print("BLE Error:", e)
            self.root.after(0, lambda: self.status_var.set("Status: Connection error."))
            
        self.root.after(0, lambda: self.scan_btn.config(state="normal"))

    def update_network_ui(self, networks):
        self.status_var.set("Status: Scan complete.")
        self.network_combo['values'] = networks
        self.network_combo.config(state="readonly")
        if networks:
            self.network_combo.current(0)
        self.send_btn.config(state="normal")

    async def ble_send_creds(self, payload):
        device = await self.find_device()
        if not device:
            self.root.after(0, lambda: self.status_var.set("Status: ESP32 not found!"))
            self.root.after(0, lambda: self.send_btn.config(state="normal"))
            return

        try:
            async with BleakClient(device) as client:
                await client.write_gatt_char(CHAR_UUID, payload.encode('utf-8'))
                self.root.after(0, lambda: self.status_var.set("Status: Sent! Check ESP32."))
                self.root.after(0, lambda: messagebox.showinfo("Success", "Credentials sent to ESP32! It will now attempt to connect."))
                self.root.after(0, self.settings_win.destroy)
        except Exception as e:
            print("BLE Error:", e)
            self.root.after(0, lambda: self.status_var.set("Status: Failed to send."))
            self.root.after(0, lambda: self.send_btn.config(state="normal"))

if __name__ == "__main__":
    root = tk.Tk()
    app = BottleApp(root)
    root.mainloop()
