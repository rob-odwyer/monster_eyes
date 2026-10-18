#!/Users/rob/.pyenv/versions/homeassistant-3.12.1/bin/python
"""Send an open/closed command to the eye over MQTT, or watch its topics.

    ./eye.py open
    ./eye.py closed
    ./eye.py watch      # print state/status messages until Ctrl+C
"""
import sys
import time
import paho.mqtt.client as mqtt

HOST, PORT = "10.0.0.13", 1883
BASE = "monster_eyes"

def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    c = mqtt.Client()
    c.connect(HOST, PORT, 10)
    if cmd in ("open", "closed"):
        c.publish(f"{BASE}/eye/set", cmd)
        c.loop(1)  # flush
        print(f"sent {cmd}")
    elif cmd == "watch":
        c.on_message = lambda _c, _u, m: print(f"{m.topic}: {m.payload.decode()}")
        c.subscribe(f"{BASE}/#")
        print(f"watching {BASE}/# on {HOST} (Ctrl+C to stop)")
        c.loop_forever()
    else:
        print(__doc__)
        sys.exit(1)

main()
