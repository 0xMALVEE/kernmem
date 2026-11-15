# [Tutorial] How to dump LoL + get addresses: The guide

Source: https://www.unknowncheats.me/forum/league-of-legends/320116-dump-lol-addresses-guide.html
Pages saved: 1–8


---

### Post #2361276 — shattter — 6th February 2019, 09:29 PM

Hey guys,

 As Matt is taking a step away due to IRL work, my inbox here and my Discord DM's have already been blowing up over the last week with questions regarding:

 "How to get the latest addresses, none in megathread?!"
 "You said before to use IDA, but how to get them?"
 "I can't dump the game doesn't work help pls".

 I do not mind AT ALL getting these multiple times a day, however instead of privately telling each of you having issues it's just going to make my life easier by writing this one time guide - as well as hopefully allowing some of you to not only rely on the offset thread, but also contribute
![image](https://www.unknowncheats.me/forum/images/smilies/smile.gif)

 First things first, you will need 3 things:

- Tarekwiz' league dumper, I recommend this for beginners due to ease of use ([https://github.com/tarekwiz/LeagueDumper](https://github.com/tarekwiz/LeagueDumper))
- IDA Pro, most of you interested in this have it already so won't be providing info on how to get
- League, obviously..
 Nice, with those 3 things you can now begin to find updated function addresses/offsets.

 Step 1: Launching into a practice tool on league

 You will need to launch a practice tool game - as this is where you will dump the game. Those of you that try to put the League of Legends.exe into IDA will know it is packed, and this is why you can't xref things / the exe is pretty useless without unpacking it. By running the game we are no longer totally limited by the packer as things have to be unpacked in order for the game to actually run.

 [https://vgy.me/FWiuUT.jpg](https://vgy.me/FWiuUT.jpg)

 Step 2: Using the dumper

 This step is pretty straightforward as well - however many people seem to get pretty stuck here. Those of you that tried dumping with scylla by suspending in process hacker etc will know it doesn't work as intended, and there are numerous posts on this in the AC thread from a while back so I won't regurgitate the info now, simply go back and read the thread.

 With tareks modified process-dump, we can put the pd.exe into a directory (desktop/league stuff) etc, wherever you want to store your dumps each patch.

 Next we open a CMD window, and navigate to our folder where we put the pd.exe, to do this type "cd /d d:\league" or wherever/whatever you decided to use.

  Code:
```
C:\Users\shatter>cd /d C:\Users\shatter\Desktop\leageu work
```
  (i know league is spelt wrong I just kinda left it cos why not lol)

 Now you will want to type  Code:
```
pd.exe -pid 12345
```
  where 12345 is your League of Legends.exe process ID. This can easily be found by opening Task Manager, navigating to Details tab and reading the process ID from there:

 [https://vgy.me/ujvbSG.png](https://vgy.me/ujvbSG.png)

 In my case, that is 7652. This means I will use -pid 7652. Before we hit the return key, tab back into league and walk around for a second, cast a spell and then back to CMD and now hit the return key and let the dumper proceed to suspend the process and dump the files.

 Some people say you should walk around and cast / others say don't but there's not harm for you to do it.

 You will receive a "Finished running" after it is done, and maybe during the dump you got 100s of failed VirtualProtect spams in the console, ignore them. The game will now no longer be responding and is fine to just close via task manager or however you want.

 Now in your folder you will see quite a few files produced during the dump, and the important thing is to notice the 2 different exe's. The way the game is packed will produce 2, but the important one is the one outlined in red, with the actual name of League of Legends.exe, rather than the hiddenmodule:

 [https://vgy.me/3QwRsu.png](https://vgy.me/3QwRsu.png)

 Step 3: Moving into IDA

 The last step is pretty simple too, just be sure to follow along. What we want to do now is to take the dumped exe and drag it into ida/onto the ida exe, whatever you prefer:

 [https://vgy.me/ueXel7.png](https://vgy.me/ueXel7.png)

 Take note of a part of the exe name too:
  Code:
```
League of Legends_exe_PID1c94_League of Legends.exe_150000_x86.exe
```
  The 150000 part tells us the game was at base address of 0x150000. This will be very useful now when it comes to finding addresses - those of you whom are used to rebasing I HEAVILY  recommend not to do that in league, most who do end up with issues with correct addresses.

 In IDA we will hit ok on the first window, and no when it asks to search for the symbol (won't find riots symbol on the microsoft server lol).

 Now you wait until at the bottom left of IDA it says AU: idle
 [https://vgy.me/XAaQs6.png](https://vgy.me/XAaQs6.png) (superb image here no flame)

 This means IDA has finished, can now go about looking for an address.

 Hit Shift+F12 to generate all the strings, and when inside the string window press Ctrl+F to start a search for something. In this tutorial I will show you how to find the localplayer, the isHero function and the isTurret function.

 For 9.3 they are:
  Code:
```
oIsHero = 0x227780
oIsTurret = 0x227950
oLocalPlayer = 0x2EC1870
```
  Search for  Code:
```
blueHero
```
  and it will look like this:
 [https://vgy.me/9ns1V0.png](https://vgy.me/9ns1V0.png)

 Double click and it will take you here:
 [https://vgy.me/AFvjzT.png](https://vgy.me/AFvjzT.png)

 Now u can press X while highlighting the blueHero, and hit Enter/return.
 [https://vgy.me/HB8uiK.png](https://vgy.me/HB8uiK.png)

 You will now see something like this:
 [https://vgy.me/XINhp1.png](https://vgy.me/XINhp1.png)

 The red outline are my comments to show you what those actually are.
 If you look at the top, above the "you" string you can see the  Code:
```
 dword_3011870
```
  in my image, for you it will be different - this is due to our different BASE ADDRESSES.

 Now without me even telling you that that is the localplayer, if we were attempting to work it out without any help, the string "you" is pretty helpful in showing us we must be dealing with "us" as in the localplayer.

 The final stage is to just grab that address, subtract the base address of the dump and voila we will have the LocalPlayer for 9.3:

  Code:
```
0x3011870 - 0x150000 = 0x2EC1870
```
  Now refer to the address above, oLocalPlayer was 0x2EC1870.

 From here you can now build a signature (many tutorials on UC) and through a mix of sigs/string cross-references you can obtain all the addresses. An easy way for you beginners would be to wait for all the 9.3 to be posted, and then do the reverse.

- 9.3 Address for GetSpellState is 0x588970
- Add your base address and GetSpellState , so for me it will be 0x588970+ 0x150000 = 0x6D8970
- This is the address I can now jump to in IDA by pressing G and pasting the address into the form; and I will be at the GetSpellState function, from here can search for strings and make patterns.
- Do this for all the stuff you use, and on patch days you won't need to wait for someone to post them, can just run a python script in IDA to grab them all for you from the signatures
 Hopefully this was helpful to you, and those with a 9.2 dump can easily update everything that you need to for 9.3, those without just wait for a complete offset list, if I have time I'll post one in a few hours. Sadly UC no longer allows game dumps so I can't upload the 9.2 for you.

 I know this was very VERY simplified but isn't aimed at anyone that's used IDA or can actually understand the process of finding things from the disassembly, more as an introduction for all the new people that join this section each day
![image](https://www.unknowncheats.me/forum/images/smilies/smile.gif)

 Enjoy - and if you need help don't stop PM'ing me I don't bite just this should clear up a lot of the confusion.

 shatter

---

### Post #2361278 — Odysex — 6th February 2019, 09:31 PM

You're nuts, thaks for the tutorial budd. Much love <3

---

### Post #2361368 — Catbert — 6th February 2019, 10:55 PM

This is awesome, thankyou

---

### Post #2361376 — shattter — 6th February 2019, 11:03 PM

> **Odysex:**
> Originally Posted by Odysex   You're nuts, thaks for the tutorial budd. Much love <3

> **Catbert:**
> Originally Posted by Catbert   This is awesome, thankyou

You're welcome

---

### Post #2361401 — themagicalgamer — 6th February 2019, 11:39 PM

Thank you

---

### Post #2361434 — munnkorn — 7th February 2019, 12:54 AM

Thanks, very helpfull!

---

### Post #2361495 — hawhawmatt — 7th February 2019, 04:37 AM

Great post by a great guy. +rep
 Btw, an important thing is that if you have offsets & exes before the anti-cheat patch, you can easily get the new offsets by just making patterns. Most of them haven't changed as far as what I've checked (9.2-)

---

### Post #2361498 — shattter — 7th February 2019, 05:03 AM

> **hawhawmatt:**
> Originally Posted by hawhawmatt   Great post by a great guy. +rep
>  Btw, an important thing is that if you have offsets & exes before the anti-cheat patch, you can easily get the new offsets by just making patterns. Most of them haven't changed as far as what I've checked (9.2-)

Thanks man, hope everything is well with you
![image](https://www.unknowncheats.me/forum/images/smilies/smile.gif)
 Great tip as well!

---

### Post #2361506 — xsm0o0tx — 7th February 2019, 05:17 AM

Thanks for the tutorial
![image](https://www.unknowncheats.me/forum/images/smilies/smile.gif)

---

### Post #2361527 — synaxis — 7th February 2019, 05:41 AM

Very nice
 hope we can achieve more in League with this

---

### Post #2361648 — hideinnull — 7th February 2019, 10:16 AM

Very useful post thank you, is there a discord for people working on league at the moment?

---

### Post #2362069 — hackedhacker — 7th February 2019, 07:11 PM

Elobuddy Rerevive 4.0, yes.

---

### Post #2362091 — shattter — 7th February 2019, 07:40 PM

> **hackedhacker:**
> Originally Posted by hackedhacker   Elobuddy Rerevive 4.0, yes.

LMAO NOT HERE AS WELL

---

### Post #2362176 — walangtayoexb — 7th February 2019, 09:05 PM

THANK YOU VERY MUCH !!!!!!!!!!

 ;how do you find objmanager and aiheroclient? i tried searching networldobject for objmanager but it's empty(didn't find offset) and how about heroclient? i saw AIHeroClient in string but also empty
![image](https://www.unknowncheats.me/forum/images/smilies/sad.gif)
 im really bad at reversing i think

---

### Post #2362652 — Apple Man — 8th February 2019, 01:24 PM

This is great, thank you.

---

### Post #2363210 — usafaqb — 9th February 2019, 01:16 AM

what a useful post! excellent write up. thank you!

---

### Post #2364322 — Enelx — 10th February 2019, 07:20 AM

Really nice tutorial, but your examples seems to be the only ones i can find with strings, how to find objectmanager for example ?

---

### Post #2364593 — shattter — 10th February 2019, 03:19 PM

> **Enelx:**
> Originally Posted by Enelx   Really nice tutorial, but your examples seems to be the only ones i can find with strings, how to find objectmanager for example ?

I did explain how to in the post. Here�s the 9.3 ObjectManager: 0x2EC0510

 Add your base address to it, go to that address in IDA, search for strings / cross reference the ObjManager anyway and look around the functions and make a pattern/use strings.

 The post is meant to help u find addresses once you have them for a patch. So u use the 9.3 addresses to build your method of finding them, FOR 9.4 onwards

---

### Post #2364814 — usafaqb — 10th February 2019, 06:51 PM

> **shattter:**
> Originally Posted by shattter   I did explain how to in the post. Here�s the 9.3 ObjectManager: 0x2EC0510
>
>  Add your base address to it, go to that address in IDA, search for strings / cross reference the ObjManager anyway and look around the functions and make a pattern/use strings.
>
>  The post is meant to help u find addresses once you have them for a patch. So u use the 9.3 addresses to build your method of finding them, FOR 9.4 onwards

Could 0x2EC0508 possibly be the offset for ObjectManager? matt's base crashes with 0x2EC0510 but runs fine when i put 0x2EC0508

---

### Post #2364925 — shattter — 10th February 2019, 08:54 PM

> **usafaqb:**
> Originally Posted by usafaqb   Could 0x2EC0508 possibly be the offset for ObjectManager? matt's base crashes with 0x2EC0510 but runs fine when i put 0x2EC0508

Yep Matt's base will use that one, as he directly accesses the object manager, some people use the previous address and subtract the 0x8. Just totally depends on how u find it easier to do when updating via IDA

---

### Post #2368364 — tonywang — 14th February 2019, 09:20 AM

"pd.h"Where?

---

### Post #2368367 — Public Void — 14th February 2019, 09:21 AM

> **tonywang:**
> Originally Posted by tonywang   "pd.h"Where?

you can just create an empty one yourself (atleast that worked for me)

---

### Post #2368416 — obelisk24 — 14th February 2019, 10:29 AM

I have been trying to get the offsets for localplayer in Mac OSX.
 I followed your guide and trying to retrieve the local player. But, in IDA i am stuck in the scenario and would love to hear some suggestions.

  Code:
```
.....
_hidden:003CCDF9 loc_3CCDF9:                                                        ; CODE XREF: _hidden:003CCC74↑j
_hidden:003CCDF9                    lea     ecx, (aYou - 3CBEC1h)[edi] ; "you"
_hidden:003CCDFF                    mov     [esp+4], ecx
_hidden:003CCE03                    jmp     loc_3CC552
......
```
  which obviously doesnt have the edi & the associated dword string. I wonder if Mac doesnt produce localplayer values as easy as windows.

 Thanks for the guide, it has been really helpful.

---

### Post #2368558 — OLD_Anonymous — 14th February 2019, 02:00 PM

> **obelisk24:**
> Originally Posted by obelisk24   I have been trying to get the offsets for localplayer in Mac OSX.

X-ref "Node <%s> tried to get Player champion with nullptr Player object\n".

 [https://cdn.[removed]/attachmen...70/unknown.png](https://cdn.[removed]/attachments/502176499189940264/545604649655336970/unknown.png).

 It's probably a bit different in the mac binary though.

---

### Post #2368725 — obelisk24 — 14th February 2019, 05:05 PM

In IDA Freeware, how did you that function code, i notice when i try to xref anything i only get assembly.

 nm. i figured out that its because I am using IDA freeware.

---

### Post #2371810 — Twm — 17th February 2019, 02:36 PM

Hai Shatter,

 Ive tried to follow your league dump tutorial.
 But i got stuck on the second step, since i couldnt find pd.exe in my folder. So i opened the project with visualstudio and tried to compile it. But when i try to compile pd.sln i get a error stating that the program needs LC.exe to work.

 So should i try to compile it or should i open the project and let it attach to league...

 to be honest idk how it all works..

 Thanks in advance <3

---

### Post #2375952 — tn04462 — 21st February 2019, 05:58 AM

thank you .

---

### Post #2380066 — tn04462 — 24th February 2019, 07:47 AM

everything went perfectly until the step for localplayer address this is the screen i got
 [https://snag.gy/IrTEfX.jpg](https://snag.gy/IrTEfX.jpg)

 so my localplayer must be 40CF4F8 - 11A0000 = 0x2F2F4F8

 what a great tutorial real fool proof

---

### Post #2381161 — shattter — 25th February 2019, 05:53 AM

> **tn04462:**
> Originally Posted by tn04462   everything went perfectly until the step for localplayer address this is the screen i got
>  [https://snag.gy/IrTEfX.jpg](https://snag.gy/IrTEfX.jpg)
>
>  so my localplayer must be 40CF4F8 - 11A0000 = 0x2F2F4F8
>
>
>  what a great tutorial real fool proof

Can�t tell if you�re being sarcastic or not with the edit, but yes that�s the correct localplayer, if you�re being genuine thanks for the compliment
![image](https://www.unknowncheats.me/forum/images/smilies/smile.gif)

---

### Post #2381213 — tn04462 — 25th February 2019, 07:50 AM

no sarcasm here, I've never done this before and i was able to do it with no issues. The only part I struggled was when i was reversing from the addresses posted and I added my base addr. It took me to a weird address so i highlighted the text and press X but there was a lot of choices.

---

### Post #2381897 — tn04462 — 25th February 2019, 09:02 PM

The more I started scripting the more this seems important.
 Got confused at this spot since it didn't seem to take me back to the same address. [at the end of your tutorial]

 So since I have found the LocalPlayer I will use that as an example:
 the Local Player: [https://snag.gy/IrTEfX.jpg](https://snag.gy/IrTEfX.jpg)

 Local Player: 0x2F2F4F8
 Local + base = jump to address of Local Player= 0x2F2F4F8 + 11A0000 = 40CF4F8

 when i jump to the address this is the screen i got
 [https://snag.gy/uB6qVr.jpg](https://snag.gy/uB6qVr.jpg)

 I tried jumping around.. i highlight on the dword and press X for there were 700 results, I guess my question is there a way to know which one is the right one?
 [https://snag.gy/gv6o9n.jpg](https://snag.gy/gv6o9n.jpg)

---

### Post #2381937 — Deftsu — 25th February 2019, 09:40 PM

> **tn04462:**
> Originally Posted by tn04462   The more I started scripting the more this seems important.
>  Got confused at this spot since it didn't seem to take me back to the same address. [at the end of your tutorial]
>
>  So since I have found the LocalPlayer I will use that as an example:
>  the Local Player: [https://snag.gy/IrTEfX.jpg](https://snag.gy/IrTEfX.jpg)
>
>  Local Player: 0x2F2F4F8
>  Local + base = jump to address of Local Player= 0x2F2F4F8 + 11A0000 = 40CF4F8
>
>  when i jump to the address this is the screen i got
>  [https://snag.gy/uB6qVr.jpg](https://snag.gy/uB6qVr.jpg)
>
>  I tried jumping around.. i highlight on the dword and press X for there were 700 results, I guess my question is there a way to know which one is the right one?
>  [https://snag.gy/gv6o9n.jpg](https://snag.gy/gv6o9n.jpg)

the question is, what are you looking for ??? if it's local player you have it already

---

### Post #2381946 — tn04462 — 25th February 2019, 09:53 PM

@[Deftsu](https://www.unknowncheats.me/forum/members/1509910.html) i'm just learning to back trace reliably to get back to the right address, so that I can:

 1. back trace to the other address like spell data, aimanager, etc. learn how those work so I can ask better questions.
 2. make sig pattern to find my own offsets if they're updated later

---

### Post #2381958 — Deftsu — 25th February 2019, 10:00 PM

> **tn04462:**
> Originally Posted by tn04462   @[Deftsu](https://www.unknowncheats.me/forum/members/1509910.html) i'm just learning to back trace reliably to get back to the right address, so that I can:
>
>  1. back trace to the other address like spell data, aimanager, etc. learn how those work so I can ask better questions.
>  2. make sig pattern to find my own offsets if they're updated later

then there is no right one, just find one that's reliable and easy to remember (has strings preferably)

---

### Post #2382032 — Arstyyy — 25th February 2019, 11:16 PM

> **shattter:**
> Originally Posted by shattter   ...

thanks for ur help, now i can get offsets on my own

 <3

---

### Post #2382049 — tn04462 — 25th February 2019, 11:28 PM

> **Deftsu:**
> Originally Posted by Deftsu   then there is no right one, just find one that's reliable and easy to remember (has strings preferably)

 @[Deftsu](https://www.unknowncheats.me/forum/members/1509910.html) : appreciate the tip, thank you.

---

### Post #2386705 — lemycanh — 2nd March 2019, 05:18 AM

Can you please share other key strings to find all the offset in [League of Legends Structs & Offsets](https://www.unknowncheats.me/forum/league-of-legends/310587-league-legends-structs-offsets.html)

---

### Post #2393406 — INPL — 8th March 2019, 02:33 AM

Thank you

---

### Post #2393763 — Alchael — 8th March 2019, 01:53 PM

Hey @[shattter](https://www.unknowncheats.me/forum/members/1676110.html), thank you so much for this... This has opened a lot of doors for newbs like me. You are now my new Lord and Savior! ALL HAIL THE SHATTTER! ;D

---

### Post #2397307 — steve612 — 11th March 2019, 05:24 PM

So if you dont have an offset for something you are looking, can't find it either, and no help from others, but you have the offset from an older version, is there any way to dump an older executable and try make a pattern to see if it hits? thanks

---

### Post #2397317 — Deftsu — 11th March 2019, 05:31 PM

> **steve612:**
> Originally Posted by steve612   So if you dont have an offset for something you are looking, can't find it either, and no help from others, but you have the offset from an older version, is there any way to dump an older executable and try make a pattern to see if it hits? thanks

unless you unpack it fully, no (because you will get stuck on loading screen and not much things would be unpacked)

---

### Post #2397332 — steve612 — 11th March 2019, 05:37 PM

Yes i understand that. i tried to unpack when an error message showed, so defenetly it was not a good dump. no skills to unpack it manually

---

### Post #2397485 — Deftsu — 11th March 2019, 07:46 PM

> **steve612:**
> Originally Posted by steve612   Yes i understand that. i tried to unpack when an error message showed, so defenetly it was not a good dump. no skills to unpack it manually

i mean unpacking not dumping tho
![image](https://www.unknowncheats.me/forum/images/smilies/tongue.gif)

---

### Post #2397515 — steve612 — 11th March 2019, 08:07 PM

> **Deftsu:**
> Originally Posted by Deftsu   i mean unpacking not dumping tho
> ![image](https://www.unknowncheats.me/forum/images/smilies/tongue.gif)

yes i know the difference
![image](https://www.unknowncheats.me/forum/images/smilies/smile.gif)
 i messed with simple unpacking in the past for learning purposes
![image](https://www.unknowncheats.me/forum/images/smilies/tongue.gif)

---

### Post #2397977 — themagicalgamer — 12th March 2019, 05:27 AM

Big fan

---

### Post #2404015 — brunofbr — 17th March 2019, 09:11 PM

could best explain how to execute and use the dumper because it was a little embarrassed the explanation to my point of view

---

### Post #2405073 — shattter — 18th March 2019, 11:06 PM

> **brunofbr:**
> Originally Posted by brunofbr   could best explain how to execute and use the dumper because it was a little embarrassed the explanation to my point of view

sorry what is it that u want

---

### Post #2405084 — brunofbr — 18th March 2019, 11:27 PM

I do not speak English, but I wanted you to explain to me better how to do the process of using the dumper

---

### Post #2406674 — vyctor17 — 20th March 2019, 02:37 PM

how to find, a object offset, like mana, health etc...?

 edit: nvm, i found.

---

### Post #2410731 — xaxoxuxu — 24th March 2019, 12:53 PM

Hey, I must be doing something wrong but I can't seem to get the correct oLocalPlayer.
 Following your guide I get in IDA dword_22AF150 and my base from the dump is 0x1200000 so hex(0x22AF150 - 0x1200000) is 0x10af150 which is incorrect

 EDIT: nvm I was looking at wrong address in ida

---

### Post #2417035 — DerBusch — 30th March 2019, 04:12 PM

Hey I don't know why but my "aBlueHero" doesn't have any xRefs do you know why?

---

### Post #2417471 — allaboutspam10 — 31st March 2019, 12:10 AM

@[shattter](https://www.unknowncheats.me/forum/members/1676110.html)
 Hey thx for the guide, but can you explain me how to get "sub addrress"?

 I'm meaning address like oObjHealth = 0xF84 etc

---

### Post #2417919 — ChEeTaH182 — 31st March 2019, 11:21 AM

> **DerBusch:**
> Originally Posted by DerBusch   Hey I don't know why but my "aBlueHero" doesn't have any xRefs do you know why?

They removed the reference. Use "hero" sligthly above/below "blueHero". Leads you to the same functions.

> **allaboutspam10:**
> Originally Posted by allaboutspam10   @[shattter](https://www.unknowncheats.me/forum/members/1676110.html)
>  Hey thx for the guide, but can you explain me how to get "sub addrress"?
>
>  I'm meaning address like oObjHealth = 0xF84 etc

Easiest is looking at the struct with ReClass and compairing the values you see ingame (like health). You will see same values in ReClass and their offset.

---

### Post #2418664 — benevolent — 1st April 2019, 03:05 AM

Thank you so much for this, as a complete noob to game reversing, this helps alot.

---

### Post #2419099 — allaboutspam10 — 1st April 2019, 02:18 PM

> **ChEeTaH182:**
> Originally Posted by ChEeTaH182   They removed the reference. Use "hero" sligthly above/below "blueHero". Leads you to the same functions.
>
>
>
>
>  Easiest is looking at the struct with ReClass and compairing the values you see ingame (like health). You will see same values in ReClass and their offset.

thx. But one more question. I'm in the object manager

 [http://prntscr.com/n5w8vq](http://prntscr.com/n5w8vq)

 How can i setup the array of object? I'm searching all around the web and making some test but nothing

 I want to access to the "suboffsets" of this records
 [http://prntscr.com/n5wb14](http://prntscr.com/n5wb14)

 The first number is the index in the array

---

### Post #2419111 — Public Void — 1st April 2019, 02:25 PM

> **allaboutspam10:**
> Originally Posted by allaboutspam10   thx. But one more question. I'm in the object manager
>
>  [http://prntscr.com/n5w8vq](http://prntscr.com/n5w8vq)
>
>  How can i setup the array of object? I'm searching all around the web and making some test but nothing
>
>  I want to access to the "suboffsets" of this records
>  [http://prntscr.com/n5wb14](http://prntscr.com/n5wb14)
>
>  The first number is the index in the array

the Red thing indicates that there might be a pointer.

 EDIT:
 btw is that your screenshot? I mean the one with the Object output

---

### Post #2419124 — allaboutspam10 — 1st April 2019, 02:34 PM

> **Public Void:**
> Originally Posted by Public Void   the Red thing indicates that there might be a pointer.
>
>  EDIT:
>  btw is that your screenshot? I mean the one with the Object output

Yea both mine. I'm trying to get more offset for Hero info, such as Mana, minions, golds etc

---

### Post #2419125 — Public Void — 1st April 2019, 02:36 PM

> **allaboutspam10:**
> Originally Posted by allaboutspam10   Yea both mine. I'm trying to get more offset for Hero info, such as Mana, minions, golds etc

 well maybe you should censor the names just in case someone figures out your account. Also if this is your screenshot, you are able to loop trough object manager, where is the problem?? xd

---

### Post #2419126 — allaboutspam10 — 1st April 2019, 02:39 PM

> **Public Void:**
> Originally Posted by Public Void   well maybe you should censor the names just in case someone figures out your account. Also if this is your screenshot, you are able to loop trough object manager, where is the problem?? xd

There isn't my GA ahaha i'm in spectator mode. I know that I can loop the objectManager, but I want to find more offsets of the Hero Structure, atm I have only Healt that i get from a thread

---

### Post #2419132 — Public Void — 1st April 2019, 02:42 PM

> **allaboutspam10:**
> Originally Posted by allaboutspam10   There isn't my GA ahaha i'm in spectator mode. I know that I can loop the objectManager, but I want to find more offsets of the Hero Structure, atm I have only Healt that i get from a thread

then just go into an object and try to look for stuff that looks like something you would want??? 3 floats in a row might indicate an vector for example

---

### Post #2419140 — allaboutspam10 — 1st April 2019, 02:49 PM

> **Public Void:**
> Originally Posted by Public Void   then just go into an object and try to look for stuff that looks like something you would want??? 3 floats in a row might indicate an vector for example

of but my question is "How can I do this in ReClass?"

---

### Post #2419873 — stevo24 — 2nd April 2019, 03:16 AM

> **Public Void:**
> Originally Posted by Public Void   then just go into an object and try to look for stuff that looks like something you would want??? 3 floats in a row might indicate an vector for example

Once I do the Lolbase + objmanager offset how the hell do I read the array of objects?

 I see that at 0x8 there is a value of 10,000 (I assume the max objmanager size).

 Thank you so much I've been at this for many many hours.

 I'm a noob.

---

### Post #2419995 — Public Void — 2nd April 2019, 08:07 AM

> **stevo24:**
> Originally Posted by stevo24   Once I do the Lolbase + objmanager offset how the hell do I read the array of objects?
>
>  I see that at 0x8 there is a value of 10,000 (I assume the max objmanager size).
>
>  Thank you so much I've been at this for many many hours.
>
>  I'm a noob.

Litterly read what the lolbase does to loop objectmanager and look at the objectmanager class????

---

### Post #2420038 — stevo24 — 2nd April 2019, 10:10 AM

> **Public Void:**
> Originally Posted by Public Void   Litterly read what the lolbase does to loop objectmanager and look at the objectmanager class????

Ok I'll just explain exactly what I see. I feel I might have a huge misunderstanding somewhere.

 SO... when I do the following

  Code:
```
	// Process Handle
	HWND hwnd = FindWindow(NULL, "League of Legends (TM) Client");
	DWORD pid;
	GetWindowThreadProcessId(hwnd, &pid);

	if (hwnd)
	{
		cout << "pid for league is " << pid << endl;
	}
	else
	{
		cout << "no window" << endl;
	}

	//GetModuleBaseAddressByName(pid);

	HANDLE process = OpenProcess(
		PROCESS_VM_OPERATION |
		PROCESS_VM_READ |
		PROCESS_VM_WRITE,
		FALSE,
		pid
	);
	if (process == INVALID_HANDLE_VALUE) {
		printf("Failed to open PID %d, error code %d",
			pid, GetLastError());
	}

	DWORD newBase;
	// get the address of kernel32.dll
	HMODULE k32 = GetModuleHandle("kernel32.dll");
	// get the address of GetModuleHandle()
	LPVOID funcAdr = GetProcAddress(k32, "GetModuleHandleA");
	if (!funcAdr)
		funcAdr = GetProcAddress(k32, "GetModuleHandleW");
	// create the thread
	HANDLE thread =
		CreateRemoteThread(process, NULL, NULL,
		(LPTHREAD_START_ROUTINE)funcAdr,
			NULL, NULL, NULL);
		// let the thread finish
		WaitForSingleObject(thread, INFINITE);
	// get the exit code
	GetExitCodeThread(thread, &newBase);
	// clean up the thread handle
	CloseHandle(thread);
```
  The result is that I have the League of Legends offfset saved to "newBase".

 When I then do newBase + (Offset for ObjManager, which is 0x2F43098) I get, for my specific case, 0x3773098.

 I then take this value and go to ReClass and punch it in. I see that at 3773098 it is showing how the values within the spot are constantly being filtered through.

 I am uncertain what exactly to do from here, as I've tried many many ideas and am stuck.

> **allaboutspam10:**
> Originally Posted by allaboutspam10   Yea both mine. I'm trying to get more offset for Hero info, such as Mana, minions, golds etc

Yo can you tell me how you were able to do this after getting to the memory location of objManager? I know you do lolbase + objManager offset but after this I am quite stuck.

 Even a small hint at what you did would mean the absolute world to me man.

 Thanks.

---

### Post #2420797 — allaboutspam10 — 2nd April 2019, 10:36 PM

> **stevo24:**
> Originally Posted by stevo24   Ok I'll just explain exactly what I see. I feel I might have a huge misunderstanding somewhere.
>
>  SO... when I do the following
>
>   Code:
> ```
> 	// Process Handle
> 	HWND hwnd = FindWindow(NULL, "League of Legends (TM) Client");
> 	DWORD pid;
> 	GetWindowThreadProcessId(hwnd, &pid);
>
> 	if (hwnd)
> 	{
> 		cout << "pid for league is " << pid << endl;
> 	}
> 	else
> 	{
> 		cout << "no window" << endl;
> 	}
>
> 	//GetModuleBaseAddressByName(pid);
>
> 	HANDLE process = OpenProcess(
> 		PROCESS_VM_OPERATION |
> 		PROCESS_VM_READ |
> 		PROCESS_VM_WRITE,
> 		FALSE,
> 		pid
> 	);
> 	if (process == INVALID_HANDLE_VALUE) {
> 		printf("Failed to open PID %d, error code %d",
> 			pid, GetLastError());
> 	}
>
> 	DWORD newBase;
> 	// get the address of kernel32.dll
> 	HMODULE k32 = GetModuleHandle("kernel32.dll");
> 	// get the address of GetModuleHandle()
> 	LPVOID funcAdr = GetProcAddress(k32, "GetModuleHandleA");
> 	if (!funcAdr)
> 		funcAdr = GetProcAddress(k32, "GetModuleHandleW");
> 	// create the thread
> 	HANDLE thread =
> 		CreateRemoteThread(process, NULL, NULL,
> 		(LPTHREAD_START_ROUTINE)funcAdr,
> 			NULL, NULL, NULL);
> 		// let the thread finish
> 		WaitForSingleObject(thread, INFINITE);
> 	// get the exit code
> 	GetExitCodeThread(thread, &newBase);
> 	// clean up the thread handle
> 	CloseHandle(thread);
> ```
>   The result is that I have the League of Legends offfset saved to "newBase".
>
>  When I then do newBase + (Offset for ObjManager, which is 0x2F43098) I get, for my specific case, 0x3773098.
>
>  I then take this value and go to ReClass and punch it in. I see that at 3773098 it is showing how the values within the spot are constantly being filtered through.
>
>  I am uncertain what exactly to do from here, as I've tried many many ideas and am stuck.
>
>
>
>  Yo can you tell me how you were able to do this after getting to the memory location of objManager? I know you do lolbase + objManager offset but after this I am quite stuck.
>
>  Even a small hint at what you did would mean the absolute world to me man.
>
>  Thanks.

Depends what you need, if you need player info such as Healt Mana etc, I'll start a game with trainer tool that search value on reclass with baseadd + localplayer. Pointer to 0x0 and you are in "hero" structure.

---

### Post #2420835 — stevo24 — 2nd April 2019, 11:21 PM

> **allaboutspam10:**
> Originally Posted by allaboutspam10   Depends what you need, if you need player info such as Healt Mana etc, I'll start a game with trainer tool that search value on reclass with baseadd + localplayer. Pointer to 0x0 and you are in "hero" structure.

Ah so my problem is that I keep trying to use the Objmanager offset to find information when I should be using localplayer?

 I'm not sure what the difference between the two is, but this is useful if true.

 TY SO MUCH
![image](https://www.unknowncheats.me/forum/images/smilies/smile.gif)

 I'll check er out now.

---

### Post #2428167 — noeye88 — 9th April 2019, 11:22 AM

Hello, i got a question, im new to grabbing offsets and i didnt find an answere on that yet, i hope you have some time to answere me but if not its okay aswell.
 I found this:
 dword_3026120 for "you"
 my base adress is 100000
 my question is if i have to - or +the 3026120 to my base adress because in one part of the tutorial you added it and in another part you subtracted it.
 I hope you have the time to help me.

---

### Post #2428860 — admiralzero — 10th April 2019, 12:08 AM

> **noeye88:**
> Originally Posted by noeye88   Hello, i got a question, im new to grabbing offsets and i didnt find an answere on that yet, i hope you have some time to answere me but if not its okay aswell.
>  I found this:
>  dword_3026120 for "you"
>  my base adress is 100000
>  my question is if i have to - or +the 3026120 to my base adress because in one part of the tutorial you added it and in another part you subtracted it.
>  I hope you have the time to help me.

i assume dword_3026120 is a pointer address to LoalPlayer("you", if that's what you meant)
 So to extract the actual LocalPlayer's address, you will need to subtract the 0x3026120 to Bass Address of 0x100000. (Extracting 100000 bytes from 3026120 bytes)
 Likewhat: 0x3026120(dword ptr) - 0x100000(Base Address) which will return the actual pointer address of LocalPlayer of 0x2F26120.

 Also, for adding; it's for you to point the address to the LocalPlayer's structure.
 So, Base(0x100000) + LocalPlayer(0x2F26120) which will return you to the LocalPlayer's structure.

 Hope that helped you out a bit, BTW this was written off of my head away from my pc so somewhat maybe incorrect so please someone correct me if i'm wrong here.

---

### Post #2429620 — noeye88 — 10th April 2019, 08:18 PM

> **admiralzero:**
> Originally Posted by admiralzero   i assume dword_3026120 is a pointer address to LoalPlayer("you", if that's what you meant)
>  So to extract the actual LocalPlayer's address, you will need to subtract the 0x3026120 to Bass Address of 0x100000. (Extracting 100000 bytes from 3026120 bytes)
>  Likewhat: 0x3026120(dword ptr) - 0x100000(Base Address) which will return the actual pointer address of LocalPlayer of 0x2F26120.
>
>  Also, for adding; it's for you to point the address to the LocalPlayer's structure.
>  So, Base(0x100000) + LocalPlayer(0x2F26120) which will return you to the LocalPlayer's structure.
>
>  Hope that helped you out a bit, BTW this was written off of my head away from my pc so somewhat maybe incorrect so please someone correct me if i'm wrong here.

actually helps a lot thanks.
 in addition, how do i know which adress is the one i need when there is something like that :

  Code:
```
                     .text:0027780E                 push    offset aMspellblock ; "mSpellBlock"
.text:00277813                 add     esi, 1E0h
.text:00277819                 push    esi
.text:0027781A                 push    [esp+18h+arg_0]
.text:0027781E                 call    sub_327EA0
.text:00277823                 mov     [esi+4], eax
.text:00277826                 mov     ecx, ebp
.text:00277828                 mov     [esi+8], ebx
.text:0027782B                 mov     [esi+0Ch], edi
.text:0027782E                 mov     esi, [esp+10h+arg_4]
```
  i assume it's that one :call sub_327EA0
 isn't it?

---

### Post #2429752 — admiralzero — 10th April 2019, 10:22 PM

> **noeye88:**
> Originally Posted by noeye88   actually helps a lot thanks.
>  in addition, how do i know which adress is the one i need when there is something like that :
>
>
>   Code:
> ```
>                      .text:0027780E                 push    offset aMspellblock ; "mSpellBlock"
> .text:00277813                 add     esi, 1E0h
> .text:00277819                 push    esi
> .text:0027781A                 push    [esp+18h+arg_0]
> .text:0027781E                 call    sub_327EA0
> .text:00277823                 mov     [esi+4], eax
> .text:00277826                 mov     ecx, ebp
> .text:00277828                 mov     [esi+8], ebx
> .text:0027782B                 mov     [esi+0Ch], edi
> .text:0027782E                 mov     esi, [esp+10h+arg_4]
> ```
>   i assume it's that one :call sub_327EA0
>  isn't it?

Correct

---

### Post #2430370 — noeye88 — 11th April 2019, 02:33 PM

> **admiralzero:**
> Originally Posted by admiralzero   Correct

but how do i know it when it is not that obvs, for example attackrange/spellbook seem to have the same adress

---

### Post #2430422 — admiralzero — 11th April 2019, 03:27 PM

> **noeye88:**
> Originally Posted by noeye88   but how do i know it when it is not that obvs, for example attackrange/spellbook seem to have the same adress

 Show me both of the address.
 Also, they both have different pointer offset(when extracted) and is slightly similar but different.
  Code:
```
oObjAtkRange 0x12A8
oObjSpellBook 0x28A8
```

---

### Post #2456332 — KingJulio — 5th May 2019, 04:25 AM

Thank you for the HQ Guide!

---

### Post #2456709 — Tulio1 — 5th May 2019, 02:38 PM

There are easier but perfect tutorial alternatives thank you. Thank you for sharing!!!

---

### Post #2673611 — omarbougma — 20th January 2020, 10:38 PM

would this work in any other game i mean for dumping a game offsets or only league ?

---

### Post #2674676 — singz5489 — 22nd January 2020, 09:02 AM

I got a problem in step 3, when I hit x on blueHero it gives me this error.
 [https://i.vgy.me/4qgch1.jpg](https://i.vgy.me/4qgch1.jpg)

---

### Post #2674831 — munnkorn — 22nd January 2020, 02:50 PM

> **singz5489:**
> Originally Posted by singz5489   I got a problem in step 3, when I hit x on blueHero it gives me this error.
>  [https://i.vgy.me/4qgch1.jpg](https://i.vgy.me/4qgch1.jpg)

You have to xref the blue word aBlueHero, literally the image you posted shows the xref

---

### Post #2675198 — Holli22 — 22nd January 2020, 10:56 PM

@[shattter](https://www.unknowncheats.me/forum/members/1676110.html) PM

---

### Post #2691673 — Azzie2k8 — 10th February 2020, 05:03 PM

Hi all,

 noob question here but related to TFT. The game always detects reclass running in the background and crashes. Is there a workaround for this? Just going through IDA without looking at reclass would be a pain.

 Edit: Sorry, nvm. Found this [https://www.unknowncheats.me/forum/2...-post1736.html](https://www.unknowncheats.me/forum/2590879-post1736.html)

---

### Post #2708557 — josue9243 — 1st March 2020, 08:31 PM

Thanks, i see later if i can create a auto-offsets with this dumper.

---

### Post #2783857 — Siyarbekir — 5th May 2020, 02:11 PM

I dont understand it, I got the oLocalPlayer and when i paste them in Reclass nothing happens there. Its everything on zero.
 [https://prnt.sc/sbamkt](https://prnt.sc/sbamkt)
 same with the other addres
 already tried objManager and use [<League of Legends.exe>+ 0x34BA188] also.

 Can anyone help me out here

---

### Post #2784395 — Public Void — 5th May 2020, 07:34 PM

> **Siyarbekir:**
> Originally Posted by Siyarbekir   I dont understand it, I got the oLocalPlayer and when i paste them in Reclass nothing happens there. Its everything on zero.
>  [https://prnt.sc/sbamkt](https://prnt.sc/sbamkt)
>  same with the other addres
>  already tried objManager and use [<League of Legends.exe>+ 0x34BA188] also.
>
>  Can anyone help me out here

these addresses are relative to the game module.
 in my version of reclass I do it like this: League of Legends.exe + [address]

---

### Post #2847599 — Catbert — 28th June 2020, 06:52 PM

Firstly id like to say thanks for the great guide, although it seems a little outdated now its helped a lot and I have managed to find a lot of stuff in ida over the last few days. I do have a question or a bit of advice would be great please.

 Although I am really grateful for the sigs people post I would rather do it for myself and although I have been able to find castSpell, localPlayer etc by searching for strings I would like to find out how people find the more advanced stuff? Do you just string search and hope that it leads somewhere or is there better ways of doing it?

 Also I have compiled my own undetected cheat engine (without debugger) but it doesn't seem that helpful and I find it easier just to use IDA. I have also compiled a undetected reclass (this has definitely been helpful for working out structures). Would you say anything else is useful for working with league?

 Any advice would be greatly appreciated!

 Thanks

---

### Post #2847815 — lobotomee — 28th June 2020, 11:22 PM

that tutorial help me a lot but now I stuck trying to find ObjectManager offset... can you explain how we find this one?

 plz dont answer: The offset is 0x1C41DB0.. I wanna know how to find it on IDA pro.

 Thanks to everyone!

---

### Post #2850742 — kmsmym — 1st July 2020, 01:52 PM

> **lobotomee:**
> Originally Posted by lobotomee   that tutorial help me a lot but now I stuck trying to find ObjectManager offset... can you explain how we find this one?
>
>  plz dont answer: The offset is 0x1C41DB0.. I wanna know how to find it on IDA pro.
>
>  Thanks to everyone!

The easiest way to do this is by using the signature pattern, using them in 2 clicks you can find the offset.
 [https://github.com/ajkhoury/SigMaker-x64](https://github.com/ajkhoury/SigMaker-x64)

 First you get the signaling, knowing the current offset, and then in the new database use it

---

### Post #2861281 — kaizanagi — 12th July 2020, 07:40 AM

How can i make my reclass undetectable from lol's anticheat and what program should i use?
 i already have my LocalPlayer's offset - 0x3501F1C but, i can't do much since i can't run reclass with lol running.

---

### Post #2861690 — ConcordStud733 — 12th July 2020, 08:01 PM

> **kaizanagi:**
> Originally Posted by kaizanagi   How can i make my reclass undetectable from lol's anticheat and what program should i use?
>  i already have my LocalPlayer's offset - 0x3501F1C but, i can't do much since i can't run reclass with lol running.

 You have to download Reclass's source file, replace the icon with anything else and compile it on your own.
 There's no download source for an undetected version.

---

### Post #2871950 — vitalflea — 25th July 2020, 09:58 PM

> **Catbert:**
> Originally Posted by Catbert   Firstly id like to say thanks for the great guide, although it seems a little outdated now its helped a lot and I have managed to find a lot of stuff in ida over the last few days. I do have a question or a bit of advice would be great please.
>
>  Although I am really grateful for the sigs people post I would rather do it for myself and although I have been able to find castSpell, localPlayer etc by searching for strings I would like to find out how people find the more advanced stuff? Do you just string search and hope that it leads somewhere or is there better ways of doing it?
>
>  Also I have compiled my own undetected cheat engine (without debugger) but it doesn't seem that helpful and I find it easier just to use IDA. I have also compiled a undetected reclass (this has definitely been helpful for working out structures). Would you say anything else is useful for working with league?
>
>  Any advice would be greatly appreciated!
>
>  Thanks

most of the stuff i have been finding the past couple of weeks is because of abusing strings and knowing the addresses for previous game versions and then having the old dump to compare with the new dump. Other than that i'm not too great at reversing. I haven't really been able to find better ways either
![image](https://www.unknowncheats.me/forum/images/smilies/thinking.gif)

---

### Post #2872631 — ConcordStud733 — 26th July 2020, 11:05 PM

> **vitalflea:**
> Originally Posted by vitalflea   most of the stuff i have been finding the past couple of weeks is because of abusing strings and knowing the addresses for previous game versions and then having the old dump to compare with the new dump. Other than that i'm not too great at reversing. I haven't really been able to find better ways either
> ![image](https://www.unknowncheats.me/forum/images/smilies/thinking.gif)

Since TFT runs on league's engine the TFT mobile SDK is a good source. It's x64 though so have to understand the differences.

 It's completely unobfuscated

---

### Post #2873571 — ANDRELOL — 28th July 2020, 05:20 AM

Okay, I got LocalPlayer without any difficulty.

 I would like to know how to use REClass to get LoL values properly, few words can help me <3

 Like to know how to get the value from that offset:
 oGameVersion 0x57C140

 BaseAddress + 0x57C140 leads to nothing

 Also, thx for the tutorial

---

### Post #2876530 — Catbert — 31st July 2020, 11:56 AM

Sorry in advance, bit of a noob question!

 So just to clear my up my understanding. I can see in this example we minus the base address:
 0x3011870 - 0x150000 = 0x2EC1870

 But then when I look at lolbase and other bases they add the module back on like e.g. GetModuleHandle(NULL) + address.

 So my question is which value are we using at this point and if people are adding the base back on what starting value are they using? I am guessing the minus base version but then why do they use that value and not just the pure IDA address?

 Isnt it just like doing:
 0x3011870 - 0x150000 = 0x2EC1870 (league address)
 dll injection:
 0x2EC1870 + 0x150000 = 0x3011870 (ida address for league)

 Thanks, and any help would be greatly appreciated!

---

### Post #2876558 — Elki — 31st July 2020, 12:25 PM

> **Catbert:**
> Originally Posted by Catbert   ...

basicly "the ModuleBaseAddress can change"

---

### Post #2876561 — Catbert — 31st July 2020, 12:31 PM

> **Elki:**
> Originally Posted by Elki   basicly "the ModuleBaseAddress can change"

Ah okay thanks, for things like mini patches? As whenever I print it, its always the same base addr depending on the patch

---

### Post #2876597 — Elki — 31st July 2020, 01:45 PM

> **Catbert:**
> Originally Posted by Catbert   Ah okay thanks, for things like mini patches? As whenever I print it, its always the same base addr depending on the patch

yea it should be different from PC to PC, so if you share offsets they are always relative to the base.

---

### Post #2942231 — lecason — 7th October 2020, 03:00 AM

Anyone can dump 10.20 ? I dump the file fail with this erorr

  Quote:    dump_process CreateToolhelp32Snapshot failed with error 5: dumping process unknown with pid 0x1930...
 ... building import reconstruction table ...
 Finished running.

---

### Post #2942845 — xaxoxuxu — 7th October 2020, 03:26 PM

> **lecason:**
> Originally Posted by lecason   Anyone can dump 10.20 ? I dump the file fail with this erorr

I am dumping just fine, how do you pass arguments?
 ./pd64.exe -pid 1234

---

### Post #3007009 — lucasbois1 — 15th December 2020, 03:54 AM

Question, how did u do that exactly?

  Code:
```
0x3011870 - 0x150000 = 0x2EC1870
```
  Nice tutorial btw

---

### Post #3022456 — yuri20198 — 30th December 2020, 02:46 AM

I've compiled reclass from source and don't have problem in searching values.
 But when I attach debugger to find out what accesses to it, LOL always keeps crushing.
 How can I solve this problem?

---

### Post #3070386 — MORIBIACK — 18th February 2021, 07:27 PM

last night it was woking properly and now CMD is giving me [removed]ing aids holy [removed]

---

### Post #3094470 — obatra — 19th March 2021, 08:34 AM

Great help, thanks for creating!

---

### Post #3157339 — hernos — 11th June 2021, 10:16 PM

Very usefull tutorial thanks...

---

### Post #3157475 — drdraven — 12th June 2021, 05:32 AM

im interested in this and i dont have IDA Pro, searched in and it is pay to use, is IDA Free okay for this? I don't have any background in these kind of things, sorry.

---

### Post #3157508 — Hecticism — 12th June 2021, 07:15 AM

> **drdraven:**
> Originally Posted by drdraven   im interested in this and i dont have IDA Pro, searched in and it is pay to use, is IDA Free okay for this? I don't have any background in these kind of things, sorry.

Yes the free version can do this, but the paid one has the Hex Ray's Decompiler plugin and you cant install the sigmaker plugin in free one

---

### Post #3160082 — XuraiStay — 15th June 2021, 11:15 PM

First of thank you for taking your time making this tutorial.
 I'm failing to understand how to find the others offsets beside the ones you've shown us.
 I understood that I should take the latest patch offsets and subtract from the dumped .exe base address (included in the name) but how am I supposed to do that?
 I'm really trying to learn but am kinda struggling with this plus English is not my main language too

---

### Post #3221331 — itvhot1 — 20th August 2021, 03:26 PM

[https://snipboard.io/AdP5tT.jpg](https://snipboard.io/AdP5tT.jpg)
 [https://snipboard.io/Oemhjv.jpg](https://snipboard.io/Oemhjv.jpg)

 Hello guys, Did I make any mistakes?
 Dword_Localplayer and Sub_isHero not show -_-

 Have a nice day ...

---

### Post #3221474 — Midi12 — 20th August 2021, 06:20 PM

> **itvhot1:**
> Originally Posted by itvhot1   [https://snipboard.io/AdP5tT.jpg](https://snipboard.io/AdP5tT.jpg)
>  [https://snipboard.io/Oemhjv.jpg](https://snipboard.io/Oemhjv.jpg)
>
>  Hello guys, Did I make any mistakes?
>  Dword_Localplayer and Sub_isHero not show -_-
>
>  Have a nice day ...

The blue strings are comments added by OP during the reverse engineering of the game executable. You can add comments by pressing the ";" key in IDA.

---

### Post #3222740 — Hecticism — 22nd August 2021, 10:11 AM

> **Midi12:**
> Originally Posted by Midi12   The blue strings are comments added by OP during the reverse engineering of the game executable. You can add comments by pressing the ";" key in IDA.

these comments are actually automatically generated by IDA

---

### Post #3236670 — MrSimba — 7th September 2021, 12:46 AM

what is the pd.exe? i dont have in my tariks folder any exe

---

### Post #3236879 — Dencelle — 7th September 2021, 08:52 AM

> **MrSimba:**
> Originally Posted by MrSimba   what is the pd.exe? i dont have in my tariks folder any exe

you need to compile it.

---

### Post #3237113 — magiok — 7th September 2021, 04:11 PM

> **Dencelle:**
> Originally Posted by Dencelle   you need to compile it.

Dencelle, how are you?

 I have already my dump, i have IDAPRO7 and Reclass..

 Now i need the guide for how to make patterns..
 because i dont wanna lose my offsets..

 can you share if you know some guide for that?

---

### Post #3237123 — NotATrap — 7th September 2021, 04:29 PM

> **magiok:**
> Originally Posted by magiok   Dencelle, how are you?
>
>  I have already my dump, i have IDAPRO7 and Reclass..
>
>  Now i need the guide for how to make patterns..
>  because i dont wanna lose my offsets..
>
>  can you share if you know some guide for that?

go to plugins -> sigmaker -> create signature

---

### Post #3265724 — tgf6720518 — 9th October 2021, 04:58 PM

when i excute "pid.exe -pid xxx", it returns msg "dump_process CreateToolhelp32Snapshot failed with error". the error type is 5 Access Deny, but i run cmd as administrator.

 who can help me, thx!

---

### Post #3272323 — whoitchy — 17th October 2021, 08:36 PM

How to find oZoomInstance? Like why is blueHero called oLocalPlayer? Where do I find zoom instance? When I search zoom in strings i only get 4 results that are far from what i am finding
 [http://prntscr.com/1wj24e4](http://prntscr.com/1wj24e4)

---

### Post #3320741 — MichaelAlex — 16th December 2021, 05:37 AM

What do I select?

![image](https://i.imgur.com/dZ0mQbl.png)

---

### Post #3320751 — lucas22490 — 16th December 2021, 06:09 AM

> **MichaelAlex:**
> Originally Posted by MichaelAlex   What do I select?
>
>  [https://i.imgur.com/dZ0mQbl.png](https://i.imgur.com/dZ0mQbl.png)

The button at the bottom that says "OK"

---

### Post #3321186 — MichaelAlex — 16th December 2021, 05:31 PM

> **lucas22490:**
> Originally Posted by lucas22490   The button at the bottom that says "OK"

I understand, thanks but I still don't understand how to find the offsets apart from the localplayer as it doesn't show me names or anything like that only addresses.

---

### Post #3321197 — lucas22490 — 16th December 2021, 05:41 PM

> **MichaelAlex:**
> Originally Posted by MichaelAlex   I understand, thanks but I still don't understand how to find the offsets apart from the localplayer as it doesn't show me names or anything like that only addresses.

It's not as easy now starting fresh and trying to learn when you can't attach a debugger as easily, I'd recommend you to start with something that has no anticheat that is simple so you understand how to use a debugger and ida

---

### Post #3321202 — MichaelAlex — 16th December 2021, 05:48 PM

> **lucas22490:**
> Originally Posted by lucas22490   It's not as easy now starting fresh and trying to learn when you can't attach a debugger as easily, I'd recommend you to start with something that has no anticheat that is simple so you understand how to use a debugger and ida

I guess you are right, in the guide they put it very simple but for people who do not know it is very complicated like me who only need offsets.

---

### Post #3372663 — rainzee — 17th February 2022, 08:40 PM

i got this whem im trying to dump

 Process Dump v2.1
 Copyright 漏 2017, Geoff McDonald

 Loading clean hash database from 'C:\Users\rainzee\Desktop\Working\dump\clean.hashes'.
 Did not find an existing clean hash database, using an empty one.
 dump_process CreateToolhelp32Snapshot failed with error 5: dumping process unknown with pid 0x16ac...
 ... building import reconstruction table ...
 Finished running.

---

### Post #3372999 — nipicoco — 18th February 2022, 06:28 AM

> **rainzee:**
> Originally Posted by rainzee   i got this whem im trying to dump
>
>  Process Dump v2.1
>  Copyright 漏 2017, Geoff McDonald
>
>
>  Loading clean hash database from 'C:\Users\rainzee\Desktop\Working\dump\clean.hashes'.
>  Did not find an existing clean hash database, using an empty one.
>  dump_process CreateToolhelp32Snapshot failed with error 5: dumping process unknown with pid 0x16ac...
>  ... building import reconstruction table ...
>  Finished running.

You could try with processhacker, suspend xigncode before completely loading ( right click - suspend ) then dump the process -> resume

---

### Post #3388164 — thea168 — 12th March 2022, 08:15 PM

what wrong ? can help me ?

 [https://ibb.co/mv5WxSz](https://ibb.co/mv5WxSz)

---

### Post #3388323 — bastien8070 — 13th March 2022, 03:31 AM

> **thea168:**
> Originally Posted by thea168   what wrong ? can help me ?
>
>  [https://ibb.co/mv5WxSz](https://ibb.co/mv5WxSz)

You are using Lview i bet. LView works externally, meaning it doesn't have access to the inbuilt renderer. For that reason, it draws an overlay, on top of the screen, and assumes you are in fullscreen. Since you are not, the program looks broken.

 (In game) go to settings, graphics, and change the window mode from windowed to fullscreen (or borderless + the max screen resolution supported)

---

### Post #3388585 — thea168 — 13th March 2022, 03:00 PM

friend have base internal full souce

---

### Post #3398357 — thea168 — 29th March 2022, 04:09 AM

Hello everybody can you help me ?

---

### Post #3398490 — str4ever1 — 29th March 2022, 11:54 AM

> **thea168:**
> Originally Posted by thea168   friend have base internal full souce

but it literally shows in your screenshot that you are using exsharp, which is not internal
![image](https://www.unknowncheats.me/forum/images/smilies/pat.gif)

 So use the advice from bastien8070 since he already told you how to fix it

---

### Post #3399706 — datrhi — 31st March 2022, 04:55 AM

I can get and create signature with most of offset but how can I find out sub offset like oObjectArmor, AtkRange, ...etc without using ReClass. I knew we can find sub offset but dont know how?

---

### Post #3399719 — Dencelle — 31st March 2022, 05:36 AM

> **datrhi:**
> Originally Posted by datrhi   I can get and create signature with most of offset but how can I find out sub offset like oObjectArmor, AtkRange, ...etc without using ReClass. I knew we can find sub offset but dont know how?

open up a custom version of CE, find out where it is written or read from, and create a signature from there....

---

### Post #3399724 — Hecticism — 31st March 2022, 05:51 AM

> **Dencelle:**
> Originally Posted by Dencelle   open up a custom version of CE, find out where it is written or read from, and create a signature from there....

wouldnt that also require you to attach cheat engine debugger? to find out what instruction accesses an address

---

### Post #3399746 — Dencelle — 31st March 2022, 07:17 AM

> **Hecticism:**
> Originally Posted by Hecticism   wouldnt that also require you to attach cheat engine debugger? to find out what instruction accesses an address

yes? i'm not seeing the reason for asking this, you're saying what i just said except in question form...

 if you're confused about how to attach cheat engine debugger, re-read what i wrote... *custom* (the one you download from their official site will not work, you need something *custom*)... its not hard to make if you understand why CE is dt...

---

### Post #3405968 — LuigeFonsei — 10th April 2022, 11:19 AM

Hi, can i get a help? Plsss
 my discord: mzoodias#9415

---

### Post #3408407 — MadLio — 13th April 2022, 09:55 PM

> **thea168:**
> Originally Posted by thea168   what wrong ? can help me ?
>
>  [https://ibb.co/mv5WxSz](https://ibb.co/mv5WxSz)

You can try use hook from Vault 21 or Vault 7

---

### Post #3409179 — cocoonemsm — 15th April 2022, 04:10 AM

how do you make rek'sai oneshot? watch the video there [https://www.youtube.com/watch?v=ykkihAQ3nAY&t=3s](https://www.youtube.com/watch?v=ykkihAQ3nAY&t=3s) //////////// who knows how to do it? he changes the nexus address and puts the nexus address for his hero, how do you do that? I AM STUPID, IDIOT AND NEW IN HACKS, IF ANYONE CAN HELP, I THANK YOU SO MUCH, kisses!

---

### Post #3424210 — yoloboyyolo — 6th May 2022, 08:46 PM

can i use the ida free? i dont see anywhere i can install ida pro.

---

### Post #3424232 — lavoethedin — 6th May 2022, 09:21 PM

> **Hecticism:**
> Originally Posted by Hecticism   Yes the free version can do this, but the paid one has the Hex Ray's Decompiler plugin and you cant install the sigmaker plugin in free one

> **yoloboyyolo:**
> Originally Posted by yoloboyyolo   can i use the ida free? i dont see anywhere i can install ida pro.

IDA Pro is very expensive, you can use IDA Free but it's limited.

---

### Post #3449634 — Tsn0w — 10th June 2022, 12:39 PM

Hi, thanks for the awesome guide, help me a lot!!

 hope someone would know if that's a real issue, but when I run the pd.exe on the league process I'm start seeing a lot VirtualProtectExFailed errors.

 Even with errors, when the pd.exe finish I do get the  Code:
```
League of Legends_exe_PID29a4_League of Legends.exe_690000_x86
```
  and got the right offsets.
 is that ok? plus I got plenty of files with the name code_chunk_XXXXX should I care about those as well?

 thanks ahaed!

---

### Post #3465164 — Stealthed — 1st July 2022, 08:09 PM

-- DELETED --
 Sorry

---

### Post #3474644 — PROGER — 14th July 2022, 05:34 PM

Hello everyone.
 Thanks for the tutorial, I was able to get dwLocalPlayer, turret and minion thanks to this.

 But I can't figure out where to find everything else? I mean, how did you understand that you need to search for localplayer in blueHero?? For example, how do I find gameTime and the rest? I tried searching in IDA gameTime but found only 3 hours wasted. I am completely new to this and I ask you to explain how I can understand which keyword I need to search for?

---

### Post #3507289 — yixin2001 — 20th August 2022, 10:34 AM

> **Dencelle:**
> Originally Posted by Dencelle   open up a custom version of CE, find out where it is written or read from, and create a signature from there....

Yes😘! I use office CE is been detected, and I learn some [tutorial](https://www.youtube.com/watch?v=hZusqy64URM&list=PL8TEddGT5w_j80QNqkFwl3stUTxlHol_h&index=2) to compile CE and its DBK driver to solve anti-debug.

---

### Post #3508918 — yixin2001 — 22nd August 2022, 06:29 AM

> **lucas22490:**
> Originally Posted by lucas22490   It's not as easy now starting fresh and trying to learn when you can't attach a debugger as easily, I'd recommend you to start with something that has no anticheat that is simple so you understand how to use a debugger and ida

you are right, but debugg a program that without anticheat is easy than debugg a program that have anticheat. more important thing is to pass anticheat

---

### Post #3580025 — decil — 25th November 2022, 11:03 PM

Code:
```
Double click and it will take you here:
https://vgy.me/AFvjzT.png

Now u can press X while highlighting the blueHero, and hit Enter/return.
https://vgy.me/HB8uiK.png

You will now see something like this:
https://vgy.me/XINhp1.png
```
  These images are out dated. Can we get updated images?

---

### Post #3636533 — xeverwykoff — 22nd December 2022, 05:17 PM

I'd also like the last image there reuploaded please, the rest was easy to understand without images
