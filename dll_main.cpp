#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <d3d9.h>
#include <cstdio>
#include <cmath>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_dx9.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "game.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static HMODULE g_hModule = nullptr;

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return true;
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

/* --- vars --- */
static bool bEsp       = true;
static bool bBox       = true;
static bool bBox3d     = false;
static bool bSkel      = true;
static bool bNames     = true;
static bool bDist      = true;
static bool bHpBar     = true;
static bool bHead      = true;
static bool bSnap      = false;
static bool bMenu      = true;
static bool bOnlyEnemy = false;
static bool bItems     = false;
static bool bItemBox   = true;
static bool bItemName  = true;
static bool bItemDist  = true;
static float fItemDist = 50.f;
static bool itemFilter[9] = {true,true,true,true,true,true,true,true,true};
static float cItem[4] = {1.f, .85f, 0.f, 1.f};
static bool bInteract   = false;
static float fInteractDist = 50.f;
static float cInteract[4] = {0.f, 1.f, .8f, 1.f};
static bool bCrosshair  = true;
static bool bTeleport   = false;
static float fTpHeight  = 5.f;
static bool bNoFallDmg  = false;
static bool bNoclip     = false;
static float fNoclipSpeed = 15.f;
static float fMaxDist  = 150.f;
static bool bMagicBullet = false;
static float fAimFov = 200.f;
static bool bAimVisCheck = true;
static bool bWallhack = false;
static bool bGodMode = false;
static bool bInfAmmo = false;
static bool bInfMana = false;
static bool bInfHealth = false;
static bool bInfAir = false;
static bool bInfElixir = false;
static bool bInfAdrenaline = false;
static bool bAIBlind = false;
static bool bAIDeaf = false;
static bool bAIOff = false;
static bool bAutoDodge = false;
static bool bBuddhaMode = false;
static bool bNoKnockdown = false;
static bool bForceKillCam = false;
static bool bDarkVision = false;
static bool bNoSpread = false;
static bool bRapidFire = false;
static bool bNoCooldown = false;
static bool bInstantReload = false;
static bool bSpeedHack = false;
static float fGroundSpeed = 440.f;
static float fJumpZ = 520.f;
static float fBlinkRange = 1500.f;
static bool bBlinkRange = false;
static float fTimeDilation = 1.0f;
static bool bTimeDilation = false;
static float fGravity = -1150.f;
static bool bGravity = false;
static float fBoxThk   = 1.5f;
static float fSkelThk  = 1.5f;
static int iBoxMode    = 0;
static int iSnapFrom   = 0;

static float cHostile[4]  = {1.f, .15f, .15f, 1.f};
static float cSuspect[4]  = {1.f, .7f, 0.f, 1.f};
static float cPassive[4]  = {.2f, .6f, 1.f, 1.f};
static float cFriend[4]   = {.3f, 1.f, .3f, 1.f};
static float cBones[4]    = {1.f, 1.f, 0.f, .8f};
static float cSnap[4]     = {1.f, 1.f, 1.f, .5f};

#define CFG_PATH "esp.cfg"
static char cfgMsg[48] = "";
static DWORD cfgTime = 0;

static void cfgSave() {
    FILE* fp = fopen(CFG_PATH, "w");
    if (!fp) { strcpy(cfgMsg, "err: cant write cfg"); cfgTime = GetTickCount(); return; }
    fprintf(fp, "esp=%d\nbox=%d\nbox3d=%d\nskel=%d\nnames=%d\ndist=%d\n", bEsp, bBox, bBox3d, bSkel, bNames, bDist);
    fprintf(fp, "hpbar=%d\nhead=%d\nsnap=%d\nsnapfrom=%d\nonlyenemy=%d\n", bHpBar, bHead, bSnap, iSnapFrom, bOnlyEnemy);
    fprintf(fp, "maxdist=%.1f\nboxthk=%.2f\nskelthk=%.2f\nboxmode=%d\n", fMaxDist, fBoxThk, fSkelThk, iBoxMode);
    fprintf(fp, "ch=%.3f,%.3f,%.3f,%.3f\n", cHostile[0], cHostile[1], cHostile[2], cHostile[3]);
    fprintf(fp, "cu=%.3f,%.3f,%.3f,%.3f\n", cSuspect[0], cSuspect[1], cSuspect[2], cSuspect[3]);
    fprintf(fp, "cp=%.3f,%.3f,%.3f,%.3f\n", cPassive[0], cPassive[1], cPassive[2], cPassive[3]);
    fprintf(fp, "cf=%.3f,%.3f,%.3f,%.3f\n", cFriend[0], cFriend[1], cFriend[2], cFriend[3]);
    fprintf(fp, "cb=%.3f,%.3f,%.3f,%.3f\n", cBones[0], cBones[1], cBones[2], cBones[3]);
    fprintf(fp, "cs=%.3f,%.3f,%.3f,%.3f\n", cSnap[0], cSnap[1], cSnap[2], cSnap[3]);
    fprintf(fp, "items=%d\nitembox=%d\nitemname=%d\nitemdist=%d\nitemdistf=%.1f\n", bItems, bItemBox, bItemName, bItemDist, fItemDist);
    fprintf(fp, "ci=%.3f,%.3f,%.3f,%.3f\n", cItem[0], cItem[1], cItem[2], cItem[3]);
    fprintf(fp, "ifilter=%d,%d,%d,%d,%d,%d,%d,%d,%d\n", itemFilter[0],itemFilter[1],itemFilter[2],itemFilter[3],itemFilter[4],itemFilter[5],itemFilter[6],itemFilter[7],itemFilter[8]);
    fprintf(fp, "interact=%d\ninteractdist=%.1f\ncrosshair=%d\nteleport=%d\ntpheight=%.1f\nnofalldmg=%d\nnoclipspd=%.1f\nmagicbullet=%d\naimfov=%.1f\naimvischeck=%d\nwallhack=%d\n", bInteract, fInteractDist, bCrosshair, bTeleport, fTpHeight, bNoFallDmg, fNoclipSpeed, bMagicBullet, fAimFov, bAimVisCheck, bWallhack);
    fprintf(fp, "cint=%.3f,%.3f,%.3f,%.3f\n", cInteract[0], cInteract[1], cInteract[2], cInteract[3]);
    fprintf(fp, "godmode=%d\ninfammo=%d\ninfmana=%d\ninfhealth=%d\ninfair=%d\ninfelixir=%d\n", bGodMode, bInfAmmo, bInfMana, bInfHealth, bInfAir, bInfElixir);
    fprintf(fp, "infadrenaline=%d\naiblind=%d\naideaf=%d\naioff=%d\nautododge=%d\nbuddha=%d\nnoknockdown=%d\n", bInfAdrenaline, bAIBlind, bAIDeaf, bAIOff, bAutoDodge, bBuddhaMode, bNoKnockdown);
    fprintf(fp, "nospread=%d\nrapidfire=%d\ninstantreload=%d\nnocooldown=%d\nspeedhack=%d\ngroundspd=%.1f\njumpz=%.1f\nblinkrange=%d\nblinkrangef=%.1f\n", bNoSpread, bRapidFire, bInstantReload, bNoCooldown, bSpeedHack, fGroundSpeed, fJumpZ, bBlinkRange, fBlinkRange);
    fprintf(fp, "timedilation=%d\ntimescale=%.2f\ngravity=%d\ngravityf=%.1f\nforcekillcam=%d\ndarkvision=%d\n", bTimeDilation, fTimeDilation, bGravity, fGravity, bForceKillCam, bDarkVision);
    fclose(fp);
    strcpy(cfgMsg, "saved"); cfgTime = GetTickCount();
}

