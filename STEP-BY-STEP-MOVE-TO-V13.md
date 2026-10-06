# Step by step: moving the JK Active Balancer from BE 8.13 (v3.4 LUCRU) to Battery-Emulator v13

Folders used below:
- OLD = `C:\Users\MYOde\Desktop\CLAUDE-VS-CODE\BE 8.13 JK BMS v.3.4 LUCRU` (do not change anything in here any more)
- NEW = `C:\Users\MYOde\Desktop\CLAUDE-VS-CODE\Battery-Emulator-13` (the downloaded v13; all work happens here)

Keep the board that runs 8.13 untouched until the end. Use a second LilyGo for the port if you have one.

---------------------------------------------------------------------------------------------------
## PHASE 1 - Prepare the NEW folder (10 minutes, one time)

1. Open VS Code. Menu File -> Open Folder... -> select the NEW folder -> Open.
   (Open the folder itself, not a file inside it. The PlatformIO toolbar appears at the bottom only when the
   opened folder contains `platformio.ini`.)
2. If VS Code asks "Do you trust the authors?" click Yes.
3. Open a terminal inside VS Code: Menu Terminal -> New Terminal. Make sure it is PowerShell (the tab says
   "powershell"). Paste these lines one by one and press Enter after each:
   ```
   git init
   git add .
   git commit -m "Upstream Battery-Emulator v13 as downloaded"
   ```
   This creates a save point. Any time later you can see exactly what Claude changed.
4. Copy the porting instructions into the NEW folder: in the terminal run
   ```
   Copy-Item "..\BE 8.13 JK BMS v.3.4 LUCRU\PORTING-TO-BE13.md" .
   ```
   Check in the VS Code file list on the left that `PORTING-TO-BE13.md` now exists in NEW.

---------------------------------------------------------------------------------------------------
## PHASE 2 - Build the untouched v13 once (15-40 minutes the first time, mostly downloading)

5. Wait until PlatformIO has finished initializing (bottom bar shows a house icon, a tick, an arrow, a trash can).
   The first time it downloads the toolchain and the pioarduino platform (about 1 GB). Let it finish; do not close VS Code.
6. Select the board: click the environment selector in the bottom bar (it says "Default" or an env name) and choose
   `env:lilygo_2CAN_330` 
7. Click the tick icon (Build) in the bottom bar. Wait. The terminal must end with
   `[SUCCESS] Took ... seconds` and a line like `RAM: ... Flash: ...`. Write down the Flash percentage.
   - If it ends with `[FAILED]`: do not continue. Copy the last 40 lines of the terminal and give them to Claude in
     Phase 3 as the first problem to solve. Typical causes: PlatformIO Core older than 6.2.0 (Claude will tell you
     how to update), antivirus blocking the toolchain download, no internet.
8. Commit nothing; nothing changed. Go on.

---------------------------------------------------------------------------------------------------
## PHASE 3 - Start the Claude session that does the port

9. Make sure the NEW folder is the one open in VS Code (window title shows "Battery-Emulator-13").
10. Open Claude Code (the Claude icon in the left bar, or Ctrl+Shift+P -> "Claude Code: Open").
    A fresh session is important: it must work in the NEW folder, not in OLD.
11. Paste this as the first message (copy it exactly):

    ```
    You are porting the JK Active Balancer integration from an older Battery-Emulator 8.13 fork into this
    Battery-Emulator v13 project (this folder). Read PORTING-TO-BE13.md in this folder first and follow it in
    order; it contains all decisions already taken, do not re-ask them. The 8.13 source to port from is in
    "C:\Users\MYOde\Desktop\CLAUDE-VS-CODE\BE 8.13 JK BMS v.3.4 LUCRU\Software" (read only, never modify it).
    Target board environment: lilygo_2CAN_330. Work through checklist section 3 one item at a time: after each item
    run "pio run -e lilygo_2CAN_330" yourself, show me the result, and commit with git before starting the next item.
    Explain to me in two or three plain sentences what each item changed. Stop and ask me when PORTING-TO-BE13.md
    says CHECK and you cannot verify it from the code. I will test on hardware only after section 3.6 and again
    after 3.9.
    ```
12. Claude will start with item 3.1 (which you already did in Phase 2; it may rebuild to confirm) and continue.
    Every time it says it finished an item and the build succeeded, answer "continue". If a build fails, Claude fixes
    it; you only need to answer questions.
13. Questions Claude may ask and what to answer:
    - "Keep the discharge ramp bottom at 7 % or use SOC min?" -> your choice; 8.13 behaviour is 7 %.
    - "Fix the full/empty flag bug?" -> yes.
    - Anything about analog CT, GPIO, CAN speed settings, serial link -> "not ported, see section 0".
    - If it asks about a "CHECK" item it could not verify -> tell it to read the file it names and decide; if it
      still cannot, tell it to choose the safest option and write a note in PORTING-TO-BE13.md.
