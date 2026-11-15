# [Release] League of Legends All-in-One Offset Dumper

Source: https://www.unknowncheats.me/forum/league-of-legends/602017-league-legends-offset-dumper.html
Pages saved: 1–3


---

### Post #3859860 — iBaseult — 17th September 2023, 02:17 AM

I noticed that there are a few "starters" asking for updated offsets within the Reversal Thread.
 So I decided to code a beginner friendly pattern scanner which should help out with automatically updating all offsets.

 Difference to other Dumpers:
- This one doesn't require any additional configurations like specifying the Type or Size for each pattern.
- It also doesn't need any additional third party dumpers like "LeagueDumper".
- Simply just start the game and dynamically dump all offsets live at runtime, eliminating the need for static dumps.
 How to use it:
- Go into Practice Tool and start the offset dumper
- Select the Process and Module
- Paste your Patterns into the input field
- Press the search button
- Profit!
 Features:
- Patternscanning and Disassembler at runtime
- Automatically detects size and offset type (Address, Function, Offset)
- Detailed output for more information (Assembly, Pattern Location, Type)
- Use VirtualProtectEx to gain access to protected pages (optional)
- Suspend process while scanning
 Tutorial about Patterns & Patternscanning:
 If you need an Indepth Tutorial about Patterns and Patternscanning, take a look at my new thread: [CLICK ME](https://www.unknowncheats.me/forum/league-of-legends/603613-patterns-patternscanning-indepth-guide.html)

 Example:
   Code:
```
Input:
dw_hero_list: 48 8b 05 ? ? ? ? 48 89 6c 24 ? 48 89 74 24 ? 48 8b 58
dw_team: 8b 78 ?? eb ?? 8b 7e
E8 ?? ?? ?? ?? 48 8B 53 08 48 8B 42 60
48 8B 0D ? ? ? ? 48 8D 54 24 ? E8 ? ? ? ? 48 8B 7C 24 ?

Output:
dw_hero_list: 0x21894B0
dw_team: 0x3C
Offset: 0x897870
Offset: 0x21A5780

Detailed Output:
Pattern found at: 7FF6CFB3B04E
dw_team: 0x3C
asm: mov edi,[rax+3C]
Type: Offset
...
```
  Update:
- Increased Patternscan speed x10 times.
- Added C# and C++ output.
 Download: [Click Me](https://www.unknowncheats.me/forum/downloads.php?do=file&id=42017)

 Info: This will be a "beta release" which means that buggs can happen, if so please report them to me within this post.
 Also feel free to share any feedback and improvement ideas.

---

### Post #3859867 — SN00Z3R — 17th September 2023, 02:38 AM

+rep amazing work as always supporting new comers

---

### Post #3859876 — leech123 — 17th September 2023, 02:57 AM

+REP Was looking for a tool like this!

---

### Post #3859879 — jjggwds — 17th September 2023, 03:06 AM

Do you recommend C# over python?

---

### Post #3859901 — hybaby9 — 17th September 2023, 04:15 AM

I think add function to export declarations of offsets according to selected language,
 For example
 After dumping the offset
 I choose C++ as the output language
 At this point I will receive a header file or cpp, whatever...
 has the declarations
  Code:
```
inline uintptr_t LocalPlayer = 0x21a56f8;
inline uintptr_t LocalPlayer = 0x21a56f8;
inline uintptr_t GameTime = 0x2198714;
inline uintptr_t HeroList = 0x21894b0;
....
```

---

### Post #3859910 — byyyxd — 17th September 2023, 04:41 AM

+rep as always, great post

---

### Post #3859932 — SmallWhite5 — 17th September 2023, 05:36 AM

This is a good project. May I ask if the Sunday matching algorithm was used? From my understanding, using ReadProcessMemory and GameSizeDump before applying the Sunday algorithm can greatly increase efficiency.

---

### Post #3860019 — iBaseult — 17th September 2023, 10:16 AM

> **hybaby9:**
> Originally Posted by hybaby9   I think add function to export declarations of offsets according to selected language,
>  For example
>  After dumping the offset
>  I choose C++ as the output language
>  At this point I will receive a header file or cpp, whatever...
>  has the declarations
>   Code:
> ```
> inline uintptr_t LocalPlayer = 0x21a56f8;
> inline uintptr_t LocalPlayer = 0x21a56f8;
> inline uintptr_t GameTime = 0x2198714;
> inline uintptr_t HeroList = 0x21894b0;
> ....
> ```

That's a good idea, I'll add that

> **SmallWhite5:**
> Originally Posted by SmallWhite5   This is a good project. May I ask if the Sunday matching algorithm was used? From my understanding, using ReadProcessMemory and GameSizeDump before applying the Sunday algorithm can greatly increase efficiency.

I'm not using any algorithm right now while still testing if it detects everything correctly, but I will look into it once everything works reliable.

---

### Post #3860766 — Synixxal — 18th September 2023, 10:36 AM

This is amazing. Could you add function parameters when using detailed mode? like after everything is functional?

---

### Post #3861011 — ciceron — 18th September 2023, 06:15 PM

Thanks for sharing!

 [https://www.unknowncheats.me/forum/d...=file&id=42017](https://www.unknowncheats.me/forum/downloads.php?do=file&id=42017)

 File Approved
![League of Legends All-in-One Offset Dumper](https://www.unknowncheats.me/forum/images/icons/app_check.png)

- SHA256: 33B063CCD10C71A72EF9ACF82B524D74A915E3260FD8ED863195C9156154B2C6 - Baseult AiO Offset Dumper.zipInterested in how we analyze files? [Click here to find out.](https://www.unknowncheats.me/forum/faq.php?faq=uc_dl_faq)

---

### Post #3861067 — anniechannie — 18th September 2023, 07:30 PM

Hi, thank you for the release!

 Does the original post's example input signatures not work for anyone else?

 I entered the input example from the post to test out the dumper, but of those 4 patterns (dw_herolist, dw_team, offset1, offset2), [it only finds dw_herolist](https://i.imgur.com/Y2k4JH3.png)and seems to "hang" at dw_team?

 I let it run for 15 minutes and I never got any output after the "dw_hero_list" offset. Is 15 minutes a reasonable wait? Or should I let it run even longer.

 I don't actually have these signatures so I can't confirm if they're correct or not lol.

 So not sure if I did something wrong. When I enter correct signatures, I get the "Finished" output. When I enter an incorrect signature on purpose, I get "Offset Failed to find the Pattern". But for this no output case, does it mean the dumper is still searching and if so, how long should I wait?

 Thanks again!

---

### Post #3861133 — dumdum85 — 18th September 2023, 09:24 PM

nice tool. Will try using it later, will hopefully make life easier for me and others. Thank you!

---

### Post #3861512 — hybaby9 — 19th September 2023, 01:14 PM

nice thank for great tool ::

![League of Legends All-in-One Offset Dumper-screenshot-2023-09-19-191316-png](https://www.unknowncheats.me/forum/attachments/league-of-legends/23058d1695125627-league-legends-offset-dumper-screenshot-2023-09-19-191316-png)

---

### Post #3861524 — iBaseult — 19th September 2023, 02:03 PM

> **ciceron:**
> Originally Posted by ciceron   Thanks for sharing!
>
>  [https://www.unknowncheats.me/forum/d...=file&id=42017](https://www.unknowncheats.me/forum/downloads.php?do=file&id=42017)
>
>  File Approved
> ![League of Legends All-in-One Offset Dumper](https://www.unknowncheats.me/forum/images/icons/app_check.png)
>
> - SHA256: 33B063CCD10C71A72EF9ACF82B524D74A915E3260FD8ED863195C9156154B2C6 - Baseult AiO Offset Dumper.zipInterested in how we analyze files? [Click here to find out.](https://www.unknowncheats.me/forum/faq.php?faq=uc_dl_faq)

thanks for approving
![image](https://www.unknowncheats.me/forum/images/smilies/smile.gif)

> **anniechannie:**
> Originally Posted by anniechannie   Hi, thank you for the release!
>
>  Does the original post's example input signatures not work for anyone else? !

Hey, this can happen if you chose the "Change Page Protection..." Option without using the "Freeze while Scanning" option.
 It can cause the client to freeze / crash and the pattern scanner wont be able to scan anymore.
 Let me know if that fixes your issue.

---

### Post #3861554 — BOYIS — 19th September 2023, 03:06 PM

Hi, i really appreciate for your work! I have a suggestion; you can add memory section decryption and dump (the target process) option

---

### Post #3861573 — vamph — 19th September 2023, 04:06 PM

are there any plans to make source code open-source?
 I am not part of League of Legends community...but by far this tool can be pretty useful in other games too (at least I suppose that it wouldn't require many changes).

 Anyway, extremely great work! as always.

---

### Post #3861991 — Navalgazer — 20th September 2023, 02:02 AM

Thank you so much! This is exactly what I needed.
 And comparing with other dumping methods, this seems really outstanding! +rep

---

### Post #3862067 — 91xrz — 20th September 2023, 04:52 AM

An access denied error occurred

---

### Post #3862189 — iBaseult — 20th September 2023, 10:02 AM

> **BOYIS:**
> Originally Posted by BOYIS   Hi, i really appreciate for your work! I have a suggestion; you can add memory section decryption and dump (the target process) option

Thanks for your suggestion I will think about that
![image](https://www.unknowncheats.me/forum/images/smilies/smile.gif)

> **vamph:**
> Originally Posted by vamph   are there any plans to make source code open-source?
>  I am not part of League of Legends community...but by far this tool can be pretty useful in other games too (at least I suppose that it wouldn't require many changes).
>
>  Anyway, extremely great work! as always.

Right now the source is kinda messy but it's not obfuscated so you can read it with DnSpy if you want to. However I will clean and optimize a few things first before I share the source but yes, once I know that everything is working fine I will share the Source here.

> **91xrz:**
> Originally Posted by 91xrz   An access denied error occurred

Can you share a Screenshot or any more information about that Error message?

---

### Post #3862206 — iTrick — 20th September 2023, 10:19 AM

> **iBaseult:**
> Originally Posted by iBaseult   Can you share a Screenshot or any more information about that Error message?

Maybe it's CN server with Anti-Cheat Expert.
![image](https://www.unknowncheats.me/forum/images/smilies/sad.gif)

---

### Post #3862249 — 91xrz — 20th September 2023, 11:36 AM

Right！The China Server Binding Launcher starts and with ACE protect...

---

### Post #3862704 — hernos — 20th September 2023, 11:32 PM

> **91xrz:**
> Originally Posted by 91xrz   Right！The China Server Binding Launcher starts and with ACE protect...

Read from ring0 or hjack handle from trusted process.

---

### Post #3862752 — marthus15 — 21st September 2023, 01:20 AM

Works perfectly! +rep

---

### Post #3863699 — Kappatain — 22nd September 2023, 05:38 AM

Congratulations on the nice release,
 i was writing a IDA plugin for update faster the offsets, but yours is way better and faster.

 Bugs:
- On around 92 offsets, 64 of them were sorted without problems.
- For some it "Could not get the Type" .
 Suggestions:
- If text is not formatted correctly, add a messageBox or whatever, actually if you press the button you can stare at it even for 100 years wondering why it takes so long hahaah.
- I had to split my 92 sigs in 4 parts if im not wrong, probably there is a text limit or something like that? if so, i would suggest to add a line counter or whatever.
- After some offsets, it doesnt want to keep search and literally return the addresses it wants, instead to match the list of sigs

---

### Post #3864128 — iBaseult — 22nd September 2023, 06:35 PM

> **Kappatain:**
> Originally Posted by Kappatain   Congratulations on the nice release,
>  i was writing a IDA plugin for update faster the offsets, but yours is way better and faster.
>
>  Bugs:
> - On around 92 offsets, 64 of them were sorted without problems.
> - For some it "Could not get the Type" .
>  Suggestions:
> - If text is not formatted correctly, add a messageBox or whatever, actually if you press the button you can stare at it even for 100 years wondering why it takes so long hahaah.
> - I had to split my 92 sigs in 4 parts if im not wrong, probably there is a text limit or something like that? if so, i would suggest to add a line counter or whatever.
> - After some offsets, it doesnt want to keep search and literally return the addresses it wants, instead to match the list of sigs

Thanks a lot for the detailed feedback.
 Could you send me the not working patterns in dms so I can fix it?
 I will also see what I can do about your suggestions.

---

### Post #3864149 — Kappatain — 22nd September 2023, 07:03 PM

> **iBaseult:**
> Originally Posted by iBaseult   Thanks a lot for the detailed feedback.
>  Could you send me the not working patterns in dms so I can fix it?
>  I will also see what I can do about your suggestions.

Trough IDA, the addresses are easily reachable and works good, but seems that your exe doesnt like them.
 Since i deleted the text with all the sigs i pasted in there for gather the offsets, Ill post a bunch of them here, dont worry ^^.

 All the addresses listed in the code block below are reached from the exe and all gets the "Could not retrieve the Offset":
  Code:
```
IsNotLocalPlayer
IsVisible
GetOwnerPlayer
OnStartSpellCast
GetRespawnTimeRemaining
GameMap
```
  Sigs:
  Code:
```
33 C0 48 3B 0D ? ? ? ?
40 56 48 83 EC 20 48 8B F1 48 81 C1 ? ? ? ? E8 ? ? ? ?
40 53 48 83 EC 20 48 81 C1 E8 02 00 00 48 8B DA 48 8B
40 53 48 83 EC 30 4C 8B 0A
40 53 48 83 EC 20 48 8B D9 48 81 C1 ? ? ? ? E8 ? ? ? ? 0F B6 83 ? ? ? ?
48 8B DA 48 8B 0D ? ? ? ? E8 ? ? ? ?
```

---

### Post #3864351 — m00n111 — 23rd September 2023, 12:05 AM

> **iBaseult:**
> Originally Posted by iBaseult   I noticed that there are a few "starters" asking for updated offsets within the Reversal Thread.
>  So I decided to code a beginner friendly pattern scanner which should help out with automatically updating all offsets.
>
>
>
>  Difference to other Dumpers:
> - This one doesn't require any additional configurations like specifying the Type or Size for each pattern.
> - It also doesn't need any additional third party dumpers like "LeagueDumper".
> - Simply just start the game and dynamically dump all offsets live at runtime, eliminating the need for static dumps.
>  How to use it:
> - Go into Practice Tool and start the offset dumper
> - Select the Process and Module
> - Paste your Patterns into the input field
> - Press the search button
> - Profit!
>  Features:
> - Patternscanning and Disassembler at runtime
> - Automatically detects size and offset type (Address, Function, Offset)
> - Detailed output for more information (Assembly, Pattern Location, Type)
> - Use VirtualProtectEx to gain access to protected pages (optional)
> - Suspend process while scanning
>  Example:
>    Code:
> ```
> Input:
> dw_hero_list: 48 8b 05 ? ? ? ? 48 89 6c 24 ? 48 89 74 24 ? 48 8b 58
> dw_team: 8b 78 ?? eb ?? 8b 7e
> E8 ?? ?? ?? ?? 48 8B 53 08 48 8B 42 60
> 48 8B 0D ? ? ? ? 48 8D 54 24 ? E8 ? ? ? ? 48 8B 7C 24 ?
>
> Output:
> dw_hero_list: 0x21894B0
> dw_team: 0x3C
> Offset: 0x897870
> Offset: 0x21A5780
>
> Detailed Output:
> Pattern found at: 7FF6CFB3B04E
> dw_team: 0x3C
> asm: mov edi,[rax+3C]
> Type: Offset
> ...
> ```
>   Update:
> - Increased Patternscan speed x10 times.
> - Added C# and C++ output.
>  Download: [Click Me](https://www.unknowncheats.me/forum/downloads.php?do=file&id=42017)
>
>  Info: This will be a "beta release" which means that buggs can happen, if so please report them to me within this post.
>  Also feel free to share any feedback and improvement ideas.

 seems to not dump whatsoever. i add my addresses, click dump and nothing happens.

---

### Post #3864441 — CoolA1d — 23rd September 2023, 03:27 AM

every baseult post is a banger

---

### Post #3864659 — iBaseult — 23rd September 2023, 11:19 AM

> **BOYIS:**
> Originally Posted by BOYIS   I have a suggestion; you can add memory section decryption and dump (the target process) option

Working on that now
![image](https://www.unknowncheats.me/forum/images/smilies/smile.gif)

> **m00n111:**
> Originally Posted by m00n111   seems to not dump whatsoever. i add my addresses, click dump and nothing happens.

How does your input look like?

---

### Post #3866758 — Tumpes — 25th September 2023, 12:06 PM

Small quality of life change: you could make it remember the process and module names from the last time it was used, just like extreme injector.

 Thanks! This is great!

 Also, this sig is not found: E9 ? ? ? ? CC CC CC CC CC CC 40 53 48 83 EC 20 48 8B 1D ? ? ? ?

 With the error:

 Pattern found at: 7FF72E48BE95
 oSendChat: Something went wrong with retrieving the Offset
 asm: jmp 00007FF72E0FD7F0
 Type: Could not get the Type

---

### Post #3867147 — iBaseult — 25th September 2023, 08:41 PM

> **Tumpes:**
> Originally Posted by Tumpes   Small quality of life change: you could make it remember the process and module names from the last time it was used, just like extreme injector.
>
>  Thanks! This is great!
>
>  Also, this sig is not found: E9 ? ? ? ? CC CC CC CC CC CC 40 53 48 83 EC 20 48 8B 1D ? ? ? ?
>
>  With the error:
>
>  Pattern found at: 7FF72E48BE95
>  oSendChat: Something went wrong with retrieving the Offset
>  asm: jmp 00007FF72E0FD7F0
>  Type: Could not get the Type

Could you give me the offset name for that pattern and the correct offset?

---

### Post #3867518 — Tumpes — 26th September 2023, 09:59 AM

> **iBaseult:**
> Originally Posted by iBaseult   Could you give me the offset name for that pattern and the correct offset?

That the SendChat func

  Code:
```
#define oSendChat 0x83AF85
```

---

### Post #3867712 — Heworia — 26th September 2023, 02:07 PM

+REP, great work

---

### Post #3868335 — sprtms05 — 27th September 2023, 05:52 AM

local_player = 0x21a56f8
 game_time = 0x2198714
 view_proj_matrix = 0x21ec740
 champion_list = 0x21894b0
 minion_list = 0x218c328
 turret_list = 0x2192bc0
 obj_name = 0x3860
 obj_team = 0x3C
 obj_gold = 0x2138
 obj_health = 0x1058
 obj_max_health = 0x1070
 obj_armor = 0x1694
 obj_base_attack = 0x1670
 obj_bonus_attack = 0x15D8
 obj_bonus_as = 0x1678
 obj_magic_damage = 0x15F8
 obj_attack_range = 0x16B4
 obj_spawn_count = 0x328
 obj_targetable = 0xEB0
 obj_visible = 0x310
 obj_x = 0x220
 obj_y = obj_x + 0x8
 obj_z = obj_x + 0x4
 obj_spell_book = 0x30B8
 spell_info = 0x130
 spell_data = 0x60
 spell_name = 0x80
 spell_level = 0x28
 spell_cooldown = 0x30
 I put this offset in, but it doesn't translate. I'm a newbie so I'm having a hard time.
 Can anyone please share the offset for version 13.19?

---

### Post #3868391 — MarekJalovsky — 27th September 2023, 08:07 AM

This program is perfect, but can anyone share working signatures for all important offsets?

---

### Post #3868410 — mingeroflees — 27th September 2023, 08:39 AM

How do I find ZoomInstance with this so I won't have to bother people about it in the reversal structs and offsets thread anymore?

---

### Post #3868492 — iBaseult — 27th September 2023, 10:17 AM

> **sprtms05:**
> Originally Posted by sprtms05   local_player = 0x21a56f8
>  I put this offset in, but it doesn't translate. I'm a newbie so I'm having a hard time.
>  Can anyone please share the offset for version 13.19?

Hey, this is a "Pattern" based Offset Dumper.
 You are inputting "Offsets" which is what you want to get using patterns.

 You need to input the pattern for each offset in order to get the updated offsets.

---

### Post #3868503 — sprtms05 — 27th September 2023, 10:30 AM

Can you explain the pattern? Just an example...

---

### Post #3868525 — KittenBaicell — 27th September 2023, 10:53 AM

Based on the information collected in the forum, I have entered the following information. However, the obtained offset is incorrect (or possibly all incorrect) because the program still fails to run properly after updating the offsets:

 LocalPlayer: 48 8B 05? 4C 8B D2 4C 8B C1
 Renderer: 48 8B 0D? 4C 8D 44 24 50 48 8B D0
 ViewPorjMatrix: 48 8B 3D? FF 90?
 HeroList: 48 8b 05? 48 89 6c 24? 48 89 74 24? 48 8b 58
 HudInstance: 48 8B 0D? 8B 57 10
 MinionList: 48 8B 0D? E8? EB 07

 If the values such as "dw_hero_list: 48 8b 05??? 48 89 6c 24? 48 89 74 24? 48 8b 58" do not change, could someone please share the common offsets "Pattern" values?

---

### Post #3868791 — kingofwoods — 27th September 2023, 05:14 PM

Very nice work + rep

---

### Post #3868952 — superrhksgh2 — 27th September 2023, 08:38 PM

it's nice !!!! ah Please add oGetCollisionFlags, too

  Code:
```
oGetCollisionFlags:48 83 EC 28 48 8B D1 48 8B 0D ?? ?? ?? ?? 48 8B 49 08 E8 ?? ?? ?? ?? 48 8B C8 48 85 C0 74 1A
```

---

### Post #3869273 — asltet — 28th September 2023, 03:40 AM

Hey, I was wondering if you know of any resource to learn how to find Patterns myself when the game updates?

---

### Post #3870068 — iBaseult — 28th September 2023, 07:26 PM

> **asltet:**
> Originally Posted by asltet   Hey, I was wondering if you know of any resource to learn how to find Patterns myself when the game updates?

Sure, take a look at my new thread
![image](https://www.unknowncheats.me/forum/images/smilies/smile.gif)

 [Patterns & Patternscanning - Indepth Guide](https://www.unknowncheats.me/forum/league-of-legends/603613-patterns-patternscanning-indepth-guide.html)

---

### Post #3909666 — hybaby9 — 9th November 2023, 03:16 AM

Hey @[iBaseult](https://www.unknowncheats.me/forum/members/3154276.html), can you add a feature that allows user to import pattern from a file, or if the user has used it before, save the input pattern so user don't have to re-enter it every time the tool opened

---

### Post #3930663 — Phobie — 4th December 2023, 07:33 PM

Hello ! Wonderful tool ! But I'm having issue, doing like the Youtube video but nothing happen.
 In the windows events I got a "League of Legends.exe stopped to interract with windows and closed"

---

### Post #3933389 — yixin2001 — 8th December 2023, 05:36 AM

I found a bug.
 I need offset 11B8(offset type is Offset, is not Function), but this program Automatically detect offset type is Function!

 ```
 2. Input Patterns:
 48 8D 8A ? ? 00 00 0F 29 70

 4. Detailed Output
 Pattern found at: 7FF7D59833DA
 Offset: 0x9B33DA
 asm: lea rcx,[rdx+11B8]
 Type: Function
 ```
