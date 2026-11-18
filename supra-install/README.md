# Supra iDrive apps

This adds four apps to a Supra with iDrive 6:

- BoostDeck
- ShiftDeck
- Engine Fault Codes
- Bootmod3 Suite

Park the car and leave it parked until you are done. The whole thing takes about 15 minutes. You do not type a password into the car.

You need the ENET cable, a Mac or Windows computer, and the unzipped `supra-install` folder.

## Before you go to the car

Leave the ENET cable unplugged while you set up the computer.

1. Unzip `Supra-iDrive6-install.zip`.
2. Download Bavarian SSH Client from https://ssh.bavarian-aa.uk/
   - Mac: unzip it and open `bavarian-ssh-client.app`. If the Mac blocks it, right-click the app, choose Open, then Open again.
   - Windows: if a blue warning appears, choose More info, then Run anyway.
3. Download Python 3.12 from https://www.python.org/downloads/ . If the big download button says 3.13 or 3.14, skip it and pick the newest 3.12 from the list under it.

**On a Mac**

1. Open the Python installer and click through it.
2. Open Terminal. It is in Applications, then Utilities.
3. Type this and press Return. Change the path if you did not put the folder on your Desktop:

```bash
cd ~/Desktop/supra-install
python3.12 -m pip install paramiko==2.12.0
```

**On Windows**

1. Run the Python installer. On the first screen, check the box that says **Add python.exe to PATH**, then click Install Now.
2. Click Start, type `PowerShell`, right-click **Windows PowerShell**, and choose **Run as administrator**.
3. Type this and press Enter. Change the path if the folder is not on your Desktop:

```powershell
cd $env:USERPROFILE\Desktop\supra-install
py -3.12 -m pip install paramiko==2.12.0
```

Leave that window open.

## In the car

1. Open the driver's door and sit down. Keep your foot off the brake so the engine does not start.
2. Press the start button 3 times quickly. That puts the car in diagnostic mode and turns the screen on.
3. Plug the ENET cable into the car and into the computer now. Wait 10 seconds. Leave Wi-Fi on.

## Copy the apps

Go back to the same Terminal or PowerShell window.

Mac:

```bash
python3.12 install.py
```

Windows:

```powershell
py -3.12 install.py
```

On a Mac, it asks for your Mac login password. Nothing shows while you type. Press Return. That is your computer password.

When it tells you to, open Bavarian. Leave the IP box empty and press **Enable SSH** once. When it says SSH is enabled, click back to Terminal or PowerShell and press Return.

Leave the cable plugged in until it says the copy finished.

If Bavarian says it cannot find the car, quit Bavarian and open it again. Leave the IP box empty and press Enable SSH once more.

## When it finishes

1. Unplug the ENET cable.
2. Stay parked. Hold the volume knob by the screen until the screen goes completely dark.
3. Wait for iDrive to come back.
4. Open **My Vehicle**. You should see BoostDeck, ShiftDeck, Engine Fault Codes, and Bootmod3 Suite.

## Using them

BoostDeck is the gauge screen. Fuel pressure around 5000 psi is normal. Press **OPTIONS** on the iDrive controller to change the color, or to switch between psi and °F and bar and °C.

ShiftDeck is the tach. The shift light starts at 6000 rpm. Press **OPTIONS**, then **Shift light setting**, to change it. The choices are 5800, 6000, 6250, 6500, and 6700. After iDrive restarts, it goes back to 6000.

Engine Fault Codes can read codes. Do not clear them unless you mean to erase them.

Do not change anything in Bootmod3 Suite unless that car has a Bootmod3 tune and you mean to change it.

Open BoostDeck with the car on. The temperatures fill in on their own. A `--` means that reading has not arrived yet. The cable and the 3-press diagnostic mode are only for copying the apps on. Transmission temperature can stay `--` when the other readings are live. That reading comes from the transmission computer, which the screen cannot reach on its own.

Oil temperature sits in the middle of its bar at 220°F. Fuel pressure sits in the middle around 5000 psi. A normal reading is green. The bar turns yellow as that reading gets high, and red when it is very high.