14. After item 3.6 Claude will ask you to test on hardware. Go to Phase 4, then come back and say "continue".
15. After item 3.9 do Phase 5. Then tell Claude "done, update the manual" so it revises
    `JK-Active-Balancer-Manual.docx` (copy it from OLD into NEW first:
    `Copy-Item "..\BE 8.13 JK BMS v.3.4 LUCRU\JK-Active-Balancer-Manual.docx" .`).

---------------------------------------------------------------------------------------------------
## PHASE 4 - First hardware test (after checklist item 3.6: driver registered, no settings page yet)

16. Connect the test LilyGo by USB. In VS Code bottom bar click the arrow icon (Upload). PlatformIO writes
    bootloader, partition table and firmware over USB; the BE-Web-Installer is NOT needed when you flash this way.
    Wait for `[SUCCESS]`. If it says the port was not found: unplug/replug, close any serial monitor, try again.
17. Power the board. Join its WiFi access point (name starts with `BatteryEmulator`) or find it on your router.
    Open `http://192.168.4.1` (access point) or its IP.
18. Settings page (v13 has one page for everything): set
    - Battery: `JK Active Balancer (CAB500)`
    - Battery CAN interface (BATTCOMM): `CAN add-on MCP2515`
    - Inverter: `BYD Battery-Box Premium HVS (CAN)`; inverter CAN interface: `CAN native`; tick the Deye workaround
      if you use a Deye.
    - Contactor control: on. Precharge time as before (500 ms).
    - Battery capacity Wh: your real value. Max charge / discharge current: as before (30 A).
    - Target discharge voltage: your pack cut-off (37.0 V for the 12S pack). Important: v13 default is 300 V.
    - WiFi credentials.
    Save, reboot (button on the page).
19. Check "More battery info": both balancers Connected, CAB500 Connected, pack voltage equals your multimeter,
    current positive while charging. If the current sign is wrong, write it down: in Phase 5 the "Reverse current
    sensor direction" box appears on the Settings page.
20. If balancers are Disconnected: check the CAN add-on wiring and termination; the speed is fixed at 250 kbps by
    the driver now, there is no setting for it. Give Claude the serial log (Ctrl+Shift+P -> "PlatformIO: Serial
    Monitor") if it does not work.
21. Back in Claude: "continue".

---------------------------------------------------------------------------------------------------
## PHASE 5 - Full test (after checklist item 3.9)

22. Upload again (arrow icon). Reboot. On the Settings page the JK section now shows all the fields you know from
    8.13 (power W, SOC window, deadband, recalibration, ramps, max balancers, reverse current, LFP, voltage limits,
    cells, cells per balancer, low-voltage/bridge, firmware version and the 15 balancer boxes). Set them to your
    8.13 values (the 8.13 web page of the old board shows them; they are also in the manual, Appendix A). Save, reboot.
23. Run the bench list from PORTING-TO-BE13.md section 9: pack voltage, current sign, SOC moving, rest recalibration,
    unplug a balancer (fault after 10 s, 50 W), unplug the CAB500 (fault after 30 s + 10 s), set an impossible layout
    (red banner, 0 W), put it back.
24. Only when all of that behaves: connect the inverter and watch one full charge and one full discharge.
25. Anything odd: copy the page text or the serial log and give it to Claude.

---------------------------------------------------------------------------------------------------
## PHASE 6 - Keep it safe

26. In the VS Code terminal: `git log --oneline` shows one commit per step. To go back to a step: ask Claude
    ("revert to the commit before X"), do not do it by hand.
27. Make a zip of the NEW folder when everything works (right-click the folder in Explorer -> Send to ->
    Compressed folder). Keep OLD as is; it still builds in Arduino IDE if you ever need the 8.13 firmware.
28. Later updates from upstream (v13.2 ...): tell Claude "merge upstream tag vX.Y into this branch"; the shared files
    were touched as little as possible so that merge stays small.
29. Leaf MOD: start a new Claude session in NEW with: "Port the Nissan Leaf MOD following section 10 of
    PORTING-TO-BE13.md; the 8.13 source is in <OLD>\Software\src\battery\NISSAN-LEAF-BATTERY-MOD.cpp".

---------------------------------------------------------------------------------------------------
## If something goes wrong

- Build fails right after opening (Phase 2): PlatformIO Core version. Ctrl+Shift+P -> "PlatformIO: Upgrade
  PlatformIO Core", then rebuild. Release notes say Core 6.2.0 is required.
- `sdkconfig.defaults` error on first build: delete the file `sdkconfig.defaults` in NEW if it exists (not
  `sdkconfig.be_size.defaults`) and rebuild - this is in the v13 release notes.
- Upload works but the board boots to the 8.13 web page: you uploaded to the wrong COM port (the old board). Check
  which USB cable goes where.
- The board web page never appears: open the serial monitor at 115200 and give the text to Claude.
- Claude seems lost or edits OLD: stop it, start a new session in NEW and paste the Phase 3 message again; it
  continues from the git commits.