static void cfgLoad() {
    FILE* fp = fopen(CFG_PATH, "r");
    if (!fp) return;
    char ln[256];
    while (fgets(ln, 256, fp)) {
        int v; float f;
        if (sscanf(ln, "esp=%d", &v)==1) bEsp=v;
        else if (sscanf(ln, "box=%d", &v)==1) bBox=v;
        else if (sscanf(ln, "box3d=%d", &v)==1) bBox3d=v;
        else if (sscanf(ln, "skel=%d", &v)==1) bSkel=v;
        else if (sscanf(ln, "names=%d", &v)==1) bNames=v;
        else if (sscanf(ln, "dist=%d", &v)==1) bDist=v;
        else if (sscanf(ln, "hpbar=%d", &v)==1) bHpBar=v;
        else if (sscanf(ln, "head=%d", &v)==1) bHead=v;
        else if (sscanf(ln, "snap=%d", &v)==1) bSnap=v;
        else if (sscanf(ln, "snapfrom=%d", &v)==1) iSnapFrom=v;
        else if (sscanf(ln, "onlyenemy=%d", &v)==1) bOnlyEnemy=v;
        else if (sscanf(ln, "maxdist=%f", &f)==1) fMaxDist=f;
        else if (sscanf(ln, "boxthk=%f", &f)==1) fBoxThk=f;
        else if (sscanf(ln, "skelthk=%f", &f)==1) fSkelThk=f;
        else if (sscanf(ln, "boxmode=%d", &v)==1) iBoxMode=v;
        else if (sscanf(ln, "ch=%f,%f,%f,%f", &cHostile[0],&cHostile[1],&cHostile[2],&cHostile[3])==4) {}
        else if (sscanf(ln, "cu=%f,%f,%f,%f", &cSuspect[0],&cSuspect[1],&cSuspect[2],&cSuspect[3])==4) {}
        else if (sscanf(ln, "cp=%f,%f,%f,%f", &cPassive[0],&cPassive[1],&cPassive[2],&cPassive[3])==4) {}
        else if (sscanf(ln, "cf=%f,%f,%f,%f", &cFriend[0],&cFriend[1],&cFriend[2],&cFriend[3])==4) {}
        else if (sscanf(ln, "cb=%f,%f,%f,%f", &cBones[0],&cBones[1],&cBones[2],&cBones[3])==4) {}
        else if (sscanf(ln, "cs=%f,%f,%f,%f", &cSnap[0],&cSnap[1],&cSnap[2],&cSnap[3])==4) {}
        else if (sscanf(ln, "items=%d", &v)==1) bItems=v;
        else if (sscanf(ln, "itembox=%d", &v)==1) bItemBox=v;
        else if (sscanf(ln, "itemname=%d", &v)==1) bItemName=v;
        else if (sscanf(ln, "itemdist=%d", &v)==1) bItemDist=v;
        else if (sscanf(ln, "itemdistf=%f", &f)==1) fItemDist=f;
        else if (sscanf(ln, "ci=%f,%f,%f,%f", &cItem[0],&cItem[1],&cItem[2],&cItem[3])==4) {}
        else if (strncmp(ln, "ifilter=", 8)==0) { int a[9]; if(sscanf(ln+8,"%d,%d,%d,%d,%d,%d,%d,%d,%d",&a[0],&a[1],&a[2],&a[3],&a[4],&a[5],&a[6],&a[7],&a[8])==9) for(int i=0;i<9;i++) itemFilter[i]=a[i]; }
        else if (sscanf(ln, "interact=%d", &v)==1) bInteract=v;
        else if (sscanf(ln, "interactdist=%f", &f)==1) fInteractDist=f;
        else if (sscanf(ln, "crosshair=%d", &v)==1) bCrosshair=v;
        else if (sscanf(ln, "teleport=%d", &v)==1) bTeleport=v;
        else if (sscanf(ln, "tpheight=%f", &f)==1) fTpHeight=f;
        else if (sscanf(ln, "nofalldmg=%d", &v)==1) bNoFallDmg=v;
        else if (sscanf(ln, "noclipspd=%f", &f)==1) fNoclipSpeed=f;
        else if (sscanf(ln, "magicbullet=%d", &v)==1) bMagicBullet=v;
        else if (sscanf(ln, "aimfov=%f", &f)==1) fAimFov=f;
        else if (sscanf(ln, "aimvischeck=%d", &v)==1) bAimVisCheck=v;
        else if (sscanf(ln, "magicbullet=%d", &v)==1) bMagicBullet=v;
        else if (sscanf(ln, "wallhack=%d", &v)==1) bWallhack=v;
        else if (sscanf(ln, "cint=%f,%f,%f,%f", &cInteract[0],&cInteract[1],&cInteract[2],&cInteract[3])==4) {}
        else if (sscanf(ln, "godmode=%d", &v)==1) bGodMode=v;
        else if (sscanf(ln, "infammo=%d", &v)==1) bInfAmmo=v;
        else if (sscanf(ln, "infmana=%d", &v)==1) bInfMana=v;
        else if (sscanf(ln, "infhealth=%d", &v)==1) bInfHealth=v;
        else if (sscanf(ln, "infair=%d", &v)==1) bInfAir=v;
        else if (sscanf(ln, "infelixir=%d", &v)==1) bInfElixir=v;
        else if (sscanf(ln, "infadrenaline=%d", &v)==1) bInfAdrenaline=v;
        else if (sscanf(ln, "aiblind=%d", &v)==1) bAIBlind=v;
        else if (sscanf(ln, "aideaf=%d", &v)==1) bAIDeaf=v;
        else if (sscanf(ln, "aioff=%d", &v)==1) bAIOff=v;
        else if (sscanf(ln, "autododge=%d", &v)==1) bAutoDodge=v;
        else if (sscanf(ln, "buddha=%d", &v)==1) bBuddhaMode=v;
        else if (sscanf(ln, "noknockdown=%d", &v)==1) bNoKnockdown=v;
        else if (sscanf(ln, "nospread=%d", &v)==1) bNoSpread=v;
        else if (sscanf(ln, "rapidfire=%d", &v)==1) bRapidFire=v;
        else if (sscanf(ln, "instantreload=%d", &v)==1) bInstantReload=v;
        else if (sscanf(ln, "nocooldown=%d", &v)==1) bNoCooldown=v;
        else if (sscanf(ln, "speedhack=%d", &v)==1) bSpeedHack=v;
        else if (sscanf(ln, "groundspd=%f", &f)==1) fGroundSpeed=f;
        else if (sscanf(ln, "jumpz=%f", &f)==1) fJumpZ=f;
        else if (sscanf(ln, "blinkrange=%d", &v)==1) bBlinkRange=v;
        else if (sscanf(ln, "blinkrangef=%f", &f)==1) fBlinkRange=f;
        else if (sscanf(ln, "timedilation=%d", &v)==1) bTimeDilation=v;
        else if (sscanf(ln, "timescale=%f", &f)==1) fTimeDilation=f;
        else if (sscanf(ln, "gravity=%d", &v)==1) bGravity=v;
        else if (sscanf(ln, "gravityf=%f", &f)==1) fGravity=f;
        else if (sscanf(ln, "forcekillcam=%d", &v)==1) bForceKillCam=v;
        else if (sscanf(ln, "darkvision=%d", &v)==1) bDarkVision=v;
    }
    fclose(fp);
    strcpy(cfgMsg, "loaded"); cfgTime = GetTickCount();
}

static inline ImU32 c4(const float* c) {
    return IM_COL32((int)(c[0]*255),(int)(c[1]*255),(int)(c[2]*255),(int)(c[3]*255));
}

static inline ImU32 colAlpha(ImU32 c, uint8_t a) {
    return (c & 0x00FFFFFF) | ((ImU32)a << 24);
}

static inline ImU32 colFade(ImU32 c, float dist, float maxDist) {
    if (dist > maxDist * 0.7f) {
        float f = 1.f - (dist - maxDist * 0.7f) / (maxDist * 0.3f);
        if (f < 0.3f) f = 0.3f;
        uint8_t a = (uint8_t)((c >> 24) * f);
        return (c & 0x00FFFFFF) | ((ImU32)a << 24);
    }
    return c;
}

static void txtLabel(ImDrawList* d, float x, float y, ImU32 col, const char* t) {
    auto ts = ImGui::CalcTextSize(t);
    float px = 4.f, py = 1.f, r = 3.f;
    d->AddRectFilled({x - px, y - py}, {x + ts.x + px, y + ts.y + py}, IM_COL32(0, 0, 0, 170), r);
    d->AddText({x, y}, col, t);
}

static void txtOutline(ImDrawList* d, float x, float y, ImU32 col, const char* t) {
    ImU32 bg = IM_COL32(0, 0, 0, 180);
    for (int dx = -1; dx <= 1; dx++)
        for (int dy = -1; dy <= 1; dy++)
            if (dx || dy) d->AddText({x + dx, y + dy}, bg, t);
    d->AddText({x, y}, col, t);
}

static void cornerBox(ImDrawList* d, float x1, float y1, float x2, float y2, ImU32 c, float t) {
    float w = (x2 - x1) * 0.22f, h = (y2 - y1) * 0.22f;
    ImU32 shadow = IM_COL32(0, 0, 0, 100);
    d->AddLine({x1-1,y1}, {x1+w,y1}, shadow, t+1.f); d->AddLine({x1,y1-1}, {x1,y1+h}, shadow, t+1.f);
    d->AddLine({x2+1,y1}, {x2-w,y1}, shadow, t+1.f); d->AddLine({x2,y1-1}, {x2,y1+h}, shadow, t+1.f);
    d->AddLine({x1-1,y2}, {x1+w,y2}, shadow, t+1.f); d->AddLine({x1,y2+1}, {x1,y2-h}, shadow, t+1.f);
    d->AddLine({x2+1,y2}, {x2-w,y2}, shadow, t+1.f); d->AddLine({x2,y2+1}, {x2,y2-h}, shadow, t+1.f);
    d->AddLine({x1,y1}, {x1+w,y1}, c, t); d->AddLine({x1,y1}, {x1,y1+h}, c, t);
    d->AddLine({x2,y1}, {x2-w,y1}, c, t); d->AddLine({x2,y1}, {x2,y1+h}, c, t);
    d->AddLine({x1,y2}, {x1+w,y2}, c, t); d->AddLine({x1,y2}, {x1,y2-h}, c, t);
    d->AddLine({x2,y2}, {x2-w,y2}, c, t); d->AddLine({x2,y2}, {x2,y2-h}, c, t);
}

