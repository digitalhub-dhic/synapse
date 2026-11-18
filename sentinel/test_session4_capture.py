import pygetwindow as gw
import pyautogui

titles = [t for t in gw.getAllTitles() if t.strip()]
with open(r"C:\SynapseAISentinel\window_debug.txt", "w", encoding="utf-8") as f:
    f.write("Visible titles in session:\n")
    for t in titles:
        f.write(f"  '{t}'\n")

print(f"Recorded {len(titles)} window titles to C:\\SynapseAISentinel\\window_debug.txt")
