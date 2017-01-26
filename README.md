# TOTP-Lock-Project
Project to open a lock using a TOTP code rather than a normal 6-digit code.

This project was made to solve a problem: Our university club's clubroom door is always locked. There are only a few keys and these are the heavy-security laser-cut type that can't be copied (easily). The owner of the building and the university management want one. A few of the committee members want one (but they're not always around to open the clubrooms when members want in). But generally any member should be able to access the clubrooms if they have something to do in there...so a key is also stored at a university office on the other side of campus. But of course that is subject whether or not the person is on their list. Updating the list is cumbersome and not reliable. The people at the office are generally also not reliable in how they give out the key dispite the rules in place - we have had situations where a staff member will just give out the key to some sweet-talking person, another who took a gym membership card instead of a drivers license as "ID" even though it did not actually identify the person, and some staff who actually follow the rules strictly, but for some reason or another their boss hasn't taken the committee's updated list and given it to them. All of this is if (and only if) the office is even open... which it generally isn't when members want access to the clubrooms. 
Anyway, that system doesn't work well at all.

One more note - our old clubrooms had older locks with normal keys... very easy to copy. We found that pretty soon (over a matter of years/changes in committee etc) we had multiple copies of the keys floating around with various people and there was no way to track who had them - all attempts at tracking just got lost in the mists of time. I guess it was a bit of luck that all the gear stored in there didn't just disappear one day over the years - we have very honest members, both old and new.

So I envisioned a door lock that has a 6-digit passcode instead, meaning no physical key is necessary. But a static passcode wouldn't work - that code would also just get shared and shared until nobody knew who had it. Relying on someone to change it often isn't going to work - people pass through the club often, and having one person in charge of (a) changing the code weekly, and (b) answering their phone every time someone wants the new code... is just too cumbersome and won't work.

So the code needs to change all by itself. Ah Ha! TOTP codes do that... and they do that every 30 seconds by default... which avoids another potential security concern - any passer by on the street observing someone entering the correct code, waiting until they leave, and gaining access to steal everything. That won't work with a TOTP code. So all we need is a keypad with a code that changes every 30 seconds... that actually sounds easy to do with an arduino.

But there's one problem: how to notify the member who is standing in front of the door what the current code is? Well these days pretty much everybody carries a phone around, so pretty much everybody can call one of the committee members. There's 10 or so of them, so at least one is bound to be in reception at any one time (we're an outdoors club), and they more than likely are answering the call on their SMARTphone. So the member standing in front of the door calls a committee member, who can do two things (1) verify that they know the member and that they should let them in, and (2) look up the current code on their phone, read it out to the member who types it in and gains access. Booyah!

So off I went to Bunnings to purchase a cheap digital safe that I could hack. I took the door off, found out that it works by simply opening a solenoid which allows the user to turn a handle and open the safe door. Perfect. Replaced the circuitry in the safe door with an arduino, a time module, a battery pack and a mosfet (to run the solenoid) and out we came with a TOTP-enabled safe lock! That alone actually sounds like something that would sell but who cares about the marketability of an industrial TOTP safe really... I'm all about THIS clubrooms door. If I don't put the safe back together I can just cut a massive safe-door-sized hole in the clubrooms door, replacing the current lock and handle mechanism with the TOTP-enabled safe door. Perfect-ish! (But don't tell the university, they probably won't approve of me doing that last bit).

References: http://www.lucadentella.it/en/totp-libreria-per-arduino/
Generate a new code: http://www.lucadentella.it/OTP


Recent Changes (not shown in git history)
FIXED:
- got it working with the DS3231 Clock Module
- cleaned up setup() and loop() sections, moving a lot of stuff into functions.
- don't allow key presses while the 2second blink is active or the solenoid is open.
- reset keyboard buffer 4 seconds after last key pressed.
- move Uno and Nano code together using #defines (thanks Daniel!)

/  Todo:
- investigate what happens if currentMillis() rolls over (pretty sure nothing because it is unsigned)
- introduce watchdog to reset the arduino if it doesn't respond in a certain time.
- introduce a way to change the shared key
- one-time use code (don't allow repeat-use?)
- ...take into account clock-drift by auto-resetting the clock if a pattern is detected.
- QR codes can have longer periods - https://github.com/google/google-authenticator/wiki/Key-Uri-Format
- QR code generator (use free text option) - https://www.the-qrcode-generator.com/