static void draw3DBox(ImDrawList* dl, const Game::Vec3& loc, const Game::Vec3& ext,
                      const Game::Matrix4& vpm, float sw, float sh, ImU32 col, float thk = 1.f) {
    float hw = ext.X, hd = ext.Y, hh = ext.Z;
    Game::Vec3 corners[8] = {
        {loc.X-hw, loc.Y-hd, loc.Z-hh}, {loc.X+hw, loc.Y-hd, loc.Z-hh},
        {loc.X+hw, loc.Y+hd, loc.Z-hh}, {loc.X-hw, loc.Y+hd, loc.Z-hh},
        {loc.X-hw, loc.Y-hd, loc.Z+hh}, {loc.X+hw, loc.Y-hd, loc.Z+hh},
        {loc.X+hw, loc.Y+hd, loc.Z+hh}, {loc.X-hw, loc.Y+hd, loc.Z+hh},
    };
    Game::Vec3 s[8];
    float minX=9999,minY=9999,maxX=-9999,maxY=-9999;
    for (int c = 0; c < 8; c++) {
        if (!Game::WorldToScreen(corners[c], vpm, sw, sh, s[c])) return;
        if (s[c].X < minX) minX = s[c].X; if (s[c].Y < minY) minY = s[c].Y;
        if (s[c].X > maxX) maxX = s[c].X; if (s[c].Y > maxY) maxY = s[c].Y;
    }
    ImU32 shadow = IM_COL32(0, 0, 0, 80);
    for (int c = 0; c < 4; c++) {
        dl->AddLine({s[c].X,s[c].Y}, {s[(c+1)%4].X,s[(c+1)%4].Y}, shadow, thk+1.f);
        dl->AddLine({s[c+4].X,s[c+4].Y}, {s[((c+1)%4)+4].X,s[((c+1)%4)+4].Y}, shadow, thk+1.f);
        dl->AddLine({s[c].X,s[c].Y}, {s[c+4].X,s[c+4].Y}, shadow, thk+1.f);
    }
    for (int c = 0; c < 4; c++) {
        dl->AddLine({s[c].X,s[c].Y}, {s[(c+1)%4].X,s[(c+1)%4].Y}, col, thk);
        dl->AddLine({s[c+4].X,s[c+4].Y}, {s[((c+1)%4)+4].X,s[((c+1)%4)+4].Y}, col, thk);
        dl->AddLine({s[c].X,s[c].Y}, {s[c+4].X,s[c+4].Y}, col, thk);
    }
}

static void draw3DBoxLabel(ImDrawList* dl, const Game::Vec3& loc, const Game::Vec3& ext,
                           const Game::Matrix4& vpm, float sw, float sh, ImU32 col,
                           const char* label, float thk = 1.f) {
    float hw = ext.X, hd = ext.Y, hh = ext.Z;
    Game::Vec3 corners[8] = {
        {loc.X-hw, loc.Y-hd, loc.Z-hh}, {loc.X+hw, loc.Y-hd, loc.Z-hh},
        {loc.X+hw, loc.Y+hd, loc.Z-hh}, {loc.X-hw, loc.Y+hd, loc.Z-hh},
        {loc.X-hw, loc.Y-hd, loc.Z+hh}, {loc.X+hw, loc.Y-hd, loc.Z+hh},
        {loc.X+hw, loc.Y+hd, loc.Z+hh}, {loc.X-hw, loc.Y+hd, loc.Z+hh},
    };
    Game::Vec3 s[8];
    float minX=9999,minY=9999,maxX=-9999,maxY=-9999;
    for (int c = 0; c < 8; c++) {
        if (!Game::WorldToScreen(corners[c], vpm, sw, sh, s[c])) return;
        if (s[c].X < minX) minX = s[c].X; if (s[c].Y < minY) minY = s[c].Y;
        if (s[c].X > maxX) maxX = s[c].X; if (s[c].Y > maxY) maxY = s[c].Y;
    }
    ImU32 shadow = IM_COL32(0, 0, 0, 80);
    for (int c = 0; c < 4; c++) {
        dl->AddLine({s[c].X,s[c].Y}, {s[(c+1)%4].X,s[(c+1)%4].Y}, shadow, thk+1.f);
        dl->AddLine({s[c+4].X,s[c+4].Y}, {s[((c+1)%4)+4].X,s[((c+1)%4)+4].Y}, shadow, thk+1.f);
        dl->AddLine({s[c].X,s[c].Y}, {s[c+4].X,s[c+4].Y}, shadow, thk+1.f);
    }
    for (int c = 0; c < 4; c++) {
        dl->AddLine({s[c].X,s[c].Y}, {s[(c+1)%4].X,s[(c+1)%4].Y}, col, thk);
        dl->AddLine({s[c+4].X,s[c+4].Y}, {s[((c+1)%4)+4].X,s[((c+1)%4)+4].Y}, col, thk);
        dl->AddLine({s[c].X,s[c].Y}, {s[c+4].X,s[c+4].Y}, col, thk);
    }
    if (label && label[0]) {
        auto ts = ImGui::CalcTextSize(label);
        float cx = (minX + maxX) * .5f;
        txtLabel(dl, cx - ts.x * .5f, minY - ts.y - 4.f, col, label);
    }
}

static int itemCategory(const std::string& name) {
    if (name == "Rune") return 0;
    if (name == "Bone Charm") return 1;
    if (name == "Health Elixir") return 2;
    if (name == "Mana Elixir") return 3;
    if (name == "Key") return 4;
    if (name == "Loot") return 5;
    if (name == "Weapon") return 6;
    if (name == "Note" || name == "Audio Log") return 7;
    return 8;
}

static std::vector<Game::ActorInfo> g_lastEnts;

