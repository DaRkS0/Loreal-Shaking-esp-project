import socket
import time

UDP_IP = "255.255.255.255"
UDP_PORT = 5000

print(f"Sending dummy UDP broadcast to {UDP_IP}:{UDP_PORT}")
print("Make sure host_app.py is running to see the effect!")

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
# Enable broadcasting mode
sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

try:
    for i in range(10):
        print(f"Sending SHAKE signal {i+1}/10...")
        sock.sendto(b"SHAKE", (UDP_IP, UDP_PORT))
        time.sleep(0.3) # sending rapidly to simulate continuous shake
    
    print("Done testing. The bottle in the UI should stop shaking shortly.")
    
except PermissionError:
    print("\n[!] Permission Error: Broadcasting to 255.255.255.255 failed.")
    print("Try changing UDP_IP to '<broadcast>' or your specific subnet broadcast IP (e.g., 192.168.1.255) in this script.")
except KeyboardInterrupt:
    print("Test stopped.")
