# [Release] LOLDumper - Auto Offset Dumper DLL (Inject & Dump)

Source: https://www.unknowncheats.me/forum/league-of-legends/740963-loldumper-auto-offset-dumper-dll-inject-dump.html
Pages saved: 1–3


---

### Post #4610158 — trankhanhtinh1 — 2nd March 2026, 04:45 AM

LOLDumper - Auto Offset Dumper DLL (Inject & Dump)
 Hey everyone,
 Sharing my offset dumper DLL for League of Legends. Inject it into the game, wait a few seconds, and it dumps all offsets to LOLDumper_full.h in the game directory.
 What it dumps:
 Global pointers (LocalPlayer, HeroList, GameTime, MissileList, NavGrid, HUD, etc.)
 Function addresses (IssueOrder, CastSpell, WorldToScreen, GetPing, IsMinion, IsTurret, etc.)
 Struct offsets via pattern scan (NetID, Position, SpellBook, BuffManager, BasicAttack, AiManager, etc.)
 All replicated properties with correct absolute offsets (HP, MaxHP, Armor, AD, MoveSpeed, AttackRange, Gold, etc.)
 All internal struct offsets (AiManager, BuffInstance, SpellSlot, SpellInfo, MissileData, etc.)
 RTTI class names, events, spell parameters

 Looking for feedback — especially if any offsets are wrong or missing for your use case. Let me know what you find.

 Link: [https://www.unknowncheats.me/forum/d...=file&id=53416](https://www.unknowncheats.me/forum/downloads.php?do=file&id=53416)

---

### Post #4610165 — hernos — 2nd March 2026, 05:21 AM

Thats good share. I also have something like that but its IDA plugin instead. I run my IDA plugin and it extracts new offsets from dump and renames functions, arg names

---

### Post #4610185 — trankhanhtinh1 — 2nd March 2026, 06:13 AM

> **hernos:**
> Originally Posted by hernos   Thats good share. I also have something like that but its IDA plugin instead. I run my IDA plugin and it extracts new offsets from dump and renames functions, arg names

can u share it w me?

---

### Post #4610189 — hernos — 2nd March 2026, 06:20 AM

> **trankhanhtinh1:**
> Originally Posted by trankhanhtinh1   can u share it w me?

Uploaded [https://www.unknowncheats.me/forum/d...=file&id=53417](https://www.unknowncheats.me/forum/downloads.php?do=file&id=53417)

 Offsets will be dumped to C:/lol

---

### Post #4610191 — trankhanhtinh1 — 2nd March 2026, 06:24 AM

> **hernos:**
> Originally Posted by hernos   Uploaded [https://www.unknowncheats.me/forum/d...=file&id=53417](https://www.unknowncheats.me/forum/downloads.php?do=file&id=53417)
>
>  Offsets will be dumped to C:/lol

many thanks for sharing

---

### Post #4610816 — CoolA1d — 3rd March 2026, 03:43 AM

File Approved
![LOLDumper - Auto Offset Dumper DLL (Inject &amp; Dump)](https://www.unknowncheats.me/forum/images/icons/app_check.png)

- SHA256: 81d010be808750d774c41b7b75b69615fd2ad89b75589d8d9ad81ce51be159e9 - ida_lol_plugin.dllInterested in how we analyze files? [Click here to find out.](https://www.unknowncheats.me/forum/faq.php?faq=uc_dl_faq)

 Download: [https://www.unknowncheats.me/forum/d...=file&id=53417](https://www.unknowncheats.me/forum/downloads.php?do=file&id=53417)

 File Approved
![LOLDumper - Auto Offset Dumper DLL (Inject &amp; Dump)](https://www.unknowncheats.me/forum/images/icons/app_check.png)

- SHA256: 2b6ae92d47f89e4efcc0f17041490a06f1b4330f5886830f9fb1a30a5a93695b - LOLDumper.dllInterested in how we analyze files? [Click here to find out.](https://www.unknowncheats.me/forum/faq.php?faq=uc_dl_faq)

 Download: [https://www.unknowncheats.me/forum/d...=file&id=53416](https://www.unknowncheats.me/forum/downloads.php?do=file&id=53416)

 Thanks for sharing!

---

### Post #4610818 — znob — 3rd March 2026, 03:45 AM

It will take a long time for the moderator to release the files
![image](https://www.unknowncheats.me/forum/images/smilies/sad.gif)

 Finally, I won't have to look up offsets manually anymore, thanks to both of you. @hernos @trankhanhtinh1

 Finally, I won't have to look up offsets manually anymore, thanks to both of you. @hernos @trankhanhtinh1

> **hernos:**
> Originally Posted by hernos   Uploaded [https://www.unknowncheats.me/forum/d...=file&id=53417](https://www.unknowncheats.me/forum/downloads.php?do=file&id=53417)
>
>  Offsets will be dumped to C:/lol

Do you use PD64 to dump League of Legends?
 I tried using pd64 but it says it couldn't find any offset.

---

### Post #4610848 — hernos — 3rd March 2026, 05:03 AM

> **znob:**
> Originally Posted by znob   It will take a long time for the moderator to release the files
> ![image](https://www.unknowncheats.me/forum/images/smilies/sad.gif)
>
>
>  Finally, I won't have to look up offsets manually anymore, thanks to both of you. @hernos @trankhanhtinh1
>
>  Finally, I won't have to look up offsets manually anymore, thanks to both of you. @hernos @trankhanhtinh1
>
>
>
>  Do you use PD64 to dump League of Legends?
>  I tried using pd64 but it says it couldn't find any offset.

You can use any dumper. Open it in IDA then run plugin from plugins tab > league of legends

 It will work then.

![image](https://i.imgur.com/Iz4Pyrx.png)

![image](https://i.imgur.com/0MGbDzG.png)

![image](https://i.imgur.com/SwVq08Y.png)

![image](https://i.imgur.com/fXCr3GY.png)

---

### Post #4610849 — znob — 3rd March 2026, 05:04 AM

> **hernos:**
> Originally Posted by hernos   You can use any dumper. Open it in IDA then run plugin from plugins tab > league of legends
>
>  It will work then.
>
>  [https://i.imgur.com/Iz4Pyrx.png](https://i.imgur.com/Iz4Pyrx.png)
>  [https://i.imgur.com/0MGbDzG.png](https://i.imgur.com/0MGbDzG.png)
>  [https://i.imgur.com/SwVq08Y.png](https://i.imgur.com/SwVq08Y.png)

I tried using your plugin with IDA free using PD to dump League of Legends, but I didn't find any offset.

 Im use IDA FREE

---

### Post #4610850 — hernos — 3rd March 2026, 05:08 AM

> **znob:**
> Originally Posted by znob   I tried using your plugin with IDA free using PD to dump League of Legends, but I didn't find any offset.
>
>  Im use IDA FREE

I use IDA 9.1 maybe thats why. Also try installing Python 3.11

---

### Post #4610857 — znob — 3rd March 2026, 05:26 AM

> **hernos:**
> Originally Posted by hernos   I use IDA 9.1 maybe thats why. Also try installing Python 3.11

[https://prnt.sc/q5O3WS8R2-Ti](https://prnt.sc/q5O3WS8R2-Ti)

 I installed the same Python version and the IDA.

---

### Post #4610886 — hernos — 3rd March 2026, 06:23 AM

> **znob:**
> Originally Posted by znob   [https://prnt.sc/q5O3WS8R2-Ti](https://prnt.sc/q5O3WS8R2-Ti)
>
>  I installed the same Python version and the IDA.

Check if you have file C:/lol/offsets.hpp

---

### Post #4610907 — v0ld3m0rd — 3rd March 2026, 07:28 AM

Does this risk a hw ban? New in this area
![image](https://www.unknowncheats.me/forum/images/smilies/big_smile.png)

 Could this hw ban be avoided?

---

### Post #4610910 — grandmaster55 — 3rd March 2026, 07:39 AM

> **trankhanhtinh1:**
> Originally Posted by trankhanhtinh1   LOLDumper - Auto Offset Dumper DLL (Inject & Dump)
>  Hey everyone,
>  Sharing my offset dumper DLL for League of Legends. Inject it into the game, wait a few seconds, and it dumps all offsets to LOLDumper_full.h in the game directory.
>  What it dumps:
>  Global pointers (LocalPlayer, HeroList, GameTime, MissileList, NavGrid, HUD, etc.)
>  Function addresses (IssueOrder, CastSpell, WorldToScreen, GetPing, IsMinion, IsTurret, etc.)
>  Struct offsets via pattern scan (NetID, Position, SpellBook, BuffManager, BasicAttack, AiManager, etc.)
>  All replicated properties with correct absolute offsets (HP, MaxHP, Armor, AD, MoveSpeed, AttackRange, Gold, etc.)
>  All internal struct offsets (AiManager, BuffInstance, SpellSlot, SpellInfo, MissileData, etc.)
>  RTTI class names, events, spell parameters
>
>  Looking for feedback � especially if any offsets are wrong or missing for your use case. Let me know what you find.
>
>  Link: [https://www.unknowncheats.me/forum/d...=file&id=53416](https://www.unknowncheats.me/forum/downloads.php?do=file&id=53416)

 What kind of injector did you use? I�ve tried Xenos and a few injectors, but every time I get errors like �access denied.� Is there any public injector you could recommend?

---

### Post #4611016 — trankhanhtinh1 — 3rd March 2026, 11:16 AM

> **hernos:**
> Originally Posted by hernos   You can use any dumper. Open it in IDA then run plugin from plugins tab > league of legends
>
>  It will work then.
>
>  [https://i.imgur.com/Iz4Pyrx.png](https://i.imgur.com/Iz4Pyrx.png)
>  [https://i.imgur.com/0MGbDzG.png](https://i.imgur.com/0MGbDzG.png)
>  [https://i.imgur.com/SwVq08Y.png](https://i.imgur.com/SwVq08Y.png)
>  [https://i.imgur.com/fXCr3GY.png](https://i.imgur.com/fXCr3GY.png)

why I copy ida_lol_plugin to IDA Professional 9.0\plugins but when I open IDA it don't have this plugin?
 I use IDA 9.0 not load but 9.1 loaded haha
 but it miss offset why?
  Code:
```
#pragma once
#include <windows.h>

namespace offsets
{
    //HOOKS
    constexpr uintptr_t CreateClientEffect             = 0x923E70;
    constexpr uintptr_t OnGameUpdate                   = 0x54CAD0;
    constexpr uintptr_t OnSpellImpact                  = 0x98BD30;
    constexpr uintptr_t OnCreateObject                 = 0x55B6D0;
    constexpr uintptr_t OnProcessSpell                 = 0x18FB0E0;
    constexpr uintptr_t OnStopCast                     = 0x998E70;
    constexpr uintptr_t OnFinishCast                   = 0x2D6CA0;
    constexpr uintptr_t OnBuffAdd                      = 0xBEF610;
    //FUNCTIONS
    constexpr uintptr_t CastSpell                      = 0x0;
    constexpr uintptr_t CastSpellSafe                  = 0xBDBC60;
    constexpr uintptr_t IsTargetable                   = 0x0;
    constexpr uintptr_t IsTargetableByUnit             = 0x2AE5A0;
    constexpr uintptr_t IsVulnerable                   = 0x2AC2D0;
    constexpr uintptr_t GetJungleType                  = 0x6AE2C0;
    constexpr uintptr_t IsDragon                       = 0x2AB8D0;
    constexpr uintptr_t IsElderDragon                  = 0x2AB940;
    constexpr uintptr_t IsBaron                        = 0x2AACD0;
    constexpr uintptr_t IsRiftHerald                   = 0x0;
    constexpr uintptr_t GetSpellCastInfo               = 0x293AC0;
    constexpr uintptr_t GetBoundingRadius              = 0x295360;
    constexpr uintptr_t IssueOrder                     = 0x2B0070;
    constexpr uintptr_t GetSpellSlot                   = 0x9842E0;
    constexpr uintptr_t FindObject                     = 0x54F000;
    constexpr uintptr_t GetFirstObject                 = 0xA50F20;
    constexpr uintptr_t GetNextObject                  = 0x5509B0;
    constexpr uintptr_t PrintChat                      = 0x1100F30;
    constexpr uintptr_t GetPing                        = 0x6AB4D0;
    constexpr uintptr_t GetGameTime                    = 0x3612B0;
    constexpr uintptr_t WorldToScreen                  = 0x12ACAE0;
    constexpr uintptr_t CompareObjectTypeFlags         = 0x2ACFE0;
    constexpr uintptr_t IsBuilding                     = 0x31CD20;
    constexpr uintptr_t HasBuff                        = 0x0;
    constexpr uintptr_t HasBuffOfType                  = 0x2A6450;
    constexpr uintptr_t IsDead                         = 0x2AB620;
    constexpr uintptr_t IsVisible                      = 0x0;
    constexpr uintptr_t IsAlive                        = 0x2F9760;
    constexpr uintptr_t IsJungleMonster                = 0x2AC490;
    constexpr uintptr_t GetAttackDelay                 = 0x574390;
    constexpr uintptr_t GetAttackCastDelay             = 0x574290;
    constexpr uintptr_t GetGoldRedirectTarget          = 0x20A910;
    constexpr uintptr_t GetResourceType                = 0x290DD0;
    constexpr uintptr_t GetAIManager                   = 0x29D430;
    constexpr uintptr_t CanAttack                      = 0x203B10;
    constexpr uintptr_t CanCast                        = 0x0;
    constexpr uintptr_t CanMove                        = 0x0;
    constexpr uintptr_t IsSlowed                       = 0x0;
    constexpr uintptr_t IsSelectable                   = 0x21CF10;
    constexpr uintptr_t IsFleeing                      = 0x21A0D0;
    //OBJECT ARRAYS
    constexpr uintptr_t IsNoRender                     = 0x21A120;
    constexpr uintptr_t HeroManager                    = 0x1DEAEE0;
    constexpr uintptr_t MinionManager                  = 0x1DEAEE0;
    constexpr uintptr_t TurretManager                  = 0x1DEEF78;
    constexpr uintptr_t MissileManager                 = 0x0;
    //INSTANCES
    constexpr uintptr_t ObjectManager                  = 0x1DEAE88;
    constexpr uintptr_t HudInstance                    = 0x1DEB028;
    constexpr uintptr_t NetInstance                    = 0x1DEAE80;
    //VARIABLES
    constexpr uintptr_t CursorInstance                 = 0x1E7CFA0;
    constexpr uintptr_t ViewMatrixInstance             = 0x1EA3660;
    constexpr uintptr_t GameTime                       = 0x1DF9650;
    constexpr uintptr_t LocalPlayer                    = 0x1E257E0;
    constexpr uintptr_t UnderMouseObject               = 0x1A38668;
    constexpr uintptr_t AIHeroVTABLE                   = 0x0;
    constexpr uintptr_t AIMinionVTABLE                 = 0x1947B60;
}
```

> **grandmaster55:**
> Originally Posted by grandmaster55   What kind of injector did you use? I’ve tried Xenos and a few injectors, but every time I get errors like ‘access denied.’ Is there any public injector you could recommend?

u need disabled Vanguard first

---

### Post #4616554 — Alexis913 — 9th March 2026, 11:09 PM

Hi all!

 @[trankhanhtinh1](https://www.unknowncheats.me/forum/members/6407580.html): What injector is needed? Can you recommend one?

 @[hernos](https://www.unknowncheats.me/forum/members/544452.html): Regarding the .dll for IDA, it doesn't work for me with the LoL 26.05 patch + IDA Pro 9.1 + Python 3.14.3. All offsets come out as zero.
  Code:
```
//HOOKS
constexpr uintptr_t CreateClientEffect             = 0x0;
constexpr uintptr_t OnGameUpdate                   = 0x0;
constexpr uintptr_t OnSpellImpact                  = 0x0;
```

---

### Post #4616822 — Zeuby — 10th March 2026, 08:13 AM

> **Alexis913:**
> Originally Posted by Alexis913
>  @[hernos](https://www.unknowncheats.me/forum/members/544452.html): Regarding the .dll for IDA, it doesn't work for me with the LoL 26.05 patch + IDA Pro 9.1 + Python 3.14.3. All offsets come out as zero.

same for me, return 0x0 in the whole .hpp file
 using ida pro 9.3, python 3.14.3
 game dumped with reclass.

---

### Post #4616885 — xxKaled — 10th March 2026, 10:30 AM

Does anyone know of an AHK for this game?

---

### Post #4618509 — Jwbejsbs — 11th March 2026, 11:54 PM

Respect from VN

---

### Post #4618763 — trankhanhtinh1 — 12th March 2026, 04:26 AM

> **Jwbejsbs:**
> Originally Posted by Jwbejsbs   Respect from VN

Haha cảm ơn người anh em

> **Alexis913:**
> Originally Posted by Alexis913   Hi all!
>
>  @[trankhanhtinh1](https://www.unknowncheats.me/forum/members/6407580.html): What injector is needed? Can you recommend one?
>
>  @[hernos](https://www.unknowncheats.me/forum/members/544452.html): Regarding the .dll for IDA, it doesn't work for me with the LoL 26.05 patch + IDA Pro 9.1 + Python 3.14.3. All offsets come out as zero.
>   Code:
> ```
> //HOOKS
> constexpr uintptr_t CreateClientEffect             = 0x0;
> constexpr uintptr_t OnGameUpdate                   = 0x0;
> constexpr uintptr_t OnSpellImpact                  = 0x0;
> ```

Dll outdate, I will update dll inject correct soon, my this dll still wrong offset

---

### Post #4618868 — Jwbejsbs — 12th March 2026, 08:48 AM

'm working on a TFT project, but it crashes as soon as I inject it into the game. Can you help me update it?

---

### Post #4619421 — Alexis913 — 12th March 2026, 08:13 PM

Quote:    Dll outdate, I will update dll inject correct soon, my this dll still wrong offset    Please don't forget to name or recommend an injector.

---

### Post #4619525 — trankhanhtinh1 — 12th March 2026, 10:08 PM

> **Jwbejsbs:**
> Originally Posted by Jwbejsbs   'm working on a TFT project, but it crashes as soon as I inject it into the game. Can you help me update it?

I need know your offset u need

---

### Post #4619534 — aniltosun29 — 12th March 2026, 10:18 PM

> **trankhanhtinh1:**
> Originally Posted by trankhanhtinh1   I need know your offset u need

Hey I also need to offset for my tft project

 all offset, I actualy find most of it buut it will quite good if I check with yours too;
 GameManager,
 HudInstance,
 ViewPort,
 LocalPlayer,
 ObjPosition,
 ViewMatrix,
 ShopComponent,
 ShopList

 also some click function I analys it but its just return the function not the order

> **Jwbejsbs:**
> Originally Posted by Jwbejsbs   'm working on a TFT project, but it crashes as soon as I inject it into the game. Can you help me update it?

hey wanna help each other?

---

### Post #4619607 — Zeuby — 12th March 2026, 11:50 PM

> **aniltosun29:**
> Originally Posted by aniltosun29   ~~

 You guys just need minionlist offset to do a pool tracker
 also use the buffmanager to know if unit is 1/2/3 stars

---

### Post #4619662 — Jwbejsbs — 13th March 2026, 01:27 AM

> **aniltosun29:**
> Originally Posted by aniltosun29   Hey I also need to offset for my tft project
>
>  all offset, I actualy find most of it buut it will quite good if I check with yours too;
>  GameManager,
>  HudInstance,
>  ViewPort,
>  LocalPlayer,
>  ObjPosition,
>  ViewMatrix,
>  ShopComponent,
>  ShopList
>
>
>  also some click function I analys it but its just return the function not the order
>
>
>
>  hey wanna help each other?

welcome

---

### Post #4621291 — chen399516 — 14th March 2026, 08:07 PM

> **Jwbejsbs:**
> Originally Posted by Jwbejsbs   welcome

I'd like to too, can we discuss this together?

---

### Post #4621688 — trankhanhtinh1 — 15th March 2026, 06:01 AM

new update dumpper offset
 [https://www.unknowncheats.me/forum/d...=file&id=53677](https://www.unknowncheats.me/forum/downloads.php?do=file&id=53677)

---

### Post #4627369 — BlackMax97 — 20th March 2026, 11:50 AM

File Approved
![LOLDumper - Auto Offset Dumper DLL (Inject &amp; Dump)](https://www.unknowncheats.me/forum/images/icons/app_check.png)

- SHA256: 3d2445bc187b425c0e62fe15a35b46e949530a356ea2dbe213e9e296cd710348 - LOLDumper.dllInterested in how we analyze files? [Click here to find out.](https://www.unknowncheats.me/forum/faq.php?faq=uc_dl_faq)

 Download: [https://www.unknowncheats.me/forum/d...=file&id=53677](https://www.unknowncheats.me/forum/downloads.php?do=file&id=53677)

 Thanks for sharing!

---

### Post #4630617 — znob — 24th March 2026, 03:24 AM

> **trankhanhtinh1:**
> Originally Posted by trankhanhtinh1   new update dumpper offset
>  [https://www.unknowncheats.me/forum/d...=file&id=53677](https://www.unknowncheats.me/forum/downloads.php?do=file&id=53677)

Is it working?

---

### Post #4631437 — thunartx — 24th March 2026, 11:49 PM

How can we find this offsets (from r3nz skin changer) ;
 any method for them?

 "
 &offsets::global::GameClient
 &offsets::global::Player
 &offsets::global::ManagerTemplate_AIHero_
 &offsets::global::ChampionManager
 &offsets::global::ManagerTemplate_AIMinionClient_
 &offsets::global::ManagerTemplate_AITurret_
 &offsets::global::Riot__g_window
 &offsets::AIBaseCommon::CharacterDataStack
 &offsets::AIBaseCommon::SkinId
 &offsets::MaterialRegistry::SwapChain
 &offsets::functions::CharacterDataStack__Push
 &offsets::functions::CharacterDataStack__Update
 &offsets::functions::Riot__Renderer__MaterialRegistry__GetSingletonPtr
 &offsets::functions::translateString_UNSAFE_DONOTUSE
 &offsets::functions::GetGoldRedirectTarget

 IsLaneMinion = 0xF0,
 IsEliteMinion = IsLaneMinion + 0x1,
 IsEpicMinion = IsEliteMinion + 0x1,
 IsMinion = IsEpicMinion + 0x4,
 IsJungle = IsMinion + 0x1
 };
 };
 enum {
 Team = 0x259,
 Name = 0x68
 "

---

### Post #4633188 — andrey1818 — 26th March 2026, 05:03 PM

> **thunartx:**
> Originally Posted by thunartx   How can we find this offsets (from r3nz skin changer) ;
>  any method for them?
>
>
>  "
>  &offsets::global::GameClient
>  &offsets::global::Player
>  &offsets::global::ManagerTemplate_AIHero_
>  &offsets::global::ChampionManager
>  &offsets::global::ManagerTemplate_AIMinionClient_
>  &offsets::global::ManagerTemplate_AITurret_
>  &offsets::global::Riot__g_window
>  &offsets::AIBaseCommon::CharacterDataStack
>  &offsets::AIBaseCommon::SkinId
>  &offsets::MaterialRegistry::SwapChain
>  &offsets::functions::CharacterDataStack__Push
>  &offsets::functions::CharacterDataStack__Update
>  &offsets::functions::Riot__Renderer__MaterialRegistry__GetSingletonPtr
>  &offsets::functions::translateString_UNSAFE_DONOTUSE
>  &offsets::functions::GetGoldRedirectTarget
>
>  IsLaneMinion = 0xF0,
>  IsEliteMinion = IsLaneMinion + 0x1,
>  IsEpicMinion = IsEliteMinion + 0x1,
>  IsMinion = IsEpicMinion + 0x4,
>  IsJungle = IsMinion + 0x1
>  };
>  };
>  enum {
>  Team = 0x259,
>  Name = 0x68
>  "

I already installed "IDA Pro 9.3.260213" and put the plugin "ida_lol_plugin_[unknowncheats.me]_.dll" inside "IDA Professional 9.3\plugins" and now what?

---

### Post #4636225 — ibrahimcelikx — 29th March 2026, 09:55 PM

@[hernos](https://www.unknowncheats.me/forum/members/544452.html) @[trankhanhtinh1](https://www.unknowncheats.me/forum/members/6407580.html)
 Can you help for some offsets its about skin changer? How we can find them;

 GameClient
 Player
 ManagerTemplate_AIHero_
 ChampionManager
 ManagerTemplate_AIMinionClient_
 ManagerTemplate_AITurret_
 Riot__g_window
 AIBaseCommon::CharacterDataStack
 AIBaseCommon::SkinId
 MaterialRegistry::SwapChain
 functions::CharacterDataStack__Push
 functions::CharacterDataStack__Update
 functions::Riot__Renderer__MaterialRegistry__GetSingletonPtr
 functions::translateString_UNSAFE_DONOTUSE
 functions::GetGoldRedirectTarget

 for example he found ChampionManager = "48 8B 0D ? ? ? ? 48 69 D0 ? ? 00 00 48 8B 05"
 and than for example AIBaseCommon::CharacterDataStack : 48 8D 8D ? ? 00 00 44 8B 8C 24 ? ? 00 00
 SwapChain: 48 8D BB ? ? ? ? C6 83 ? ? ? ? ? 0F 84

 can u help for them its r3nz skin changer

---

### Post #4636341 — thunartx — 30th March 2026, 12:55 AM

@[hernos](https://www.unknowncheats.me/forum/members/544452.html) @[trankhanhtinh1](https://www.unknowncheats.me/forum/members/6407580.html)

 Can you help for some offsets its about skin changer? How we can find them;

 GameClient
 Player
 ManagerTemplate_AIHero_
 ChampionManager
 ManagerTemplate_AIMinionClient_
 ManagerTemplate_AITurret_
 Riot__g_window
 AIBaseCommon::CharacterDataStack
 AIBaseCommon::SkinId
 MaterialRegistry::SwapChain
 functions::CharacterDataStack__Push
 functions::CharacterDataStack__Update
 functions::Riot__Renderer__MaterialRegistry__GetSingletonPtr
 functions::translateString_UNSAFE_DONOTUSE
 functions::GetGoldRedirectTarget

 for example he found ChampionManager = "48 8B 0D ? ? ? ? 48 69 D0 ? ? 00 00 48 8B 05"
 and than for example AIBaseCommon::CharacterDataStack : 48 8D 8D ? ? 00 00 44 8B 8C 24 ? ? 00 00
 SwapChain: 48 8D BB ? ? ? ? C6 83 ? ? ? ? ? 0F 84

 can u help for them its r3nz skin changer any guide?

---

### Post #4661232 — AdoAdrijan — 26th April 2026, 02:58 AM

hey, what injector do i use for this? would someone mind sharing the injector to run this dll?

---

### Post #4664963 — coderduc — 30th April 2026, 02:59 AM

> **trankhanhtinh1:**
> Originally Posted by trankhanhtinh1   LOLDumper - Auto Offset Dumper DLL (Inject & Dump)
>  Hey everyone,
>  Sharing my offset dumper DLL for League of Legends. Inject it into the game, wait a few seconds, and it dumps all offsets to LOLDumper_full.h in the game directory.
>  What it dumps:
>  Global pointers (LocalPlayer, HeroList, GameTime, MissileList, NavGrid, HUD, etc.)
>  Function addresses (IssueOrder, CastSpell, WorldToScreen, GetPing, IsMinion, IsTurret, etc.)
>  Struct offsets via pattern scan (NetID, Position, SpellBook, BuffManager, BasicAttack, AiManager, etc.)
>  All replicated properties with correct absolute offsets (HP, MaxHP, Armor, AD, MoveSpeed, AttackRange, Gold, etc.)
>  All internal struct offsets (AiManager, BuffInstance, SpellSlot, SpellInfo, MissileData, etc.)
>  RTTI class names, events, spell parameters
>
>  Looking for feedback � especially if any offsets are wrong or missing for your use case. Let me know what you find.
>
>  Link: [https://www.unknowncheats.me/forum/d...=file&id=53416](https://www.unknowncheats.me/forum/downloads.php?do=file&id=53416)

Chia sẻ c�ch người ae inject dll v� game đi?

---

### Post #4667847 — debjuuuu — 3rd May 2026, 03:27 AM

so it works found many correct offsets but i had problem with these ones at least from my quick test:
 oObjteam - wrong
 oobjname - wrong doesnt find anything
 oobjposition - wrong
 oobjisalive - wrong

 thats the ones the dumper got wrong not sure about other ones.

---

### Post #4672614 — clanclasherxx — 8th May 2026, 12:50 PM

> **hernos:**
> Originally Posted by hernos   Uploaded [https://www.unknowncheats.me/forum/d...=file&id=53417](https://www.unknowncheats.me/forum/downloads.php?do=file&id=53417)
>
>  Offsets will be dumped to C:/lol

Thanks for sharing, but somehow all offsets in offsets.hpp are all 0x0.

---

### Post #4673010 — acc569 — 8th May 2026, 10:20 PM

How to update memory signatures

 How to update pattern signatures dll

---

### Post #4673235 — hernos — 9th May 2026, 05:11 AM

> **clanclasherxx:**
> Originally Posted by clanclasherxx   Thanks for sharing, but somehow all offsets in offsets.hpp are all 0x0.

Probably updates broke signatures of scanner or you are not playing on RIOT region.

---

### Post #4673622 — clanclasherxx — 9th May 2026, 02:39 PM

> **hernos:**
> Originally Posted by hernos   Probably updates broke signatures of scanner or you are not playing on RIOT region.

All good, i just didn't properly red your msg, so you run it after dumping league. Thanks for replying.

---

### Post #4674359 — jinnnl — 10th May 2026, 07:31 AM

how can use it dll?

---

### Post #4676201 — jinnnl — 12th May 2026, 04:00 AM

> **hernos:**
> Originally Posted by hernos   Thats good share. I also have something like that but its IDA plugin instead. I run my IDA plugin and it extracts new offsets from dump and renames functions, arg names

Hello, do you know about the lane offset values ​​in Season 16? I'm looking for the bot lane and support reward slots for the Season 16 lane offset values, but I can't find them and I can't get the support's control wards or the marksman's boots.

---

### Post #4682186 — UnawakenDream — 18th May 2026, 10:02 AM

Thanks for sharing. I've downloaded the DLL file. I'm a newbie and I'd like to know how to inject it into the game. Is it safe? Will my account get banned? Any recommended tools?

---

### Post #4682323 — Alexis913 — 18th May 2026, 02:18 PM

Does the LOLDumper DLL still work?
 Which injector should be used with it, or which one is compatible?

---

### Post #4685500 — ax9378 — 21st May 2026, 11:17 PM

Do you guys know one dumper for MacOS ? (offsets / patterns)

---

### Post #4685614 — aniltosun29 — 22nd May 2026, 02:03 AM

> **ax9378:**
> Originally Posted by ax9378   Do you guys know one dumper for MacOS ? (offsets / patterns)

skip the dumper, is there any memory offsets that for mac? Or even can you read any memory thing in macos?

---

### Post #4715285 — acc569 — 20th June 2026, 07:49 PM

How do you inject the game and do you share the injected code

---

### Post #4754414 — cttbot13 — 28th July 2026, 07:43 AM

im from vn too, thank your share. im cttbot, still working on lol project

---

### Post #4755404 — cttbot13 — 29th July 2026, 12:37 PM

[https://ibb.co/1fgGSCVx](https://ibb.co/1fgGSCVx)

---

### Post #4757048 — waveywidit2 — 31st July 2026, 01:35 AM

> **cttbot13:**
> Originally Posted by cttbot13   [https://ibb.co/1fgGSCVx](https://ibb.co/1fgGSCVx)

Looks good

---

### Post #4758837 — k2de3 — 2nd August 2026, 12:02 AM

> **cttbot13:**
> Originally Posted by cttbot13   [https://ibb.co/1fgGSCVx](https://ibb.co/1fgGSCVx)

thanks

---

### Post #4797480 — notalk1337 — 12th September 2026, 06:31 PM

> **hernos:**
> Originally Posted by hernos   You can use any dumper. Open it in IDA then run plugin from plugins tab > league of legends
>
>  It will work then.
>
>
> ![image](https://i.imgur.com/Iz4Pyrx.png)
>
>
> ![image](https://i.imgur.com/0MGbDzG.png)
>
>
> ![image](https://i.imgur.com/SwVq08Y.png)
>
>
> ![image](https://i.imgur.com/fXCr3GY.png)

 @[hernos](https://www.unknowncheats.me/forum/members/544452.html) i using 9.2 ida pro but can't get offsets all offsets are 0x0 can help?

---

### Post #4797682 — hernos — 12th September 2026, 10:51 PM

> **notalk1337:**
> Originally Posted by notalk1337   @[hernos](https://www.unknowncheats.me/forum/members/544452.html) i using 9.2 ida pro but can't get offsets all offsets are 0x0 can help?

Rebase dump to 0x0

---

### Post #4797736 — notalk1337 — 13th September 2026, 12:12 AM

ye its work but onyl gets 30-35 offsets, and can't find;

 CastSpell
 IsTargetable
 IsRiftHerald
 CanCast
 CanMove
 IsSlowed
 MinionManager
 AIHeroVTABLE

 it's normal? or bad dump?

---

### Post #4798500 — hernos — 13th September 2026, 10:16 PM

> **notalk1337:**
> Originally Posted by notalk1337   ye its work but onyl gets 30-35 offsets, and can't find;
>
>  CastSpell
>  IsTargetable
>  IsRiftHerald
>  CanCast
>  CanMove
>  IsSlowed
>  MinionManager
>  AIHeroVTABLE
>
>  it's normal? or bad dump?

Probably signatures changed and plugin needs to updated

---

### Post #4844778 — Lauv — 29th October 2026, 12:09 AM

c�n work kh�ng người ae

---

### Post #4844794 — trankhanhtinh1 — 29th October 2026, 12:27 AM

> **Lauv:**
> Originally Posted by Lauv   c�n work kh�ng người ae

cần th� lh tui, tui gửi cho c�i script lấy offset trong IDA