/* main render */
static void render(Game::GameReader& g, float sw, float sh)
{
    if (!bEsp) return;

    Game::Vec3 cp; Game::Rotator cr;
    if (!g.GetCameraData(cp, cr)) return;

    Game::Matrix4 vpm;
    g.BuildVPM(cp, cr, sw, sh, vpm);
    auto ents = g.GatherActors(cp, fMaxDist);
    g_lastEnts = ents;
    auto* dl = ImGui::GetBackgroundDrawList();
    dl->PushClipRect({0,0}, {sw,sh});

    ImU32 cH = c4(cHostile), cU = c4(cSuspect), cP = c4(cPassive), cFr = c4(cFriend);
    ImU32 cb = c4(cBones), cs = c4(cSnap);

    for (auto& e : ents)
    {
        if (bOnlyEnemy && e.relation != 2) continue;

        ImU32 col;
        if (e.relation == 0) col = cFr;
        else if (e.relation == 1) col = cP;
        else if (e.aiState == Game::AI_Combat) col = cH;
        else if (e.aiState == Game::AI_Suspicious) col = cU;
        else col = cH;

        col = colFade(col, e.Distance, fMaxDist);
        if (!e.bIsVisible) col = colAlpha(col, 70);

        std::vector<bool> used(e.Bones.size(), false);
        for (auto& [a,b] : e.BoneConnections) {
            if (a < (int)used.size()) used[a] = true;
            if (b < (int)used.size()) used[b] = true;
        }
        if (e.headEndBoneIdx >= 0 && e.headEndBoneIdx < (int)used.size())
            used[e.headEndBoneIdx] = true;

        // project bones for bounding box
        float x0=sw, y0=sh, x1=0, y1=0;
        int np = 0;
        std::vector<Game::Vec3> sb;
        std::vector<bool> bv;
        if (e.Bones.size() >= 2) {
            sb.resize(e.Bones.size());
            bv.resize(e.Bones.size(), false);
            for (size_t i = 0; i < e.Bones.size(); i++) {
                auto& bn = e.Bones[i];
                if (bn.X==0.f && bn.Y==0.f && bn.Z==0.f) continue;
                bv[i] = Game::WorldToScreen(bn, vpm, sw, sh, sb[i]);
                if (bv[i] && used[i]) {
                    if (sb[i].X<x0) x0=sb[i].X; if (sb[i].Y<y0) y0=sb[i].Y;
                    if (sb[i].X>x1) x1=sb[i].X; if (sb[i].Y>y1) y1=sb[i].Y;
                    np++;
                }
            }
        }
        bool gotBox = np >= 2;
        float pd = 8.f;

        // 2D Box
        if (bBox) {
            if (gotBox) {
                if (iBoxMode == 1)
                    cornerBox(dl, x0-pd, y0-pd, x1+pd, y1+pd, col, fBoxThk);
                else {
                    dl->AddRect({x0-pd-1,y0-pd-1}, {x1+pd+1,y1+pd+1}, IM_COL32(0,0,0,100), 0, 0, fBoxThk+1.f);
                    dl->AddRect({x0-pd,y0-pd}, {x1+pd,y1+pd}, col, 0, 0, fBoxThk);
                }
            } else {
                Game::Vec3 ft = e.Location, hd = e.Location;
                hd.Z += 180.f;
                Game::Vec3 sH, sF;
                if (Game::WorldToScreen(hd, vpm, sw, sh, sH) &&
                    Game::WorldToScreen(ft, vpm, sw, sh, sF)) {
                    float h = fabsf(sF.Y - sH.Y), w = h * 0.45f;
                    float bx0 = sH.X-w*.5f, by0 = sH.Y, bx1 = sH.X+w*.5f, by1 = sF.Y;
                    if (iBoxMode == 1) cornerBox(dl, bx0, by0, bx1, by1, col, fBoxThk);
                    else {
                        dl->AddRect({bx0-1,by0-1},{bx1+1,by1+1}, IM_COL32(0,0,0,100), 0, 0, fBoxThk+1.f);
                        dl->AddRect({bx0,by0},{bx1,by1}, col, 0, 0, fBoxThk);
                    }
                }
            }
        }

        // 3D Box
        if (bBox3d && e.Bones.size() >= 2) {
            float ax,ay,az,bxx,byy,bz; bool first=true;
            for (size_t i=0;i<e.Bones.size();i++) {
                if (!used[i]) continue;
                auto& b = e.Bones[i];
                if (b.X==0.f&&b.Y==0.f&&b.Z==0.f) continue;
                if (first) { ax=bxx=b.X; ay=byy=b.Y; az=bz=b.Z; first=false; continue; }
                if(b.X<ax)ax=b.X; if(b.X>bxx)bxx=b.X;
                if(b.Y<ay)ay=b.Y; if(b.Y>byy)byy=b.Y;
                if(b.Z<az)az=b.Z; if(b.Z>bz)bz=b.Z;
            }
            if (!first) {
                float mx = (ax+bxx)*.5f, my = (ay+byy)*.5f, mz = (az+bz)*.5f;
                Game::Vec3 loc = {mx, my, mz};
                Game::Vec3 ext = {(bxx-ax)*.5f+15.f, (byy-ay)*.5f+15.f, (bz-az)*.5f+10.f};
                draw3DBox(dl, loc, ext, vpm, sw, sh, col, fBoxThk);
            }
        }

        // Skeleton with shadow
        if (bSkel && sb.size() >= 2) {
            ImU32 skelShadow = IM_COL32(0, 0, 0, 90);
            ImU32 skelCol = colFade(cb, e.Distance, fMaxDist);
            if (!e.bIsVisible) skelCol = colAlpha(skelCol, 50);
            for (auto& [a,b] : e.BoneConnections) {
                if (a<(int)sb.size() && b<(int)sb.size() && bv[a] && bv[b]) {
                    dl->AddLine({sb[a].X,sb[a].Y},{sb[b].X,sb[b].Y}, skelShadow, fSkelThk+1.5f);
                    dl->AddLine({sb[a].X,sb[a].Y},{sb[b].X,sb[b].Y}, skelCol, fSkelThk);
                }
            }
        }

        // Head circle with fill
        if (bHead && sb.size() >= 2) {
            int hi = e.headBoneIdx, he = e.headEndBoneIdx;
            if (hi>=0 && hi<(int)sb.size() && bv[hi] && he>=0 && he<(int)sb.size() && bv[he]) {
                float cx = (sb[hi].X+sb[he].X)*.5f, cy = (sb[hi].Y+sb[he].Y)*.5f;
                float dx = sb[he].X-sb[hi].X, dy = sb[he].Y-sb[hi].Y;
                float r = sqrtf(dx*dx+dy*dy) * .5f;
                if (r < 3.f) r = 3.f;
                dl->AddCircleFilled({cx,cy}, r+1.f, IM_COL32(0,0,0,80), 16);
                dl->AddCircleFilled({cx,cy}, r, colAlpha(col, 60), 16);
                dl->AddCircle({cx,cy}, r, col, 16, fSkelThk);
            }
        }

        // Snaplines with gradient
        if (bSnap && gotBox) {
            float sx = sw*.5f;
            float sy = (iSnapFrom==2) ? 0.f : (iSnapFrom==1) ? sh*.5f : sh;
            float ex = (x0+x1)*.5f, ey = y1+pd;
            ImU32 c1 = colAlpha(cs, 30);
            ImU32 c2 = cs;
            dl->AddLine({sx,sy}, {ex,ey}, IM_COL32(0,0,0,40), 2.f);
            // gradient: from faded to solid
            float mx = (sx+ex)*.5f, my = (sy+ey)*.5f;
            dl->AddLine({sx,sy}, {mx,my}, c1, 1.f);
            dl->AddLine({mx,my}, {ex,ey}, c2, 1.f);
        }

        // Name + Distance label with pill background
        if (bNames || bDist) {
            Game::Vec3 scr; bool ok = false;
            int he = e.headEndBoneIdx;
            if (he>=0 && he<(int)e.Bones.size() && !(e.Bones[he].X==0.f&&e.Bones[he].Y==0.f&&e.Bones[he].Z==0.f))
                ok = Game::WorldToScreen(e.Bones[he], vpm, sw, sh, scr);
            if (!ok) {
                Game::Vec3 hp = e.Location; hp.Z += 180.f;
                ok = Game::WorldToScreen(hp, vpm, sw, sh, scr);
            }
            if (ok) {
                char buf[256];
                const char* stateTag = "";
                if (e.relation == 0) stateTag = " [F]";
                else if (e.relation == 1) stateTag = " [P]";
                else if (e.aiState == Game::AI_Combat) stateTag = " [!]";
                else if (e.aiState == Game::AI_Suspicious) stateTag = " [?]";

                if (bNames && bDist) sprintf(buf, "%s%s  %.0fm", e.Name.c_str(), stateTag, e.Distance);
                else if (bNames) sprintf(buf, "%s%s", e.Name.c_str(), stateTag);
                else sprintf(buf, "%.0fm", e.Distance);
                auto ts = ImGui::CalcTextSize(buf);
                float lx = scr.X - ts.x * .5f;
                float ly = scr.Y - ts.y - 16.f;
                txtLabel(dl, lx, ly, col, buf);
            }
        }

        // HP Bar with rounded ends and gradient color
        if (bHpBar && gotBox) {
            float barY = y1 + pd + 4.f;
            float barH = 4.f;
            float bl = x0 - pd, br = x1 + pd;
            float barW = br - bl;
            float fillW = barW * e.healthPct;
            // background
            dl->AddRectFilled({bl-1, barY-1}, {br+1, barY+barH+1}, IM_COL32(0,0,0,200), 2.f);
            // fill with smooth gradient: green -> yellow -> red
            uint8_t hr, hg;
            if (e.healthPct > 0.5f) {
                float t = (e.healthPct - 0.5f) * 2.f;
                hr = (uint8_t)(255 * (1.f - t));
                hg = 255;
            } else {
                float t = e.healthPct * 2.f;
                hr = 255;
                hg = (uint8_t)(255 * t);
            }
            ImU32 hpCol = IM_COL32(hr, hg, 0, 220);
            if (fillW > 2.f)
                dl->AddRectFilled({bl, barY}, {bl+fillW, barY+barH}, hpCol, 2.f);
        }
    }

    // Crosshair indicator
    if (bCrosshair && bMagicBullet && !ents.empty()) {
        float cx = sw * 0.5f, cy = sh * 0.5f;
        float bestD = 9999.f;
        Game::Vec3 bestScr;
        bool found = false;
        for (auto& e : ents) {
            if (bOnlyEnemy && e.relation != 2) continue;
            Game::Vec3 hp = e.Location; hp.Z += 90.f;
            Game::Vec3 scr;
            if (!Game::WorldToScreen(hp, vpm, sw, sh, scr)) continue;
            float dx = scr.X - cx, dy = scr.Y - cy;
            float d = sqrtf(dx*dx + dy*dy);
            if (d < bestD) { bestD = d; bestScr = scr; found = true; }
        }
        if (found && bestD < 200.f) {
            float f = 1.f - bestD / 200.f;
            uint8_t a = (uint8_t)(220 * f);
            ImU32 xc = IM_COL32(255, 50, 50, a);
            float sz = 8.f + 4.f * f;
            dl->AddLine({bestScr.X - sz, bestScr.Y}, {bestScr.X + sz, bestScr.Y}, xc, 2.f);
            dl->AddLine({bestScr.X, bestScr.Y - sz}, {bestScr.X, bestScr.Y + sz}, xc, 2.f);
            dl->AddCircle({bestScr.X, bestScr.Y}, sz, IM_COL32(255, 50, 50, (uint8_t)(160*f)), 16, 1.5f);
        }
    }

    // Items
    if (bItems) {
        auto items = g.GatherItems(cp, fItemDist);
        ImU32 ci = c4(cItem);
        for (auto& it : items) {
            int cat = itemCategory(it.Name);
            if (cat < 9 && !itemFilter[cat]) continue;
            ImU32 ic = colFade(ci, it.Distance, fItemDist);
            char buf[128]; buf[0] = 0;
            if (bItemName && bItemDist) sprintf(buf, "%s  %.0fm", it.Name.c_str(), it.Distance);
            else if (bItemName) sprintf(buf, "%s", it.Name.c_str());
            else if (bItemDist) sprintf(buf, "%.0fm", it.Distance);
            if (bItemBox) {
                draw3DBoxLabel(dl, it.Location, it.BoxExtent, vpm, sw, sh, ic, buf);
            } else {
                Game::Vec3 scr;
                if (!Game::WorldToScreen(it.Location, vpm, sw, sh, scr)) continue;
                if (buf[0]) {
                    auto ts = ImGui::CalcTextSize(buf);
                    txtLabel(dl, scr.X - ts.x*.5f, scr.Y - ts.y - 4.f, ic, buf);
                }
            }
        }
    }

    // Interactables
    if (bInteract) {
        auto interacts = g.GatherInteractables(cp, fInteractDist);
        ImU32 ci2 = c4(cInteract);
        for (auto& ia : interacts) {
            ImU32 ic = colFade(ci2, ia.Distance, fInteractDist);
            char buf[128];
            sprintf(buf, "%s  %.0fm", ia.Name.c_str(), ia.Distance);
            draw3DBoxLabel(dl, ia.Location, ia.BoxExtent, vpm, sw, sh, ic, buf);
        }
    }

    // Wallhack overlay
    if (bWallhack) {
        float maxR = fInteractDist > fItemDist ? fInteractDist : fItemDist;
        auto wallObjs = g.GatherWallhackAll(cp, maxR);
        for (auto& wo : wallObjs) {
            ImU32 cw = colFade(IM_COL32(0, 255, 200, 180), wo.Distance, maxR);
            char buf[128];
            sprintf(buf, "%s  %.0fm", wo.Name.c_str(), wo.Distance);
            draw3DBoxLabel(dl, wo.Location, wo.BoxExtent, vpm, sw, sh, cw, buf);
        }
    }

    dl->PopClipRect();
}

static void setupStyle() {
    auto& s = ImGui::GetStyle();
    s.WindowRounding = 8.f;
    s.ChildRounding = 6.f;
    s.FrameRounding = 4.f;
    s.GrabRounding = 4.f;
    s.PopupRounding = 6.f;
    s.ScrollbarRounding = 6.f;
    s.TabRounding = 4.f;
    s.WindowPadding = {12, 10};
    s.FramePadding = {8, 4};
    s.ItemSpacing = {8, 6};
    s.ItemInnerSpacing = {6, 4};
    s.WindowBorderSize = 1.f;
    s.ChildBorderSize = 1.f;
    s.TabBorderSize = 0.f;
    s.WindowTitleAlign = {0.5f, 0.5f};
    s.ScrollbarSize = 12.f;
    s.GrabMinSize = 10.f;

    auto* c = s.Colors;
    c[ImGuiCol_WindowBg]            = {0.06f, 0.06f, 0.09f, 0.96f};
    c[ImGuiCol_ChildBg]             = {0.08f, 0.08f, 0.12f, 0.6f};
    c[ImGuiCol_TitleBg]             = {0.06f, 0.06f, 0.09f, 1.f};
    c[ImGuiCol_TitleBgActive]       = {0.10f, 0.06f, 0.20f, 1.f};
    c[ImGuiCol_Border]              = {0.30f, 0.22f, 0.50f, 0.4f};
    c[ImGuiCol_FrameBg]             = {0.10f, 0.10f, 0.15f, 1.f};
    c[ImGuiCol_FrameBgHovered]      = {0.16f, 0.14f, 0.24f, 1.f};
    c[ImGuiCol_FrameBgActive]       = {0.20f, 0.17f, 0.30f, 1.f};
    c[ImGuiCol_CheckMark]           = {0.55f, 0.35f, 1.f, 1.f};
    c[ImGuiCol_SliderGrab]          = {0.45f, 0.28f, 0.75f, 1.f};
    c[ImGuiCol_SliderGrabActive]    = {0.60f, 0.38f, 1.f, 1.f};
    c[ImGuiCol_Button]              = {0.14f, 0.12f, 0.22f, 1.f};
    c[ImGuiCol_ButtonHovered]       = {0.22f, 0.18f, 0.36f, 1.f};
    c[ImGuiCol_ButtonActive]        = {0.30f, 0.22f, 0.48f, 1.f};
    c[ImGuiCol_Header]              = {0.14f, 0.12f, 0.22f, 1.f};
    c[ImGuiCol_HeaderHovered]       = {0.22f, 0.18f, 0.36f, 1.f};
    c[ImGuiCol_HeaderActive]        = {0.28f, 0.22f, 0.44f, 1.f};
    c[ImGuiCol_Tab]                 = {0.10f, 0.08f, 0.16f, 1.f};
    c[ImGuiCol_TabHovered]          = {0.28f, 0.20f, 0.46f, 1.f};
    c[ImGuiCol_TabActive]           = {0.22f, 0.15f, 0.38f, 1.f};
    c[ImGuiCol_TabUnfocused]        = {0.08f, 0.06f, 0.12f, 1.f};
    c[ImGuiCol_TabUnfocusedActive]  = {0.16f, 0.10f, 0.26f, 1.f};
    c[ImGuiCol_Separator]           = {0.30f, 0.22f, 0.50f, 0.25f};
    c[ImGuiCol_SeparatorHovered]    = {0.45f, 0.30f, 0.70f, 0.6f};
    c[ImGuiCol_Text]                = {0.92f, 0.90f, 0.96f, 1.f};
    c[ImGuiCol_TextDisabled]        = {0.45f, 0.40f, 0.55f, 1.f};
    c[ImGuiCol_PopupBg]             = {0.08f, 0.07f, 0.12f, 0.97f};
    c[ImGuiCol_ScrollbarBg]         = {0.04f, 0.03f, 0.06f, 0.4f};
    c[ImGuiCol_ScrollbarGrab]       = {0.26f, 0.18f, 0.40f, 0.7f};
    c[ImGuiCol_ScrollbarGrabHovered]= {0.36f, 0.26f, 0.52f, 0.8f};
    c[ImGuiCol_ScrollbarGrabActive] = {0.44f, 0.32f, 0.62f, 0.9f};
    c[ImGuiCol_ResizeGrip]          = {0.30f, 0.20f, 0.50f, 0.3f};
    c[ImGuiCol_ResizeGripHovered]   = {0.45f, 0.30f, 0.70f, 0.6f};
    c[ImGuiCol_ResizeGripActive]    = {0.55f, 0.38f, 0.85f, 0.9f};
}

static DWORD WINAPI OverlayThread(LPVOID)
{
    HINSTANCE inst = g_hModule;

    WNDCLASSEXA wc={sizeof(wc)};
    wc.style = CS_HREDRAW|CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = "OverlayWndDLL";
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(
        WS_EX_TOPMOST|WS_EX_TRANSPARENT|WS_EX_LAYERED,
        wc.lpszClassName, "", WS_POPUP,
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        0, 0, inst, 0);

    SetLayeredWindowAttributes(hwnd, RGB(0,0,0), 0, LWA_COLORKEY);
    MARGINS m = {-1};
    DwmExtendFrameIntoClientArea(hwnd, &m);
    ShowWindow(hwnd, SW_SHOWDEFAULT);

    auto pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) return 1;

    D3DPRESENT_PARAMETERS pp={};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D16;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    pp.hDeviceWindow = hwnd;

    IDirect3DDevice9* dev = 0;
    if (FAILED(pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev)))
    { pD3D->Release(); return 1; }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = 0;
    setupStyle();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(dev);

    Game::GameReader game;
    game.AttachInternal();
    cfgLoad();

    auto applyCheatFlags = [&]() {
        game.ToggleGodMode(bGodMode);
        game.ToggleBuddhaMode(bBuddhaMode);
        game.ToggleInfiniteAmmo(bInfAmmo);
        game.ToggleInfiniteAdrenaline(bInfAdrenaline);
        game.ToggleAIBlind(bAIBlind);
        game.ToggleAIDeaf(bAIDeaf);
        game.ToggleAIOff(bAIOff);
        game.ToggleAutoDodge(bAutoDodge);
        game.ToggleNoKnockdown(bNoKnockdown);
        game.ToggleForceKillCam(bForceKillCam);
    };

    bool run = true;
    MSG msg;
    int tab = 0;

    while (run)
    {
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
            if (msg.message == WM_QUIT) run = false;
        }
        if (!run) break;

        RECT gr = game.GetWindowRect_();
        int gw = gr.right-gr.left, gh = gr.bottom-gr.top;
        if (gw > 0 && gh > 0)
            SetWindowPos(hwnd, HWND_TOPMOST, gr.left, gr.top, gw, gh, SWP_NOACTIVATE);

        static int lw=0, lh=0;
        if (gw>0 && gh>0 && (gw!=lw||gh!=lh)) {
            lw=gw; lh=gh;
            ImGui_ImplDX9_InvalidateDeviceObjects();
            pp.BackBufferWidth=gw; pp.BackBufferHeight=gh;
            dev->Reset(&pp);
            ImGui_ImplDX9_CreateDeviceObjects();
        }

        float sw=(float)gw, sh=(float)gh;

        // keys
        static bool ks[256]={};
        auto kp = [](int k, bool& s) { bool d=(GetAsyncKeyState(k)&0x8000)!=0; if(d&&!s){s=true;return true;} if(!d)s=false; return false; };
        if (kp(VK_INSERT, ks[VK_INSERT])) bEsp = !bEsp;
        if (kp(VK_HOME, ks[VK_HOME])) bMenu = !bMenu;
        if (kp(VK_END, ks[VK_END])) { run=false; break; }

        if (bTeleport && kp(VK_F5, ks[VK_F5])) {
            Game::Vec3 tpCam; Game::Rotator tpRot;
            if (game.GetCameraData(tpCam, tpRot))
                game.TeleportAboveTarget(tpCam, tpRot, sw, sh, fMaxDist, fTpHeight);
        }

        if (kp(VK_F7, ks[VK_F7])) bMagicBullet = !bMagicBullet;
        if (kp(VK_F8, ks[VK_F8])) bWallhack = !bWallhack;

        static Game::GameReader::AimResult aimTarget;
        if (bMagicBullet) {
            Game::Vec3 mbCam; Game::Rotator mbRot;
            if (game.GetCameraData(mbCam, mbRot))
                aimTarget = game.MagicBullet(mbCam, mbRot, sw, sh, g_lastEnts, fAimFov);
            else
                aimTarget.found = false;
        } else {
            aimTarget.found = false;
        }


        static Game::GameReader::WallInteractResult wallTarget;
        if (bWallhack) {
            Game::Vec3 whCam; Game::Rotator whRot;
            if (game.GetCameraData(whCam, whRot))
                wallTarget = game.InteractThroughWalls(whCam, whRot, sw, sh);
            else
                wallTarget.found = false;
        } else {
            wallTarget.found = false;
        }

        if (kp(VK_F6, ks[VK_F6])) bNoclip = !bNoclip;

        if (bNoclip) {
            Game::Vec3 ncCam; Game::Rotator ncRot;
            if (game.GetCameraData(ncCam, ncRot)) {
                float fv = 0.f, rv = 0.f, uv = 0.f;
                if (GetAsyncKeyState('W') & 0x8000) fv += 1.f;
                if (GetAsyncKeyState('S') & 0x8000) fv -= 1.f;
                if (GetAsyncKeyState('D') & 0x8000) rv += 1.f;
                if (GetAsyncKeyState('A') & 0x8000) rv -= 1.f;
                if (GetAsyncKeyState(VK_SPACE) & 0x8000) uv += 1.f;
                if (GetAsyncKeyState(VK_CONTROL) & 0x8000) uv -= 1.f;
                if (fv != 0.f || rv != 0.f || uv != 0.f) {
                    float spd = fNoclipSpeed;
                    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) spd *= 2.f;
                    game.NoclipMove(ncRot, fv, rv, uv, spd);
                }
                else {
                    uintptr_t lp = 0;
                    if (game.RPM(Game::Off::LocalPawn, lp) && lp) {
                        Game::Vec3 zero = {0.f, 0.f, 0.f};
                        game.WPMBytes(lp + 0x01B4, &zero, 12);
                    }
                }
            }
        }

        if (bNoFallDmg) {
            uintptr_t lp = 0;
            if (game.RPM(Game::Off::LocalPawn, lp) && lp) {
                float vz = 0.f;
                if (game.RPM(lp + 0x01BC, vz) && vz < -100.f) {
                    vz = -100.f;
                    game.WPM(lp + 0x01BC, vz);
                }
            }
        }

        game.UpdateWorldTime();

        // ===== PE vtable hook for line traces =====
        {
            static DWORD lastHookCheck = 0;
            DWORD hookNow = GetTickCount();
            if (hookNow - lastHookCheck > 500) {
                lastHookCheck = hookNow;
                if (Game::PEHook::g_hookInstalled)
                    Game::PEHook::Validate();
                else
                    Game::PEHook::Install();
            }
        }

        // ===== per-tick cheats =====
        if (bGodMode || bInfHealth) game.InfiniteHealth();
        if (bGodMode || bInfMana) game.InfiniteMana();
        if (bInfAir) game.InfiniteAir();
        if (bInfElixir) game.InfiniteElixirs();
        if (bInfAmmo) game.InfiniteAmmoTick();
        if (bNoSpread) game.NoWeaponSpread();
        static bool dvWasOn = false;
        if (bDarkVision) { game.ForceDarkVision(); dvWasOn = true; }
        else if (dvWasOn) { game.StopDarkVision(); dvWasOn = false; }
        if (bSpeedHack) { game.SetGroundSpeed(fGroundSpeed); game.SetJumpZ(fJumpZ); }
        if (bBlinkRange) game.SetBlinkRange(fBlinkRange);
        if (bTimeDilation) game.SetTimeDilation(fTimeDilation);
        if (bGravity) game.SetGravity(fGravity);
        // CM-based flags (re-apply each tick in case CM gets created)
        if (bAIBlind) game.ToggleAIBlind(true);
        if (bAIDeaf) game.ToggleAIDeaf(true);
        if (bAIOff) game.ToggleAIOff(true);

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        render(game, sw, sh);

        // target indicators
        {
            auto* fg = ImGui::GetForegroundDrawList();
            float cx = sw * 0.5f, cy = sh * 0.5f;

            // magic bullet FOV circle + target line
            if (bMagicBullet) {
                ImU32 fovCol = IM_COL32(255, 255, 255, 40);
                fg->AddCircle({cx, cy}, fAimFov, fovCol, 64, 1.f);
            }
            if (bMagicBullet && aimTarget.found) {
                ImU32 aimCol = IM_COL32(255, 50, 50, 180);
                fg->AddLine({cx, cy}, {aimTarget.screenPos.X, aimTarget.screenPos.Y}, aimCol, 1.5f);
                float r = 12.f;
                fg->AddCircle({aimTarget.screenPos.X, aimTarget.screenPos.Y}, r, aimCol, 16, 2.f);
                fg->AddLine({aimTarget.screenPos.X-r, aimTarget.screenPos.Y},
                            {aimTarget.screenPos.X+r, aimTarget.screenPos.Y}, aimCol, 1.5f);
                fg->AddLine({aimTarget.screenPos.X, aimTarget.screenPos.Y-r},
                            {aimTarget.screenPos.X, aimTarget.screenPos.Y+r}, aimCol, 1.5f);
            }

            // wallhack interact target line + label
            if (bWallhack && wallTarget.found) {
                ImU32 whCol = IM_COL32(0, 255, 200, 180);
                fg->AddLine({cx, cy}, {wallTarget.screenPos.X, wallTarget.screenPos.Y}, whCol, 1.5f);
                float r = 8.f;
                fg->AddRect({wallTarget.screenPos.X-r, wallTarget.screenPos.Y-r},
                            {wallTarget.screenPos.X+r, wallTarget.screenPos.Y+r}, whCol, 0, 0, 2.f);

                char wtBuf[128];
                sprintf(wtBuf, "[WALL] %s [%.0fm]", wallTarget.name.c_str(), wallTarget.distance);
                auto ts = ImGui::CalcTextSize(wtBuf);
                float tx = cx - ts.x * 0.5f;
                float ty = cy + 20.f;
                fg->AddRectFilled({tx-4, ty-2}, {tx+ts.x+4, ty+ts.y+2}, IM_COL32(0,0,0,160), 3.f);
                fg->AddText({tx, ty}, whCol, wtBuf);
            }
        }

        if (bEsp) {
            auto* fg = ImGui::GetForegroundDrawList();
            const char* bar = bWallhack
                ? "[HOME] Menu | [F7] Magic Bullet | [F8] Wallhack ON | [END] Eject"
                : bMagicBullet
                ? "[HOME] Menu | [F7] Magic Bullet ON | [F8] Wallhack | [END] Eject"
                : bNoclip
                ? "[HOME] Menu | [F6] Noclip ON | [END] Eject"
                : "[HOME] Menu | [INS] Toggle | [END] Eject";
            auto ts = ImGui::CalcTextSize(bar);
            float bx = (sw-ts.x)*.5f;
            fg->AddRectFilled({bx-8,2},{bx+ts.x+8,ts.y+6}, IM_COL32(0,0,0,140), 4.f);
            fg->AddText({bx,4}, IM_COL32(170,130,240,255), bar);
        }

        if (bMenu) {
            LONG ex = GetWindowLongA(hwnd, GWL_EXSTYLE);
            if (ex & WS_EX_TRANSPARENT)
                SetWindowLongA(hwnd, GWL_EXSTYLE, ex & ~WS_EX_TRANSPARENT);

            ImGui::SetNextWindowSize({380, 480}, ImGuiCond_FirstUseEver);
            ImGui::Begin("DISHONORED##main", &bMenu, ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoScrollbar);

            // status bar
            auto stats = game.GetPlayerStats();
            {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.04f, 0.04f, 0.07f, 0.8f});
                ImGui::BeginChild("##status", {0, 36}, true, ImGuiWindowFlags_NoScrollbar);
                ImGui::TextColored({0.5f, 0.8f, 1.f, 1.f}, "HP %d/%d", stats.health, stats.maxHealth);
                ImGui::SameLine(0, 16);
                ImGui::TextColored({0.4f, 0.5f, 1.f, 1.f}, "MP %d/%d", stats.mana, stats.maxMana);
                ImGui::SameLine(0, 16);
                ImGui::TextColored({0.6f, 0.6f, 0.7f, 0.8f}, "%.0f fps", io.Framerate);
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }
            ImGui::Spacing();

            if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_FittingPolicyResizeDown)) {

                // =================== VISUALS ===================
                if (ImGui::BeginTabItem("Visuals")) {
                    ImGui::BeginChild("##vis_scroll", {0, 0}, false);

                    ImGui::Checkbox("Enable ESP (INS)", &bEsp);
                    ImGui::Spacing();

                    ImGui::TextColored({0.55f, 0.35f, 1.f, 1.f}, "Display");
                    ImGui::Separator();
                    ImGui::Columns(2, "##espcols", false);
                    ImGui::Checkbox("2D Box", &bBox);
                    ImGui::Checkbox("3D Box", &bBox3d);
                    ImGui::Checkbox("Skeleton", &bSkel);
                    ImGui::Checkbox("Head Dot", &bHead);
                    ImGui::Checkbox("HP Bar", &bHpBar);
                    ImGui::NextColumn();
                    ImGui::Checkbox("Names", &bNames);
                    ImGui::Checkbox("Distance", &bDist);
                    ImGui::Checkbox("Snaplines", &bSnap);
                    ImGui::Checkbox("Crosshair", &bCrosshair);
                    ImGui::Checkbox("Only Enemies", &bOnlyEnemy);
                    ImGui::Columns(1);

                    if (bSnap) {
                        ImGui::SetNextItemWidth(120);
                        const char* sp[] = {"Bottom","Center","Top"};
                        ImGui::Combo("Snap origin", &iSnapFrom, sp, 3);
                    }

                    ImGui::Spacing();
                    ImGui::SetNextItemWidth(-1);
                    ImGui::SliderFloat("##maxdist", &fMaxDist, 10.f, 500.f, "Render dist: %.0f m");

                    ImGui::Spacing();
                    ImGui::TextColored({0.55f, 0.35f, 1.f, 1.f}, "Items & Objects");
                    ImGui::Separator();
                    ImGui::Checkbox("Show Items", &bItems);
                    ImGui::SameLine(0, 16);
                    ImGui::Checkbox("Interactables", &bInteract);

                    if (bItems) {
                        ImGui::Indent(12);
                        ImGui::Columns(2, "##itemopt", false);
                        ImGui::Checkbox("3D Box##it", &bItemBox);
                        ImGui::Checkbox("Name##it", &bItemName);
                        ImGui::NextColumn();
                        ImGui::Checkbox("Distance##it", &bItemDist);
                        ImGui::Columns(1);
                        ImGui::SetNextItemWidth(-1);
                        ImGui::SliderFloat("##itemr", &fItemDist, 5.f, 200.f, "Item range: %.0f m");

                        if (ImGui::TreeNode("Item Filters")) {
                            const char* fn[] = {"Runes","Bone Charms","Health Elixir","Mana Elixir",
                                                "Keys","Loot","Weapons","Notes/Logs","Other"};
                            ImGui::Columns(2, "##filtcols", false);
                            for (int i=0;i<5;i++) ImGui::Checkbox(fn[i], &itemFilter[i]);
                            ImGui::NextColumn();
                            for (int i=5;i<9;i++) ImGui::Checkbox(fn[i], &itemFilter[i]);
                            ImGui::Columns(1);
                            ImGui::TreePop();
                        }
                        ImGui::Unindent(12);
                    }

                    if (bInteract) {
                        ImGui::Indent(12);
                        ImGui::SetNextItemWidth(-1);
                        ImGui::SliderFloat("##intr", &fInteractDist, 5.f, 200.f, "Interact range: %.0f m");
                        if (ImGui::Button("Unlock Nearby")) {
                            Game::Vec3 uc; Game::Rotator ur;
                            if (game.GetCameraData(uc, ur)) {
                                int n = game.UnlockAll(uc, fInteractDist);
                                sprintf(cfgMsg, "unlocked %d", n); cfgTime = GetTickCount();
                            }
                        }
                        ImGui::Unindent(12);
                    }

                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                // =================== COMBAT ===================
                if (ImGui::BeginTabItem("Combat")) {
                    ImGui::BeginChild("##cbt_scroll", {0, 0}, false);

                    ImGui::TextColored({1.f, 0.4f, 0.4f, 1.f}, "Magic Bullet");
                    ImGui::Separator();
                    ImGui::Checkbox("Enable (F7)", &bMagicBullet);
                    if (bMagicBullet) {
                        ImGui::SetNextItemWidth(-1);
                        ImGui::SliderFloat("##aimfov", &fAimFov, 20.f, 800.f, "FOV radius: %.0f px");
                        ImGui::Checkbox("Vis Check (only visible)", &bAimVisCheck);
                    }

                    ImGui::Spacing();
                    ImGui::TextColored({0.f, 1.f, 0.8f, 1.f}, "Wallhack");
                    ImGui::Separator();
                    ImGui::Checkbox("Through Walls (F8)", &bWallhack);

                    ImGui::Spacing();
                    ImGui::TextColored({0.55f, 0.35f, 1.f, 1.f}, "Weapons");
                    ImGui::Separator();
                    ImGui::Checkbox("Infinite Ammo", &bInfAmmo);
                    ImGui::Checkbox("No Spread", &bNoSpread);
                    ImGui::Checkbox("Infinite Elixirs", &bInfElixir);
                    if (ImGui::Button("Max All Ammo", {-1, 0})) { game.MaxAllAmmo(); strcpy(cfgMsg, "all ammo maxed"); cfgTime = GetTickCount(); }

                    ImGui::Spacing();
                    ImGui::TextColored({1.f, 0.7f, 0.3f, 1.f}, "AI Control");
                    ImGui::Separator();
                    ImGui::Checkbox("AI Blind", &bAIBlind);
                    ImGui::SameLine(0, 16);
                    ImGui::Checkbox("AI Deaf", &bAIDeaf);
                    ImGui::Checkbox("AI Off", &bAIOff);

                    ImGui::Spacing();
                    if (Game::PEHook::g_hookInstalled)
                        ImGui::TextColored({0.f,1.f,0.f,0.7f}, "Hook: active [%ld traces]", Game::PEHook::g_tracesExecuted);
                    else
                        ImGui::TextColored({0.5f,0.5f,0.5f,0.6f}, "Hook: pending...");

                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                // =================== PLAYER ===================
                if (ImGui::BeginTabItem("Player")) {
                    ImGui::BeginChild("##plr_scroll", {0, 0}, false);

                    ImGui::TextColored({0.4f, 1.f, 0.4f, 1.f}, "Survival");
                    ImGui::Separator();
                    ImGui::Checkbox("God Mode", &bGodMode);
                    ImGui::Checkbox("Infinite Health", &bInfHealth);
                    ImGui::SameLine(0, 16);
                    ImGui::Checkbox("Infinite Mana", &bInfMana);
                    ImGui::Checkbox("Infinite Air", &bInfAir);
                    ImGui::Checkbox("No Fall Damage", &bNoFallDmg);

                    ImGui::Spacing();
                    ImGui::TextColored({0.3f, 0.7f, 1.f, 1.f}, "Movement");
                    ImGui::Separator();
                    ImGui::Checkbox("Noclip / Fly (F6)", &bNoclip);
                    if (bNoclip) {
                        ImGui::Indent(12);
                        ImGui::SetNextItemWidth(-1);
                        ImGui::SliderFloat("##flyspd", &fNoclipSpeed, 1.f, 100.f, "Speed: %.0f");
                        ImGui::TextDisabled("WASD + Space/Ctrl");
                        ImGui::Unindent(12);
                    }
                    ImGui::Checkbox("Teleport (F5)", &bTeleport);
                    if (bTeleport) {
                        ImGui::Indent(12);
                        ImGui::SetNextItemWidth(-1);
                        ImGui::SliderFloat("##tph", &fTpHeight, 1.f, 50.f, "Height: %.0f m");
                        ImGui::Unindent(12);
                    }
                    ImGui::Checkbox("Speed Hack", &bSpeedHack);
                    if (bSpeedHack) {
                        ImGui::Indent(12);
                        ImGui::SetNextItemWidth(-1);
                        ImGui::SliderFloat("##gspd", &fGroundSpeed, 100.f, 5000.f, "Ground: %.0f");
                        ImGui::SetNextItemWidth(-1);
                        ImGui::SliderFloat("##jmpz", &fJumpZ, 100.f, 5000.f, "Jump: %.0f");
                        ImGui::Unindent(12);
                    }
                    ImGui::Checkbox("Blink Range", &bBlinkRange);
                    if (bBlinkRange) {
                        ImGui::Indent(12);
                        ImGui::SetNextItemWidth(-1);
                        ImGui::SliderFloat("##blnk", &fBlinkRange, 500.f, 50000.f, "Range: %.0f");
                        ImGui::Unindent(12);
                    }

                    ImGui::Spacing();
                    ImGui::TextColored({0.8f, 0.4f, 1.f, 1.f}, "Abilities");
                    ImGui::Separator();
                    ImGui::Checkbox("Dark Vision (Glow ESP)", &bDarkVision);
                    ImGui::SameLine(); ImGui::TextDisabled("(?)");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Forces Dark Vision Lv2 permanently.\nNPCs and items glow through walls.\nIncludes max distance + orange tint.");

                    ImGui::Spacing();
                    ImGui::Checkbox("Custom Gravity", &bGravity);
                    if (bGravity) {
                        ImGui::Indent(12);
                        ImGui::SetNextItemWidth(-1);
                        ImGui::SliderFloat("##grav", &fGravity, -5000.f, 5000.f, "Gravity: %.0f");
                        ImGui::Unindent(12);
                    }
                    ImGui::Checkbox("Time Dilation", &bTimeDilation);
                    if (bTimeDilation) {
                        ImGui::Indent(12);
                        ImGui::SetNextItemWidth(-1);
                        ImGui::SliderFloat("##tdil", &fTimeDilation, 0.01f, 10.f, "Scale: %.2f");
                        ImGui::Unindent(12);
                    }

                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                // =================== ACTIONS ===================
                if (ImGui::BeginTabItem("Actions")) {
                    ImGui::BeginChild("##act_scroll", {0, 0}, false);

                    ImGui::TextColored({1.f, 0.85f, 0.3f, 1.f}, "Quick Actions");
                    ImGui::Separator();
                    if (ImGui::Button("Max Powers", {-1, 26})) { game.CheatMaxPowers(); strcpy(cfgMsg, "powers maxed"); cfgTime = GetTickCount(); }
                    if (ImGui::Button("Max Upgrades", {-1, 26})) { game.CheatMaxUpgrades(); strcpy(cfgMsg, "upgrades unlocked"); cfgTime = GetTickCount(); }
                    if (ImGui::Button("Give Bone Charm", {-1, 26})) { game.CheatGiveBoneCharm(); strcpy(cfgMsg, "bone charm given"); cfgTime = GetTickCount(); }
                    if (ImGui::Button("Unlock All Doors", {-1, 26})) { game.UnlockAllDoors(); strcpy(cfgMsg, "doors unlocked"); cfgTime = GetTickCount(); }
                    if (ImGui::Button("Max All Ammo", {-1, 26})) { game.MaxAllAmmo(); strcpy(cfgMsg, "ammo maxed"); cfgTime = GetTickCount(); }

                    ImGui::Spacing();
                    static int goldAmount = 10000;
                    ImGui::SetNextItemWidth(120);
                    ImGui::InputInt("##gold", &goldAmount, 1000, 10000);
                    ImGui::SameLine();
                    if (ImGui::Button("Give Gold", {-1, 0})) { game.CheatGiveMoney(goldAmount); strcpy(cfgMsg, "gold given"); cfgTime = GetTickCount(); }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::TextColored({1.f, 0.85f, 0.3f, 1.f}, "Spawner");
                    ImGui::Spacing();

                    static std::vector<Game::GameReader::SpawnableItem> spawnList;
                    static bool scanned = false;
                    static char filterBuf[128] = "";

                    if (!scanned) {
                        if (ImGui::Button("Scan Classes", {-1, 26})) {
                            spawnList = game.GetSpawnableList();
                            scanned = true;
                            snprintf(cfgMsg, sizeof(cfgMsg), "found %d classes", (int)spawnList.size());
                            cfgTime = GetTickCount();
                        }
                    } else {
                        if (ImGui::Button("Rescan")) { spawnList = game.GetSpawnableList(); snprintf(cfgMsg, sizeof(cfgMsg), "found %d", (int)spawnList.size()); cfgTime = GetTickCount(); }
                        ImGui::SameLine();
                        ImGui::TextDisabled("%d classes", (int)spawnList.size());
                    }

                    ImGui::SetNextItemWidth(-1);
                    ImGui::InputTextWithHint("##filter", "Search...", filterBuf, 128);

                    if (scanned && !spawnList.empty()) {
                        ImGui::BeginChild("##spawnlist", {0, -80}, true);
                        for (auto& si : spawnList) {
                            if (filterBuf[0]) {
                                bool match = false;
                                std::string lower = si.className;
                                std::string filt = filterBuf;
                                for (auto& ch : lower) ch = tolower(ch);
                                for (auto& ch : filt) ch = tolower(ch);
                                if (lower.find(filt) != std::string::npos) match = true;
                                if (!match) { lower = si.name; for (auto& ch : lower) ch = tolower(ch); if (lower.find(filt) != std::string::npos) match = true; }
                                if (!match) continue;
                            }
                            ImGui::PushID(si.name.c_str());
                            if (ImGui::Button("+", {22, 0})) {
                                Game::Vec3 spCam; Game::Rotator spRot;
                                if (game.GetCameraData(spCam, spRot)) game.SpawnAtCrosshair(si.name.c_str(), spCam, spRot);
                                else game.spawnDebug = "no camera data";
                                snprintf(cfgMsg, sizeof(cfgMsg), "spawn: %s", si.className.c_str()); cfgTime = GetTickCount();
                            }
                            ImGui::SameLine();
                            ImGui::TextWrapped("%s", si.className.c_str());
                            ImGui::PopID();
                        }
                        ImGui::EndChild();
                    }

                    ImGui::Spacing();
                    static char customCmd[256] = "";
                    ImGui::SetNextItemWidth(-50);
                    ImGui::InputTextWithHint("##cmd", "console cmd...", customCmd, 256);
                    ImGui::SameLine();
                    if (ImGui::Button("Run", {-1, 0}) && customCmd[0]) {
                        game.ExecCommand(customCmd);
                        snprintf(cfgMsg, sizeof(cfgMsg), "exec: %s", customCmd); cfgTime = GetTickCount();
                    }

                    if (cfgMsg[0] && (GetTickCount()-cfgTime) < 2500)
                        ImGui::TextColored({.4f,1.f,.4f,1.f}, "%s", cfgMsg);

                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                // =================== STYLE ===================
                if (ImGui::BeginTabItem("Style")) {
                    ImGui::BeginChild("##sty_scroll", {0, 0}, false);

                    ImGui::TextColored({0.55f, 0.35f, 1.f, 1.f}, "Box & Skeleton");
                    ImGui::Separator();
                    const char* bm[] = {"Normal","Corners"};
                    ImGui::Combo("Box type", &iBoxMode, bm, 2);
                    ImGui::SetNextItemWidth(-1);
                    ImGui::SliderFloat("##bthk", &fBoxThk, .5f, 4.f, "Box thickness: %.1f");
                    ImGui::SetNextItemWidth(-1);
                    ImGui::SliderFloat("##sthk", &fSkelThk, .5f, 4.f, "Skel thickness: %.1f");

                    ImGui::Spacing();
                    ImGui::TextColored({0.55f, 0.35f, 1.f, 1.f}, "Colors");
                    ImGui::Separator();
                    ImGui::ColorEdit4("Hostile", cHostile, ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_AlphaBar);
                    ImGui::ColorEdit4("Suspect", cSuspect, ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_AlphaBar);
                    ImGui::ColorEdit4("Passive", cPassive, ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_AlphaBar);
                    ImGui::ColorEdit4("Friendly", cFriend, ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_AlphaBar);
                    ImGui::ColorEdit4("Bones", cBones, ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_AlphaBar);
                    ImGui::ColorEdit4("Snaplines", cSnap, ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_AlphaBar);
                    ImGui::ColorEdit4("Items", cItem, ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_AlphaBar);
                    ImGui::ColorEdit4("Interact", cInteract, ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_AlphaBar);

                    ImGui::Spacing();
                    ImGui::TextColored({0.55f, 0.35f, 1.f, 1.f}, "Config");
                    ImGui::Separator();
                    if (ImGui::Button("Save", {80, 26})) cfgSave();
                    ImGui::SameLine();
                    if (ImGui::Button("Load", {80, 26})) { cfgLoad(); applyCheatFlags(); }
                    if (cfgMsg[0] && (GetTickCount()-cfgTime) < 2500)
                        ImGui::TextColored({.4f,1.f,.4f,1.f}, "%s", cfgMsg);

                    ImGui::Spacing();
                    ImGui::TextColored({0.55f, 0.35f, 1.f, 1.f}, "Hotkeys");
                    ImGui::Separator();
                    ImGui::TextDisabled("INS   ESP on/off");
                    ImGui::TextDisabled("HOME  Menu");
                    ImGui::TextDisabled("F5    Teleport");
                    ImGui::TextDisabled("F6    Noclip");
                    ImGui::TextDisabled("F7    Magic Bullet");
                    ImGui::TextDisabled("F8    Wallhack");
                    ImGui::TextDisabled("END   Eject");

                    ImGui::Spacing();
                    if (ImGui::Button("Dump Items")) { game.DumpAllItems(); strcpy(cfgMsg, "item_dump.txt written"); cfgTime = GetTickCount(); }

                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }

            ImGui::End();
        } else {
            LONG ex = GetWindowLongA(hwnd, GWL_EXSTYLE);
            if (!(ex & WS_EX_TRANSPARENT))
                SetWindowLongA(hwnd, GWL_EXSTYLE, ex | WS_EX_TRANSPARENT);
        }

        ImGui::EndFrame();
        dev->Clear(0, 0, D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER, D3DCOLOR_ARGB(0,0,0,0), 1.f, 0);
        if (dev->BeginScene() >= 0) {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            dev->EndScene();
        }
        HRESULT hr = dev->Present(0,0,0,0);
        if (hr == D3DERR_DEVICELOST && dev->TestCooperativeLevel() == D3DERR_DEVICENOTRESET) {
            ImGui_ImplDX9_InvalidateDeviceObjects();
            dev->Reset(&pp);
            ImGui_ImplDX9_CreateDeviceObjects();
        }
    }

    Game::PEHook::Uninstall();

    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    dev->Release();
    pD3D->Release();
    DestroyWindow(hwnd);
    UnregisterClassA(wc.lpszClassName, inst);
    FreeLibraryAndExitThread(g_hModule, 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        g_hModule = hModule;
        CreateThread(nullptr, 0, OverlayThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
